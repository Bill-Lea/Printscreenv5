// logger_shim.hpp  (FIXED)
#pragma once
#include <string>
#include <string_view>
#include <filesystem>
#include <fmt/format.h>
#include "stringutils.h" // util::wstring_to_utf8 / utf8_to_wstring

namespace logger {
// Light wrappers to allow passing wide/path directly to fmt/spdlog as UTF-8
struct as_utf8 { std::wstring_view w; };
inline as_utf8 u8(std::wstring_view w) { return {w}; }

struct path_utf8 { const std::filesystem::path& p; };
inline path_utf8 u8(const std::filesystem::path& p) { return {p}; }
} // namespace logger

// Formatter specializations must be in namespace fmt
namespace fmt {

template<>
struct formatter<logger::as_utf8> : formatter<std::string_view> {
    template <typename FormatContext>
    auto format(const logger::as_utf8& v, FormatContext& ctx) {
        const std::string tmp = util::wstring_to_utf8(std::wstring{v.w});
        return formatter<std::string_view>::format(tmp, ctx);
    }
};

template<>
struct formatter<logger::path_utf8> : formatter<std::string_view> {
    template <typename FormatContext>
    auto format(const logger::path_utf8& v, FormatContext& ctx) {
        const std::string tmp = util::wstring_to_utf8(v.p.native());
        return formatter<std::string_view>::format(tmp, ctx);
    }
};

} // namespace fmt
