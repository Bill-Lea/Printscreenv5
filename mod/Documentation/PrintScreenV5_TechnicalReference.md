# PrintScreen V5 — technical reference

**Version:** 5.0.0
**Author:** William G Lea
**SKSE plugin for Skyrim Special Edition / Anniversary Edition**
**Platform:** Windows 10/11, DirectX 11

---

## Overview

PrintScreen V5 is a zero-integration capture plugin. It does not hook the game renderer, does not touch swap chains, and does not intercept anything inside Skyrim's render pipeline. Frames come from the Windows Desktop Duplication API (`IDXGIOutputDuplication`), which means captures work with any ENB, ReShade, or overlay, and also means the plugin sees the whole desktop rather than the game window.

The other design commitments, in rough order of importance to the code:

- One capture at a time, on a dedicated worker thread. The game thread never blocks on encoding.
- Cooperative cancellation everywhere. Long operations poll a `CancellationToken`; nothing is killed mid-write.
- UI hide and restore go through the same `showMenus` bool the console `tm` command uses, marshalled to the game thread via the SKSE task queue.
- Papyrus talks to C++ through a small native API and gets results back through one mod event. The polling loop from earlier versions is gone.
- No PapyrusUtil, no JContainers. JSON persistence is implemented natively.

## Repository layout

```
PrintscreenV5/
├── CMakeLists.txt, CMakePresets.json     # Root build; presets pin MSVC/SDK
├── vcpkg.json, vcpkg-configuration.json  # Manifest + registry baseline
├── build.ps1 / build.bat / build.sh      # Thin wrappers over cmake
├── BUILD.md                              # Full build recipe (GPL §6 corresponding source)
├── LICENSE, LICENSES.md, LICENSE-CommonLibSSE-NG-EXCEPTIONS.md
├── src/
│   ├── plugin.cpp                        # SKSE entry: Load, Query, messaging
│   ├── Config.h / config.cpp             # INI parsing (sectioned + legacy flat)
│   ├── logger.h / logger.cpp / logger_shim.hpp
│   ├── stringutils.h                     # wstring <-> utf8
│   ├── cancel.h                          # Cancellation helper macros
│   ├── ConsoleCommandQueue.h/.cpp        # Thread-safe console command queue
│   ├── ScreenCapture.h / screencapture.cpp   # Legacy forwarding header / tombstone
│   ├── VideoCapture.h/.cpp              # Media Foundation H.264 encoder
│   ├── capture/
│   │   ├── CaptureSession.h/.cpp        # Worker-thread orchestrator
│   │   ├── CaptureRequest.h/.cpp        # Parameter struct + format parsing
│   │   ├── FrameAcquirer.h/.cpp         # DXGI desktop duplication
│   │   ├── FrameQueue.h/.cpp            # Producer/consumer frame buffer
│   │   ├── GPUScaler.h/.cpp             # D3D11 downscaler
│   │   ├── GameCamera.h/.cpp            # FOV read -> 35mm focal length (EXIF)
│   │   ├── TempFileGuard.h/.cpp         # RAII temp cleanup + registry
│   │   └── CancellationToken.h/.cpp
│   ├── encoding/
│   │   ├── IEncoder.h                   # Interface + CaptureExif
│   │   ├── StillEncoder.h/.cpp          # PNG/JPEG/BMP/TIFF/DDS (WIC + DDS)
│   │   ├── GifEncoder.h/.cpp            # GIF89a (static GIF + AGIF)
│   │   └── ApngEncoder.h/.cpp           # APNG (fcTL/fdAT)
│   ├── Native/
│   │   └── PrintscreenJson.h/.cpp       # Native JSON bridge for Papyrus
│   ├── papyrus/
│   │   └── Bindings.h/.cpp              # Native function registration
│   └── ui/
│       ├── UIController.h/.cpp          # showMenus + granular Scaleform helpers
│       └── MenuEventSink.h/.cpp         # MenuOpenCloseEvent sink
├── Papyrus Scripts/                      # .psc sources, Compiled/ for .pex
├── mod/                                  # ESP, MCM splash textures, default JSON, docs
└── tools/                                # Package-Mod.ps1, Make-CorrespondingSource.ps1
```

