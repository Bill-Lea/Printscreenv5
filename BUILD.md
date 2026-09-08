# Building PrintScreen V5

This document is the build recipe that accompanies the Corresponding Source
for `Printscreen.dll` (GPL-3.0 section 6). Following it on a clean machine
reproduces the released plugin.

## Toolchain used for the 5.0.0 release

| Component | Version |
|---|---|
| Visual Studio 2022 Community, MSVC toolset | 14.44.35207 |
| Windows SDK | 10.0.26100.0 |
| CMake | 3.24 or newer (3.31 used) |
| Ninja | any recent (1.11+) |
| vcpkg | the copy bundled with Visual Studio 2022 (`VC\vcpkg`), registry baseline `ee12231b20c95013c6638d845d04c91559a1d1ff` |
| vcpkg triplet | `x64-windows-static-md` (static libraries, dynamic CRT) |
| Papyrus compiler | Skyrim SE Creation Kit `PapyrusCompiler.exe` with SkyUI SDK sources |

`CMakePresets.json` hard-codes the MSVC and Windows SDK paths above. If your
installation differs, edit the `base` preset's `environment` and
`cacheVariables` blocks, or pass the equivalent `-D` values on the command
line.

## Native plugin (`Printscreen.dll`)

```powershell
git clone https://github.com/Bill-Lea/Printscreenv5.git
cd Printscreenv5
git checkout v5.0.0
cmake --preset release
cmake --build --preset release
```

The DLL lands in `build/bin/Printscreen.dll`. `build.ps1` wraps the same two
CMake calls with a Visual Studio developer shell and a stale-cache check:

```powershell
.\build.ps1 -Config Release
```

### What the configure step fetches

- **CommonLibSSE-NG v6.4.0** (commit `e7863a71523a2896c92ea9d3105c0d121dcdba0d`)
  via CMake `FetchContent` from https://github.com/alandtse/CommonLibSSE-NG,
  with submodules.
- **hde64** from MinHook v1.3.4 via `FetchContent` inside CommonLibSSE-NG
  (only `src/hde` is compiled).
- **vcpkg ports**, resolved against the baseline in
  `vcpkg-configuration.json`:

  | Port | Version |
  |---|---|
  | directxmath | 2025-04-03 |
  | directxtex | 2025-10-27 |
  | directxtk | 2025-10-27 |
  | fmt | 12.1.0 |
  | libpng (feature `apng`) | 1.6.53 |
  | zlib | 1.3.1 |
  | lodepng | 2021-12-04 |
  | nlohmann-json | 3.12.0 |
  | rapidcsv | 8.90 |
  | simpleini | 4.25 |
  | spdlog | 1.16.0 |
  | toml11 | 4.4.0 |
  | xbyak | 7.28 |

If you are building from the offline `Printscreen-<version>-corresponding-source.7z`
bundle instead of the network, the `deps/` folder contains every one of these
sources. Point `FETCHCONTENT_SOURCE_DIR_COMMONLIBSSE` and
`FETCHCONTENT_SOURCE_DIR_HDE64` at the extracted trees, and place the vcpkg
tarballs in your vcpkg `downloads` directory
(`%LOCALAPPDATA%\vcpkg\downloads` for the Visual Studio copy) so vcpkg
uses them instead of downloading.

### CommonLibSSE-NG options fixed by this project

Set in the top-level `CMakeLists.txt`:

- `ENABLE_SKYRIM_SE=ON`, `ENABLE_SKYRIM_AE=ON`, `ENABLE_SKYRIM_VR=OFF`
- `BUILD_TESTS=OFF`
- `REX_OPTION_INI/JSON/TOML=OFF`
- `SKSE_SUPPORT_XBYAK=OFF`
- `SKSE_SUPPORT_PATCH_SAFETY` left at its default (ON), which is what pulls in hde64.

## Papyrus scripts

Sources are in `Papyrus Scripts/*.psc`. `Printscreen_MCM_Script.psc` extends
`SKI_ConfigBase`, so the SkyUI SDK sources must be on the import path.
Compile with the Creation Kit compiler, for example:

```powershell
& "$CK\Papyrus Compiler\PapyrusCompiler.exe" "Papyrus Scripts" -all `
    -f="TESV_Papyrus_Flags.flg" `
    -i="Papyrus Scripts;$SkyUISDK\Scripts\Source;$SkyrimData\Scripts\Source" `
    -o="Papyrus Scripts\Compiled"
```

Compiled `.pex` files are ignored by git and are expected in
`Papyrus Scripts/Compiled/` by the packaging script.

## Packaging

- `tools/Package-Mod.ps1` assembles `dist/Printscreen.7z` for Nexus from the
  built DLL, compiled scripts, the `mod/` payload, and the license files.
- `tools/Make-CorrespondingSource.ps1` assembles
  `dist/Printscreen-<version>-corresponding-source.7z` from the git tree,
  the fetched CommonLibSSE-NG and hde64 sources in `build/_deps`, and the
  vcpkg source tarballs. Upload this alongside every binary release.

Both scripts need `7z.exe` on `PATH` or at `C:\Program Files\7-Zip\7z.exe`.
