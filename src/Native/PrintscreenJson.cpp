#include "PCH.h"
#include "PrintscreenJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    using json = nlohmann::json;

    constexpr auto kScriptName = "Printscreen_JSON_script";

    struct CachedJson
    {
        json data = json::object();
        // File name exactly as first requested. The cache is keyed case-
        // insensitively, but the file is created/opened with this spelling.
        std::string diskName;
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

    std::string CacheKey(const std::string& fileName)
    {
        // Papyrus is case-insensitive, so "PrintScreen" and "printscreen"
        // name the same file. Fold the cache key to match, otherwise the two
        // spellings get separate cache entries over one file on disk and the
        // writes made through one of them are silently lost on save.
        std::string key = fileName;
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    }

    std::string NormalizeKey(std::string key)
    {
        // Every key is folded to lower case, which is also what PapyrusUtil's
        // JsonUtil did.
        //
        // Papyrus itself is case-insensitive, and the compiler emits a single
        // string table in which entries are deduplicated without regard to
        // case. A literal such as "LoopCount" therefore reaches this function
        // spelled the way the string was FIRST seen in the script - normally
        // the property declaration. Re-casing a property (Loopcount ->
        // LoopCount) silently changes the key the game writes, which used to
        // orphan the stored value and leave a duplicate behind in the file.
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    }

    // PapyrusUtil's JsonUtil did not store values on the root object; it
    // grouped them into per-type containers:
    //
    //     { "string": { "path": ... }, "int": { ... }, "float": { ... } }
    //
    // This implementation stores them flat on the root instead, so a file
    // carried over from PapyrusUtil parses fine but every lookup misses and
    // the stale containers were written back out on every save. Hoist those
    // values onto the root once and drop the containers.
    //
    // Returns true when the document was altered.
    bool MigrateLegacyData(json& data)
    {
        if (!data.is_object()) {
            return false;
        }

        bool changed = false;

        // Collapse root keys that differ only by case. The already-lowercase
        // spelling is canonical and wins; a differently-cased duplicate is a
        // leftover from an older build and is discarded.
        std::vector<std::string> rootKeys;
        rootKeys.reserve(data.size());
        for (auto it = data.begin(); it != data.end(); ++it) {
            rootKeys.push_back(it.key());
        }

        for (const auto& rootKey : rootKeys) {
            const auto normalized = NormalizeKey(rootKey);
            if (normalized == rootKey || normalized.empty()) {
                continue;
            }

            if (data.find(normalized) == data.end()) {
                data[normalized] = data[rootKey];
            } else {
                logger::warn("PrintscreenJson: discarding duplicate key \"{}\"; keeping \"{}\"",
                             rootKey, normalized);
            }

            data.erase(rootKey);
            changed = true;
        }

        // Hoist the PapyrusUtil scalar containers. A value already present on
        // the root is newer and is left alone.
        static constexpr const char* kLegacyBuckets[] = { "string", "int", "float" };

        for (const char* bucketName : kLegacyBuckets) {
            const auto bucket = data.find(bucketName);
            if (bucket == data.end()) {
                continue;
            }

            if (bucket->is_object()) {
                const json contents = *bucket;
                for (auto it = contents.begin(); it != contents.end(); ++it) {
                    const auto key = NormalizeKey(it.key());
                    if (key.empty() || data.find(key) != data.end()) {
                        continue;
                    }
                    data[key] = it.value();
                }
            }

            data.erase(bucketName);
            changed = true;
        }

        return changed;
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

    CachedJson& LoadEntry(const std::string& fileName, bool forceReload = false)
    {
        auto& entry = g_cache[CacheKey(fileName)];

        if (entry.diskName.empty()) {
            entry.diskName = fileName;
        }

        if (entry.loaded && !forceReload) {
            return entry;
        }

        auto diskName = std::move(entry.diskName);
        entry = CachedJson{};
        entry.diskName = std::move(diskName);
        entry.loaded = true;

        const auto path = ResolvePath(entry.diskName);
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

            if (MigrateLegacyData(entry.data)) {
                // Persisted by the next Save(); reads are already correct.
                entry.dirty = true;
                logger::info("PrintscreenJson: migrated legacy layout in {}", path.string());
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

    CachedJson& MutableEntry(const std::string& fileName)
    {
        auto& entry = LoadEntry(fileName);

        // A missing/corrupt file must still be writable so WriteJson()
        // can recreate it with default/current values.
        if (!entry.good) {
            entry.data = json::object();
        }

        return entry;
    }

    bool SaveEntry(const std::string& fileName)
    {
        auto& entry = MutableEntry(fileName);
        const auto path = ResolvePath(entry.diskName);

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
        key = NormalizeKey(std::move(key));
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
        key = NormalizeKey(std::move(key));
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
        key = NormalizeKey(std::move(key));
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
        key = NormalizeKey(std::move(key));
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
        key = NormalizeKey(std::move(key));
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
        key = NormalizeKey(std::move(key));
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
        key = NormalizeKey(std::move(key));
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
        key = NormalizeKey(std::move(key));
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
        key = NormalizeKey(std::move(key));
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