## Runtime architecture

```
Papyrus hotkey (Printscreen_MainQuest_script.OnKeyUp)
        │
        ▼
Printscreen_Formula_script.TakePhoto(...)  [native, 20 parameters]
        │
        ▼
Bindings.cpp: BuildRequest() clamps everything -> CaptureRequest
        │        HideUIAsync() queued to game thread (if autoUI)
        ▼
CaptureSession::Start()  ── spawns worker thread, hands out token
        │
        ▼
FrameAcquirer (DXGI duplication) ──► GPUScaler (video) ──► VideoCapture (H.264)
        │
        ├── stills: GameCamera FOV read -> CaptureExif -> StillEncoder
        └── animated: frames staged in temp dir -> Gif/ApngEncoder
        │
        ├── acquisition callback: restore UI before slow encodes (not for H264)
        ▼
completion callback -> QueueModEvent("PrintScreenComplete", JSON payload)
                   -> RestoreUIAsync()
```

Ordering guarantee worth knowing: `TakePhoto()` queues the UI hide before calling `CaptureSession::Start()`. `Start()` holds its mutex until the worker thread exists, so the worker cannot queue a restore before the hide lands. The SKSE task queue therefore always sees hide before restore, even for a still that finishes within the same frame.

## CaptureSession

The single-flight orchestrator. One worker thread per capture; a second `Start()` while one runs is rejected.

```
State: Idle -> Starting -> Running -> Done -> (consumed) -> Idle
         │          │
         └──────────┴── RequestCancel() -> CALLBACK_CANCELLED
```

```cpp
enum class StartResult : int {
    Accepted       = 0,
    AlreadyRunning = 1,
    InvalidState   = 2,
    BusyCancelled  = 3   // active capture cancelled; call Start() again
};

StartResult Start(CaptureRequest request,
                  CompletionCallback   onComplete            = nullptr,
                  AcquisitionCallback  onAcquisitionComplete = nullptr);
```

Two callbacks:

- `AcquisitionCallback` fires after all frames are acquired, before encoding. `TakePhoto()` uses it to restore the HUD so a slow DDS or AGIF encode does not leave the UI dark for minutes. Not called for H264, where acquisition and encoding interleave.
- `CompletionCallback` fires after encoding (or error/cancel), on the worker thread. `TakePhoto()` uses it to send the Papyrus event and restore UI.

Threading details that have bitten before, hence the comments in the source: the destructor joins the worker; `JoinWorker()` is guarded by a separate `joinMutex_` because two concurrent `Start()` calls must not both join or assign the same `std::thread` (undefined behavior), and joining while holding `mutex_` deadlocks because the worker's `SetResult` acquires that same mutex.

`GetResult()` remains for the legacy polling protocol: `Ready`, `Starting`, `Running`, `CALLBACK_SUCCESS`, `CALLBACK_CANCELLED`, `CALLBACK_ERROR: <msg>`, consumed on read. The Papyrus binding for it is deprecated (see below).

`GetToken()` hands the active `CancellationToken` to `MenuEventSink`, which holds it weak so an expired session is ignored.

## FrameAcquirer and GPUScaler

`FrameAcquirer` owns the D3D11 device and the `IDXGIOutputDuplication` session, and offers a CPU path (into a `DirectX::ScratchImage`) and a GPU path (raw `ID3D11Texture2D` for the video pipeline).

`GPUScaler` downscales for video targets (720p through 4K) with a bilinear fullscreen quad, letterboxed to preserve aspect ratio. Shaders are compiled at runtime via `D3DCompile`; there are no `.cso` files to ship.

## GameCamera and EXIF metadata

New in V5. At capture time the session reads the active camera FOV from the engine:

- First person: `PlayerCamera::firstPersonFOV`
- Third person / free: `PlayerCamera::worldFOV`

