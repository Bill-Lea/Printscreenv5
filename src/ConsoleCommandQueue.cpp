#include "PCH.h"
#include "ConsoleCommandQueue.h"

// ============================================================
// Singleton
// ============================================================
ConsoleCommandQueue& ConsoleCommandQueue::GetSingleton() {
    static ConsoleCommandQueue instance;
    return instance;
}

// ============================================================
// Enqueue (any thread)
// ============================================================
void ConsoleCommandQueue::Enqueue(std::string command) {
    std::lock_guard lock(mutex_);
    queue_.push(std::move(command));
}

// ============================================================
// Drain (game thread ONLY)
// ============================================================
void ConsoleCommandQueue::Drain() {
    // Move local to avoid holding lock during CompileAndRun
    std::queue<std::string> local;
    {
        std::lock_guard lock(mutex_);
        std::swap(local, queue_);
    }

    if (local.empty()) return;

    auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
    if (!factory) {
        logger::error("ConsoleCommandQueue::Drain: no Script factory");
        return;
    }

    while (!local.empty()) {
        const auto& cmd = local.front();

        RE::Script* script = factory->Create();
        if (!script) {
            logger::error("ConsoleCommandQueue::Drain: Script create failed for '{}'", cmd);
            local.pop();
            continue;
        }

        script->SetCommand(cmd.c_str());
        script->CompileAndRun(nullptr, RE::COMPILER_NAME::kSystemWindowCompiler);
        script->ClearCommand();
        script->SetDelete(true);

        logger::info("ConsoleCommandQueue: executed '{}'", cmd);
        local.pop();
    }
}

// ============================================================
// EnqueueAndDrainAsync (any thread -> game thread via AddTask)
// ============================================================
void ConsoleCommandQueue::EnqueueAndDrainAsync(std::string command) {
    Enqueue(std::move(command));

    auto* task = SKSE::GetTaskInterface();
    if (task) {
        task->AddTask([]() {
            ConsoleCommandQueue::GetSingleton().Drain();
        });
    } else {
        logger::error("ConsoleCommandQueue: no task interface; command queued but not drained");
    }
}
