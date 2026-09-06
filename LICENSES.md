# PrintScreen V5 — License and Third-Party Notices

## Project license

PrintScreen V5 is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version.

Copyright (C) 2026 Bill Lea

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details. The full license text is in [LICENSE](LICENSE).

SPDX-License-Identifier: `GPL-3.0-or-later`

### Why GPL

PrintScreen V5 statically links [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)
v6.4.0, which is licensed GPL-3.0-or-later. The compiled plugin
(`Printscreen.dll`) is therefore a combined work with CommonLibSSE-NG and
must be distributed under GPL-compatible terms. To keep the source and the
binary under one consistent license, the project as a whole is licensed
GPL-3.0-or-later starting with PrintScreen 5.0.0.

Earlier PrintScreen releases (4.x and below) were licensed MIT and built
against MIT-licensed CommonLibSSE-NG (3.7.0 through 4.39.3). Those releases
remain available under their original terms; this relicense applies to
5.0.0 and later only.

### Obligations when redistributing Printscreen.dll

Anyone who distributes the compiled plugin (on Nexus Mods, GitHub Releases,
in a modlist, or elsewhere) must, per GPL-3.0 section 6:

1. **Provide Corresponding Source.** Either bundle the source with the
   binary or give a written offer / durable link to it. The source must be
   the exact revision used for the build and must identify the
   CommonLibSSE-NG revision (currently Git tag `v6.4.0`, commit
   `e7863a71523a2896c92ea9d3105c0d121dcdba0d`) and the build steps.
   The canonical source is https://github.com/Bill-Lea/Printscreenv5.
2. **Include the license texts.** Ship [LICENSE](LICENSE) (GPL-3.0),
   [LICENSE-CommonLibSSE-NG-EXCEPTIONS.md](LICENSE-CommonLibSSE-NG-EXCEPTIONS.md),
   and this file alongside the binary.
3. **Preserve notices.** Do not remove copyright or license notices from
   the source or this file.
4. **Add no further restrictions.** Distribution terms (for example Nexus
   Mods permission settings) must not forbid redistribution or
   modification, because the GPL grants those rights to every recipient.

---

## 1. CommonLibSSE-NG (v6.4.0)

- **Source:** https://github.com/alandtse/CommonLibSSE-NG
- **Version:** v6.4.0, Git tag `v6.4.0`, commit
  `e7863a71523a2896c92ea9d3105c0d121dcdba0d` (fetched via CMake
  FetchContent, see the top-level `CMakeLists.txt`)
- **License:** GPL-3.0-or-later WITH Modding Exception AND GPL-3.0 Linking
  Exception (with Corresponding Source)
- **Linkage:** static library compiled into `Printscreen.dll`
- **Copyright:** the CommonLibSSE-NG contributors; originally based on
  code © 2018 Ryan-rsm-McKenzie under MIT (see below)

