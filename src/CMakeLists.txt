# src/CMakeLists.txt

cmake_minimum_required(VERSION 3.25)

# -------------------------
# Options / user knobs
# -------------------------
set(PLUGIN_TARGET "Printscreen" CACHE STRING "SKSE plugin DLL name (no prefix)")
set(MO2_MOD_DIR "C:/Modding/Mo2/Mods/Printscreen" CACHE PATH "MO2 mod root for deployment")
set(MO2_SKSE_PLUGIN_DIR "${MO2_MOD_DIR}/SKSE/Plugins" CACHE PATH "MO2 SKSE/Plugins deployment path")
set(PLUGIN_TOML "Printscreen.toml" CACHE STRING "Plugin .toml to deploy (relative to repo root)")
set(PLUGIN_DB_FILES "" CACHE STRING "Semicolon-separated list of DB files to copy (relative to repo root)")
set(PLUGIN_DB_GLOB "*.db" CACHE STRING "Glob for DB files to copy if PLUGIN_DB_FILES is empty")
option(PRINTSCREEN_WITH_LODEPNG "Link to lodepng if available" ON)

# -------------------------
# Language / compiler flags
# -------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(
    /permissive-
    /Zc:__cplusplus
    /Zc:preprocessor
    /Zc:externConstexpr
    /EHsc
    /MP
    /W4
    /utf-8
    /wd4100 /wd4189 /wd4324
  )
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS)

# Allow MSVC STL to work with Clang 18 (expects Clang 19+)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_definitions(-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH)
endif()

# -------------------------
# Dependencies (via vcpkg)
# -------------------------
# Your Presets wire the vcpkg toolchain & x64 triplet already.  (CMakePresets.json)
# CommonLibSSE-NG 6.4.0 is supplied by the top-level FetchContent block.
# Keep find_package as a fallback for developers who provide an installed package.
if(NOT TARGET CommonLibSSE::CommonLibSSE)
  find_package(CommonLibSSE CONFIG REQUIRED)
endif()
find_package(directxtex CONFIG REQUIRED)        # Microsoft::DirectXTex
find_package(directxtk  CONFIG REQUIRED)        # Microsoft::DirectXTK

# Strip MSVC-only /wd and /we flags from imported targets when using Clang
# (CommonLibSSE exports INTERFACE_COMPILE_OPTIONS meant for cl.exe)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND TARGET CommonLibSSE::CommonLibSSE)
  get_target_property(_clsse_opts CommonLibSSE::CommonLibSSE INTERFACE_COMPILE_OPTIONS)
  if(_clsse_opts)
    set(_clean_opts "")
    foreach(_opt IN LISTS _clsse_opts)
      if(NOT _opt MATCHES "^/w")
        list(APPEND _clean_opts "${_opt}")
      endif()
    endforeach()
    set_target_properties(CommonLibSSE::CommonLibSSE PROPERTIES INTERFACE_COMPILE_OPTIONS "${_clean_opts}")
  endif()
endif()

# Optional PNG (APNG enabled by your vcpkg.json features)
find_package(PNG QUIET)                         # PNG::PNG (optional)
if(PRINTSCREEN_WITH_LODEPNG)
  find_package(lodepng QUIET)
endif()

# -------------------------
# Sources
# -------------------------
set(PROJ_ROOT "${CMAKE_CURRENT_LIST_DIR}/..") # repo root (parent of src)

file(GLOB_RECURSE PRINTSCREEN_SOURCES
  "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/*.cc"
  "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
)
# Exclude backup/patched/legacy files and CMake-generated files from build
list(FILTER PRINTSCREEN_SOURCES EXCLUDE REGEX ".*_patched\\.cpp$")
list(FILTER PRINTSCREEN_SOURCES EXCLUDE REGEX ".*/out/.*")
list(FILTER PRINTSCREEN_SOURCES EXCLUDE REGEX ".*/files5000/.*")
list(FILTER PRINTSCREEN_SOURCES EXCLUDE REGEX ".*/PapyrusInterface_NoMenu_Update/.*")
list(FILTER PRINTSCREEN_SOURCES EXCLUDE REGEX ".*CMakeCXXCompilerId\\.cpp$")
file(GLOB_RECURSE PRINTSCREEN_HEADERS
  "${CMAKE_CURRENT_LIST_DIR}/*.h"
  "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
)

# set(RESOURCES "${CMAKE_CURRENT_LIST_DIR}/version.rc") # optional

# -------------------------
# Target
# -------------------------
add_library(${PLUGIN_TARGET} SHARED
  ${PRINTSCREEN_SOURCES}
  ${PRINTSCREEN_HEADERS}
  ${RESOURCES}
)

# Local include paths: src/ and (optionally) repo root for pch.h or shared headers
target_include_directories(${PLUGIN_TARGET} PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}"
  "${PROJ_ROOT}"
)

