# PrintScreen V5 User Guide

Version 5.0.2
Author: William G Lea
For Skyrim Special Edition and Anniversary Edition (SKSE64 plugin)
Windows 10 or 11

---

## What it does

PrintScreen takes screenshots, animated images, and short videos from inside Skyrim. You press a key, the HUD drops out for a moment, and a file lands in a folder you pick.

It doesn't hook the game's renderer. It reads the finished frame from Windows itself, using the same desktop duplication feature that screen recorders use. That has a nice consequence: whatever ENB, ReShade preset, or overlay you run, the file shows what your monitor showed.

It also has a less nice consequence, which is covered under "Things to know" below.

### Formats

| Kind | Formats |
|------|---------|
| Still image | PNG, JPG, BMP, TIF, DDS, GIF (single frame) |
| Animation | AGIF (animated GIF), APNG (animated PNG) |
| Video | H.264 in an MP4 file |

### If you're coming from V4

- PapyrusUtil and JContainers are no longer required. The plugin reads and writes its own settings file.
- Still captures get camera metadata. JPGs carry the in-game field of view as an EXIF focal length, and PNGs get a small companion file with the same numbers. Panorama software can use this instead of asking you.
- The hotkey re-arms itself every time you load a save. A dead hotkey after a mid-playthrough install was a real problem in V4.
- Captures cancel cleanly when you open a menu, and the plugin tidies up after itself, including after a crash.

---

## Requirements

