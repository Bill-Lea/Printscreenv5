#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <mutex>
#include <set>

// ============================================================
// TempFileGuard — RAII cleanup for animated-capture temp files
//
// Automatically cleans up the temp directory and all files inside
// when it goes out of scope, unless Release() is called first.
// This ensures temp files are deleted even if encoding crashes.
//
// Also registers temp directories in a global registry so they can
// be cleaned up on game reload even if the RAII guard is orphaned.
// ============================================================
class TempFileGuard {
public:
    TempFileGuard() = default;
    explicit TempFileGuard(std::wstring tempDir);
    ~TempFileGuard() { Cleanup(); }

    TempFileGuard(const TempFileGuard&) = delete;
    TempFileGuard& operator=(const TempFileGuard&) = delete;
    TempFileGuard(TempFileGuard&& other) noexcept;
    TempFileGuard& operator=(TempFileGuard&& other) noexcept;

    // Prevent automatic cleanup on destruction
    void Release() noexcept;

    // Manually trigger cleanup early (safe to call multiple times)
    void Cleanup() noexcept;

    const std::wstring& Dir() const noexcept { return m_dir; }
    bool HasDir() const noexcept { return !m_dir.empty(); }

    // --- Global registry for orphaned temp directories ---
    // Called on game reload to clean up any temps from interrupted captures
    static void CleanupAllRegistered();
    static void RegisterDirectory(const std::wstring& dir);
    static void UnregisterDirectory(const std::wstring& dir);

private:
    std::wstring m_dir;
    bool m_released = false;

    static std::mutex s_registryMutex;
    static std::set<std::wstring> s_registeredDirs;
};

// ============================================================
// StaleTempCleanup
//
// Scans a directory for leftover temp directories from previous
// crashed/interrupted sessions and removes them.
//
// Temp directories are identified by their naming pattern:
//   gif_temp_YYYYMMDD_HHMMSS_mmm
//   apng_temp_YYYYMMDD_HHMMSS_mmm
// ============================================================
class StaleTempCleanup {
public:
    // Scan and remove stale temp directories in the given output directory.
    // Safe to call even if outputDir doesn't exist.
    static void ScanAndRemove(const std::wstring& outputDir);

private:
    static bool IsTempDirectory(const std::filesystem::path& dirName);
};