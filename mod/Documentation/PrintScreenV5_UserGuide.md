# PrintScreen V5 — user guide

**Version:** 5.0.0
**Author:** William G Lea
**Game:** Skyrim Special Edition / Anniversary Edition (SKSE64 plugin)
**Platform:** Windows 10/11

---

## What PrintScreen does

PrintScreen takes screenshots, animated images, and video from inside Skyrim. It grabs frames straight from the Windows display output instead of hooking the game's renderer, so it works no matter what you layer on top of the game: ENB, ReShade, Steam overlay, ShadowPlay, anything. What you see on screen is what lands in the file.

Capture types:

- Stills: PNG, JPEG, BMP, TIFF, DDS
- Animated: GIF (single frame), AGIF (animated GIF), APNG
- Video: H.264 in an MP4 container, with GPU encoding when your card supports it

The trade-off of capturing at the desktop level: PrintScreen sees your whole desktop. On a multi-monitor setup every monitor ends up in the shot, and content protected by HDCP will come through black. There is no way around either, it is how Windows desktop duplication works.

### What changed in V5

If you used V4, the short version:

- **No more PapyrusUtil or JContainers.** The plugin reads and writes its own JSON config natively. Two fewer required mods.
- **Your screenshots carry EXIF metadata.** Still captures record the camera FOV at the moment of the shot and write a 35mm-equivalent focal length into the file. Panorama stitchers (PhotoFileMerge V2, Hugin, PTGui) pick this up automatically instead of asking you for numbers.
- **The hotkey survives bad saves.** The plugin re-arms the Take Photo key from C++ on every save load, so a stale or mid-playthrough install can no longer leave you with a dead key.
- **Opening a menu mid-capture cancels it cleanly** and cleans up its temporary files. If the game crashes during an animated capture, the leftovers are swept on the next launch.
- **Completion is event-driven.** The old polling loop is gone; the plugin gets called back when the capture finishes.

---

## Requirements

- Skyrim Special Edition 1.5.39 or newer, including all Anniversary Edition builds. VR is not supported.
- [SKSE64](https://skse.silverlock.org/) matching your game version.
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604) for the MCM. The mod captures fine without it, but then your only configuration route is editing the JSON file by hand.
- Windows 10 or 11. Desktop duplication needs DXGI 1.2, which rules out anything older.

PapyrusUtil and JContainers are **not** needed anymore. If you have them for other mods, leave them; PrintScreen no longer touches them.

---

## Installation

Install the downloaded archive with Mod Organizer 2 or Vortex. The archive is laid out the way the game expects, so a mod manager install needs no decisions from you.

