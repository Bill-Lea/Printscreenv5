# PrintScreen V5 Technical Reference

Version 5.0.3
Author: William G Lea
SKSE plugin for Skyrim Special Edition and Anniversary Edition
Windows 10/11, Direct3D 11, CommonLibSSE-NG 6.4.0

This document describes the plugin as the code stands at 5.0.3. Where the code and the previous documentation disagreed, the code won. Anything surprising is listed under "Known issues" at the end rather than glossed over.

---

## 1. Overview

PrintScreen captures the Windows desktop through the DXGI Desktop Duplication API. It doesn't hook Skyrim's swap chain or renderer. The upside is that it sees exactly what the monitor shows, ENB and overlays included. The downside is that it captures a monitor rather than the game window, and only one monitor at that (see section 5).

The pieces, top to bottom:

1. A Papyrus quest script owns the settings, the hotkey, and the MCM.
2. The hotkey calls one native function, `TakePhoto`, with every setting as an argument.
3. C++ builds a `CaptureRequest`, hides the UI, and starts a worker thread.
4. The worker acquires frames from DXGI and hands them to an encoder: WIC or DirectXTex for stills, a custom GIF or APNG writer for animations, Media Foundation for H.264.
5. When it's done, the worker queues an SKSE mod event back to Papyrus, and the UI is restored.

Design rules the code follows:

- One capture at a time, on its own thread. The game thread never waits on encoding.
- Cancellation is cooperative. Every long loop checks a `CancellationToken`; nothing is killed mid-write.
- UI hide and restore go through the engine's `showMenus` flag, the one the `tm` console command flips.
- Papyrus never polls. Completion arrives as a mod event carrying a numeric status code and a sequence number.
- Settings persist through a small native JSON layer. No PapyrusUtil, no JContainers.

---

## 2. Repository layout

```
PrintscreenV5/
  CMakeLists.txt              root build; pins CommonLibSSE-NG v6.4.0 via FetchContent
  CMakePresets.json           Ninja presets with hard-coded MSVC/SDK paths
  vcpkg.json                  manifest (also the single source of the version string)
  vcpkg-configuration.json    registry baseline
  build.ps1 / .bat / .sh      wrappers around the two cmake calls
  BUILD.md                    full build recipe (GPL section 6 corresponding source)
  LICENSE, LICENSES.md, LICENSE-CommonLibSSE-NG-EXCEPTIONS.md
  src/
    plugin.cpp                SKSE entry point and messaging listener
    pch.h                     precompiled header (CommonLib, Win32, D3D11, WIC, MF, STL)
    Config.h / config.cpp     INI parsing
    logger.*, logger_shim.hpp spdlog setup and UTF-8 helpers
    stringutils.h             UTF-8 <-> UTF-16
    ConsoleCommandQueue.*     run console commands on the game thread
    VideoCapture.*            Media Foundation H.264 sink writer
    capture/
      CaptureSession.*        the worker-thread orchestrator; RunStill/RunAnimated/RunVideo
      CaptureRequest.*        request struct, format enums, string parsers
      CancellationToken.*     shared atomic flag plus a throwing check
      FrameAcquirer.*         D3D11 device + IDXGIOutputDuplication
      FrameQueue.*            bounded producer/consumer ring for video
      GPUScaler.*             bilinear letterbox downscale on the GPU
      GameCamera.*            reads PlayerCamera FOV, converts to 35 mm focal length
      TempFileGuard.*         RAII temp-dir cleanup with a crash-safe registry
    encoding/
      IEncoder.h              interface, EncodeResult, CaptureExif
      StillEncoder.*          PNG/JPEG/BMP/TIFF/GIF via WIC; DDS via DirectXTex; EXIF
      GifEncoder.*            animated GIF via WIC with a shared palette
      ApngEncoder.*           APNG assembled by hand from WIC-encoded PNG frames
    Native/
      PrintscreenJson.*       the JSON API exposed to Papyrus
    papyrus/
      Bindings.*              native function registration, completion event, load hooks
    ui/
      UIController.*          showMenus hide/restore, Scaleform helpers
      MenuEventSink.*         MenuOpenCloseEvent sink; cancels captures
    ScreenCapture.h, screencapture.cpp, cancel.h    legacy forwarding stubs
    IniConfiguration.md       stale; describes an INI schema the code doesn't read
  Papyrus Scripts/            .psc sources; Compiled/ holds the .pex (git-ignored)
  mod/                        ESP, MCM splash textures, default JSON, this documentation
  tools/                      Package-Mod.ps1, Make-CorrespondingSource.ps1, Common.ps1
```

Two zip files under `src/` (`printscreenV4.zip`, `PapyrusInterface_NoMenu_Update.zip`) and the `src/.vs` and `src/out` folders are leftovers and are excluded from the build by the source glob filters.

---

## 3. Building

`BUILD.md` is the authoritative recipe. The short version:

```powershell
cmake --preset release
cmake --build --preset release
```

or `.\build.ps1 -Config Release`, which enters a Visual Studio developer shell, repairs a broken system PATH, detects a stale CMake cache copied from another checkout, and then runs the same two commands.

Toolchain for 5.0.x: Visual Studio 2022 with MSVC 14.44.35207, Windows SDK 10.0.26100, CMake 3.24+, Ninja, and the vcpkg bundled with Visual Studio. Triplet `x64-windows-static-md`. The presets hard-code those paths; edit the `base` preset if yours differ.

