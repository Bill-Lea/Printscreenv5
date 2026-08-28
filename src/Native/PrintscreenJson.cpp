#include "PCH.h"
#include "PrintscreenJson.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
    using json = nlohmann::json;

    constexpr auto kScriptName = "Printscreen_JSON_script";

    struct CachedJson
    {
        json data = json::object();
        bool loaded = false;
        bool good = false;
        bool dirty = false;
        std::string error;
    };

    std::mutex g_jsonMutex;
    std::unordered_map<std::string, CachedJson> g_cache;

    std::string NormalizeFileName(std::string fileName)
    {
        if (fileName.empty()) {
            return {};
        }

        std::filesystem::path p(fileName);

        // Printscreen only needs normal relative config filenames.
        // Reject absolute paths and parent traversal.
        if (p.is_absolute()) {
            return {};
        }
        for (const auto& part : p) {
            if (part == "..") {
                return {};
            }
        }

        if (!p.has_extension()) {
            p += ".json";
        }

        return p.generic_string();
    }

    std::filesystem::path ResolvePath(const std::string& fileName)
    {
        // Deliberately uses PapyrusUtil's former JsonUtil location so an
        // existing PrintScreen.json continues to work after PapyrusUtil
        // is removed:
        //
        // Data/SKSE/Plugins/StorageUtilData/<filename>.json
        return std::filesystem::path("Data") /
               "SKSE" /
               "Plugins" /
               "StorageUtilData" /
               std::filesystem::path(fileName);
    }

    CachedJson& LoadEntry(const std::string& normalizedName, bool forceReload = false)
    {
        auto& entry = g_cache[normalizedName];

        if (entry.loaded && !forceReload) {
            return entry;
        }

        entry = CachedJson{};
        entry.loaded = true;

        const auto path = ResolvePath(normalizedName);
        std::error_code ec;

        if (!std::filesystem::exists(path, ec) || ec) {
            entry.good = false;
            entry.error = "File does not exist";
            return entry;
        }

        try {
            std::ifstream in(path, std::ios::binary);
            if (!in.is_open()) {
                entry.good = false;
                entry.error = "Unable to open JSON file";
                return entry;
            }

            in >> entry.data;

            if (!entry.data.is_object()) {
                entry.good = false;
                entry.error = "JSON root must be an object";
                entry.data = json::object();
                return entry;
            }

            entry.good = true;
            entry.error.clear();
        }
        catch (const std::exception& e) {
            entry.good = false;
            entry.error = e.what();
            entry.data = json::object();
        }

        return entry;
    }

    CachedJson& MutableEntry(const std::string& normalizedName)
    {
        auto& entry = LoadEntry(normalizedName);

        // A missing/corrupt file must still be writable so WriteJson()
        // can recreate it with default/current values.
        if (!entry.good) {
            entry.data = json::object();
        }

        return entry;
    }

    bool SaveEntry(const std::string& normalizedName)
    {
        auto& entry = MutableEntry(normalizedName);
        const auto path = ResolvePath(normalizedName);

        try {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                entry.error = "Unable to create JSON directory: " + ec.message();
                return false;
            }

            const auto tempPath = path.string() + ".tmp";

            {
                std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) {
                    entry.error = "Unable to open temporary JSON file for writing";
                    return false;
                }

                out << entry.data.dump(4) << '\n';
                out.flush();

                if (!out.good()) {
                    entry.error = "Failed while writing JSON file";
                    return false;
                }
            }

            // Replace the destination only after a complete temporary file
            // has been written.
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(tempPath, path, ec);
            if (ec) {
                entry.error = "Unable to replace JSON file: " + ec.message();
                std::filesystem::remove(tempPath, ec);
                return false;
            }

            entry.good = true;
            entry.dirty = false;
            entry.error.clear();
            return true;
        }
        catch (const std::exception& e) {
            entry.error = e.what();
            return false;
        }
    }

    bool JsonExists(RE::StaticFunctionTag*, std::string fileName)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty()) {
            return false;
        }

        std::error_code ec;
        return std::filesystem::is_regular_file(ResolvePath(name), ec) && !ec;
    }

    bool IsGood(RE::StaticFunctionTag*, std::string fileName)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty()) {
            return false;
        }

        // Force a disk read here so IsGood really verifies the current file.
        return LoadEntry(name, true).good;
    }

    std::string GetErrors(RE::StaticFunctionTag*, std::string fileName)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty()) {
            return "Invalid JSON filename";
        }

        return LoadEntry(name).error;
    }

    bool Load(RE::StaticFunctionTag*, std::string fileName)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty()) {
            return false;
        }

        return LoadEntry(name, true).good;
    }

    bool Save(RE::StaticFunctionTag*, std::string fileName)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty()) {
            return false;
        }

        return SaveEntry(name);
    }

    int SetIntValue(RE::StaticFunctionTag*, std::string fileName, std::string key, int value)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty() || key.empty()) {
            return value;
        }

        auto& entry = MutableEntry(name);
        entry.data[key] = value;
        entry.dirty = true;
        return value;
    }

    float SetFloatValue(RE::StaticFunctionTag*, std::string fileName, std::string key, float value)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty() || key.empty()) {
            return value;
        }

        auto& entry = MutableEntry(name);
        entry.data[key] = value;
        entry.dirty = true;
        return value;
    }

    std::string SetStringValue(RE::StaticFunctionTag*, std::string fileName, std::string key, std::string value)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty() || key.empty()) {
            return value;
        }

        auto& entry = MutableEntry(name);
        entry.data[key] = value;
        entry.dirty = true;
        return value;
    }

    int GetIntValue(RE::StaticFunctionTag*, std::string fileName, std::string key, int missing)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty() || key.empty()) {
            return missing;
        }

        const auto& entry = LoadEntry(name);
        if (!entry.good) {
            return missing;
        }

        const auto it = entry.data.find(key);
        if (it == entry.data.end() || !it->is_number_integer()) {
            return missing;
        }

        try {
            return it->get<int>();
        } catch (...) {
            return missing;
        }
    }

    float GetFloatValue(RE::StaticFunctionTag*, std::string fileName, std::string key, float missing)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty() || key.empty()) {
            return missing;
        }

        const auto& entry = LoadEntry(name);
        if (!entry.good) {
            return missing;
        }

        const auto it = entry.data.find(key);

        // Accept either JSON integer or floating-point numbers. This is more
        // forgiving when a user hand-edits e.g. 90.0 as 90.
        if (it == entry.data.end() || !it->is_number()) {
            return missing;
        }

        try {
            return it->get<float>();
        } catch (...) {
            return missing;
        }
    }

    std::string GetStringValue(RE::StaticFunctionTag*, std::string fileName, std::string key, std::string missing)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty() || key.empty()) {
            return missing;
        }

        const auto& entry = LoadEntry(name);
        if (!entry.good) {
            return missing;
        }

        const auto it = entry.data.find(key);
        if (it == entry.data.end() || !it->is_string()) {
            return missing;
        }

        try {
            return it->get<std::string>();
        } catch (...) {
            return missing;
        }
    }

    bool HasIntValue(RE::StaticFunctionTag*, std::string fileName, std::string key)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty() || key.empty()) {
            return false;
        }

        const auto& entry = LoadEntry(name);
        if (!entry.good) {
            return false;
        }

        const auto it = entry.data.find(key);
        return it != entry.data.end() && it->is_number_integer();
    }

    bool HasFloatValue(RE::StaticFunctionTag*, std::string fileName, std::string key)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty() || key.empty()) {
            return false;
        }

        const auto& entry = LoadEntry(name);
        if (!entry.good) {
            return false;
        }

        const auto it = entry.data.find(key);
        return it != entry.data.end() && it->is_number();
    }

    bool HasStringValue(RE::StaticFunctionTag*, std::string fileName, std::string key)
    {
        std::scoped_lock lock(g_jsonMutex);

        const auto name = NormalizeFileName(std::move(fileName));
        if (name.empty() || key.empty()) {
            return false;
        }

        const auto& entry = LoadEntry(name);
        if (!entry.good) {
            return false;
        }

        const auto it = entry.data.find(key);
        return it != entry.data.end() && it->is_string();
    }
}

namespace PrintscreenJson
{
    bool Register(RE::BSScript::IVirtualMachine* vm)
    {
        if (!vm) {
            return false;
        }

        bool ok = true;

        auto reg = [&](const char* name, auto fn) {
            try {
                vm->RegisterFunction(name, kScriptName, fn, true);
            }
            catch (...) {
                logger::error("Failed to register {}.{}", kScriptName, name);
                ok = false;
            }
        };

        reg("JsonExists",     JsonExists);
        reg("IsGood",         IsGood);
        reg("GetErrors",      GetErrors);
        reg("Load",           Load);
        reg("Save",           Save);

        reg("SetIntValue",    SetIntValue);
        reg("SetFloatValue",  SetFloatValue);
        reg("SetStringValue", SetStringValue);

        reg("GetIntValue",    GetIntValue);
        reg("GetFloatValue",  GetFloatValue);
        reg("GetStringValue", GetStringValue);

        reg("HasIntValue",    HasIntValue);
        reg("HasFloatValue",  HasFloatValue);
        reg("HasStringValue", HasStringValue);

        logger::info("Printscreen JSON Papyrus functions registered: {}", ok ? "OK" : "WITH ERRORS");
        return ok;
    }
}