If you insist on doing it by hand, the files go to these places under `Data\`:

| File | Destination |
|------|-------------|
| `Printscreen.esp` | `Data\` |
| `SKSE\Plugins\Printscreen.dll` | `Data\SKSE\Plugins\` |
| `Scripts\Printscreen_*.pex` | `Data\Scripts\` |
| `Interface\PrintScreen\*.dds` | `Data\Interface\PrintScreen\` |
| `SKSE\Plugins\StorageUtilData\PrintScreen.json` | `Data\SKSE\Plugins\StorageUtilData\` |

Then enable the ESP and play. There is no INI to create, no console command to run.

---

## Quick start

1. Launch the game through SKSE.
2. Open the MCM (Mods section, ESC → Mod Configuration) and find PrintScreen.
3. Set **Path** to wherever you want screenshots. The folder is created if it does not exist. It must be absolute (`C:/Games/Shots`, not `Shots`).
4. Close the MCM. Press the **PrintScreen** key (the actual PrtScn key, which is the default) to take a shot.

You get a notification when the capture finishes, and a counter of shots taken. Press the hotkey again while a capture is running to cancel it. During animated and video captures, opening any pause menu (inventory, map, journal) cancels the capture too.

The hotkey does not fire while a menu is open, while text input is active, or within 0.75 seconds of the last press. If the key ever stops responding, loading a save or closing the MCM re-arms it.

---

## The MCM

The menu has two pages. The splash screen picks one of six sample shots at random; that is just decoration.

### Settings page

**Path** — where captures are written. Absolute path, no illegal characters. If the path is longer than 30 characters the MCM stops offering the text box and tells you to edit the JSON file directly; long paths are awkward to type in the MCM's input widget, not broken.

**Select Image File Type** — one of PNG, APNG, BMP, TIF, JPG, GIF, AGIF, DDS, H264. Choosing a type enables the settings that apply to it, and the rest stay greyed out.

**Automatic Menu Removal** — hides the entire HUD and all menus during the capture, the same thing the console `tm` command does, then puts everything back.

**Select Take Photo Key** — click it and press any key. Keys already claimed by another control are refused.

**Save/Restore Configuration** — when on (the default), your settings persist to `Data\SKSE\Plugins\StorageUtilData\PrintScreen.json` whenever you close the MCM, and load again on game start. Leave it on unless you want settings reset to defaults every session.

The remaining sliders and dropdowns belong to specific formats:

| Setting | Applies to | Range | Default |
|---------|-----------|-------|---------|
| JPG Compression | JPG | 0–100 | 90 |
| PNG Compression | PNG | 0–9 (zlib level) | 9 |
| Quality | APNG, AGIF | 0–100% | 85% |
| Capture Duration | APNG, AGIF | 1–15 seconds | 5 |
| FPS | APNG, AGIF | 1–30 | 15 |
| Loop Count | APNG, AGIF | 0–10 (0 = loops forever) | 0 |
| Optimize | APNG, AGIF | 0/1 | 1 |
| Delta Mode | APNG, AGIF | 0, 1, or 2 | 0 |
| Tif Compression Mode | TIF | UNCOMPRESSED, RLE, LZW, ZIP | UNCOMPRESSED |
| DDS Mode | DDS | see below | UNCOMPRESSED |

The duration slider goes up to 30, but animated captures are capped at 15 seconds. If you set 20 and switch image type, you will get a notification that it was clamped to 15. The cap exists because every frame of an animated capture is held before encoding, and 15 seconds is already a lot of frames.

**Delta Mode** controls how animation frames are stored:

- 0 — every frame stored whole. Biggest files, no artifacts possible.
- 1 — only the changed rectangle of each frame is stored. Much smaller files for mostly-still scenes.
- 2 — pixel-level differences between frames, with transparency for unchanged pixels. Best compression for slow, subtle motion.

**DDS modes:** UNCOMPRESSED, BC1 through BC5, BC6h, and three speeds of BC7. BC1 is fine for opaque textures. BC6h and BC7 variants take several minutes to compress. That is not a hang, it is the codec. Plan accordingly before capturing a batch of them.

### Video Settings page

Everything on this page is greyed out until the image type is H264. Video and image settings live apart because they do not share units or defaults.

Capture group:

- **Duration (seconds)** — 1 to 120. Default 10.
- **Target Resolution** — Native, 720p, 1080p, 1440p, or 4K. Output is scaled on the GPU, preserving aspect ratio with letterboxing if needed. Native means no scaling.
- **Frame Rate** — 30 or 60. At 60 you record twice the frames, so expect roughly double the file size.

Encoding group:

- **Quality Preset** — Low, Medium, High, Very High, or Custom. The presets set the bitrate for you. Custom unlocks the bitrate slider. Default High.
- **Bitrate (kbps)** — 1000 to 50000. Only active with the Custom preset, and ignored when rate control is CQP.
- **Keyframe Interval (seconds)** — 0.5 to 10. How often a full frame is stored. Lower means better seeking in a video player but a larger file. Default 2.
- **Encoder Preference** — Auto, Prefer Hardware, or Force Software. Auto picks the GPU encoder if there is one (NVIDIA NVENC, AMD AMF, Intel QuickSync, all through Windows Media Foundation) and falls back to software otherwise. If a hardware encoder misbehaves, Force Software sidesteps it at the cost of CPU time.
- **Rate Control** — CBR (constant bitrate, predictable file size), VBR (variable, best quality per byte, the default), CQP (constant quality; you set the target, the file size is what it is).
- **Container** — MP4. MKV may arrive in a future version.

One property of this recorder worth knowing: frames are encoded as they are captured and streamed to disk. A 120-second capture uses about the same memory as a 5-second one.

---

## File formats, in one table

| Type | Extension | Use it for | Watch out for |
|------|-----------|-----------|---------------|
| PNG | .png | Default stills, editing, EXIF | zlib level 0–9 |
| JPG | .jpg | Small files, quick sharing | Lossy, quality 0–100 |
| BMP | .bmp | Raw pixels | Large, no compression |
| TIF | .tif | Editing pipelines | Compression mode applies |
| DDS | .dds | Texture work | BC6h/BC7 take minutes |
| GIF | .gif | Static 256-color image | Not animated |
| AGIF | .gif | Animation, small size | 256 colors |
| APNG | .png | Animation, full color | Larger than AGIF |
| H264 | .mp4 | Video | See Video Settings |

Yes, plain GIF is a single frame in V5. If you want the animation, pick AGIF.

---

## The two configuration files

### The JSON file (your settings)

`Data\SKSE\Plugins\StorageUtilData\PrintScreen.json`

The mod owns this file. It is written when you close the MCM and read on game start. If it is missing or corrupt, the mod notices, tells you, and writes a fresh one with defaults. Hand-editing works, but every value is validated on load, and anything invalid quietly reverts to its default with a notification. Typos in key names mean the value is simply ignored.

One historical note: the archive ships a PrintScreen.json from an older test setup. On first launch it fails the completeness check and is rewritten with clean defaults. That is expected, not a bug.

### The INI file (diagnostics)

`Documents\My Games\Skyrim Special Edition\SKSE\PrintScreen.ini`

This file is optional and is not created for you. It controls logging and encoder threading, nothing else. If it does not exist, the defaults below apply.

```ini
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