Things worth knowing about `src/CMakeLists.txt`:

- C++20, `/W4`, `/permissive-`, `/Zc:preprocessor`, `/utf-8`. Warnings 4100, 4189, and 4324 are silenced.
- Sources are globbed recursively. Anything under `out/`, `files5000/`, `PapyrusInterface_NoMenu_Update/`, or ending in `_patched.cpp` is excluded.
- Precompiled headers: `src/CMakeLists.txt` looks for a `pch.h` at the repository root, which doesn't exist, so it adds CommonLib's `SKSE/Impl/PCH.h`. The root `CMakeLists.txt` then adds `src/pch.h` as well. Both end up in the PCH list.
- Windows SDK libraries are linked by absolute path. The SDK version is taken from `WindowsSDKVersion` in the environment, or the newest folder under `Windows Kits\10\Lib`.
- Neither `version.rc` nor `printscreen.rc` is compiled (the `RESOURCES` line is commented out), so the DLL carries no version resource. Both files are stale anyway (4.0.0 and 5.0.0).
- A post-build step copies the DLL to `C:/Modding/Mo2/Mods/Printscreen/SKSE/Plugins` (cache variable `MO2_MOD_DIR`). Set it to an empty string if you don't want that.

CommonLibSSE-NG options fixed by the root file: SE on, AE on, VR off, tests off, REX INI/JSON/TOML off, Xbyak off. Patch safety stays at its default (on), which is what pulls hde64 in.

Papyrus scripts are compiled with the Creation Kit compiler; `Printscreen_MCM_Script.psc` extends `SKI_ConfigBase`, so the SkyUI SDK sources must be on the import path. Output goes to `Papyrus Scripts/Compiled/`. The MO2 mod folder is the working copy the author actually edits; the git copy was last synced in commit `3995960`. Three of the source files were renamed in `a0b0b51` to match MO2's casing; the script names inside are unchanged.

---

## 4. Runtime flow

```
Printscreen_MainQuest_script.OnKeyUp
   |  debounce, menu checks, "Taking screenshot..." notification
   v
CaptureImage()  ->  Printscreen_Formula_script.TakePhoto(19 args)      [native]
   |
   v  Bindings.cpp: TakePhoto()
   |   BuildRequest() clamps every argument into a CaptureRequest
   |   if autoUI: HideUIAsync() queued to the game thread; sink told menus are hidden
   |   seq = ++g_captureSequence
   v
CaptureSession::Start(req, onComplete, onAcquired)  ->  spawns worker thread
   |                                                    returns "Started:<seq>"
   v  worker thread
   |   RunStill    : FrameAcquirer -> onAcquired() -> GameCamera FOV -> StillEncoder
   |   RunAnimated : FrameAcquirer -> N BMPs in temp dir -> onAcquired() -> Gif/ApngEncoder
   |   RunVideo    : FrameAcquirer (GPU) -> producer thread -> FrameQueue -> VideoCapture
   v
SetResult("CALLBACK_SUCCESS" | "CALLBACK_CANCELLED" | "CALLBACK_ERROR: msg")
   |
   v  onComplete (worker thread)
   |   QueueModEvent("PrintScreenComplete", seq, status, message, path)
   |   RestoreUIAsync() if autoUI
   v
Printscreen_MainQuest_script.OnPrintScreenComplete   (numArg = seq*10 + status)
        matches seq, notifies the player, bumps the shot counter
```

Ordering guarantee: `TakePhoto` queues the UI hide before calling `Start()`, and `Start()` holds its mutex until the worker thread object exists. The worker therefore cannot queue a restore before the hide is queued, so the SKSE task queue always runs hide before restore, even for a still that finishes inside the same frame.

---

## 5. Frame acquisition

`FrameAcquirer` creates its own D3D11 hardware device (with `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`) and enables multithread protection on it, because Media Foundation will share it. It then takes adapter 0 of that device, output 0 of that adapter, and calls `DuplicateOutput`. That is one monitor: the first output on the primary GPU. There's no monitor selection and no logic to find which monitor the game window is on.

Two acquisition paths:

- `AcquireFrame(ScratchImage&)`: the CPU path for stills and animations. Waits up to 100 ms per attempt, five attempts, then copies the desktop texture into a staging texture, maps it, and copies the rows into a `DirectX::ScratchImage` in the desktop's native format (normally BGRA8). The duplication frame is released before the function returns.
- `AcquireFrameGPU(ID3D11Texture2D*&, w, h)`: the video path. Zero timeout, two attempts, returns the raw desktop texture without copying. The texture stays valid until the next call or `ReleaseDesktopFrame()`. The caller must copy it somewhere before then, which `RunVideo` does.

Every step checks the cancellation token and throws `Cancelled` if it's set. The pixel-copy loop checks once before the loop, not inside it, so that a throw can't leak the mapped texture.

---

## 6. CaptureSession

The single-flight orchestrator. One worker thread per capture; the destructor cancels and joins.

```cpp
enum class StartResult { Accepted, AlreadyRunning, InvalidState, BusyCancelled };
StartResult Start(CaptureRequest, CompletionCallback, AcquisitionCallback);
void RequestCancel();
void ForceReset();
bool IsIdle() const;          // Idle or Done
CancellationToken::Ptr GetToken() const;
std::string GetResult();      // legacy polling; consumed on read
```

State goes Idle, Starting, Running, Done. A Done state that nobody consumed is treated as Idle by the next `Start()`.

