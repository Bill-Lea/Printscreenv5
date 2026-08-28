Printscreen V4 -> CommonLibSSE-NG 6.4.0 build-file migration

Replace these files in the repository:
  vcpkg.json
  vcpkg-configuration.json
  CMakeLists.txt
  src/CMakeLists.txt

Then remove stale configuration before rebuilding:
  Remove-Item -Recurse -Force .\build -ErrorAction SilentlyContinue
  Remove-Item -Recurse -Force .\vcpkg_installed -ErrorAction SilentlyContinue

Configure/build:
  cmake --preset release
  cmake --build --preset release

Notes:
- CommonLibSSE-NG is pinned to Git tag v6.4.0.
- SE and AE are enabled; VR is disabled.
- CommonLib tests are disabled.
- vcpkg no longer uses the Color-Glass CommonLib registry because that port
  currently exposes CommonLibSSE-NG only through 3.7.0.
- The Microsoft vcpkg baseline matches the one declared by upstream
  CommonLibSSE-NG 6.4.0.
