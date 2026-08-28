#include "logger.h"
#include "Config.h"
#include <vector>
#include <filesystem>
#include <system_error>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/msvc_sink.h> // for msvc_sink_mt on MSVC

namespace {
inline spdlog::level::level_enum to_spd_level(int n) {
    // 0..5 (NONE..TRACE) as used by your INI
    switch (n) {
        case 0: return spdlog::level::off;
        case 1: return spdlog::level::err;
        case 2: return spdlog::level::warn;
        case 3: return spdlog::level::info;
        case 4: return spdlog::level::debug;
        case 5: return spdlog::level::trace;
        default: return spdlog::level::info;
    }
}
}

namespace logger {

void SetupLog()
{
    const auto& s = Config::Get();

    std::vector<spdlog::sink_ptr> sinks;
    sinks.reserve(2);

    if (s.IsFileOutputEnabled()) {
        // Use absolute path to avoid MO2 virtual filesystem issues.
        // SKSE plugins should log to Documents\My Games\Skyrim Special Edition\SKSE\Logs
        wchar_t* userProfile = nullptr;
        size_t len = 0;
        _wdupenv_s(&userProfile, &len, L"USERPROFILE");
        std::filesystem::path logPath;
        if (userProfile) {
            logPath = std::filesystem::path(userProfile) / L"Documents" / L"My Games"
                     / L"Skyrim Special Edition" / L"SKSE" / L"Logs" / L"Printscreen.log";
            free(userProfile);
        } else {
            // Fallback to relative path if USERPROFILE is unavailable
            logPath = std::filesystem::path("Data") / "SKSE" / "Plugins" / "Printscreen.log";
        }
        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);

#if SPDLOG_WCHAR_FILENAMES
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logPath.wstring(), static_cast<size_t>(s.maxLogFileSizeMB) * 1024ull * 1024ull, 3);
#else
        auto fileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logPath.string(), static_cast<size_t>(s.maxLogFileSizeMB) * 1024ull * 1024ull, 3);
#endif
        sinks.emplace_back(std::move(fileSink));
    }

    if (s.IsConsoleOutputEnabled()) {
    #if defined(_MSC_VER)
        sinks.emplace_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
    #else
        sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    #endif
    }

    if (sinks.empty()) {
        sinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    auto lg = std::make_shared<spdlog::logger>("printscreen", sinks.begin(), sinks.end());
    lg->set_level(to_spd_level(s.GetLogLevel()));

    spdlog::set_default_logger(std::move(lg));

    // Set pattern AFTER setting default logger to ensure it's applied correctly
    if (s.ShowTimestamps()) {
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    } else {
        spdlog::set_pattern("[%^%l%$] %v");
    }

    spdlog::flush_on(spdlog::level::err);
}

void SetLevel(spdlog::level::level_enum level)
{
    spdlog::set_level(level);
}

} // namespace logger
