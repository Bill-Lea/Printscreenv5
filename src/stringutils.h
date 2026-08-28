#pragma once
#include <string>
#include <windows.h>

namespace util {

    // UTF-8 (narrow) → UTF-16 (wide)
    inline std::wstring utf8_to_wstring(const std::string& str) {
        if (str.empty()) {
            return {};
        }
        int size_needed = MultiByteToWideChar(CP_UTF8, 0,
                                              str.c_str(), (int)str.size(),
                                              nullptr, 0);
        if (size_needed <= 0) {
            return {};
        }
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0,
                            str.c_str(), (int)str.size(),
                            &wstr[0], size_needed);
        return wstr;
    }

    // UTF-16 (wide) → UTF-8 (narrow)
    inline std::string wstring_to_utf8(const std::wstring& wstr) {
        if (wstr.empty()) {
            return {};
        }
        int size_needed = WideCharToMultiByte(CP_UTF8, 0,
                                              wstr.c_str(), (int)wstr.size(),
                                              nullptr, 0, nullptr, nullptr);
        if (size_needed <= 0) {
            return {};
        }
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0,
                            wstr.c_str(), (int)wstr.size(),
                            &str[0], size_needed,
                            nullptr, nullptr);
        return str;
    }
}
