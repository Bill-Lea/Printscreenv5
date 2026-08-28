#pragma once
#include <mutex>
#include <queue>
#include <string>

// ============================================================
// ConsoleCommandQueue
// Thread-safe queue for console commands that must run on the
// Skyrim game thread.  Worker threads enqueue; the game thread
// drains via SKSE::GetTaskInterface()->AddTask.
// ============================================================
class ConsoleCommandQueue {
public:
    static ConsoleCommandQueue& GetSingleton();

    // Safe to call from ANY thread (worker thread, menu sinks, etc.).
    void Enqueue(std::string command);

    // Execute all queued commands on the calling thread.
    // Must ONLY be called from the game thread.
    void Drain();

    // Enqueue + schedule Drain() via SKSE task interface.
    // Safe to call from ANY thread; the actual CompileAndRun
    // happens asynchronously on the game thread.
    void EnqueueAndDrainAsync(std::string command);

private:
    ConsoleCommandQueue() = default;
    ConsoleCommandQueue(const ConsoleCommandQueue&)            = delete;
    ConsoleCommandQueue& operator=(const ConsoleCommandQueue&) = delete;

    std::mutex              mutex_;
    std::queue<std::string> queue_;
};