- **LogLevel** — NONE, ERROR, WARN, INFO (default), DEBUG, or TRACE, or the numbers 0–5. Use INFO normally, DEBUG when reporting a bug, TRACE never unless you enjoy enormous files.
- **ConsoleOutput** — also echo the log to the in-game console.
- **FileOutput** — write the log file.
- **ShowTimestamps** — timestamp each line.
- **MaxLogFileSizeMB** — 1 to 50. The log rotates when it hits this size.
- **ParallelCompression** — compress PNGs with multiple threads.
- **CompressionThreads** — how many threads, 0 meaning auto-detect.
- **LogCaptureProgress** / **LogTimingInfo** — per-frame progress and timing lines in the log. Off by default, on when you are benchmarking.

A legacy flat-format INI from V2 (`SKSE\Plugins\Printscreen_Log.ini`) is still read if present. Unrecognized keys produce a warning in the log rather than a crash.

### The log

`Documents\My Games\Skyrim Special Edition\SKSE\Logs\Printscreen.log`

This is the first place to look when something misbehaves. It records plugin startup, runtime version, encoder selection, capture progress, and every warning. If you report a bug, attach this file.

---

## EXIF metadata: screenshots that know their focal length

Every PNG and JPEG still gets EXIF fields written into it, recording the camera FOV at the instant of capture and the 35mm-equivalent focal length that follows from it (first person uses the first-person FOV, third person the world FOV).

The point of this is stitching. Panorama tools need a focal length to line up images. Tools that read EXIF, PhotoFileMerge V2 among them, get it from the file and never ask you. If the FOV could not be read at capture time, the metadata is simply left out, and the shot is unaffected.

---

## Troubleshooting

**No files appear.** Check the Path setting first, then the log. The path is validated when you type it: it must be absolute, and the mod must be able to create and write in it.

**The hotkey does nothing.** Confirm which key is bound in the MCM, it might not be the one you think. The key is suppressed while any menu is open, while text input is active, and for 0.75 seconds after a press. If it stays dead, load a save or open and close the MCM; both re-register the key.

**The capture cancels itself when I open my inventory.** Intended. Opening a pause or input menu during an animated or video capture cancels it, because nine times out of ten you did not mean to keep recording. For stills there is nothing to interrupt.

**The HUD shows up in my shots.** Something is forcing menus on. PrintScreen hides the UI through the same flag as the `tm` console command; if `tm` does not hide it in your load order, PrintScreen cannot either. (The 5.0.0 and 5.0.1 releases had a settings bug where this toggle did not affect captures; 5.0.2 fixes it.)

**The capture finished but I never got a notification, and the next key press says "Cancelling...".** This was an event-matching defect in 5.0.0 (the file itself saved fine). 5.0.1 and later match completion events by capture number and add a watchdog that clears the stuck state, confirmed working in 5.0.2; if you still see it, report it with the Papyrus log.

**DDS captures take forever.** BC6h and BC7 compress on the CPU at seconds per frame, not milliseconds. The UI comes back as soon as the frame is grabbed; only the encoding is slow. Pick BC1 or BC7_FAST when you do not need the quality.

**My MP4 will not play.** The file needs its finalization step to be playable. A crash or forced quit mid-recording leaves it unplayable, and there is no repair for that file. Recordings that complete normally are fine.

**Screenshots include my second monitor.** Desktop-level capture. Crop afterward, or accept it.

---

## Compatibility

Safe alongside ENB, ReShade, Steam/Discord/NVIDIA overlays, and other SKSE plugins that do not register the same Papyrus function names. Real conflicts are rare; the ones that exist are with other screenshot mods shipping scripts named `Printscreen_*`, which is a load-order problem no setting can fix.

---

## License

PrintScreen V5 is GPL-3.0-or-later. Every binary release is accompanied by a corresponding-source bundle on the matching GitHub release, containing the full source of the plugin and every statically linked dependency. Releases up to 4.x were MIT and remain available under those terms.

---

*For the architecture, threading model, and Papyrus API, see the [technical reference](PrintScreenV5_TechnicalReference.md).*