- Skyrim Special Edition 1.5.39 or later. Every Anniversary Edition build is fine. Skyrim VR is not supported.
- [SKSE64](https://skse.silverlock.org/) for your game version.
- [SkyUI](https://www.nexusmods.com/skyrimspecialedition/mods/12604), for the settings menu. The plugin will capture without it, but then the only way to change settings is to edit the settings file by hand.
- Windows 10 or 11.

You do not need PapyrusUtil or JContainers. If other mods want them, leave them installed. PrintScreen ignores them.

---

## Installation

Install the archive with Mod Organizer 2 or Vortex. It's laid out the way the game expects, so there's nothing to choose.

For a manual install, these go under your `Data` folder:

| File | Goes in |
|------|---------|
| `Printscreen.esp` | `Data\` |
| `SKSE\Plugins\Printscreen.dll` | `Data\SKSE\Plugins\` |
| `Scripts\Printscreen_*.pex` | `Data\Scripts\` |
| `Interface\PrintScreen\*.dds` | `Data\Interface\PrintScreen\` |
| `SKSE\Plugins\StorageUtilData\PrintScreen.json` | `Data\SKSE\Plugins\StorageUtilData\` |

Enable the ESP and start the game through SKSE.

### Check two settings before your first shot

The settings file that ships in the archive came from a test machine. It loads fine, but it sets two things you almost certainly want to change:

- The output folder is `C:/pictures/test/vanilla`. The plugin will create that folder and happily fill it.
- The hotkey is Right Shift.

Open the MCM, set **Path** to where you want your screenshots, and pick a key you like. Both are saved when you close the menu.

---

## Quick start

1. Launch through SKSE. You'll see "PrintScreen 5.02 initialized (event-driven)" the first time, and "PrintScreen 5.02 re-initialized" on every later load.
2. Open the MCM (Escape, then Mod Configuration) and find PrintScreen.
3. On the Settings page, set **Path**. It has to be a full path such as `D:/Skyrim/Shots`, not a relative one. The folder is created if it doesn't exist.
4. Set **Select Take Photo Key** to a key nothing else uses.
5. Close the menu. Your settings are written to disk at that point.
6. Press the key.

You get "Taking screenshot..." right away and "Screenshot saved! Total: N" when the file is written. The count resets each session.

---

## Taking a capture

Press the key once to start. What happens next depends on the format:

- A still is grabbed in a fraction of a second. The HUD comes back as soon as the frame is captured, before the file is even encoded.
- An animation records for the duration you set, with the HUD hidden the whole time. The HUD returns when recording stops, and the file is assembled after that. Assembling a long APNG can take a while.
- A video records for the duration you set, encoding as it goes. The HUD stays hidden until the recording ends.

Press the key again during a capture to cancel it. Cancelling a video keeps what was recorded so far as a playable MP4 when possible.

While a capture is running with **Automatic Menu Removal** on, opening almost any menu cancels it. Inventory, map, journal, dialogue, a container, the favourites menu, all of them. The console is the exception; it doesn't cancel anything. If you'd rather menus didn't cancel captures, turn that toggle off, but then the HUD isn't hidden either.

The key is ignored while any menu is open, while you're typing in a text box, and for three quarters of a second after the last press.

### The settings summary spell

The ESP adds a spell called **PS_Configuration**. Casting it pops up a message box listing your current format, path, hotkey, and shot count. It's a quick way to check what's set without opening the MCM.

---

## The MCM

Two pages. The blank landing page shows one of six sample screenshots picked at random.

### Settings page

**Path**. Where captures are written. If the path is longer than 30 characters the text box is disabled and the menu tells you to edit the settings file instead. The MCM's input widget is awkward for long strings, that's all.

**Select Image File Type**. PNG, APNG, BMP, TIF, JPG, GIF, AGIF, DDS, or H264. Picking a type enables the options that apply to it and greys out the rest. H264 is configured on the second page.

**Automatic Menu Removal**. Hides the HUD and every menu during capture, the same way the `tm` console command does, then puts them back. On by default. This toggle also decides whether opening a menu cancels a capture.

**Select Take Photo Key**. Click it and press a key. Keys already used by a game control are refused.

**Save/Restore Configuration**. On by default. When on, your settings are written to the settings file whenever you close the MCM and read back on game start. Turn it off only if you want every session to start from defaults.

The rest of the page is format-specific:

| Option | Applies to | Range | Default | Notes |
|--------|-----------|-------|---------|-------|
| JPG Compression | JPG | 0 to 100 | 90 | Higher is better quality and a bigger file |
| PNG Compression | PNG | 0 to 9 | 9 | 0 to 2 skip PNG filtering for speed; 3 and up use adaptive filtering, which compresses better. There's no difference between 3 and 9 |
| Capture Duration | AGIF, APNG | 1 to 30 s | 5 | Anything over 15 is cut to 15 |
| FPS | AGIF, APNG | 1 to 30 | 15 | Values below 15 are reset to 10 the next time the file is loaded. See "Things to know" |
| Loop Count | AGIF, APNG | 0 to 10 | 0 | 0 loops forever |
| Delta Mode | AGIF, APNG | 0, 1, 2 | 0 | Explained below |
| Tif Compression Mode | TIF | UNCOMPRESSED, RLE, LZW, ZIP | UNCOMPRESSED | LZW is a safe choice for lossless, smaller files |
| DDS Mode | DDS | see below | UNCOMPRESSED | |

**Delta Mode** decides how animation frames are stored:

- 0 stores every frame whole. Largest files, nothing can go wrong.
- 1 stores only the rectangle that changed since the previous frame. Much smaller for scenes where most of the picture holds still.
- 2 is like 1, but pixels that didn't change inside that rectangle are made transparent. Smallest files for slow, subtle motion.

A note on Skyrim specifically: temporal anti-aliasing, film grain, and moving light touch nearly every pixel every frame. Delta modes help most in still scenes with a small moving subject. Try 1 first.

**DDS modes**: UNCOMPRESSED, BC1, BC2, BC3, BC4, BC5, BC6h, BC7_SLOW, BC7_NORMAL, BC7_FAST. Two things to know. First, "UNCOMPRESSED" doesn't actually produce an uncompressed file; the plugin treats it as BC1. Second, BC6h and BC7_SLOW compress on the CPU and can take a minute or more per screenshot. The HUD comes back immediately; only the file write is slow. BC7_NORMAL and BC7_FAST use the quick BC7 path and are far faster.

### Video Settings page

Everything here is greyed out unless the image type is H264.

Capture:

- **Duration (seconds)**: 1 to 120, default 10.
- **Target Resolution**: Native, 720p, 1080p, 1440p, or 4K. Output is scaled on the GPU with the aspect ratio preserved, so a widescreen desktop into a 16:9 target gets black bars. Native means no scaling. If your screen is already smaller than the target, no scaling happens either.
- **Frame Rate**: 30 or 60. 60 doubles the frame count and roughly doubles the file size.

Encoding:

- **Quality Preset**: Low, Medium, High, Very High, or Custom. Presets set the bitrate for you: 4, 8, 16, and 35 Mbps respectively. Custom unlocks the slider. Default High.
- **Bitrate (kbps)**: 1000 to 50000. Only active with the Custom preset, and ignored entirely when Rate Control is CQP.
- **Keyframe Interval (s)**: 0.5 to 10, default 2. How often a complete frame is written. Shorter means smoother seeking in a player and a bigger file.
- **Encoder Preference**: Auto, Prefer Hardware, or Force Software. Auto uses your GPU's encoder if Windows can find one (NVIDIA, AMD, and Intel all provide one through Media Foundation) and falls back to software if not. Force Software avoids the GPU encoder entirely. Prefer Hardware currently behaves the same as Auto.
- **Rate Control**: CBR holds a constant bitrate, so file size is predictable. VBR (the default) lets the bitrate vary and gives the best quality for the size. CQP targets a fixed quality level and the file is whatever size that takes. The quality level for CQP is fixed at 70 and isn't adjustable from the menu.
- **Container**: MP4 only.

Video frames are encoded as they're captured and streamed straight to the file, so a two-minute recording uses no more memory than a five-second one.

---

## Formats at a glance

| Type | Extension | Good for | Keep in mind |
|------|-----------|----------|--------------|
| PNG | .png | Everyday screenshots, editing, panorama stitching | Also writes a small `.png.json` companion file with the camera data |
| JPG | .jpg | Small files, sharing | Lossy. Carries EXIF camera data |
| BMP | .bmp | Raw pixels | Big. No metadata |
| TIF | .tif | Editing workflows | Pick a compression mode or it's as big as a BMP |
| DDS | .dds | Texture work | UNCOMPRESSED is really BC1. BC6h and BC7_SLOW are slow |
| GIF | .gif | A 256-colour still | Not animated. Use AGIF for that |
| AGIF | .gif | Short looping clips | 256 colours from a single palette |
| APNG | .png | Full-colour clips | Bigger than AGIF. Every frame is held in memory while the file is built |
| H264 | .mp4 | Video | See the Video Settings page |

Files are named `SS_` followed by the date and time down to the millisecond, for example `SS_20260913_143022_417.png`.

---

## Camera metadata for panorama stitching

When you take a still, the plugin reads the game's current field of view (first-person FOV in first person, world FOV otherwise) and converts it to a 35 mm equivalent focal length. Then:

- JPGs get that written into standard EXIF fields (FocalLength and FocalLengthIn35mmFormat), plus Make, Model, and Software tags.
- PNGs get a companion file named `<screenshot>.png.json` alongside the image containing the focal length, FOV, and image size. PNG doesn't have a reliable EXIF slot through the Windows encoder, so the companion file is the dependable route.
- BMP gets nothing. TIF, GIF, and DDS shouldn't be relied on for this either.

Stitching tools that read EXIF, or PhotoFileMerge V2 which reads the companion file, can then line up shots without you typing in a focal length. If the FOV can't be read at capture time, the metadata is left out and the screenshot is otherwise normal.

The conversion assumes the game's FOV value is the horizontal field of view. If you've changed the FOV with the console `fov` command, that's the value that gets used.

---

## The files PrintScreen uses

### Settings: `Data\SKSE\Plugins\StorageUtilData\PrintScreen.json`

The plugin owns this file. It's written when you close the MCM and read when the game starts. If it's missing or unreadable, a fresh one with defaults is written and you get a notification. You can edit it by hand between sessions. Every value is checked on load, and anything out of range is quietly put back to its default, usually with a notification so you know.

Keys are stored in lower case. A file left over from V4's PapyrusUtil layout, with values nested under `int`, `float`, and `string`, is converted on first read.

### Diagnostics: `Documents\My Games\Skyrim Special Edition\SKSE\PrintScreen.ini`

Optional. The plugin never creates it. It only controls logging; there's nothing in it you need for normal use. If you create one, these are the settings and their defaults:

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

`LogLevel` accepts NONE, ERROR, WARN, INFO, DEBUG, or TRACE, or the numbers 0 to 5. INFO is right for normal play. DEBUG is what to use when you're reporting a bug. `ConsoleOutput` sends log lines to a debugger if one is attached; it doesn't write to the in-game console. The `[Performance]` and `[Capture]` keys are read but nothing currently acts on them.

If you happen to have a V2-era file at `SKSE\Plugins\Printscreen_Log.ini`, that one is read instead. Unknown keys are reported in the log, not treated as errors.

### The log: `Documents\My Games\Skyrim Special Edition\SKSE\Logs\Printscreen.log`

Look here first when something's wrong. It records startup, the game version, which encoder was chosen for video, every capture, and every warning. It rolls over at 8 MB (three files are kept). Attach it to any bug report.

---

## Things to know

**It captures a monitor, not the game window.** Desktop duplication grabs the first monitor attached to your primary graphics card. If Skyrim is running on a different monitor, you'll get the wrong screen. Windows also blanks any content it considers protected (some video players) in duplicated output.

**Animations are capped at 15 seconds.** Frames are staged to disk during recording and then read back to build the file. For APNG every frame is loaded into memory at once, and 15 seconds at 30 fps of a 4K desktop is already a lot. The slider goes to 30 for historical reasons; anything higher than 15 is clamped, and you'll get a notification saying so when you change image type.

**An animation FPS below 15 won't stick.** The settings check on load resets any value under 15 to 10. So 12 becomes 10, and 6 becomes 10. Values from 15 up are kept as you set them.

**The Quality and Optimize sliders are gone.** Earlier V5 builds showed them for animated captures, but neither one did anything. If your settings file still has `quality` or `optimize` entries from those builds, they're ignored.

**A temporary folder appears during animated captures.** It's created inside your output folder, named `gif_temp_` or `apng_temp_` followed by a timestamp, and holds one BMP per frame. It's deleted when the file is finished. If the game crashes mid-capture, the folder is removed on your next load, or on your next animated capture into the same folder, whichever comes first.

**A cancelled video is usually still playable.** The plugin closes the file properly with whatever frames it had. A crash is different: the MP4 won't have its index written and no player will open it. There's no repair for that.

---

## Troubleshooting

**Nothing appears in my folder.** Check **Path** in the MCM. Remember the shipped settings file points at `C:/pictures/test/vanilla`. Then check the log.

**The hotkey does nothing.** Confirm which key is bound. It might be Right Shift from the shipped settings rather than what you expected. The key is suppressed while any menu is open and while a text field has focus. If it's still dead, load a save or open and close the MCM; both re-register it.

**Opening my inventory cancels the capture.** Intended, while Automatic Menu Removal is on. Turn the toggle off if you want to record through menus, at the cost of the HUD showing in the capture.

**The HUD is in my screenshot.** Either Automatic Menu Removal is off, or something in your load order is forcing menus back on. PrintScreen uses the same switch as the `tm` console command; if `tm` doesn't hide your HUD, PrintScreen can't either.

**A DDS capture takes forever.** BC6h and BC7_SLOW are CPU compressors that run at seconds per frame. You can keep playing; only the file write is slow. Use BC7_FAST or BC1 unless you need the quality.

**"Capture: finished, but no completion report was received."** The plugin finished but the completion message never reached the script. The file is almost certainly fine. If you see this repeatedly, report it with both `Printscreen.log` and the Papyrus log.

**Video won't start.** Check the log for the encoder line. If you set Encoder Preference to Prefer Hardware on a machine without a GPU encoder, switch to Auto. If Auto also fails, try Force Software.

**The animation flickers or has black blotches.** Set Delta Mode to 0 and try again. If that fixes it, report it with the settings you were using.

---

## Compatibility

Works alongside ENB, ReShade, Steam and Discord overlays, and other SKSE plugins. The only real conflict is another mod that ships scripts named `Printscreen_*`, which is a load-order problem no setting can solve.

---

## License

PrintScreen V5 is GPL-3.0-or-later. Every release on Nexus or GitHub is accompanied by a corresponding-source bundle on the matching GitHub release, containing the plugin source and the source of every statically linked library. Releases up to 4.x were MIT and remain available on those terms.

Source: https://github.com/Bill-Lea/Printscreenv5

---

*For how it works inside, see the [technical reference](PrintScreenV5_TechnicalReference.md).*