CommonLibSSE-NG was relicensed from MIT to GPL-3.0-or-later on
2026-07-26 (upstream PR #238), effective from v5.0.0. Versions 4.39.3 and
earlier remain MIT.

The full GPL-3.0 text is in [LICENSE](LICENSE) (identical to upstream's
`COPYING`). The two additional permissions granted by upstream are
reproduced verbatim below and in
[LICENSE-CommonLibSSE-NG-EXCEPTIONS.md](LICENSE-CommonLibSSE-NG-EXCEPTIONS.md).

### Scope of the exceptions

Upstream defines the terms used in the exceptions as follows:

- **Modded Code:** Skyrim (and its variants), and hardware drivers that
  enable functionality via proprietary SDKs such as Nvidia DLSS/Streamline
  and AMD FidelityFX FSR3.
- **Modding Libraries:** SKSE and Windows.

The Modding Exception does **not** cover plugin code. Upstream states that a
plugin that statically links CommonLibSSE-NG forms a combined work and must
itself be licensed GPL-3.0-or-later or under a GPL-compatible license. That
is why PrintScreen V5 is licensed GPL-3.0-or-later.

### Exception text (verbatim from upstream `EXCEPTIONS.md`)

```
This Program is intended to be used with and modify existing code (the
"Modded Code") and to build a robust modding community with open source
principles. The purpose of this exception is to address issues when an open
source modding community interacts with potentially proprietary code. In
addition, the modding community often uses libraries (the "Modding
Libraries") under licenses that may be incompatible with the GPL ("Modding
Library Licenses").

===

Modding Exception

In addition, as a special exception, the authors give You the additional
right to link the code of this Program with the existing code that this
Program is intended to be used with or modify and to distribute linked
combinations including the two, subject to the limitations in this
paragraph. Modded Code permitted under this exception may link to the code
of this Program without causing the Modded Code and portion of the combined
work corresponding to the Modded Code to be covered by the GNU General
Public License. You must obey the GNU General Public License in all
respects for all of the Program code and other code used in conjunction
with the Program except the Modded Code covered by this exception. If you
modify this file, you may extend this exception to your version of the
file, but you are not obligated to do so. If you do not wish to provide
this exception without modification, you must delete this exception
statement from your version and license this file solely under the GPL
without exception.

===

GPL-3.0 Linking Exception (with Corresponding Source)

Additional permission under GNU GPL version 3 section 7

If you modify this Program, or any covered work, by linking or combining it
with Modding Libraries (or a modified version thereof), containing parts
covered by the terms of Modding Library Licenses, the licensors of this
Program grant you additional permission to convey the resulting work.
Corresponding Source for a non-source form of such a combination shall
include the source code for the parts of Modding Libraries used as well as
that of the covered work.
```

### Legacy MIT attribution

CommonLibSSE-NG is derived from CommonLibSSE, originally released under
MIT. Upstream keeps this notice at `licenses/LICENSE-MIT` for historical
attribution; it is **not** the current license of CommonLibSSE-NG.

```
MIT License

Copyright (c) 2018 Ryan-rsm-McKenzie

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### CommonLibSSE-NG's own dependencies

CommonLibSSE-NG pulls in DirectXMath, DirectXTK, fmt, nlohmann-json,
rapidcsv, simpleini, spdlog, toml11 and xbyak through vcpkg, and bundles
OpenVR as a Git submodule. In this build the REX INI/JSON/TOML helpers,
Xbyak trampoline support, Skyrim VR support, and tests are all disabled
(see the top-level `CMakeLists.txt`), so OpenVR (BSD-3-Clause, © 2015 Valve
Corporation) and xbyak are not linked into `Printscreen.dll`. The remaining
libraries are covered in the sections below.

---

## 2. DirectXTex

- **Source:** https://github.com/microsoft/DirectXTex
- **License:** MIT
- **Copyright:** © Microsoft Corporation

```
MIT License

Copyright (c) Microsoft Corporation.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 3. DirectXMath

- **Source:** https://github.com/microsoft/DirectXMath
- **License:** MIT
- **Copyright:** © Microsoft Corporation

Used by DirectXTex and CommonLibSSE-NG. Same MIT terms as DirectXTex above.

---

## 4. DirectX Tool Kit (DirectXTK)

- **Source:** https://github.com/microsoft/DirectXTK
- **License:** MIT
- **Copyright:** © Microsoft Corporation

Same MIT terms as DirectXTex above.

---

## 5. spdlog

- **Source:** https://github.com/gabime/spdlog
- **License:** MIT
- **Copyright:** © 2016 Gabi Melman

```
The MIT License (MIT)

Copyright (c) 2016 Gabi Melman.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 6. fmt

- **Source:** https://github.com/fmtlib/fmt
- **License:** MIT (with an optional exception permitting binary
  distribution without the notice; the notice is retained here anyway)
- **Copyright:** © 2012-present Victor Zverovich and {fmt} contributors

Same MIT terms as spdlog above.

---

## 7. nlohmann-json

- **Source:** https://github.com/nlohmann/json
- **License:** MIT
- **Copyright:** © 2013-2025 Niels Lohmann

```
MIT License

Copyright (c) 2013-2025 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 8. lodepng

- **Source:** https://github.com/lvandeve/lodepng
- **License:** zlib License
- **Copyright:** © 2005-2018 Lode Vandevenne

Linked when found (CMake option `PRINTSCREEN_WITH_LODEPNG`, default ON).

```
Copyright (c) 2005-2018 Lode Vandevenne

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software
    in a product, an acknowledgment in the product documentation would be
    appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
```

---

## 9. libpng (with APNG feature)

- **Source:** https://github.com/pnggroup/libpng
- **License:** PNG Reference Library License version 2 (libpng license)
- **Copyright:** © 1995-2025 The PNG Reference Library Authors;
  © 2018-2025 Cosmin Truta; © 2000-2002, 2004, 2006-2018 Glenn Randers-Pehrson;
  © 1996-1997 Andreas Dilger; © 1995-1996 Guy Eric Schalnat, Group 42, Inc.

Linked when found by CMake `find_package(PNG)`.

```
PNG Reference Library License version 2

Permission is hereby granted to use, copy, modify, and distribute this
software, or portions hereof, for any purpose, without fee, subject to
the following restrictions:

 1. The origin of this software must not be misrepresented. You must
    not claim that you wrote the original software. If you use this
    software in a product, an acknowledgment in the product
    documentation would be appreciated, but is not required.

 2. Altered source versions must be plainly marked as such and must not
    be misrepresented as being the original software.

 3. This Copyright notice may not be removed or altered from any
    source or altered source distribution.
```

---

## 10. zlib (libpng dependency)

- **Source:** https://zlib.net
- **License:** zlib License
- **Copyright:** © 1995-2022 Jean-loup Gailly and Mark Adler

Pulled in by libpng. Same zlib terms as lodepng above.

---

## 11. rapidcsv

- **Source:** https://github.com/d99kris/rapidcsv
- **License:** BSD-3-Clause
- **Copyright:** © 2017 Kristofer Berggren

Dependency of CommonLibSSE-NG. Not used directly by PrintScreen source.

```
BSD 3-Clause License

Copyright (c) 2017, Kristofer Berggren
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## 12. simpleini

- **Source:** https://github.com/brofield/simpleini
- **License:** MIT
- **Copyright:** © 2006-2024 Brodie Thiesfield

Dependency of CommonLibSSE-NG (REX INI helpers, disabled in this build).
Same MIT terms as spdlog above.

---

## 13. toml11

- **Source:** https://github.com/ToruNiina/toml11
- **License:** MIT
- **Copyright:** © 2017 Toru Niina

Dependency of CommonLibSSE-NG (REX TOML helpers, disabled in this build).
Same MIT terms as spdlog above.

---

## 14. xbyak

- **Source:** https://github.com/herumi/xbyak
- **License:** BSD-3-Clause
- **Copyright:** © 2007 MITSUNARI Shigeo

Declared in `vcpkg.json` and by CommonLibSSE-NG, but disabled at the
CommonLibSSE level (`SKSE_SUPPORT_XBYAK OFF`), so it is not linked into
`Printscreen.dll`. The BSD-3-Clause notice (same form as rapidcsv above,
with copyright holder MITSUNARI Shigeo) applies only if that option is
turned on.

---

## License summary

| Component | License | Linked into DLL |
|---|---|---|
| PrintScreen V5 (this project) | GPL-3.0-or-later | yes |
| CommonLibSSE-NG 6.4.0 | GPL-3.0-or-later WITH Modding Exception AND GPL-3.0 Linking Exception | yes (static) |
| DirectXTex, DirectXTK, DirectXMath | MIT | yes |
| spdlog, fmt | MIT | yes |
| nlohmann-json | MIT | yes |
| lodepng | zlib | yes, when found |
| libpng, zlib | libpng / zlib | yes, when found |
| rapidcsv | BSD-3-Clause | transitively, via CommonLibSSE-NG |
| simpleini, toml11 | MIT | no (REX helpers disabled) |
| xbyak | BSD-3-Clause | no (disabled) |
| OpenVR | BSD-3-Clause | no (VR disabled) |

The combined work is distributed under GPL-3.0-or-later. Every other
component is under a permissive license compatible with the GPL, and their
notices are retained above as those licenses require. Distributing the
compiled DLL requires providing Corresponding Source; see the
"Obligations when redistributing" section at the top of this file.