Both fields are plain degrees, seeded by the INI `fDefaultWorldFOV` / `fDefault1stPersonFOV` and updated by the console `fov` command. The read is thread-safe; if the camera state is unavailable or the value is outside 1–170 degrees, the functions return 0.0 and EXIF is skipped.

The conversion is resolution-independent:

```
focal35mm = 18 / tan(hFOV / 2)
```

A stitcher turns that into a pixel focal length with `f_px = W * focal35 / 36`. The session then fills a `CaptureExif`:

```cpp
struct CaptureExif {
    double focalLength35mm;
    double fovDegrees;
    int    imageWidth, imageHeight;
    std::string cameraMake = "Skyrim Engine";
    std::string cameraModel, software;
};
```

and passes it to `StillEncoder::Encode(...)`, which writes it into PNG and JPEG output. This exists so panorama stitchers, PhotoFileMerge V2 in particular, can consume screenshots without manual parameter entry.

## Encoders

`IEncoder` is the interface; `CaptureExif` rides along as an optional parameter.

- **StillEncoder** — PNG, JPEG, BMP, TIFF, DDS. WIC does the first four (JPEG quality 0–100, TIFF compression per `TiffMode`, PNG zlib level 0–9), DirectXTex does DDS (BC1–BC7). Note the V5 fix: TIFF compression is parsed from the `Mode` string passed from Papyrus; in earlier builds that mapping was missing and TIFFs always came out uncompressed.
- **GifEncoder** — GIF89a with LZW, palette quantization, loop count. Handles both the static GIF image type and AGIF sequences, with frame differencing (`deltaMode` 0/1/2) and optional transparency optimization.
- **ApngEncoder** — APNG with proper `fcTL`/`fdAT` chunk sequencing and per-frame compression.

Animated captures stage their frames in a temp directory next to the output (naming pattern `gif_temp_YYYYMMDD_HHMMSS_mmm` / `apng_temp_...`), which is where `TempFileGuard` comes in.

## VideoCapture (H.264/MP4)

Media Foundation `IMFSinkWriter`. Each frame goes straight into the encoder as it is captured, so memory is constant no matter the duration, unlike the animated formats which buffer.

Selection: `Auto` lets Media Foundation pick (hardware if present: NVENC, AMF, or QuickSync, all through hardware MFTs; software otherwise), `PreferHardware` fails initialization if no hardware MFT exists, `ForceSoftware` sets `MF_READWRITE_DISABLE_HARDWARE_TRANSFORMS`. D3D11 multithread protection is enforced in `Initialize()`, and `Initialize`/`EncodeFrame`/`Finalize` must all run on the same thread, which is the capture worker.

Rate control maps to `eAVEncCommonRateControlMode_*` (CBR, unconstrained VBR, Quality/CQP) and is pushed to the encoder via `CODECAPI_AVEncCommonRateControlMode`. In CQP mode the bitrate setting is ignored. Frame timestamps are computed as `(frameIndex * kHnsPerSecond) / fps` to avoid accumulated rounding drift over long recordings.

`Finalize()` flushes the MP4 index. A file abandoned without it is an unplayable MP4, full stop. The MKV enum value exists but is clamped to MP4 in request building; Media Foundation has no Matroska sink, so MKV logs a warning and writes MP4.

## UI control

**UIController.** The primary mechanism is the `RE::UI` showMenus bool, the one `tm` toggles. A plain bool checked early in the render loop; when it is false the engine skips the whole UI pass. Because it is a single bool write, hide/restore are safe from any thread. The class also keeps granular Scaleform helpers (subtitle, NPC names, compass, crosshair, quest markers, player stats, HUD alpha) for selective hiding; those touch GFx and are strictly game-thread, so they must go through the SKSE task interface.

**MenuEventSink.** A `MenuOpenCloseEvent` sink registered on `kDataLoaded`. When a pause or input menu opens during an active capture, it requests cancellation through the weak token. It also caches game-paused state so the `IsGamePaused` native can answer without walking the menu list from the Papyrus VM thread, which the previous implementation did on every poll, from the wrong thread.

