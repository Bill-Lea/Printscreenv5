# PrintScreen V4 — User Guide

**Version:** 4.0.0  
**Author:** William G Lea  
**Game:** Skyrim Special Edition (SKSE64 plugin)  
**Platform:** Windows 10/11

---

## Table of Contents

1. [Overview](#overview)
2. [Installation](#installation)
3. [INI Configuration](#ini-configuration)
   - [Configuration File Location](#configuration-file-location)
   - [Logging Section](#logging-section)
   - [Performance Section](#performance-section)
   - [Capture Section](#capture-section)
   - [Complete Default INI](#complete-default-ini)
4. [Usage](#usage)
   - [Taking Screenshots](#taking-screenshots)
   - [Capture Formats](#capture-formats)
   - [Video Capture](#video-capture)
   - [Animated Images (GIF/APNG)](#animated-images-gifapng)
5. [Troubleshooting](#troubleshooting)
6. [Log File](#log-file)
7. [Compatibility Notes](#compatibility-notes)

---

## Overview

PrintScreen V4 is a Skyrim Special Edition SKSE plugin that captures screenshots and video directly from the game. Unlike the Steam screenshot system, PrintScreen captures the raw desktop image (including any overlays, ENB, ReShade, or other post-processing effects) and saves it in high quality.

**Key Features:**

- **Still images:** PNG, JPEG, BMP, TIFF, DDS
- **Animated images:** GIF, Animated PNG (APNG)
- **Video recording:** H.264 MP4 with hardware encoding support
- **UI control:** Auto-hide HUD/menus before capture
- **GPU scaling:** Hardware-accelerated resolution scaling for video
- **Zero game integration:** Uses Windows Desktop Duplication API — no hooks into the game renderer

---

## Installation

### Requirements

- Skyrim Special Edition (version 1.5.39 or later, including 1.6.x AE builds)
- [SKSE64](https://skse.silverlock.org/) (Script Extender) installed
- Windows 10 or 11 with DirectX 11 support
- For hardware video encoding: a modern GPU (NVIDIA, AMD, or Intel)

### Files

Place these in your Skyrim `Data\SKSE\Plugins\` folder (typically managed by Mod Organizer 2 or Vortex):

```
Printscreen.dll          ← The main plugin
PrintScreen.ini          ← User configuration file (auto-created if missing)
```

### Auto-Created Files

On first run, the plugin creates:

- `C:\Users\<You>\Documents\My Games\Skyrim Special Edition\SKSE\PrintScreen.ini`
- `C:\Users\<You>\Documents\My Games\Skyrim Special Edition\SKSE\printscreen.log`

---

## INI Configuration

The `PrintScreen.ini` file controls all plugin behavior. It is organized into three sections: `[Logging]`, `[Performance]`, and `[Capture]`.

### Configuration File Location

```
C:\Users\<YourUsername>\Documents\My Games\Skyrim Special Edition\SKSE\PrintScreen.ini
```

You can edit this file with any text editor (Notepad, VS Code, etc.). Changes take effect when the game restarts, or immediately if you use the `ReloadConfig()` Papyrus function from a mod.

---

### Logging Section

Controls how much information the plugin writes to its log file and/or console.

#### `LogLevel`

| Value | Name | Description |
|-------|------|-------------|
| `0` or `NONE` | None | No logging at all |
| `1` or `ERROR` | Error | Only critical errors |
| `2` or `WARN` | Warning | Errors and warnings |
| `3` or `INFO` | Info | General operational info *(default)* |
| `4` or `DEBUG` | Debug | Detailed diagnostic output |
| `5` or `TRACE` | Trace | Extremely verbose (every operation) |

**Example:**
```ini
[Logging]
LogLevel=INFO
```

> **Recommendation:** Use `INFO` for normal play. Use `DEBUG` only when reporting a bug. `TRACE` will create very large log files.

#### `ConsoleOutput`

- `true` — Also print log messages to the in-game console
- `false` — Log file only *(default: true)*

#### `FileOutput`

- `true` — Write log messages to `printscreen.log` *(default)*
- `false` — Disable log file output

#### `ShowTimestamps`

- `true` — Prefix each log line with a timestamp *(default)*
- `false` — Plain log messages without timestamps

#### `MaxLogFileSizeMB`

Maximum size of the log file in megabytes before it is automatically rotated (old log renamed, new one started).

- **Range:** 1–50
- **Default:** 8

**Example:**
```ini
[Logging]
LogLevel=INFO
ConsoleOutput=true
FileOutput=true
ShowTimestamps=true
MaxLogFileSizeMB=8
```

---

### Performance Section

Controls CPU usage during image encoding.

#### `ParallelCompression`

- `true` — Use multiple CPU threads for PNG/APNG compression *(default)*
- `false` — Single-threaded encoding (slower, less CPU usage)

#### `CompressionThreads`

Number of threads to use for parallel compression.

- `0` — Auto-detect (uses number of CPU cores) *(default)*
- `1`–`16` — Explicit thread count

> **Note:** Only applies when `ParallelCompression=true`. For most users, `0` (auto) is optimal.

**Example:**
```ini
[Performance]
ParallelCompression=true
CompressionThreads=0
```

---

### Capture Section

Controls what information is logged during capture operations.

#### `LogCaptureProgress`

- `true` — Log progress messages during frame capture (e.g., "Frame 12/60")
- `false` — Silent capture *(default)*

#### `LogTimingInfo`

- `true` — Log how long each capture operation takes (useful for benchmarking)
- `false` — Don't log timing *(default)*

**Example:**
```ini
[Capture]
LogCaptureProgress=false
LogTimingInfo=false
```

---

### Complete Default INI

If no INI file exists, the plugin creates this automatically:

```ini
; PrintScreen V4 Configuration
; Located in: Documents\My Games\Skyrim Special Edition\SKSE\PrintScreen.ini

[Logging]
LogLevel=INFO
ConsoleOutput=true
FileOutput=true
ShowTimestamps=true
MaxLogFileSizeMB=8

[Performance]
ParallelCompression=true
CompressionThreads=0

[Capture]
LogCaptureProgress=false
LogTimingInfo=false
```

---

## Usage

### Taking Screenshots

Screenshots are triggered by:

1. **Papyrus script call** — Mods using the PrintScreen API can request captures programmatically
2. **MCM menu** — If a companion mod provides a Mod Configuration Menu

The plugin automatically:
1. Hides the UI (HUD, compass, crosshair — everything)
2. Waits one frame for the UI to clear
3. Captures the screen
4. Restores the UI
5. Saves the image

### Capture Formats

| Format | Extension | Best For | Notes |
|--------|-----------|----------|-------|
| **PNG** | `.png` | General use, editing | Lossless, good compression |
| **JPEG** | `.jpg` | Quick sharing, small size | Adjustable quality 1–100 |
| **BMP** | `.bmp` | Raw pixel data | Uncompressed, large files |
| **TIFF** | `.tif` | Professional editing | Optional LZW, ZIP, CCITT, RLE compression |
| **DDS** | `.dds` | Texture work, game mods | BC1–BC7 GPU compression modes |
| **GIF** | `.gif` | Simple animations | 256 colors, small file size |
| **APNG** | `.png` | High-quality animations | Full color, transparency support |
| **H264/MP4** | `.mp4` | Video recording | Hardware encoding, full quality |

#### JPEG Quality

When using JPEG format, quality is adjustable from 1 (very blocky, very small) to 100 (near-lossless, larger file). Default is 95 — a good balance of quality and size.

#### DDS Compression Modes

| Mode | Description | Use Case |
|------|-------------|----------|
| `BC1` | 4× compression, 1-bit alpha | Opaque textures *(default)* |
| `BC2` | 4× compression, 4-bit alpha | Textures with sharp edges |
| `BC3` | 4× compression, 8-bit alpha | Textures with smooth transparency |
| `BC4` | 2× compression, grayscale | Height maps, single-channel data |
| `BC5` | 2× compression, two channels | Normal maps |
| `BC6H` | 6× compression, HDR | High dynamic range textures |
| `BC7_SLOW` | 3× compression, highest quality | Premium textures (slowest) |
| `BC7_NORMAL` | 3× compression, balanced | General purpose *(recommended)* |
| `BC7_FAST` | 3× compression, fastest | Quick exports |

---

### Video Capture (H.264/MP4)

Video recording uses your GPU's hardware encoder when available (NVIDIA NVENC, AMD AMF, or Intel QuickSync). Falls back to software encoding if hardware is unavailable.

#### Video Parameters

| Parameter | Options | Description |
|-----------|---------|-------------|
| **Duration** | 1–120 seconds | How long to record |
| **Resolution** | Native / 720p / 1080p / 1440p / 4K | Output resolution (GPU scaled from desktop) |
| **Frame Rate** | 30 / 60 fps | Target capture frame rate |
| **Quality Preset** | Low / Medium / High / VeryHigh / Custom | Built-in quality profiles |
| **Bitrate** | 1000–50000 kbps | Data rate (only when Preset = Custom) |
| **Keyframe Interval** | 1–10 seconds | How often a full frame is stored |
| **Encoder** | Auto / Prefer Hardware / Force Software | GPU vs CPU encoding |
| **Rate Control** | CBR / VBR / CQP | Bitrate behavior mode |

#### Quality Presets

| Preset | Target Use | Typical Bitrate |
|--------|-----------|----------------|
| Low | Quick previews, small files | ~4 Mbps |
| Medium | General sharing | ~8 Mbps |
| High | Quality archiving | ~16 Mbps |
| VeryHigh | Maximum quality | ~25 Mbps |
| Custom | User-defined bitrate | You set it |

#### Rate Control Modes

| Mode | Description | Best For |
|------|-------------|----------|
| **CBR** | Constant Bitrate — fixed data rate | Streaming, consistent file sizes |
| **VBR** *(default)* | Variable Bitrate — allocates more bits for complex scenes | General use, best quality per size |
| **CQP** | Constant Quality — fixed visual quality, variable file size | Archiving, when you don't care about file size |

> **Note:** When using CQP, the Bitrate setting is ignored. Use the Quality value (1–100) instead.

#### Hardware vs Software Encoding

| Setting | Behavior |
|---------|----------|
| **Auto** *(default)* | Uses hardware if available, falls back to software |
| **Prefer Hardware** | Requires hardware encoder; fails if none available |
| **Force Software** | Disables hardware encoding entirely |

> **AMD Users (RX 6700 XT and similar):** The plugin automatically uses AMD's AMF encoder through Media Foundation. No special configuration needed.

---

### Animated Images (GIF/APNG)

For short animated captures without the overhead of video encoding:

| Parameter | Range | Description |
|-----------|-------|-------------|
| **Duration** | 1–15 seconds | Max 15s for animated formats |
| **FPS** | 1–60 | Frames per second |
| **Loop Count** | 0 = infinite, 1–65535 | How many times to loop |
| **Delta Mode** | 0 = region extraction, 1 = true delta | Optimization method |
| **Optimize** | 0 = off, 1 = on | File size optimization |

**Delta Mode:**
- `0` (region extraction): Only stores changed rectangular regions between frames (smaller files for mostly-static scenes)
- `1` (true delta): Computes pixel differences (better for full-screen motion)

---

## Troubleshooting

### No Screenshots Are Saved

1. Check the log file: `Documents\My Games\Skyrim Special Edition\SKSE\printscreen.log`
2. Ensure `FileOutput=true` in the `[Logging]` section
3. Verify the SKSE folder exists and is writable
4. Check if another overlay (Steam, Discord, GeForce Experience) is interfering

### Videos Won't Play / Are Corrupted

1. Ensure the capture completed normally (not forcibly terminated)
2. For MP4, the file must be properly finalized — abrupt game crashes may leave it unplayable
3. Try a different video player (VLC, MPC-HC, PotPlayer)

### High CPU Usage During Capture

1. Set `ParallelCompression=false` in `[Performance]`
2. Or reduce `CompressionThreads` to a lower number
3. Use JPEG instead of PNG for stills (much faster encoding)
4. For video, ensure hardware encoding is working (check log for encoder selection)

### Log File Is Too Large

1. Set `LogLevel=ERROR` or `LogLevel=WARN`
2. Set `LogCaptureProgress=false` and `LogTimingInfo=false`
3. Reduce `MaxLogFileSizeMB` to force earlier rotation

### UI Not Hiding Properly

1. Some mods add UI elements that bypass the `tm` console command
2. The plugin uses the same mechanism as the `tm` command — if `tm` doesn't hide it, PrintScreen can't either
3. Try toggling `autoUI` off in the mod's MCM if available

### "Unknown INI key" Warnings

If you see warnings like `Unknown INI key: 'SomeKey'` in the log:

1. Check for typos in your INI file
2. The plugin validates all keys and warns about unrecognized ones
3. See the full list of valid keys in the [Technical Reference](PrintScreenV4_TechnicalReference.md)

---

## Log File

Location:
```
C:\Users\<You>\Documents\My Games\Skyrim Special Edition\SKSE\printscreen.log
```

The log file is overwritten each time the game starts. When it reaches `MaxLogFileSizeMB`, it is renamed to `printscreen.log.old` and a new file is started.

### Log Format

With `ShowTimestamps=true` (default):
```
[2025-01-15 14:32:01.123] [info] Printscreen: SKSEPlugin_Load starting (v4.0 refactored)
[2025-01-15 14:32:01.145] [info] Runtime: 1.6.640
[2025-01-15 14:32:01.167] [info] Printscreen v4.0 loaded successfully
[2025-01-15 14:35:22.891] [info] CaptureSession: Starting capture (PNG, 1920x1080)
[2025-01-15 14:35:22.903] [info] UIController::HideAll: showMenus set to false
```

---

## Compatibility Notes

### Safe to Use With

- ENB Series
- ReShade
- Steam Overlay
- Discord Overlay
- NVIDIA Overlay / ShadowPlay
- AMD ReLive
- Any SKSE plugin that doesn't hook the same internals

### Potential Conflicts

- **Other screenshot mods** that use the same Papyrus function names (rare)
- **Mods that force `showMenus` on/off** — may interfere with UI hiding
- **Fullscreen borderless window tools** — may affect Desktop Duplication

### Runtime Versions Supported

The plugin explicitly supports these Skyrim SE versions:

- 1.5.39, 1.5.97 (Old SE)
- 1.6.318, 1.6.353, 1.6.629, 1.6.640, 1.6.659, 1.6.678
- 1.6.1130, 1.6.1170 (Anniversary Edition updates)

---

## Quick Reference Card

```ini
; === Essential Settings ===
[Logging]
LogLevel=INFO              ; NONE, ERROR, WARN, INFO, DEBUG, TRACE

[Performance]
ParallelCompression=true     ; true = faster, more CPU; false = slower, less CPU
CompressionThreads=0         ; 0 = auto, or 1-16

[Capture]
LogTimingInfo=false          ; true to see capture timing in log
```

---

*Last updated: 2026-06-03*  
*For technical details, see [PrintScreen V4 Technical Reference](PrintScreenV4_TechnicalReference.md)*
