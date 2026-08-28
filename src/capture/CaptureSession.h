#pragma once
#include "CancellationToken.h"
#include "CaptureRequest.h"
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

// ============================================================
// CaptureSession
//
// Thread-safe orchestrator for one capture operation at a time.
// - Start() launches a worker thread; returns false if already busy.
// - GetResult() polls the worker result (suitable for Papyrus polling).
// - RequestCancel() signals cooperative cancellation.
// - The destructor joins the worker thread — safe for DLL unload.
// ============================================================
class CaptureSession {
public:
    static CaptureSession& GetSingleton();

    // Callback invoked on the worker thread when a capture finishes.
    using CompletionCallback   = std::function<void(const std::string& result)>;
    // Callback invoked on the worker thread after all frames are acquired but
    // before encoding begins. Use this to restore the HUD so the (potentially
    // slow) encode phase does not suppress UI unnecessarily.
    // Not called for H264 video (acquisition and encoding are interleaved there).
    using AcquisitionCallback  = std::function<void()>;

    // Structured start result — distinguishes rejection reasons.
    enum class StartResult : int {
        Accepted       = 0,  // Capture started successfully
        AlreadyRunning = 1,  // A capture is already in progress (Starting or Running)
        InvalidState   = 2,  // Session is in an unexpected state (e.g. Done not yet consumed)
        BusyCancelled  = 3   // Active capture was cancelled on second call; start again to begin anew
    };

    // Attempt to start a new capture.
    // onAcquisitionComplete is called after frames are acquired, before encoding.
    // onComplete is called after encoding finishes (or on error/cancel).
    StartResult Start(CaptureRequest request,
                      CompletionCallback  onComplete            = nullptr,
                      AcquisitionCallback onAcquisitionComplete = nullptr);

    // Signal the current capture to stop.
    void RequestCancel();

    // Force-reset all state (called after game load/save as safety net).
    void ForceReset();

    // Poll result from Papyrus. Returns:
    //   "Ready"             – idle, nothing to report
    //   "Starting"          – queued but not yet running
    //   "Running"           – in progress
    //   "CALLBACK_SUCCESS"  – succeeded (consumed on read)
    //   "CALLBACK_CANCELLED"
    //   "CALLBACK_ERROR: <msg>"
    std::string GetResult();

    bool IsIdle() const;

    // The active cancellation token — handed to MenuEventSink.
    CancellationToken::Ptr GetToken() const;

private:
    CaptureSession() = default;
    ~CaptureSession();
    CaptureSession(const CaptureSession&)            = delete;
    CaptureSession& operator=(const CaptureSession&) = delete;

    void WorkerThread(CaptureRequest req);
    void SetResult(std::string result);

    // Join workerThread_ if joinable. Must NOT be called while holding mutex_
    // (the worker's SetResult acquires mutex_ — joining under it deadlocks).
    // Serialized by joinMutex_ so two concurrent Start() calls can't both
    // join / assign the same std::thread object (which is UB).
    void JoinWorker();

    enum class State { Idle, Starting, Running, Done };

    mutable std::mutex          mutex_;
    std::mutex                  joinMutex_;   // guards join/assign of workerThread_
    State                       state_{State::Idle};
    std::string                 result_{"Ready"};

    std::thread                 workerThread_;
    CancellationToken::Ptr      token_;

    CompletionCallback          onComplete_;
    AcquisitionCallback         onAcquisitionComplete_;
};