**ConsoleCommandQueue.** A mutex-guarded deque for console commands that must run on the game thread; `EnqueueAndDrainAsync` schedules the drain through the SKSE task interface.

## TempFileGuard

RAII wrapper around an animated capture's temp directory: destroyed means deleted, unless `Release()` was called after a successful encode. Directories also register in a global set, because RAII cannot help across a crash. On every `kPostLoadGame`, `CleanupAllRegistered()` sweeps anything left behind, and `StaleTempCleanup::ScanAndRemove` recognizes the `gif_temp_`/`apng_temp_` naming pattern for orphans with no registry entry.

Ordering matters and the code respects it: on load, the session is force-reset (cancelling any worker) *before* registered temp directories are deleted, so a still-running worker cannot have its directory removed mid-write.

## Papyrus layer

| Script | Extends | Role |
|--------|---------|------|
| `Printscreen_MainQuest_script` | Quest | State, defaults, JSON persistence, hotkey, capture orchestration, completion handling |
| `Printscreen_MCM_script` | `SKI_ConfigBase` | Two-page MCM; widget enable/disable per image type |
| `Printscreen_Formula_script` | Quest | Native declarations (capture API) |
| `Printscreen_JSON_script` | Quest | Native declarations (JSON API) |
| `Printscreen_MAP_script` | Quest | Pure-Papyrus key-name map; replaces the old JContainers JIntMap |
| `Printscreen_ME_script` | ActiveMagicEffect | Settings summary message box, per image type |
| `Printscreen_PlayerRef_Script` | ReferenceAlias | `OnPlayerLoadGame` -> `MainQuest.OnPlayerLoadGame()` |

### Native API (Printscreen_Formula_script)

```cpp
bool   CheckPath(string path)
string TakePhoto(string basePath, string imageType, float jpgCompression,
                 string Mode, float Duration, float Fps, int LoopCount,
                 int DeltaMode, int Optimize, int Compression,
                 float VideoDuration, int TargetResolution, int VideoFrameRate,
                 int QualityPreset, int VideoBitrate, float KeyframeInterval,
                 int EncoderPreference, int RateControl, int VideoContainer,
                 bool AutoUI = true)
string Cancel()
string MYReset(bool force = false)
bool   IsGamePaused()
string Get_Result()          // deprecated; always returns "Deprecated"
```

