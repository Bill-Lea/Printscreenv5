#include "pch.h"
#include "Config.h"
#include <Windows.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <set>
#include <cwctype>
#include "stringutils.h"  // util::wstring_to_utf8 / utf8_to_wstring

namespace {
    std::wstring skse_folder() {
        wchar_t* up = nullptr; size_t len = 0;
        _wdupenv_s(&up, &len, L"USERPROFILE");
        std::wstring home = up ? up : L"";
        free(up);
        return (std::filesystem::path(home) /
                L"Documents" / L"My Games" / L"Skyrim Special Edition" / L"SKSE").wstring();
    }
    std::wstring ini_candidate_legacy() { // V2 location/name
        return (std::filesystem::path(skse_folder()) / L"Plugins" / L"Printscreen_Log.ini").wstring();
    }
    std::wstring ini_candidate_simple() {
        return (std::filesystem::path(skse_folder()) / L"PrintScreen.ini").wstring();
    }
    bool exists(const std::wstring& p) {
        return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    std::wstring wget(const wchar_t* sec, const wchar_t* key,
                      const std::wstring& def, const std::wstring& ini) {
        wchar_t buf[1024]{};
        DWORD n = GetPrivateProfileStringW(sec, key, def.c_str(), buf, 1024, ini.c_str());
        return std::wstring(buf, n);
    }
    uint32_t wget_u32(const wchar_t* s, const wchar_t* k, uint32_t d, const std::wstring& ini) {
        wchar_t def[32]; _snwprintf_s(def, _TRUNCATE, L"%u", d);
        std::wstring v = wget(s, k, def, ini);
        return v.empty() ? d : static_cast<uint32_t>(std::wcstoul(v.c_str(), nullptr, 10));
    }
    bool wget_bool(const wchar_t* s, const wchar_t* k, bool d, const std::wstring& ini) {
        std::wstring v = wget(s, k, d ? L"1" : L"0", ini);
        std::string l = util::wstring_to_utf8(v);
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        return (l == "1" || l == "true" || l == "yes" || l == "on");
    }

    // Trim whitespace from wide string
    std::wstring trim_ws(const std::wstring& s) {
        size_t start = 0, end = s.size();
        while (start < end && std::iswspace(s[start])) ++start;
        while (end > start && std::iswspace(s[end - 1])) --end;
        return s.substr(start, end - start);
    }

    // Convert wide string to lowercase
    std::wstring to_lower_ws(std::wstring s) {
        for (auto& ch : s) ch = std::towlower(ch);
        return s;
    }

    // Set of all valid keys (lowercase) - both new sectioned format and legacy flat format
    const std::set<std::wstring> g_validKeys = {
        // Sectioned format keys (under [Logging], [Performance], [Capture])
        L"loglevel", L"consoleoutput", L"fileoutput", L"showtimestamps", L"maxlogfilesizemb",
        L"parallelcompression", L"compressionthreads",
        L"logcaptureprogress", L"logtiminginfo",
        // Legacy flat format keys (for backward compatibility)
        L"level", L"file", L"console", L"timestamps", L"logpath"
    };

    // Validate INI file and collect unknown keys
    std::vector<std::string> validateIniKeys(const std::wstring& iniPath) {
        std::vector<std::string> unknownKeys;

        std::wifstream fin(iniPath);
        if (!fin.is_open()) {
            return unknownKeys;
        }

        std::wstring line;
        while (std::getline(fin, line)) {
            // Strip comments
            auto commentPos = line.find(L';');
            if (commentPos != std::wstring::npos) {
                line = line.substr(0, commentPos);
            }
            commentPos = line.find(L'#');
            if (commentPos != std::wstring::npos) {
                line = line.substr(0, commentPos);
            }

            line = trim_ws(line);
            if (line.empty()) continue;

            // Skip section headers
            if (line.front() == L'[' && line.back() == L']') continue;

            // Parse key=value
            auto eqPos = line.find(L'=');
            if (eqPos == std::wstring::npos) continue;

            std::wstring key = trim_ws(line.substr(0, eqPos));
            std::wstring keyLower = to_lower_ws(key);

            if (g_validKeys.find(keyLower) == g_validKeys.end()) {
                unknownKeys.push_back(util::wstring_to_utf8(key));
            }
        }

        return unknownKeys;
    }
}

namespace Config {

Settings g_settings{}; // NOTE: not static; matches 'extern' in header
std::vector<std::string> g_unknownKeys{}; // Track unknown keys for later logging

static int to_level(std::wstring v) {
    std::string s = util::wstring_to_utf8(v);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "none")  return 0;
    if (s == "error") return 1;
    if (s == "warn")  return 2;
    if (s == "info")  return 3;
    if (s == "debug") return 4;
    if (s == "trace") return 5;
    try { return std::clamp(std::stoi(s), 0, 5); } catch (...) { return 3; }
}

// Parse legacy flat format (Level, File, Console, etc.).
//
// NOTE: this cannot use GetPrivateProfileStringW — passing a NULL section
// name to that API does not read "sectionless" keys, it returns the list of
// SECTION NAMES in the file (see Win32 docs), so the previous implementation
// never read a single legacy key. The Win32 profile API simply has no way to
// read keys that sit outside any [section], so parse the file manually.
// Matches the first occurrence of the key (case-insensitive) regardless of
// which section (if any) it appears under, which mirrors how the legacy V2
// flat file was laid out.
static std::wstring wget_legacy(const wchar_t* key, const std::wstring& ini) {
    std::wifstream fin(ini);
    if (!fin.is_open()) return L"";

    const std::wstring wanted = to_lower_ws(key);

    std::wstring line;
    while (std::getline(fin, line)) {
        // Strip comments
        auto cpos = line.find(L';');
        if (cpos != std::wstring::npos) line = line.substr(0, cpos);
        cpos = line.find(L'#');
        if (cpos != std::wstring::npos) line = line.substr(0, cpos);

        line = trim_ws(line);
        if (line.empty()) continue;
        if (line.front() == L'[') continue;  // section header

        auto eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;

        if (to_lower_ws(trim_ws(line.substr(0, eq))) == wanted) {
            return trim_ws(line.substr(eq + 1));
        }
    }
    return L"";  // key not found
}

bool Initialize()
{
    const std::wstring legacy = ini_candidate_legacy();
    const std::wstring simple = ini_candidate_simple();
    const std::wstring path   = exists(legacy) ? legacy : (exists(simple) ? simple : legacy);
    g_settings.iniPath = path;

    if (!exists(path)) {
        return false; // No INI file found, use defaults
    }

    // Validate INI file and collect unknown keys
    g_unknownKeys = validateIniKeys(path);

    // Try new sectioned format first
    std::wstring logLevelVal = wget(L"Logging", L"LogLevel", L"", path);

    // Check if we have sectioned format or legacy flat format
    bool useLegacyFormat = logLevelVal.empty();

    if (useLegacyFormat) {
        // Try legacy flat format (Level, File, Console, Timestamps, LogPath)
        std::wstring levelVal = wget_legacy(L"Level", path);
        if (!levelVal.empty()) {
            g_settings.logLevel = to_level(levelVal);
        }

        std::wstring fileVal = wget_legacy(L"File", path);
        if (!fileVal.empty()) {
            std::string l = util::wstring_to_utf8(fileVal);
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            g_settings.fileOutput = (l == "1" || l == "true" || l == "yes" || l == "on");
        }

        std::wstring consoleVal = wget_legacy(L"Console", path);
        if (!consoleVal.empty()) {
            std::string l = util::wstring_to_utf8(consoleVal);
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            g_settings.consoleOutput = (l == "1" || l == "true" || l == "yes" || l == "on");
        }

        std::wstring timestampsVal = wget_legacy(L"Timestamps", path);
        if (!timestampsVal.empty()) {
            std::string l = util::wstring_to_utf8(timestampsVal);
            std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            g_settings.showTimestamps = (l == "1" || l == "true" || l == "yes" || l == "on");
        }
    } else {
        // Use new sectioned format
        // Logging section
        g_settings.logLevel         = to_level(logLevelVal);
        g_settings.consoleOutput    = wget_bool(L"Logging", L"ConsoleOutput",    g_settings.consoleOutput,    path);
        g_settings.fileOutput       = wget_bool(L"Logging", L"FileOutput",       g_settings.fileOutput,       path);
        g_settings.showTimestamps   = wget_bool(L"Logging", L"ShowTimestamps",   g_settings.showTimestamps,   path);
        g_settings.maxLogFileSizeMB = wget_u32 (L"Logging", L"MaxLogFileSizeMB", g_settings.maxLogFileSizeMB, path);

        // Performance section
        g_settings.parallelCompression = wget_bool(L"Performance", L"ParallelCompression", g_settings.parallelCompression, path);
        g_settings.compressionThreads  = wget_u32 (L"Performance", L"CompressionThreads",  g_settings.compressionThreads,  path);

        // Capture section
        g_settings.logCaptureProgress  = wget_bool(L"Capture", L"LogCaptureProgress", g_settings.logCaptureProgress, path);
        g_settings.logTimingInfo       = wget_bool(L"Capture", L"LogTimingInfo",      g_settings.logTimingInfo,      path);
    }

    return true;
}

const std::vector<std::string>& GetUnknownKeys() {
    return g_unknownKeys;
}

} // namespace Config