`Start()` while a capture is active does not queue a second capture. It cancels the running one, joins it, and returns `BusyCancelled`. Papyrus surfaces that as "Previous capture cancelled" and the caller has to press the key again. This is how a stuck-but-still-running worker gets cleared.

Two callbacks, both invoked on the worker thread:

- `AcquisitionCallback` fires once all frames are in hand, before encoding. `TakePhoto` uses it to restore the HUD so a slow DDS or APNG encode doesn't leave the screen bare. Not used for video, where acquisition and encoding overlap for the whole recording.
- `CompletionCallback` receives the `CALLBACK_*` string after encoding, cancellation, or error.

Locking rules that were learned the hard way and are commented in the source: `JoinWorker()` must never run while `mutex_` is held, because the worker's `SetResult()` takes `mutex_` and would deadlock the joiner. Joins are serialised by a separate `joinMutex_` so two concurrent `Start()` calls can't both touch the `std::thread` object. `ForceReset()` on a live worker only signals cancel; it leaves the state alone so a following `Start()` can't run two workers at once.

---

## 7. Still captures

`RunStill` acquires one frame, calls the acquisition callback (HUD back), reads the camera FOV, builds a `CaptureExif`, and calls `StillEncoder::Encode`. It then checks that the output file exists and is non-empty.

### 7.1 GameCamera

`RE::PlayerCamera` holds two FOV floats in degrees: `firstPersonFOV` and `worldFOV`. The code picks the first-person one when `IsInFirstPerson()` is true and the world one otherwise. Values outside 1 to 170 degrees are rejected, with a fallback to the other field, then to 0. The read happens on the worker thread; it's two float loads from a singleton the engine doesn't mutate during a capture.

Focal length uses a 36 mm sensor width and treats the game value as the horizontal FOV:

```
focal35 = 18 / tan(fov / 2)
```

A stitcher gets the pixel focal length as `f_px = width * focal35 / 36`.

### 7.2 StillEncoder

| Format | Encoder | Options |
|--------|---------|---------|
| PNG | WIC via `DirectX::SaveToWICFile` | `PngFilterMethod`: 1 (none) when compression is 0 to 2, else 6 (adaptive). WIC exposes no zlib level, so the 0 to 9 slider only picks between those two |
| JPEG | WIC | `ImageQuality` = quality / 100 |
| BMP | WIC | none |
| TIFF | WIC | `TiffCompressionMethod` from `TiffMode`: NONE, LZW, ZIP, RLE, CCITT3, CCITT4 |
| GIF (still) | WIC | none; WIC quantises with its own default palette |
| DDS | DirectXTex `Compress` + `SaveToDDSFile` | see below |

DDS mode mapping: BC1 through BC5 and BC6H map to their `DXGI_FORMAT_*` with parallel compression. BC7_SLOW is full-quality BC7. BC7_NORMAL and BC7_FAST are the same thing: BC7 with `TEX_COMPRESS_BC7_QUICK`. Any string the parser doesn't recognise, including "UNCOMPRESSED", becomes BC1. There is no uncompressed DDS path. If compression fails the raw image is written instead.

`SaveWIC` initialises COM with a lambda guard rather than RAII, so cancellation inside it returns `E_ABORT` through the guard instead of throwing.

### 7.3 EXIF and the sidecar

After a successful WIC save (any format but BMP), `WriteExifMetadata` reopens the file with a WIC decoder, creates a fast metadata encoder, and writes:

| Tag | Query | Value |
|-----|-------|-------|
| 271 Make | `/app1/ifd/{ushort=271}` | "Skyrim Engine" |
| 272 Model | `/app1/ifd/{ushort=272}` | "PrintScreen V4" |
| 305 Software | `/app1/ifd/{ushort=305}` | "PrintScreen V4" |
| 37386 FocalLength | `/app1/ifd/exif/{ushort=37386}` | focal35 as a double |
| 41989 FocalLengthIn35mmFormat | `/app1/ifd/exif/{ushort=41989}` | rounded to a short |

Those query paths are the JPEG APP1 layout. Individual `SetMetadataByName` results are ignored; only the final `Commit` is checked, and a failed commit is logged as a warning. In practice JPEG gets the tags. TIFF uses a different metadata root in WIC and PNG has none, so those formats shouldn't be counted on. For PNG the code writes a sidecar, `<image>.png.json`, containing `focalLength35mm`, `fov`, `cameraMake`, `cameraModel`, `imageWidth`, and `imageHeight`. PhotoFileMerge V2 reads that sidecar when EXIF is absent.

---

## 8. Animated captures

`RunAnimated` does the following, in order:

1. `StaleTempCleanup::ScanAndRemove(outputDir)` removes any leftover `gif_temp_*` or `apng_temp_*` folders from earlier crashes.
2. Creates `<outputDir>/<gif|apng>_temp_YYYYMMDD_HHMMSS_mmm` and wraps it in a `TempFileGuard`.
3. `CaptureFramesToDisk`: frames = round(duration * fps), duration clamped to 0.1 to 15 s, fps clamped 1 to 60. Each frame is saved as `frame_NNNN.bmp` via WIC. Pacing sleeps in 15 ms slices so cancellation latency stays bounded.
4. Acquisition callback (HUD back).
5. `GifEncoder::EncodeFromFiles` or `ApngEncoder::EncodeFromFiles`.
6. `tempGuard.Cleanup()`, which removes the whole directory tree and only disarms the guard once the directory is confirmed gone. A locked file (antivirus, indexer) means the destructor retries, and failing that the directory stays registered for the next-load sweep.

