#pragma once

// CommonLibSSE-NG (must be first)
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <REL/Relocation.h>

// Windows headers
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0A00
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <wrl/client.h>
#include <combaseapi.h>
#include <propvarutil.h>
#include <propsys.h>

// DirectX headers
#include <d3d11.h>
#include <d3d10_1.h>   // ID3D10Multithread for multithread protection
#include <dxgi1_2.h>
#include <DirectXTex.h>

// WIC headers
#include <wincodec.h>

// Media Foundation headers
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

// Standard library headers
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <cctype>
#include <cmath>
#include <exception>
#include <mutex>
#include <unordered_map>  // For Config system
#include <ctime>          // For timestamp functions

// CRITICAL: Include Config.h in PCH so it's available everywhere
#include "Config.h"

// Logger namespace (logger::info, logger::warn, logger::error, etc.)
#include "logger.h"

// COM smart pointer namespace
using Microsoft::WRL::ComPtr;

// Define this to indicate PCH includes DirectX
#define PCH_INCLUDES_DIRECTX

using namespace std::literals;
