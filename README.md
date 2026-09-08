# PrintScreen V5

An SKSE plugin for Skyrim Special Edition / Anniversary Edition that captures
screenshots, animated PNG, and GIF sequences from inside the game, with an MCM
configuration menu.

## Layout

| Path | Contents |
|---|---|
| `src/` | C++ source for `Printscreen.dll` (CMake, CommonLibSSE-NG 6.4.0) |
| `Papyrus Scripts/` | Papyrus sources (`.psc`) for the quest, MCM, and helper scripts |
| `mod/` | Non-code mod payload: `Printscreen.esp`, MCM textures, default JSON config, user documentation |
| `tools/` | Release packaging scripts |
| `BUILD.md` | Toolchain and step-by-step build instructions |
| `LICENSES.md` | License of this project and notices for every bundled dependency |

## Building

See [BUILD.md](BUILD.md).

## License

PrintScreen V5 is licensed **GPL-3.0-or-later**. See [LICENSE](LICENSE),
[LICENSE-CommonLibSSE-NG-EXCEPTIONS.md](LICENSE-CommonLibSSE-NG-EXCEPTIONS.md),
and [LICENSES.md](LICENSES.md) for the full text, the CommonLibSSE-NG
additional permissions, and third-party notices.

Every binary release on Nexus Mods or GitHub is accompanied by a
`Printscreen-<version>-corresponding-source.7z` bundle on the matching
GitHub Release, containing this repository at the tagged commit plus the
source of every statically linked dependency.

Releases up to 4.x were MIT licensed and remain available under those terms.