# Prefer a local pch.h at repo root, else use SKSE's PCH
if(EXISTS "${PROJ_ROOT}/pch.h")
  message(STATUS "Using project PCH: ${PROJ_ROOT}/pch.h")
  target_precompile_headers(${PLUGIN_TARGET} PRIVATE "${PROJ_ROOT}/pch.h")
else()
  message(STATUS "Using SKSE PCH: <SKSE/Impl/PCH.h>")
  target_precompile_headers(${PLUGIN_TARGET} PRIVATE <SKSE/Impl/PCH.h>)
endif()

# -------------------------
# Find Windows SDK libraries for x64
# -------------------------
# Force x64 architecture for Windows SDK libraries
set(_WIN_SDK_ARCH "x64")

if(MSVC)
  get_filename_component(_MSVC_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
  get_filename_component(_MSVC_DIR "${_MSVC_DIR}" DIRECTORY)
  get_filename_component(_MSVC_DIR "${_MSVC_DIR}" DIRECTORY)
  get_filename_component(_MSVC_DIR "${_MSVC_DIR}" DIRECTORY)
  set(_MSVC_LIB_PATH "${_MSVC_DIR}/lib/x64")
  if(EXISTS "${_MSVC_LIB_PATH}")
    message(STATUS "MSVC x64 Library Path: ${_MSVC_LIB_PATH}")
    target_link_directories(${PLUGIN_TARGET} PRIVATE "${_MSVC_LIB_PATH}")
  endif()
endif()

if(DEFINED ENV{WindowsSdkDir})
  set(_WIN_SDK_DIR "$ENV{WindowsSdkDir}")
else()
  set(_WIN_SDK_DIR "C:/Program Files (x86)/Windows Kits/10")
endif()

if(DEFINED ENV{WindowsSDKVersion})
  string(REGEX REPLACE "\\\\$" "" _WIN_SDK_VER "$ENV{WindowsSDKVersion}")
else()
  file(GLOB _SDK_VERSIONS "${_WIN_SDK_DIR}/Lib/10.*")
  if(_SDK_VERSIONS)
    list(SORT _SDK_VERSIONS)
    list(GET _SDK_VERSIONS -1 _LATEST_SDK)
    get_filename_component(_WIN_SDK_VER "${_LATEST_SDK}" NAME)
  else()
    set(_WIN_SDK_VER "10.0.22621.0")
  endif()
endif()

set(_WIN_SDK_LIB_PATH "${_WIN_SDK_DIR}/Lib/${_WIN_SDK_VER}/um/${_WIN_SDK_ARCH}")
set(_WIN_SDK_UCRT_PATH "${_WIN_SDK_DIR}/Lib/${_WIN_SDK_VER}/ucrt/${_WIN_SDK_ARCH}")

message(STATUS "Windows SDK Library Path: ${_WIN_SDK_LIB_PATH}")
message(STATUS "Windows SDK UCRT Path: ${_WIN_SDK_UCRT_PATH}")

# All libraries use explicit full paths — no find_library needed
set(WINDOWSCODECS_LIB "${_WIN_SDK_LIB_PATH}/Windowscodecs.lib")
set(OLE32_LIB        "${_WIN_SDK_LIB_PATH}/Ole32.Lib")
set(D3D11_LIB        "${_WIN_SDK_LIB_PATH}/d3d11.lib")
set(DXGI_LIB         "${_WIN_SDK_LIB_PATH}/dxgi.lib")
set(PROPSYS_LIB      "${_WIN_SDK_LIB_PATH}/propsys.lib")
set(VERSION_LIB      "${_WIN_SDK_LIB_PATH}/version.lib")
set(KERNEL32_LIB     "${_WIN_SDK_LIB_PATH}/kernel32.lib")
set(USER32_LIB       "${_WIN_SDK_LIB_PATH}/user32.lib")
set(GDI32_LIB        "${_WIN_SDK_LIB_PATH}/gdi32.lib")
set(WINSPOOL_LIB     "${_WIN_SDK_LIB_PATH}/winspool.lib")
set(SHELL32_LIB      "${_WIN_SDK_LIB_PATH}/shell32.lib")
set(OLEAUT32_LIB     "${_WIN_SDK_LIB_PATH}/oleaut32.lib")
set(UUID_LIB         "${_WIN_SDK_LIB_PATH}/uuid.lib")
set(COMDLG32_LIB     "${_WIN_SDK_LIB_PATH}/comdlg32.lib")
set(ADVAPI32_LIB     "${_WIN_SDK_LIB_PATH}/advapi32.lib")
set(MFPLAT_LIB       "${_WIN_SDK_LIB_PATH}/Mfplat.lib")
set(MF_LIB           "${_WIN_SDK_LIB_PATH}/Mf.lib")
set(MFREADWRITE_LIB  "${_WIN_SDK_LIB_PATH}/mfreadwrite.lib")
set(MFUUID_LIB       "${_WIN_SDK_LIB_PATH}/mfuuid.lib")
set(STRMIIDS_LIB     "${_WIN_SDK_LIB_PATH}/strmiids.lib")
set(WINMM_LIB        "${_WIN_SDK_LIB_PATH}/winmm.lib")

message(STATUS "WindowsCodecs: ${WINDOWSCODECS_LIB}")
message(STATUS "Ole32: ${OLE32_LIB}")
message(STATUS "D3D11: ${D3D11_LIB}")
message(STATUS "DXGI: ${DXGI_LIB}")
message(STATUS "Version: ${VERSION_LIB}")
message(STATUS "Kernel32: ${KERNEL32_LIB}")

# Pass SDK lib paths as /LIBPATH: so bare implicit system libs resolve
# (CMake adds kernel32.lib etc. automatically; they must be findable).
target_link_directories(${PLUGIN_TARGET} PRIVATE
  "${_WIN_SDK_LIB_PATH}"
  "${_WIN_SDK_UCRT_PATH}"
)

# Link libs (imported targets also propagate their own include dirs)
target_link_libraries(${PLUGIN_TARGET} PRIVATE
  CommonLibSSE::CommonLibSSE
  Microsoft::DirectXTex
  Microsoft::DirectXTK
  ${WINDOWSCODECS_LIB}
  ${OLE32_LIB}
  ${D3D11_LIB}
  ${DXGI_LIB}
  ${PROPSYS_LIB}
  ${KERNEL32_LIB}
  ${USER32_LIB}
  ${GDI32_LIB}
  ${WINSPOOL_LIB}
  ${SHELL32_LIB}
  ${OLEAUT32_LIB}
  ${UUID_LIB}
  ${COMDLG32_LIB}
  ${ADVAPI32_LIB}
  ${MFPLAT_LIB}
  ${MF_LIB}
  ${MFREADWRITE_LIB}
  ${MFUUID_LIB}
  ${STRMIIDS_LIB}
  $<$<BOOL:${PNG_FOUND}>:PNG::PNG>
  $<$<TARGET_EXISTS:lodepng>:lodepng>
  ${VERSION_LIB}
  ${WINMM_LIB}
)
target_compile_options(Printscreen PRIVATE
    /UWINVER
    /U_WIN32_WINNT
)

target_compile_definitions(Printscreen PRIVATE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)
# --- vcpkg include fallback ---
# Some dependency packages (e.g. CommonLibSSE) inject /external:I... compiler
# flags that restrict the include path to only vcpkg directories.  This breaks
# standard library access for code that relies on the system MSVC include path.
# Re-add the essential system include paths via target_include_directories so
# they are available unconditionally (outside /external: gating).
set(_SYS_INCLUDE_DIRS
  "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/include"
  "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt"
  "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/shared"
  "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um"
  "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/winrt"
)
foreach(_DIR IN LISTS _SYS_INCLUDE_DIRS)
  if(EXISTS "${_DIR}")
    target_include_directories(${PLUGIN_TARGET} PRIVATE "${_DIR}")
  endif()
endforeach()

# Ensure DLL has no "lib" prefix with non-MSVC generators
set_target_properties(${PLUGIN_TARGET} PROPERTIES
  OUTPUT_NAME "${PLUGIN_TARGET}"
  PREFIX ""
)

# Place runtime outputs into build/bin for easy discovery
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set_target_properties(${PLUGIN_TARGET} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG          "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL     "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)

# -------------------------
# Post-build: copy to MO2
# -------------------------
if(MO2_MOD_DIR AND MO2_SKSE_PLUGIN_DIR)
  add_custom_command(TARGET ${PLUGIN_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MO2_SKSE_PLUGIN_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${PLUGIN_TARGET}>"
            "${MO2_SKSE_PLUGIN_DIR}/$<TARGET_FILE_NAME:${PLUGIN_TARGET}>"
  )

  if(PLUGIN_TOML AND EXISTS "${PROJ_ROOT}/${PLUGIN_TOML}")
    add_custom_command(TARGET ${PLUGIN_TARGET} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${PROJ_ROOT}/${PLUGIN_TOML}"
              "${MO2_SKSE_PLUGIN_DIR}/${PLUGIN_TOML}"
    )
  endif()

  if(PLUGIN_DB_FILES)
    separate_arguments(_DB_LIST NATIVE_COMMAND "${PLUGIN_DB_FILES}")
    foreach(_DB IN LISTS _DB_LIST)
      if(EXISTS "${PROJ_ROOT}/${_DB}")
        add_custom_command(TARGET ${PLUGIN_TARGET} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  "${PROJ_ROOT}/${_DB}"
                  "${MO2_SKSE_PLUGIN_DIR}/${_DB}"
        )
      endif()
    endforeach()
  else()
    file(GLOB _DB_GLOB "${PROJ_ROOT}/${PLUGIN_DB_GLOB}")
    foreach(_DBPATH IN LISTS _DB_GLOB)
      get_filename_component(_DBNAME "${_DBPATH}" NAME)
      add_custom_command(TARGET ${PLUGIN_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_DBPATH}"
                "${MO2_SKSE_PLUGIN_DIR}/${_DBNAME}"
      )
    endforeach()
  endif()
endif()