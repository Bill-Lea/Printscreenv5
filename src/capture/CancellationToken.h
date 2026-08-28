#pragma once
#include <atomic>
#include <memory>
#include <exception>

// ============================================================
// Cancellation exception - thrown by ThrowIfCancelled()
// ============================================================
struct Cancelled final : std::exception {
    const char* what() const noexcept override { return "Cancelled"; }
};

// ============================================================
// CancellationToken
// Shared-ownership, thread-safe cancellation primitive.
// One session creates a token; multiple pipeline stages hold Ptr.
// The UI layer (MenuEventSink) holds a weak_ptr so expired captures
// are silently ignored.
// ============================================================
class CancellationToken {
public:
    using Ptr     = std::shared_ptr<CancellationToken>;
    using WeakPtr = std::weak_ptr<CancellationToken>;

    static Ptr Make() { return std::make_shared<CancellationToken>(); }

    void Cancel()  noexcept { flag_.store(true,  std::memory_order_release); }
    void Reset()   noexcept { flag_.store(false, std::memory_order_release); }
    bool IsCancelled() const noexcept { return flag_.load(std::memory_order_acquire); }

    // Throws Cancelled if the token has been set.
    void ThrowIfCancelled(const char* stage) const;

    // For legacy code that expects a raw atomic<bool>*.
    std::atomic<bool>* RawFlag() noexcept { return &flag_; }

private:
    std::atomic<bool> flag_{false};
};
