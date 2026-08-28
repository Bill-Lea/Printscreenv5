#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Config {

// Match the modes your code expects
enum class DDSMode { UNCOMPRESSED, BC1, BC2, BC3, BC4, BC5, BC6H, BC7 };

struct Settings {
    std::wstring iniPath{};         // recorded path to the INI
    // --- Convenience accessors used by existing code ---
    int  GetLogLevel() const { return logLevel; }
    bool IsConsoleOutputEnabled() const { return consoleOutput; }
    bool IsFileOutputEnabled() const { return fileOutput; }
    bool ShowTimestamps() const { return showTimestamps; }
	
    // Logging
    int      logLevel = 3;          // 0..5
    bool     consoleOutput = true;
    bool     fileOutput = true;
    bool     showTimestamps = true;
    uint32_t maxLogFileSizeMB = 8;

    // Performance
    bool     parallelCompression = true;
    uint32_t compressionThreads  = 0;   // 0 = auto

    // Capture
    bool     logCaptureProgress = false;
    bool     logTimingInfo      = false;

    // DDS
    DDSMode  ddsMode = DDSMode::BC7;
};

// Global settings block (defined in config.cpp)
extern Settings g_settings;

// Load settings; returns true if an INI file was found/parsed
bool Initialize();

// Get list of unknown keys found in the INI file (for warning/logging)
const std::vector<std::string>& GetUnknownKeys();

inline const Settings& Get() noexcept { return g_settings; }
// Convenience accessor

inline const char* LogLevelToString(int n) {
    switch (n) {
        case 0: return "NONE";
        case 1: return "ERROR";
        case 2: return "WARN";
        case 3: return "INFO";
        case 4: return "DEBUG";
        case 5: return "TRACE";
        default: return "INFO";
    }
}
} // namespace Config
