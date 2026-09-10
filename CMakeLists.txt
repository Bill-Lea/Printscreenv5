cmake_minimum_required(VERSION 3.24)
project(Printscreen VERSION 5.0.2 LANGUAGES CXX)

# -----------------------------------------------------------------------------
# CommonLibSSE-NG 6.4.0
# Skyrim SE + AE ("flatrim") build: no VR, no CommonLib tests.
# Pinning the tag here avoids the older Color-Glass vcpkg port, which currently
# stops at CommonLibSSE-NG 3.7.0.
# -----------------------------------------------------------------------------
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Enable Skyrim SE support" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Enable Skyrim AE support" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Disable Skyrim VR support" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE unit tests" FORCE)
set(REX_OPTION_INI OFF CACHE BOOL "Disable CommonLib REX INI helpers" FORCE)
set(REX_OPTION_JSON OFF CACHE BOOL "Disable CommonLib REX JSON helpers" FORCE)
set(REX_OPTION_TOML OFF CACHE BOOL "Disable CommonLib REX TOML helpers" FORCE)
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "Disable optional CommonLib Xbyak trampoline support" FORCE)

include(FetchContent)
FetchContent_Declare(
  CommonLibSSE
  GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
  GIT_TAG v6.4.0
  GIT_SHALLOW TRUE
  GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(CommonLibSSE)

add_subdirectory(src)

# Ensure the src folder is on the include path (if not already)
target_include_directories(${PLUGIN_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# Use the local PCH wrapper if present
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h")
  target_precompile_headers(${PLUGIN_TARGET} PRIVATE "src/pch.h")
endif()
