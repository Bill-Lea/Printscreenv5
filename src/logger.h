#pragma once
#include <spdlog/spdlog.h>
#include <fmt/format.h>
#include "logger_shim.hpp"   // gives logger::u8(...) wrappers

namespace logger {

// Call once during startup after Config::Initialize().
void SetupLog();

// Optional: runtime change of global spdlog level
void SetLevel(spdlog::level::level_enum level);

// fmt-style wrappers (single source of truth for logger::*)
template <class... Args>
inline void trace(fmt::format_string<Args...> fmt, Args&&... args) {
    spdlog::trace(fmt, std::forward<Args>(args)...);
}
template <class... Args>
inline void debug(fmt::format_string<Args...> fmt, Args&&... args) {
    spdlog::debug(fmt, std::forward<Args>(args)...);
}
template <class... Args>
inline void info(fmt::format_string<Args...> fmt, Args&&... args) {
    spdlog::info(fmt, std::forward<Args>(args)...);
}
template <class... Args>
inline void warn(fmt::format_string<Args...> fmt, Args&&... args) {
    spdlog::warn(fmt, std::forward<Args>(args)...);
}
template <class... Args>
inline void error(fmt::format_string<Args...> fmt, Args&&... args) {
    spdlog::error(fmt, std::forward<Args>(args)...);
}
template <class... Args>
inline void critical(fmt::format_string<Args...> fmt, Args&&... args) {
    spdlog::critical(fmt, std::forward<Args>(args)...);
}

} // namespace logger