Both `EncodeFromFiles` entry points log the frame count, fps, loop count, and delta mode they received, so the log shows which mode a given file was made with.

Earlier V5 builds carried an `optimize` argument through this path and a `Quality` property on the Papyrus side. Neither was used by any encoder (Delta Mode 2 is what "optimize" once meant), and both were removed after 5.0.2. Old settings files may still contain `optimize` and `quality` keys; the JSON layer ignores keys it isn't asked for.

### 8.1 GifEncoder (animated)

WIC's GIF encoder writes the container; the interesting work is in preparing frames.

- One palette for the whole animation, built from frame 0 with `InitializeFromBitmap(255 colours)`. A 256th entry, fully transparent, is appended at index `transparentIdx`. Because quantisation can only emit indices below that, the transparent slot can never collide with a real colour.
- Every frame is quantised against that shared palette with `WICBitmapPaletteTypeCustom` and no dithering. Error diffusion re-randomises on the per-pixel churn of TAA and film grain, which defeated delta detection and looked like crawling noise.
- Delta modes 1 and 2 compare palette indices with the previous frame to find the changed rectangle. Mode 2 additionally writes `transparentIdx` for unchanged pixels inside that rectangle and sets the GCE transparency flag (as `VT_BOOL`, which is what the WIC schema wants) and transparent colour index.
- Disposal is 1 (do not dispose) on every frame. Restore-to-background caused whole-canvas flashes between delta frames.
- Frame delay is `round(100 / clamp(fps, 1, 30))` hundredths of a second.
- NETSCAPE2.0 loop extension carries the loop count; 0 means forever.
- Every HRESULT in the frame loop is checked and aborts the encode with a named error. The final `encoder->Commit()` is checked too; a failure there used to be reported as success with a broken file on disk.

### 8.2 ApngEncoder

`EncodeFromFiles` loads every BMP into memory first, then calls `EncodeSequence`. That is the memory ceiling for APNG and the reason the 15 s cap exists.

Frame 0 is encoded to PNG by WIC and its chunks are re-emitted with `acTL` and an `fcTL` inserted after `IHDR`. Each later frame is encoded to its own PNG and its `IDAT` chunks are rewritten as `fdAT` with a running sequence number. Delta modes compute a changed rectangle with a per-channel threshold of 2; mode 2 zeroes alpha on unchanged pixels and the frame is blended OVER, mode 1 and full frames use SOURCE. Disposal is NONE on every frame. Delay is `1000 / fps` over 1000. Output is written in one `ofstream::write`.

---

## 9. Video captures

`RunVideo` is the most involved path. In order:

1. `FrameAcquirer::Initialize`, then `AcquireFrameGPU` for the first frame, retrying up to ten times at 50 ms because desktop duplication often reports a timeout on the first call.
2. `ResolveTargetResolution`: 0 = native; 1 to 4 pick a height of 720, 1080, 1440, or 2160. If the desktop is already no taller than the target there's no scaling. Otherwise width is derived from the desktop aspect and both dimensions are rounded up to even, which Media Foundation requires.
3. `VideoCapture::Initialize` with the acquirer's D3D11 device and a `VideoCaptureConfig` from `BuildVideoConfig`.
4. If scaling, a `GPUScaler` is created.
5. A pool of 16 textures (queue capacity 12 plus 4) is allocated at the target size. Every acquired frame is copied into the next pool slot, then the duplication frame is released immediately. This exists because the duplication surface and the scaler output are both single reused textures; queueing raw pointers to them made the encoder race the producer.
6. Frame 0 is scaled, copied, and queued at t = 0.
7. A producer thread acquires frames 1 to N-1. It paces with `QueryPerformanceCounter`, sleeps to the target time, records the capture timestamp in 100 ns units at acquisition, and drops a frame if it's more than one frame interval behind schedule. A `DXGI_ERROR_WAIT_TIMEOUT` retries the same index after a 1 ms sleep.
8. The worker thread is the consumer. It pops from the `FrameQueue`, enforces strictly increasing timestamps, and calls `VideoCapture::EncodeFrame`.
9. After the producer joins: on error, `Abort()`; on cancel, `Abort()` and `CALLBACK_CANCELLED`; otherwise `Finalize()` and a non-empty-file check.

### 9.1 GPUScaler

A fullscreen triangle strip drawn from `SV_VertexID` with a bilinear sampler into a BGRA8 render target. HLSL is compiled at runtime with `D3DCompile` (vs_4_0 / ps_4_0); there are no shader files to ship. A constant buffer carries the letterbox scale and offset; bars are black. An event query is issued after the draw so the encoder's later read doesn't stall the CPU with a `Flush`.

### 9.2 VideoCapture

Media Foundation `IMFSinkWriter` writing an MP4 with one H.264 stream, Main profile, progressive, input subtype `ARGB32` (which is how MF names BGRA8). The sink writer inserts a colour converter to NV12 for the encoder.

Config mapping from `BuildVideoConfig`:

| Setting | Value |
|---------|-------|
| Bitrate by preset | Low 4000, Medium 8000, High 16000, Very High 35000 kbps; Custom uses the slider |
| Keyframe interval | `max(1, floor(seconds))`; GOP = interval * fps |
| Rate control | CBR, unconstrained VBR (default), or Quality (CQP) via `CODECAPI_AVEncCommonRateControlMode` |
| CQP quality | fixed at 70 (`qualityCqp` isn't exposed to Papyrus) |
| Encoder preference | Auto and PreferHardware both set `MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS`; ForceSoftware clears it. PreferHardware doesn't actually verify a hardware MFT was chosen; it only fails if `SetInputMediaType` fails |
| Container | MKV is accepted by the enum and immediately downgraded to MP4 with a warning. `BuildRequest` clamps the value to 0 anyway |

`MF_LOW_LATENCY` is off and sink-writer throttling is left on. Throttling matters here: it makes `WriteSample` block once a few samples are in flight, which bounds how many pool slots MF can reference and keeps the producer from overwriting one still being read.

`Abort()` tries a partial `Finalize()` if any frames were encoded, and keeps the file if that succeeds. That's why a hotkey-cancelled recording is usually playable. The partial file is only deleted if the finalize fails. A crash skips all of this and leaves an MP4 with no `moov` atom.

`Initialize`, `EncodeFrame`, and `Finalize` must all run on the same thread. They do: the capture worker.

---

## 10. UI control

### UIController

`HideAll()` saves the current `RE::UI::IsShowingMenus()` value and calls `ShowMenus(false)`; `RestoreAll()` puts the saved value back. The hidden flag is claimed with `atomic::exchange` so overlapping calls from the worker, the game thread, and the menu sink can't clobber the saved value. Because `showMenus` is a single bool checked at the top of the engine's UI render pass, the write is safe from any thread, and `HideAllAsync`/`RestoreAllAsync` are plain forwarding calls. `Bindings.cpp` still routes its calls through the SKSE task queue to get the hide-before-restore ordering described in section 4.

The Scaleform helpers (`SetSubtitlesVisible`, `SetCompassVisible`, `SetHUDAlpha`, and so on) poke `_root.HUDMovieBaseInstance.*` in the HUD movie. They are game-thread only and nothing calls them today.

### MenuEventSink

Registered on `kDataLoaded` as a `MenuOpenCloseEvent` sink. Two jobs:

1. Maintain a cached `isPaused_` flag, updated on both open and close of any menu in `kPauseOrInputMenus` or any menu whose flags include `kPausesGame`. `IsGamePaused` (native) returns this. Nothing in the shipped Papyrus calls it.
2. Cancel captures. Only when `menusHidden_` is true, meaning a capture started with the Menu toggle on. Menus in `kIgnoredMenus` (console, HUD, fader, cursor, loading, tween, and the like) are skipped. For any other menu opening, the sink locks its weak token and cancels. Pause/input menus also restore the UI immediately; other menus leave the restore to the completion callback. If there's no live token it just restores the UI as a safety measure.

The practical effect: with Automatic Menu Removal off, opening a menu never cancels a capture.

### ConsoleCommandQueue

A mutex-guarded queue of console command strings drained on the game thread through `SKSE::GetTaskInterface`. `UIController::ExecuteConsoleAsync` wraps it. Retained from V4; the current hide/restore path doesn't use console commands.

---

## 11. Temp file management

`TempFileGuard` wraps one temp directory. Destruction or `Cleanup()` removes it with `remove_all` (never a throwing iterator inside a `noexcept` destructor, which used to terminate the process). The directory is also placed in a static registry so it can be cleaned after a crash, when no destructor ever ran.

On every `kPostLoadGame` and `kNewGame`, `Bindings::OnPostLoadGame` does, in this order:

1. `CaptureSession::ForceReset()`, so a running worker is cancelled before its directory is touched.
2. `TempFileGuard::CleanupAllRegistered()`. Directories that can't be removed stay registered for the next attempt.
3. Restore the UI if the sink says it was left hidden, and clear the sink's token.
4. Re-arm the Papyrus hotkey (section 12.5).

`StaleTempCleanup::ScanAndRemove` runs at the start of every animated capture and catches directories that match the naming pattern but were never registered, for example after a crash and reinstall.

---

## 12. Papyrus layer

| Script | Extends | Attached to | Role |
|--------|---------|-------------|------|
| `Printscreen_MainQuest_script` | Quest | `Printscreen_MainQuest` | settings, JSON persistence, hotkey, capture, completion, watchdog |
| `Printscreen_MCM_Script` | `SKI_ConfigBase` | same quest | two-page MCM |
| `Printscreen_Formula_script` | Quest | (hidden) | native capture API declarations |
| `Printscreen_JSON_script` | Hidden | | native JSON API declarations |
| `Printscreen_MAP_script` | Quest | | key code to name map; replaces the old JContainers map |
| `Printscreen_ME_script` | ActiveMagicEffect | `Printscreen_MajicEffect` on spell `PS_Configuration` | settings summary message box |
| `Printscreen_PlayerRef_Script` | ReferenceAlias | player alias on the quest | `OnPlayerLoadGame` forwarder |

### 12.1 Native capture API (`Printscreen_Formula_script`)

```
bool   CheckPath(string path)
string TakePhoto(string basePath, string imageType, float jpgCompression, string Mode,
                 float Duration, float Fps, int LoopCount, int DeltaMode,
                 int Compression, float VideoDuration, int TargetResolution,
                 int VideoFrameRate, int QualityPreset, int VideoBitrate,
                 float KeyframeInterval, int EncoderPreference, int RateControl,
                 int VideoContainer, bool Menu = true)
string Cancel()                     "Cancelled" | "Nothing to cancel"
string MYReset(bool force = false)  "Reset complete" | "Cannot reset while active"
bool   IsGamePaused()
string Get_Result()                 always "Deprecated"
```

`CheckPath` accepts `X:\...`, `X:/...`, or `\\server\...`, creates the directory if needed, and writes and deletes a probe file.

`TakePhoto` returns at once with `Started:<seq>`, `Previous capture cancelled`, or `Error: Unable to start capture`. `Mode` is a single string that's parsed both as a TIFF mode and as a DDS mode; whichever parser recognises it wins, the other falls back to its default. The `Menu` argument becomes `CaptureRequest::autoUI`.

`SaveAndHideAllUI` and `RestoreAllUI` are declared in the `.psc` but not registered in C++. Calling them would fail at runtime; nothing does.

Clamps applied in `BuildRequest` before the request reaches the session: JPEG 0 to 100; animated duration 0.1 to 60 (then 0.1 to 15 in the session); fps 1 to 60; loop 0 to 100; delta 0 to 2; PNG 0 to 9; resolution 0 to 4; frame rate 30 to 60; preset 0 to 4; bitrate 1000 to 50000; keyframe 0.5 to 10; encoder 0 to 2; rate control 0 to 2; container 0.

### 12.2 Native JSON API (`Printscreen_JSON_script`)

```
bool   JsonExists(file)      bool IsGood(file)        string GetErrors(file)
bool   Load(file)            bool Save(file)
int    SetIntValue(file, key, v)    float SetFloatValue(...)   string SetStringValue(...)
int    GetIntValue(file, key, missing=0)   float GetFloatValue(..., 0.0)   string GetStringValue(..., "")
bool   HasIntValue(file, key)   bool HasFloatValue(...)   bool HasStringValue(...)
```

Files live in `Data/SKSE/Plugins/StorageUtilData/<file>.json`, the folder PapyrusUtil used, so a V4 config carries over. `.json` is appended if missing; absolute paths and `..` are refused. File names are cached case-insensitively (Papyrus is case-insensitive) and keys are folded to lower case before every read and write, because the Papyrus compiler deduplicates string literals without regard to case and a re-cased property name silently changed the key being written.

On load, `MigrateLegacyData` collapses root keys that differ only by case (the lowercase spelling wins) and hoists values out of PapyrusUtil's `int`, `float`, and `string` containers onto the root, without overwriting a root value that already exists. `IsGood` and `Load` re-read from disk; the `Get*`/`Has*` calls use the cache. `Save` writes `<file>.json.tmp` and renames it over the original, so a crash mid-save can't leave a truncated file. `GetFloatValue` accepts integers; `GetIntValue` does not accept floats.

### 12.3 Settings file schema

Written by `WriteJson()` on MCM close (when `UseJsonFile` is on), read by `ReadJson()` in `OnInit`, checked by `ValidateAll()`. All 22 keys must be present with the right type or the file is rewritten from property defaults. Extra keys (such as `optimize` and `quality` from earlier 5.0.x builds) are ignored.

| Key (stored lower case) | Type | Property default | Validation on load |
|------|------|---------|------------|
| path | string | `C:/Pictures` | `CheckPath` must pass, else reset |
| imagetype | string | PNG | canonicalised to PNG, BMP, JPG, GIF, TIF, DDS, AGIF, APNG, H264, else PNG |
| jpg_compression | float | 90 | 1 to 100 |
| mode | string | UNCOMPRESSED | one of the DDS or TIFF names, else UNCOMPRESSED |
| duration | float | 5 | 1 to 15 |
| fps | float | 15 | below 15 is reset to 10 (yes, 10) |
| loopcount | int | 0 | outside 0 to 20 resets to 0 |
| compression | int | 9 | 0 to 9 |
| deltamode | int | 0 | 0 to 2 |
| tif_mode | string | UNCOMPRESSED | RLE, LZW, ZIP, else UNCOMPRESSED |
| dds_mode | string | UNCOMPRESSED | the ten MCM names, else UNCOMPRESSED |
| videoduration | float | 10 | outside (0, 120] resets to 15 |
| targetresolution | int | 0 | 0 to 4 else 0 |
| videoframerate | int | 30 | 30 or 60 else 30 |
| qualitypreset | int | 2 | 0 to 4 else 2 |
| videobitrate | int | 8000 | Custom preset: 1000 to 50000; any other preset forces 8000 |
| keyframeinterval | float | 2 | above 10 resets to 2 |
| encoderpreference | int | 0 | 0 to 2 else 0 |
| ratecontrol | int | 1 | 0 to 2 else 0 |
| videocontainer | int | 0 | always 0 |
| menu | int | 1 | absent means true |
| key_takephoto | int | 14 (Backspace) | must be in `Printscreen_MAP_script`, else 14 |

The `PrintScreen.json` in `mod/` is a leftover from a test setup. It's in the mixed layout the migrator handles, and after migration it passes the completeness check, so it is loaded rather than replaced. Its effective values are path `C:/pictures/test/vanilla`, key 54 (Right Shift), TIFF mode LZW. See Known issues.

### 12.4 Completion protocol

Every accepted `TakePhoto` takes the next value of a process-wide counter and returns it in `Started:<seq>`. The worker's completion callback sends one mod event:

```
event   PrintScreenComplete
numArg  seq * 10 + status        0 success, 1 cancelled, 2 error
strArg  {"status":"...","seq":N,"message":"...","path":"..."}
```

Papyrus reads only `numArg`: `status = code % 10`, `seq = code / 10`. An event whose sequence isn't the one the script is waiting on is dropped. This is what makes a late cancel event harmless and why the reload hook no longer sends a synthetic one. A float holds integers exactly to 2^24, so the packing is exact for 1.6 million captures per session. `strArg` is hand-built with a JSON escaping pass (Windows paths broke it before) and is only used for the error text.

The script also arms a watchdog with `RegisterForSingleUpdate` when a capture starts: video duration plus 30 s, animation duration plus 90 s, or 60 s for stills. If it fires, the script asks `MYReset(false)`. "Cannot reset while active" means the worker is genuinely still going (BC7_SLOW, a long APNG) and the timer is re-armed for 30 s. Anything else means the completion event was lost, and the flags are cleared with a notification.

### 12.5 Hotkey handling and save-load re-arm

`OnKeyUp` ignores the key while the MCM is open, while `UI.IsTextInputEnabled()`, or while `Utility.IsInMenuMode()`. A second press during a capture calls `Cancel()` and resets the script's state. Presses within 0.75 s of the previous one are ignored, with a guard for a save-baked timestamp from a longer earlier session.

The Papyrus-side re-arm is `Printscreen_PlayerRef_Script.OnPlayerLoadGame` (which must be that exact event name; the earlier `OnGameLoad` compiled fine and never fired) calling `MainQuest.InitializePrintscreen()`. That chain depends on alias state baked into the save. So C++ does it too: on every `kPostLoadGame`/`kNewGame` the plugin scans all quests for the one with `Printscreen_MainQuest_script` bound (`FindBoundObject`, immune to FormID and load-order changes), and dispatches `InitializePrintscreen()` through the VM on the game thread.

`InitializePrintscreen()` unregisters only its own key and mod event by name. `UnregisterForAllKeys` was tried and it wiped SkyUI's registration on the same quest, which broke the MCM. It also resets the debounce timestamp and the `bConfigOpen` flag, both of which could be stuck in a save. Running it twice on a healthy load is harmless.

The MCM's `OnOptionKeyMapChange` and `OnConfigClose` both go through `MainQuest.UpdateHotkey()`, because key registrations are per script instance and the MCM script has no `OnKeyUp`.

---

## 13. Configuration and logging

### INI

Parsed once in `Config::Initialize()` at plugin load. The path is `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\PrintScreen.ini`, unless the V2 file `...\SKSE\Plugins\Printscreen_Log.ini` exists, in which case that one is used. If the game's Documents folder is redirected (OneDrive), this lookup may point somewhere different from where SKSE writes its own logs. The file is never created.

Sectioned format is detected by the presence of `[Logging] LogLevel`. Without it, the legacy flat keys `Level`, `File`, `Console`, `Timestamps` are read by a hand parser (the Win32 profile API can't read keys outside a section). Unknown keys are collected and logged as warnings.

| Section | Key | Default | Used by |
|---------|-----|---------|---------|
| Logging | LogLevel | INFO | spdlog level |
| Logging | ConsoleOutput | true | adds an `msvc_sink` (OutputDebugString), not the in-game console |
| Logging | FileOutput | true | adds the rotating file sink |
| Logging | ShowTimestamps | true | log pattern |
| Logging | MaxLogFileSizeMB | 8 | rotation size, 3 files kept |
| Performance | ParallelCompression | true | parsed, unused |
| Performance | CompressionThreads | 0 | parsed, unused |
| Capture | LogCaptureProgress | false | parsed, unused |
| Capture | LogTimingInfo | false | parsed, unused |

`Config::Settings::ddsMode` exists with a default of BC7 and is not read from the INI or used anywhere; the DDS mode comes from the request.

### Log

`Documents\My Games\Skyrim Special Edition\SKSE\Logs\Printscreen.log`, falling back to `Data\SKSE\Plugins\Printscreen.log` if `USERPROFILE` is unavailable. Pattern `[date time.ms] [level] message`. Flushed on every error. `VideoCapture` logs through `spdlog` directly with a `[VideoCapture]` prefix; everything else uses the `logger::` wrappers.

---

## 14. Threading summary

| What | Thread | Notes |
|------|--------|-------|
| `TakePhoto`, `Cancel`, `MYReset`, JSON API | Papyrus VM thread | JSON layer is fully mutexed |
| `CaptureSession::Start` | caller | holds `mutex_` until the thread object exists |
| Still and animated pipelines | capture worker | one `std::thread` per capture |
| Video acquisition | producer thread spawned by the worker | paced with QPC |
| Video encoding | capture worker | same thread as `VideoCapture::Initialize` |
| UI hide/restore | queued to game thread via SKSE tasks | the flag write itself is thread-safe |
| `MenuEventSink::ProcessEvent` | game thread | cancels via weak token |
| `PrintScreenComplete` dispatch | queued to game thread | `ModCallbackEvent` |
| Hotkey re-arm | queued to game thread | one frame after `kPostLoadGame` |
| Temp cleanup on load | game thread | after `ForceReset` |

Cancellation: `CancellationToken` is a shared atomic. `ThrowIfCancelled(stage)` logs the stage and throws `Cancelled`, which the session catches into `CALLBACK_CANCELLED`. Encoders that can't let an exception cross a COM boundary check `IsCancelled()` and return `E_ABORT` or `EncodeResult::Cancelled()` instead.

---

## 15. Packaging and release

- `tools/Package-Mod.ps1` builds `dist/Printscreen.7z`: the `mod/` tree, `SKSE/Plugins/Printscreen.dll`, every `.pex` and `.psc`, the three license files, and a generated `SOURCE.txt` naming the tag and commit. It refuses to run if any `.psc` lacks a matching `.pex`.
- `tools/Make-CorrespondingSource.ps1` builds `dist/Printscreen-<version>-corresponding-source.7z` from `git archive`, the fetched CommonLibSSE-NG and hde64 trees under `build/_deps`, the vcpkg source tarballs from the downloads cache, port recipes when the registry clone is available, a `VCPKG-BASELINE.txt`, and a SHA-256 manifest. It checks installed port versions against a hard-coded table and stops on any mismatch. OpenVR's binaries and samples are stripped since VR isn't built.
- The version string comes from `vcpkg.json`. Bumping a release means editing `CMakeLists.txt` (`project VERSION`), `vcpkg.json`, `plugin.cpp` (`kPluginVersion`), and the `Version` property in the MainQuest script.
- Both scripts need `7z.exe` on PATH or in the default install location.

---

## 16. Known issues

Verified against the 5.0.3 source. None of these are hidden from users; the user guide says what it needs to.

Behaviour that differs from what the MCM implies:

1. **Shipped settings file.** `mod/SKSE/Plugins/StorageUtilData/PrintScreen.json` survives migration and validation, so a fresh install writes to `C:/pictures/test/vanilla` with Right Shift as the hotkey. It should be replaced with a file of property defaults, or removed so `OnInit` generates one.
2. **DDS "UNCOMPRESSED" produces BC1.** `ParseDDSMode` defaults to BC1 and `SaveDDS` has no uncompressed branch. BC7_NORMAL and BC7_FAST are identical.
3. **PNG compression 3 to 9 are identical.** WIC exposes only the filter method; the slider selects none (0 to 2) or adaptive (3 to 9).
4. **`Validate_Fps` resets any value under 15 to 10.** The MCM slider's own default button says 10, the property default is 15.
5. **The single `Mode` string serves both TIFF and DDS.** Choosing a DDS mode and then switching to TIF leaves `Mode` set to a DDS name, which the TIFF parser reads as NONE. The reverse gives BC1.
6. **The animated duration slider runs to 30** while both Papyrus and C++ clamp to 15.
7. **`Validate_RateControl` resets invalid values to 0 (CBR)** rather than the default 1; `Validate_VideoDuration` resets to 15 rather than 10. Cosmetic.
8. **Only output 0 of adapter 0 is captured.** No monitor selection.

Cosmetic or inert:

9. Version strings disagree: `kPluginVersion` 5.0.3, MainQuest `Version` "5.03", `plugin.cpp` logs "v4.0 refactored", EXIF `Model`/`Software` say "PrintScreen V4", and the two uncompiled `.rc` files say 4.0.0 and 5.0.0.
10. `SaveAndHideAllUI` and `RestoreAllUI` are declared in `printscreen_formula_script.psc` but not registered.
11. `TakePhoto_Internal_Json` and `ParseRequestJson` form a JSON-string capture entry point that isn't bound to any Papyrus name. Its clamps also differ slightly from `BuildRequest` (encoder and rate control allow 0 to 4).
12. `RecalculateFPS()` in the MainQuest script is never called.
13. `IsGamePaused` is registered and cached but no shipped script calls it; `OnKeyUp` uses `Utility.IsInMenuMode()` instead.
14. `Printscreen_MAP_script.GetKeyName(183)` returns "0" for the PrintScreen key. The key still validates and works; only the summary spell's message box shows the wrong name.
15. `src/IniConfiguration.md` describes `[General]`, `DefaultFormat`, `DefaultQuality`, `ReloadConfig()`, and auto-creation of the INI. None of that exists. The `[Performance]` and `[Capture]` keys that do parse aren't consulted.
16. `mod/SKSE/Plugins/StorageUtilData/PrintScreenConfig.json` is read by nothing.
17. `Native/README_INTEGRATION.txt` describes the V4-era integration steps and is out of date.

Limits by design:

- Desktop duplication can't capture protected content and captures one monitor.
- Animated frames are staged to disk and, for APNG, fully loaded before encoding. Hence the 15 s cap.
- Bilinear is the only video scaling filter.
- MKV isn't implemented; MP4 only.
- CQP quality is fixed at 70.

---

## 17. What changed from V4

- PapyrusUtil and JContainers replaced by `PrintscreenJson` and a pure-Papyrus key map.
- Polling replaced by the `PrintScreenComplete` event with sequence matching and a watchdog.
- FOV read and EXIF focal length on stills, plus the PNG sidecar.
- `MenuEventSink` cancels captures on menu open and caches pause state.
- `TempFileGuard` registry and orphan sweep on load.
- Hotkey re-armed from C++ on every load.
- Delta mode grew a third value; GIF encoding moved to a single shared palette with a reserved transparent index, which fixed the flicker and black-blotch problems.
- The inert `Optimize` and `Quality` settings were removed after 5.0.2, taking `TakePhoto` from 20 arguments to 19.
- TIFF compression is actually applied (the mode string wasn't parsed before).
- Video acquisition moved to a producer thread with a texture pool, capture-time timestamps, and frame-drop recovery; cancelled recordings are finalised where possible.
- CommonLibSSE-NG 6.4.0, SE and AE only. License moved from MIT to GPL-3.0-or-later with a corresponding-source bundle per release.

---

*For the player-facing documentation, see the [user guide](PrintScreenV5_UserGuide.md).*
