# PrintScreen V4 — Technical Reference

**Version:** 4.0.0  
**Author:** William G Lea  
**SKSE Plugin for Skyrim Special Edition**  
**Platform:** Windows 10/11, DirectX 11

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [INI Parameter Reference](#ini-parameter-reference)
3. [Source Code Organization](#source-code-organization)
4. [Capture Pipeline](#capture-pipeline)
5. [Encoding Subsystems](#encoding-subsystems)
6. [Video Capture (H.264/MP4)](#video-capture-h264mp4)
7. [UI Controller](#ui-controller)
8. [Papyrus Integration](#papyrus-integration)
9. [Configuration System](#configuration-system)
10. [Logging System](#logging-system)
11. [Threading Model](#threading-model)
12. [Build System](#build-system)
13. [Known Issues & Limitations](#known-issues--limitations)

---

## Architecture Overview

PrintScreen V4 is a zero-integration SKSE plugin. It does **not** hook the game's renderer or intercept swap chains. Instead, it uses the Windows **Desktop Duplication API** (`IDXGIOutputDuplication`) to capture frames directly from the display output.

### Design Philosophy

- **No game integration:** Works regardless of rendering API, ENB, ReShade, or overlay
- **GPU-first:** D3D11 textures flow through GPU paths where possible (video encoding, scaling)
- **Async worker thread:** Capture runs on a dedicated thread so the game thread is never blocked
- **Cooperative cancellation:** Long-running operations (GIF encoding, video capture) check a cancellation token

### Data Flow

```
Desktop Duplication API
        │
        ▼
┌─────────────────┐
│  FrameAcquirer  │  ← Owns D3D11 device + DXGI duplication
│   (src/capture) │
└────────┬────────┘
         │ ID3D11Texture2D (GPU texture)
         ▼
┌─────────────────┐     ┌──────────────────┐
│   GPUScaler     │     │   VideoCapture   │
│ (GPU downscale) │     │  (H.264 encoder) │
└────────┬────────┘     └──────────────────┘
         │
         ▼ (optional)
┌─────────────────┐
│ ScratchImage    │  ← CPU staging copy (stills, GIF, APNG)
│ (DirectXTex)    │
└────────┬────────┘
         │
    ┌────┴────┬────────┬────────┐
    ▼         ▼        ▼        ▼
 StillEncoder GifEncoder ApngEncoder
 (PNG/JPEG/   (GIF)    (APNG)
  BMP/TIFF/
  DDS)
```

---

## INI Parameter Reference

### File Locations

| Variant | Path |
|---------|------|
| **Current** | `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\PrintScreen.ini` |
| **Legacy (V2)** | `%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\Plugins\Printscreen_Log.ini` |

If the legacy file exists, it is read for backward compatibility. Otherwise, the current path is used.

### Section: `[Logging]`

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `LogLevel` | string/int | `3` (`INFO`) | `0`–`5` or `NONE`/`ERROR`/`WARN`/`INFO`/`DEBUG`/`TRACE` | Verbosity of log output |
| `ConsoleOutput` | bool | `true` | `true`/`false` | Also print to in-game console |
| `FileOutput` | bool | `true` | `true`/`false` | Write to `printscreen.log` |
| `ShowTimestamps` | bool | `true` | `true`/`false` | Prefix log lines with timestamps |
| `MaxLogFileSizeMB` | uint32 | `8` | `1`–`50` | Log rotation threshold |

#### LogLevel Mapping

```cpp
0 → "NONE"   → spdlog::level::off
1 → "ERROR"  → spdlog::level::err
2 → "WARN"   → spdlog::level::warn
3 → "INFO"   → spdlog::level::info   (default)
4 → "DEBUG"  → spdlog::level::debug
5 → "TRACE"  → spdlog::level::trace
```

Any unrecognized string falls back to `INFO` (3).

#### Backward-Compatible Legacy Keys

The parser also accepts flat (unsectioned) legacy keys for V2 configs:

| Legacy Key | Maps To | Section |
|-----------|---------|---------|
| `Level` | `LogLevel` | `[Logging]` |
| `File` | `FileOutput` | `[Logging]` |
| `Console` | `ConsoleOutput` | `[Logging]` |
| `Timestamps` | `ShowTimestamps` | `[Logging]` |
| `LogPath` | *(unused)* | — |

### Section: `[Performance]`

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `ParallelCompression` | bool | `true` | `true`/`false` | Multi-threaded PNG/APNG compression |
| `CompressionThreads` | uint32 | `0` | `0`–`16` | Thread count (`0` = auto = CPU core count) |

### Section: `[Capture]`

| Parameter | Type | Default | Range | Description |
|-----------|------|---------|-------|-------------|
| `LogCaptureProgress` | bool | `false` | `true`/`false` | Log per-frame progress during capture |
| `LogTimingInfo` | bool | `false` | `true`/`false` | Log operation timing (benchmarking) |

### Valid Key Registry

The config parser maintains a whitelist. Unknown keys trigger warnings:

```cpp
// Sectioned format keys
L"loglevel", L"consoleoutput", L"fileoutput", L"showtimestamps", L"maxlogfilesizemb"
L"parallelcompression", L"compressionthreads"
L"logcaptureprogress", L"logtiminginfo"

// Legacy flat format keys
L"level", L"file", L"console", L"timestamps", L"logpath"
```

### INI Validation

`Config::Initialize()` performs key validation by:
1. Opening the INI file directly (not via Win32 APIs)
2. Stripping comments (`;` and `#`)
3. Parsing `key=value` pairs
4. Checking each key against the whitelist
5. Storing unknown keys in `g_unknownKeys` for logging

---

## Source Code Organization

```
PrintscreenV4/
├── CMakeLists.txt              # Root: SKSE dependency, vcpkg
├── src/
│   ├── CMakeLists.txt          # Main: all targets, PCH, link libs
│   ├── pch.h                   # Precompiled header (spdlog, fmt, Windows)
│   ├── plugin.cpp              # SKSE entry points (Load, Query, Version)
│   ├── Config.h / config.cpp   # INI parsing, Settings struct
│   ├── logger.h / logger.cpp   # spdlog wrapper, SetupLog()
│   ├── logger_shim.hpp         # UTF-8 conversion helpers
│   ├── stringutils.h           # wstring<->utf8 converters
│   ├── ConsoleCommandQueue.cpp/h  # Thread-safe console command queue
│   ├── ScreenCapture.cpp/h     # Legacy stub (replaced by capture/)
│   ├── cancel.h                # Cooperative cancellation macro
│   ├── VideoCapture.cpp/h      # Media Foundation H.264 encoder
│   ├── capture/
│   │   ├── CaptureSession.cpp/h     # Worker thread orchestrator
│   │   ├── CaptureRequest.cpp/h     # Request struct + format parsing
│   │   ├── FrameAcquirer.cpp/h      # DXGI Desktop Duplication wrapper
│   │   ├── FrameQueue.cpp/h         # Thread-safe frame buffer queue
│   │   ├── GPUScaler.cpp/h          # D3D11 bilinear downscaler
│   │   └── CancellationToken.cpp/h  # Cooperative cancellation
│   ├── encoding/
│   │   ├── IEncoder.h               # Abstract encoder interface
│   │   ├── StillEncoder.cpp/h       # PNG/JPEG/BMP/TIFF/DDS
│   │   ├── GifEncoder.cpp/h         # Animated GIF (GIF89a)
│   │   └── ApngEncoder.cpp/h        # Animated PNG
│   ├── papyrus/
│   │   └── Bindings.h               # Papyrus registration stubs
│   └── ui/
│       ├── UIController.cpp/h       # UI hide/show (showMenus)
│       └── MenuEventSink.cpp/h      # Menu open/close event listener
└── PrintScreenV4_VideoCapture_DesignNotes.md
```

---

## Capture Pipeline

### 1. CaptureSession (Orchestrator)

```cpp
class CaptureSession {
public:
    enum class StartResult {
        Accepted,        // Capture started
        AlreadyRunning,  // Worker thread busy
        InvalidState,    // Unexpected state machine state
        BusyCancelled    // Previous capture cancelled, ready for retry
    };

    StartResult Start(CaptureRequest request,
                      CompletionCallback onComplete,
                      AcquisitionCallback onAcquisitionComplete);
    void RequestCancel();
    std::string GetResult();  // Papyrus polling: "Ready", "Starting", "Running", "CALLBACK_SUCCESS", ...
};
```

**State Machine:**

```
Idle → Starting → Running → Done → (consumed) → Idle
         │           │
         └───────────┘ (RequestCancel → CALLBACK_CANCELLED)
```

### 2. FrameAcquirer (Desktop Duplication)

```cpp
class FrameAcquirer {
    HRESULT Initialize();                          // Create D3D11 device + IDXGIOutputDuplication
    HRESULT AcquireFrame(DirectX::ScratchImage& out);       // CPU path
    HRESULT AcquireFrameGPU(ID3D11Texture2D*& out, uint32_t& w, uint32_t& h);  // GPU path
};
```

**Critical:** The D3D11 device **must** be created with `D3D11_CREATE_DEVICE_VIDEO_SUPPORT` for hardware video encoding to work. Without this flag, Media Foundation hardware MFTs silently refuse to bind.

### 3. GPUScaler (Resolution Scaling)

Implements bilinear downscaling via a fullscreen render quad:
- No vertex buffer (generates quad from `SV_VertexID`)
- Letterbox-preserving aspect ratio scaling
- Output: `DXGI_FORMAT_B8G8R8A8_UNORM` with `D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET`
- Shaders compiled at runtime via `D3DCompile` (no external `.cso` files)

### 4. CaptureRequest (Parameter Struct)

```cpp
struct CaptureRequest {
    std::wstring outputDir;
    ImageFormat  format = ImageFormat::PNG;
    float        jpegQuality = 95.0f;
    TiffMode     tiffMode = TiffMode::NONE;
    DDSMode      ddsMode = DDSMode::BC1;

    // Animated formats
    float animDuration = 3.0f;   // seconds
    float animFPS = 10.0f;       // frames/sec
    int   loopCount = 0;         // 0 = infinite
    int   deltaMode = 0;         // 0=region, 1=true delta
    int   optimize = 0;          // 0=off, 1=on

    // Video capture (H.264)
    float videoDuration = 10.0f;      // 1–120s
    int   targetResolution = 0;       // 0=Native, 1=720p, 2=1080p, 3=1440p, 4=4K
    int   videoFrameRate = 30;        // 30 or 60
    int   qualityPreset = 2;          // 0=Low, 1=Medium, 2=High, 3=VeryHigh, 4=Custom
    int   videoBitrateKbps = 8000;    // kbps (Custom only)
    float keyframeIntervalSec = 2.0f; // seconds
    int   encoderPreference = 0;      // 0=Auto, 1=PreferHW, 2=ForceSW
    int   rateControl = 1;            // 0=CBR, 1=VBR, 2=CQP
    int   videoContainer = 0;         // 0=MP4

    bool autoUI = true;               // Auto-hide/show UI via showMenus
};
```

### Resolution Mapping (`targetResolution`)

| Value | Name | Dimensions (typical) |
|-------|------|----------------------|
| `0` | Native | Desktop resolution (no scaling) |
| `1` | 720p | 1280×720 |
| `2` | 1080p | 1920×1080 |
| `3` | 1440p | 2560×1440 |
| `4` | 4K | 3840×2160 |

Actual output is computed by `ResolveTargetResolution()` which preserves aspect ratio via letterboxing.

---

## Encoding Subsystems

### IEncoder Interface

```cpp
class IEncoder {
public:
    virtual EncodeResult Encode(
        const DirectX::ScratchImage& frame,
        const std::wstring& outputPath,
        CancellationToken::Ptr token) = 0;

    virtual EncodeResult EncodeSequence(
        std::vector<DirectX::ScratchImage>& frames,
        const std::wstring& outputPath,
        float fps, int loopCount, int deltaMode,
        CancellationToken::Ptr token) = 0;
};
```

### StillEncoder

Supports: PNG, JPEG, BMP, TIFF, DDS

| Format | Library/Method | Notes |
|--------|---------------|-------|
| PNG | DirectXTex `SaveToWICFile` | WIC encoder, parallel option |
| JPEG | DirectXTex `SaveToWICFile` | Quality 1–100 via WIC property |
| BMP | DirectXTex `SaveToWICFile` | Uncompressed RGBA |
| TIFF | DirectXTex `SaveToWICFile` | Compression via `WICBitmapEncoder` property |
| DDS | DirectXTex `SaveToDDSFile` | BC1–BC7 compression modes |

### GifEncoder

Custom GIF89a writer with:
- LZW compression
- Color quantization (8-bit palette)
- Frame differencing (delta mode)
- Region extraction optimization
- Loop count support

### ApngEncoder

Custom APNG encoder:
- PNG-compliant per-frame compression
- `fcTL`/`fdAT` chunk sequencing
- Optional per-frame region optimization

---

## Video Capture (H.264/MP4)

### Architecture: Media Foundation SinkWriter

```cpp
namespace Printscreen {
    class VideoCapture {
        bool Initialize(ID3D11Device*, const VideoCaptureConfig&);
        bool EncodeFrame(ID3D11Texture2D*, uint64_t frameIndex);
        bool EncodeFrame(ID3D11Texture2D*, uint64_t frameIndex,
                           LONGLONG sampleTimeHns, LONGLONG sampleDurationHns);
        bool Finalize();   // Flush moov atom, close file
        void Abort();      // Discard partial file
    };
}
```

### VideoCaptureConfig

```cpp
struct VideoCaptureConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t frameRate = 60;
    uint32_t bitrateKbps = 16000;
    uint32_t qualityCqp = 70;           // Only for CQP mode
    uint32_t keyframeIntervalSec = 2;
    EncoderPreference encoderPreference = EncoderPreference::Auto;
    RateControlMode   rateControl       = RateControlMode::VBR;
    VideoContainer    container         = VideoContainer::MP4;
    std::filesystem::path outputPath;
};
```

### Encoder Selection (Media Foundation)

| Preference | Behavior |
|-----------|----------|
| `Auto` | MF chooses: hardware if available, else software |
| `PreferHardware` | Requires hardware MFT; `Initialize()` fails if none |
| `ForceSoftware` | Sets `MF_READWRITE_DISABLE_HARDWARE_TRANSFORMS` |

Hardware encoder selection is automatic:
- **NVIDIA:** NVENC via `MFVideoFormat_H264` MFT
- **AMD:** AMF via `MFVideoFormat_H264` MFT
- **Intel:** QuickSync via `MFVideoFormat_H264` MFT

### Rate Control Mapping

```cpp
UINT32 EncoderRateControlConstant(RateControlMode mode) {
    switch (mode) {
        case CBR: return eAVEncCommonRateControlMode_CBR;
        case VBR: return eAVEncCommonRateControlMode_UnconstrainedVBR;
        case CQP: return eAVEncCommonRateControlMode_Quality;
    }
}
```

Passed to encoder via `CODECAPI_AVEncCommonRateControlMode`.

### Timestamp Math

To minimize accumulated rounding error over long captures:

```cpp
// Correct (minimizes drift):
LONGLONG timeHns = (frameIndex * kHnsPerSecond) / fps;

// Incorrect (accumulates rounding error):
// LONGLONG timeHns = frameIndex * (kHnsPerSecond / fps);
```

Where `kHnsPerSecond = 10'000'000LL` (100-nanosecond units).

### Thread Safety

- `Initialize()`, `EncodeFrame()`, `Finalize()` must all be called from the **same thread** (the CaptureSession worker thread)
- D3D11 multithread protection is **enforced** in `Initialize()`:
  ```cpp
  ComPtr<ID3D10Multithread> mt;
  device->QueryInterface(IID_PPV_ARGS(&mt));
  mt->SetMultithreadProtected(TRUE);
  ```

### Streaming vs Buffered

Unlike GIF/APNG (which buffer all frames in memory then encode), VideoCapture:
1. Encodes each frame immediately via `IMFSinkWriter::WriteSample()`
2. Streams to disk — no full in-memory frame buffer
3. Memory usage is constant regardless of duration

### MKV Status

The `VideoContainer::MKV` enum exists but falls back to MP4 with a log warning. Media Foundation's built-in sink writer does not support Matroska. Future versions may add FFmpeg integration (LGPL considerations apply).

---

## UI Controller

### Primary Mechanism: `RE::UI::ShowMenus()`

The UI controller uses the same bool that the `tm` console command toggles:

```cpp
bool UIController::HideAll() {
    auto* ui = RE::UI::GetSingleton();
    savedShowMenus_ = ui->IsShowingMenus();
    ui->ShowMenus(false);   // Single bool write, thread-safe
    hidden_ = true;
}

bool UIController::RestoreAll() {
    auto* ui = RE::UI::GetSingleton();
    ui->ShowMenus(savedShowMenus_);  // Restore previous state
    hidden_ = false;
}
```

**Thread safety:** `ShowMenus(bool)` is a plain bool write checked early in the engine render loop. It is safe to call from the capture worker thread without SKSE task marshalling.

### Granular Scaleform Helpers (Deprecated)

The class retains per-element visibility helpers for selective UI hiding:
- `SetHUDAlpha()`, `HideHUD()`, `ShowHUD()`
- `SetSubtitlesVisible()`, `SetNPCNamesVisible()`, etc.

**These require game-thread execution** (via `SKSE::GetTaskInterface()->AddTask()`) because GFx is not thread-safe.

### Async Wrappers

```cpp
void UIController::HideAllAsync()    { HideAll(); }    // Inline, thread-safe
void UIController::RestoreAllAsync()   { RestoreAll(); } // Inline, thread-safe
```

Kept for API consistency when called from worker threads.

---

## Papyrus Integration

### Registration

```cpp
// plugin.cpp
auto* papyrus = SKSE::GetPapyrusInterface();
papyrus->Register(PapyrusBindings::Register);
```

### Lifecycle Hooks

```cpp
// Messaging interface listener
messaging->RegisterListener([](SKSE::MessagingInterface::Message* msg) {
    switch (msg->type) {
        case kDataLoaded:
            PapyrusBindings::OnDataLoaded();      // Mod config loaded
            break;
        case kPostLoadGame:
        case kNewGame:
            PapyrusBindings::OnPostLoadGame();    // Reset session state
            break;
    }
});
```

### Script Dependencies

| Dependency | Purpose | Nexus ID |
|-----------|---------|----------|
| **PapyrusUtil SE** | JsonUtil, StorageUtil | [13048](https://www.nexusmods.com/skyrimspecialedition/mods/13048) |
| **JContainers SE** | JIntMap, JArray | [16495](https://www.nexusmods.com/skyrimspecialedition/mods/16495) |
| **SkyUI** | SKI_ConfigBase, MCM | Built-in |

### Compilation Status

As of last check, all Papyrus scripts require the extended SKSE compiler flags (for `Hidden` property/script flags) and the above dependencies. The scripts do not compile with the base game compiler alone.

### Papyrus API Functions

The plugin exposes these functions to Papyrus (exact signatures in `papyrus/Bindings.h`):

- **Capture functions** — Trigger screenshots/video with parameters
- **Config reload** — `ReloadConfig()` to re-read INI without restart
- **Status polling** — Check capture progress via `GetResult()`
- **UI control** — Hide/restore UI elements

---

## Configuration System

### Settings Struct

```cpp
namespace Config {
    struct Settings {
        std::wstring iniPath;       // Path to loaded INI
        int      logLevel = 3;      // 0..5
        bool     consoleOutput = true;
        bool     fileOutput = true;
        bool     showTimestamps = true;
        uint32_t maxLogFileSizeMB = 8;
        bool     parallelCompression = true;
        uint32_t compressionThreads = 0;  // 0 = auto
        bool     logCaptureProgress = false;
        bool     logTimingInfo = false;
        DDSMode  ddsMode = DDSMode::BC7;  // Only used by legacy path
    };
    extern Settings g_settings;
}
```

### Initialization Flow

```
SKSEPlugin_Load()
    └── Config::Initialize()
        ├── Check legacy path first (backward compat)
        ├── Check current path
        ├── Validate INI keys → g_unknownKeys
        ├── Parse sectioned format [Logging]/[Performance]/[Capture]
        └── If sectioned missing, parse legacy flat format
```

### Runtime Access

```cpp
const Config::Settings& s = Config::Get();
int level = s.GetLogLevel();        // Convenience accessor
bool console = s.IsConsoleOutputEnabled();
```

---

## Logging System

### Implementation

Uses **spdlog** with fmt-style formatting:

```cpp
// logger.h
namespace logger {
    void SetupLog();   // One-time init
    void SetLevel(spdlog::level::level_enum level);

    template<class... Args>
    void info(fmt::format_string<Args...> fmt, Args&&... args) {
        spdlog::info(fmt, std::forward<Args>(args)...);
    }
    // ... trace, debug, warn, error, critical
}
```

### Log File

```
%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\printscreen.log
```

- Overwritten on each game start
- Rotated when size exceeds `MaxLogFileSizeMB`
- Old file renamed to `printscreen.log.old`

### UTF-8 Support

`logger_shim.hpp` provides `logger::u8()` wrappers for strings that may contain non-ASCII characters (paths with Unicode, etc.).

---

## Threading Model

### Worker Thread (Capture)

```cpp
// CaptureSession::Start() spawns:
std::thread workerThread_([this, req]() {
    WorkerThread(req);
});
```

The worker thread:
1. Acquires frames from Desktop Duplication
2. Runs encoding (CPU-bound for stills/GIF/APNG, GPU-bound for video)
3. Calls completion callback
4. Sets result state for Papyrus polling

### Cancellation Token

```cpp
class CancellationToken : public std::enable_shared_from_this<CancellationToken> {
public:
    void RequestCancel();
    bool IsCancelled() const;
    void ThrowIfCancelled(const char* context);  // Throws Cancelled exception
};
```

Long-running operations periodically check the token. Frame acquirer checks before each `AcquireNextFrame()` call.

### Main Thread Safety

| Component | Thread | Notes |
|-----------|--------|-------|
| `CaptureSession::Start()` | Any | Spawns worker, returns immediately |
| `CaptureSession::GetResult()` | Any (Papyrus) | Poll from game thread |
| `UIController::HideAll()` | Any | Bool write is atomic |
| `UIController::SetHUDAlpha()` | Game only | GFx requires main thread |
| `VideoCapture::EncodeFrame()` | Worker only | Same thread as Initialize() |
| `FrameAcquirer::AcquireFrame()` | Worker | DXGI is thread-safe |

### Thread Synchronization

- `CaptureSession` uses `std::mutex` for state machine transitions
- `FrameQueue` uses `std::mutex + std::condition_variable` for producer/consumer
- `ConsoleCommandQueue` uses `std::mutex + std::deque` for thread-safe command enqueue/dequeue

---

## Build System

### CMake Configuration

```cmake
# Key dependencies
target_link_libraries(Printscreen PRIVATE
    CommonLibSSE::CommonLibSSE
    d3d11.lib d3dcompiler.lib dxgi.lib
    mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib strmiids.lib
)
```

### Required Libraries

| Library | Source | Purpose |
|---------|--------|---------|
| **CommonLibSSE** | GitHub / vcpkg | SKSE abstraction layer |
| **DirectXTex** | vcpkg | Image I/O, texture utilities |
| **spdlog** | vcpkg | Logging |
| **fmt** | vcpkg (spdlog dep) | String formatting |

### Windows SDK Components

- `mfplat`, `mf`, `mfreadwrite` — Media Foundation
- `mfuuid`, `strmiids` — MF GUIDs
- `d3d11`, `d3dcompiler` — Direct3D 11 + runtime shader compilation
- `dxgi` — Desktop Duplication API

### Precompiled Header

```cpp
// pch.h
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include <Windows.h>
#include <filesystem>
#include <math>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
```

### vcpkg Manifest

No custom vcpkg ports required. All dependencies are in the main registry.

---

## Known Issues & Limitations

### Papyrus Script Compilation

All `.psc` scripts currently fail to compile without:
- PapyrusUtil SE (JsonUtil, StorageUtil)
- JContainers SE (JIntMap)
- SKSE extended compiler flags (`Hidden` property support)

**Status:** Scripts may be shipped as pre-compiled `.pex` files, or dependencies must be installed.

### Video Capture Edge Cases

1. **MKV container** — Not implemented. Falls back to MP4 with warning.
2. **CQP + Bitrate** — Bitrate is ignored when `rateControl=CQP`. Quality value is used instead.
3. **Hardware encoder unavailability** — Falls back to software. May be slow at high resolutions.
4. **Abrort without Finalize** — MP4 file is unplayable (no `moov` atom). Use `Abort()` on game crash/force quit.
5. **D3D11 video support flag** — If the device was created without `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`, hardware encoding silently fails.

### Resolution Scaling

`GPUScaler` uses bilinear filtering. For highest quality downscaling (e.g., 4K→1080p), Lanczos or bicubic would be better but require more complex shader code.

### Memory Usage

- **Stills:** One frame (~8MB for 1080p RGBA)
- **GIF/APNG:** All frames buffered in memory before encoding
  - 15s @ 30fps @ 1080p = ~450 frames × ~8MB = **~3.6GB**
  - Duration clamped to 15s to prevent excessive memory use
- **H.264:** Constant memory — frames encoded and streamed to disk immediately

### Desktop Duplication Limitations

- Cannot capture content protected by HDCP/DRM
- Captures the entire desktop, not just the game window (so multi-monitor setups capture all monitors unless cropped)
- Requires Windows 8.1+ ( DXGI 1.2+)

### UI Hiding

- Uses `RE::UI::ShowMenus(bool)` — same as `tm` console command
- Hides **all** UI: HUD, compass, crosshair, quest markers, mod HUDs, menus
- Cannot selectively hide individual elements without Scaleform API (which requires game-thread execution)

---

## API Quick Reference

### Config Access

```cpp
#include "Config.h"
Config::Initialize();                    // Load INI
const auto& s = Config::Get();
int level = s.logLevel;                  // 0-5
bool parallel = s.parallelCompression;   // true/false
```

### Logger

```cpp
#include "logger.h"
logger::info("Format string with {} args", 42);
logger::error("Failed to open {}", path);
logger::SetupLog();  // Call once after Config::Initialize()
```

### Capture Session

```cpp
#include "capture/CaptureSession.h"
auto& session = CaptureSession::GetSingleton();

CaptureRequest req;
req.outputDir = L"C:/Screenshots";
req.format = ImageFormat::PNG;
req.autoUI = true;

auto result = session.Start(req,
    [](const std::string& msg){ /* on complete */ },
    [](){ /* after acquisition, before encoding — restore UI here */ }
);
```

### Video Capture

```cpp
#include "VideoCapture.h"
Printscreen::VideoCapture vc;
Printscreen::VideoCaptureConfig cfg;
cfg.width = 1920; cfg.height = 1080;
cfg.frameRate = 60;
cfg.bitrateKbps = 16000;
cfg.outputPath = "C:/Videos/capture.mp4";

vc.Initialize(device, cfg);
vc.EncodeFrame(texture, frameIndex, timeHns, durationHns);
vc.Finalize();  // Must call for playable MP4
```

---

## Change Log

### V4.0.0 (Current)

- Complete refactor from V3 architecture
- Desktop Duplication API replaces game hooks
- Modular encoder system (Still/GIF/APNG/H264)
- GPU-accelerated video encoding via Media Foundation
- Cooperative cancellation throughout
- Sectioned INI format with legacy fallback
- spdlog-based logging with rotation

### V3 (Legacy)

- Game-renderer hooking approach
- Simpler format support (PNG/JPG/BMP)
- No video encoding

---

*Last updated: 2026-06-03*  
*For end-user documentation, see [PrintScreen V4 User Guide](PrintScreenV4_UserGuide.md)*