`TakePhoto` returns immediately: `Started:<seq>` (where `<seq>` is the capture's sequence number, see the completion protocol below), `Previous capture cancelled`, or `Error: Unable to start capture`. All parameters travel on every call; C++ reads only the ones the image type uses.

### Native JSON API (Printscreen_JSON_script)

`JsonExists`, `IsGood`, `GetErrors`, `Load`, `Save`, plus `Set/Get/Has` for int, float, and string values, keyed by filename. The implementation deliberately reads and writes `Data/SKSE/Plugins/StorageUtilData/PrintScreen.json`, the same base folder PapyrusUtil's JsonUtil used, so an existing config survives. `Save` writes a temp file first and swaps it in only after a clean write, so a crash mid-save cannot corrupt the config. This native bridge is what made PapyrusUtil and JContainers unnecessary.

### Completion event protocol

Every accepted `TakePhoto` call takes the next value of a process-wide capture sequence counter and returns it as `Started:<seq>`. The C++ completion callback then sends one mod event:

```
event:  PrintScreenComplete
numArg: seq * 10 + status        status: 0 = success, 1 = cancelled, 2 = error
strArg: {"status":"success"|"cancelled"|"error", "seq":N,
         "message":"...", "path":"..."}
```

The outcome travels in `numArg` so Papyrus never has to parse the string: `StringUtil.Find` is case-sensitive, and substring matching over a payload that contains a user-chosen output path was how the previous version misclassified every event. A float holds integers exactly up to 2^24, so the packing stays exact for 1.6 million captures per session.

The JSON in `strArg` is kept for logging and for the error text. It is built by hand with an escaping pass, because Windows paths and error text produce invalid JSON otherwise (`\G` is not a legal escape). The event is queued through the SKSE task interface and delivered as a `ModCallbackEvent`.

On the Papyrus side, `OnPrintScreenComplete` decodes `numArg` (`status = code % 10`, `seq = code / 10`) and drops any event whose `seq` is not the capture it is waiting on. That is what makes a late event harmless: a capture cancelled from the hotkey, or interrupted by a reload, still reports when its worker unwinds, but by then the script has moved on and the stale event cannot clear the state of the capture that replaced it. For the same reason the reload hook no longer sends a synthetic cancelled event; the worker's own callback is the single source of terminal status.

Two more guards live in the script. The busy return string is matched exactly (`Previous capture cancelled`), and a watchdog `RegisterForSingleUpdate` is armed at start for the expected capture length plus a margin. If it fires while the script still thinks a capture is active, it calls `MYReset(false)`: a refusal means the worker is genuinely still running (a slow BC7 or APNG encode) and the timer is re-armed; anything else means the completion event was lost, and the flags are cleared with a notification instead of waiting for the next hotkey press.

### Save-load reliability (the C++ re-arm)

The only Papyrus path that re-registers the hotkey on load is a `ReferenceAlias::OnPlayerLoadGame` chain whose state is baked into the save when the quest first starts. A save from before the alias script existed never fires it, and no replacement `.pex` fixes that save.

So the plugin does it itself. On every `kPostLoadGame` / `kNewGame`:

1. If a capture is active, send a `cancelled` event, then `ForceReset()` the session.
2. Sweep orphaned temp directories.
3. Restore the UI if it was left hidden.
4. Scan every quest for the one with `Printscreen_MainQuest_script` attached (`FindBoundObject`; survives FormID, load-order, and ESP-rename changes), then `DispatchMethodCall` its `InitializePrintscreen()` on the game thread.

`InitializePrintscreen()` is idempotent and unregisters only its own key and event by name, never `UnregisterForAllKeys`, because the MCM script lives on the same quest and the blanket calls also wiped SkyUI's own registration (a real bug from earlier builds, documented in the script comments). Both the Papyrus path and the C++ path firing on a healthy save is harmless; that is the point.

## Configuration

### INI (diagnostics and threading)

Parsed by `Config::Initialize()` at plugin load. Path preference: legacy V2 file first (`...\SKSE\Plugins\Printscreen_Log.ini`), then the current path (`Documents\My Games\Skyrim Special Edition\SKSE\PrintScreen.ini`). If the sectioned format is absent, the legacy flat format (`Level`, `File`, `Console`, `Timestamps`) is parsed for V2 compatibility. Unknown keys are collected and logged as warnings at startup. The file is optional; it is never created by the plugin.

| Section | Key | Type | Default | Range |
|---------|-----|------|---------|-------|
| Logging | `LogLevel` | int/string | 3 (INFO) | 0–5: NONE, ERROR, WARN, INFO, DEBUG, TRACE |
| Logging | `ConsoleOutput` | bool | true | |
| Logging | `FileOutput` | bool | true | |
| Logging | `ShowTimestamps` | bool | true | |
| Logging | `MaxLogFileSizeMB` | uint32 | 8 | 1–50 |
| Performance | `ParallelCompression` | bool | true | |
| Performance | `CompressionThreads` | uint32 | 0 (auto) | 0–16 |
| Capture | `LogCaptureProgress` | bool | false | |
| Capture | `LogTimingInfo` | bool | false | |

### JSON (user settings)

Owned by the MainQuest script. On `OnInit`, if `UseJsonFile` is set: `CheckJson()` (exists? valid JSON object?), then a completeness check on all 24 flat keys. Either failure rewrites the file with current property defaults. Otherwise `ReadJson()` then `ValidateAll()`, where every value is range-checked and out-of-range values reset to defaults, mostly with a notification so you can tell it happened.

## Logging

spdlog with three sinks: a rotating file sink (`Documents\My Games\Skyrim Special Edition\SKSE\Logs\Printscreen.log`, rotating at `MaxLogFileSizeMB`, with a `Data\SKSE\Plugins\Printscreen.log` fallback path), a console sink for the in-game console, and an MSVC output sink for debugger sessions. Levels map 0–5 onto spdlog off/err/warn/info/debug/trace. `logger_shim.hpp` provides UTF-8-safe wrappers for paths with non-ASCII characters.

## Threading model

| Component | Thread | Notes |
|-----------|--------|-------|
| `CaptureSession::Start` | Any (Papyrus VM) | Holds mutex until worker exists |
| Worker (acquire + encode) | Dedicated worker | `std::thread` per capture |
| UI hide/restore | Queued to game thread | `SKSE::GetTaskInterface()->AddTask` |
| Granular Scaleform helpers | Game thread only | GFx is not thread-safe |
| `MenuEventSink::ProcessEvent` | Game thread | Fires the capture cancel |
| `VideoCapture` encode calls | Worker only | Same thread as Initialize |
| `ConsoleCommandQueue` | Any in, game thread out | Mutex-guarded deque |
| `IsGamePaused` native | Any | Reads sink's cached atomic |

Cancellation is cooperative: the token is checked before each frame acquisition and between encode steps. `CancellationToken::ThrowIfCancelled` unwinds with an exception that the session catches into `CALLBACK_CANCELLED`.

## Build and packaging

The full recipe with pinned versions lives in `BUILD.md`; it is the corresponding-source build instruction under GPL-3.0 section 6. Summary:

- Visual Studio 2022 (MSVC 14.44.35207), Windows SDK 10.0.26100, CMake 3.24+ with the presets in `CMakePresets.json`.
- vcpkg triplet `x64-windows-static-md`, registry baseline matching upstream CommonLibSSE-NG 6.4.0. Ports include DirectXTex/DirectXTK, fmt, libpng (apng feature), zlib, lodepng, nlohmann-json, simpleini, spdlog, toml11, rapidcsv, xbyak.
- CommonLibSSE-NG v6.4.0 pinned by tag via `FetchContent`, with `ENABLE_SKYRIM_SE=ON`, `ENABLE_SKYRIM_AE=ON`, `ENABLE_SKYRIM_VR=OFF`, `BUILD_TESTS=OFF`. hde64 comes in through patch-safety support.
- Papyrus: `Printscreen_MCM_Script.psc` extends `SKI_ConfigBase`, so compile with the Creation Kit's `PapyrusCompiler.exe` with SkyUI SDK sources on the import path. `.pex` output goes to `Papyrus Scripts/Compiled/`.
- Packaging: `tools/Package-Mod.ps1` builds the Nexus archive; `tools/Make-CorrespondingSource.ps1` builds the corresponding-source bundle (git tree + fetched deps + vcpkg tarballs) that ships beside every binary release.
- Offline builds from the corresponding-source bundle: point `FETCHCONTENT_SOURCE_DIR_COMMONLIBSSE` and `FETCHCONTENT_SOURCE_DIR_HDE64` at the extracted trees and drop the vcpkg tarballs into the vcpkg downloads cache.

## Known issues and open questions

Verified against source as of this writing; the behavioral ones have not been confirmed in a live game session.

**Suspected, behavioral:**

1. **The MCM's "Automatic Menu Removal" toggle writes the wrong property.** The toggle sets `MainQuest.Menu`; the capture path passes `AutoUI` (parameter 20 of `TakePhoto`), which only the JSON file sets. `Menu` is validated, persisted, and displayed, but never consumed by a capture. `Validate_AutoUI` only falls back to `Menu` when the `AutoUI` key is absent from the JSON. Expected visible effect: the toggle does nothing either way.

**Fixed since the 5.0.0 review, pending a live test:**

- The completion event was never recognized by Papyrus (lowercase JSON statuses against a case-sensitive matcher looking for `CALLBACK_` prefixes and capitalized keywords), so the success notification and shot counter never fired and the state flags stayed set until the next hotkey press. Status now travels as a numeric code with a capture sequence number; see "Completion event protocol".
- Papyrus checked the busy return for `"Already running"`, which `TakePhoto` never returned, so a busy start surfaced as two "Capture failed" notifications. It now matches `Previous capture cancelled` and every rejected start produces one notification.
- The reload hook sent a synthetic cancelled event in addition to the one the interrupted worker sends itself. Removed.
- Leftovers of the removed polling loop (`_IsLongRunningCapture`, an unread start timestamp, a `Result == "Ready"` branch) were deleted; the start timestamp now feeds the watchdog.

**Cosmetic or inert, verified:**

2. Version strings disagree: the plugin is 5.0.0 (`SKSEPluginInfo`), but the MCM header shows the MainQuest `Version` property, still `"4.02"`. `plugin.cpp` also logs "(v4.0 refactored)", and the EXIF `cameraModel`/`software` strings say "PrintScreen V4".
3. `SaveAndHideAllUI` and `RestoreAllUI` are declared native in `Printscreen_Formula_script.psc` but are not registered in `Bindings.cpp`; calling them would fail at runtime. Nothing currently calls them.
4. `TakePhoto_Internal_Json` and `ParseRequestJson` in `Bindings.cpp` form a complete JSON-string capture API, but no Papyrus function is bound to them. Inert until wired up.
5. `Printscreen_MAP_script.GetKeyName(183)` returns "0". Scancode 183 is PrintScreen/SysRq, the default hotkey. Also, the default-key fallback in `Validate_Key_TakePhoto` is 14 (Backspace), not the 183 property default.
6. `RecalculateFPS()` in the MainQuest script is never called.
7. The MCM duration slider runs to 30 seconds while `Validate_Duration` clamps animated captures to 15.
8. The shipped `mod/SKSE/Plugins/StorageUtilData` payloads are stale test data: `PrintScreen.json` is in the old sectioned format (fails the completeness check and is rewritten with defaults on first run, as designed), and nothing in the current scripts reads `PrintScreenConfig.json` at all.
9. `src/IniConfiguration.md` describes an INI schema (`[General]`, `DefaultFormat`, `EnablePerformanceLogging`, `LogPapyrusCalls`) that `config.cpp` does not parse, and claims the INI is auto-created, which it is not. The live schema is in the table above.
10. Since the V4 docs: the log file moved to `SKSE\Logs\Printscreen.log` (previously `SKSE\printscreen.log`).

**Inherited limits, by design:**

- Desktop duplication captures every monitor and cannot capture HDCP-protected content.
- Animated formats stage all frames before encoding, which is why duration is capped at 15 seconds.
- Bilinear is the only scaling filter; Lanczos would look better at 4K-to-1080p but needs a more involved shader.
- MKV falls back to MP4 with a warning.

## Changes from V4

- Native JSON bridge replaces PapyrusUtil and JContainers dependencies.
- Completion switched from Papyrus polling to the `PrintScreenComplete` mod event: numeric status plus capture sequence in `numArg`, JSON detail in `strArg`, and a Papyrus watchdog for a lost event.
- `GameCamera` FOV read and EXIF focal-length metadata on stills.
- `MenuEventSink` cancels captures when pause menus open and caches paused state.
- `TempFileGuard` with a global registry; orphaned temp directories swept on game load.
- C++-driven hotkey re-arm on every save load, replacing the save-baked alias chain as the source of truth.
- Delta mode extended to three values (off / region extraction / true delta with transparency).
- TIFF compression actually applied; previously always uncompressed due to a missing mode mapping.
- Platform move to CommonLibSSE-NG 6.4.0 (SE and AE, no VR), license move from MIT to GPL-3.0-or-later with corresponding-source bundles on every release.

---

*For end-user documentation, see the [user guide](PrintScreenV5_UserGuide.md).*