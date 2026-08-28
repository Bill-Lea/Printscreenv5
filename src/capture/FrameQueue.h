#pragma once
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <wrl/client.h>
#include <d3d11.h>

// ============================================================
// FrameQueue
// Lock-free-ish ring buffer for GPU textures between acquisition and encoding.
// Capacity is fixed at construction (default 3 frames = ~2-3 frames of latency).
//
// Threading:
//   - Producer (acquisition thread): Push() — blocks if full
//   - Consumer (encoder thread): Pop() — blocks if empty
//   - Both threads call Stop() to unblock waits on shutdown.
// ============================================================
class FrameQueue {
public:
    explicit FrameQueue(std::uint32_t capacity = 3);
    ~FrameQueue() = default;

    FrameQueue(const FrameQueue&) = delete;
    FrameQueue& operator=(const FrameQueue&) = delete;

    // Push a texture into the queue. Blocks if queue is full.
    // timestampHns is the capture-time presentation timestamp (100ns units),
    // recorded by the producer at acquisition time so that queue latency does
    // not distort frame timing on the encoder side.
    // Returns false if Stop() was called (queue shutting down).
    [[nodiscard]] bool Push(ID3D11Texture2D* texture, std::uint32_t frameIndex,
                            long long timestampHns);

    // Pop a texture from the queue. Blocks if queue is empty.
    // Returns false if Stop() was called and queue is empty.
    [[nodiscard]] bool Pop(Microsoft::WRL::ComPtr<ID3D11Texture2D>& outTexture,
                           std::uint32_t& outFrameIndex,
                           long long& outTimestampHns);

    // Signal shutdown — unblocks all waiting Push/Pop calls.
    void Stop() noexcept;

    [[nodiscard]] bool IsStopped() const noexcept { return stopped_.load(); }
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] bool IsFull() const noexcept;

private:
    struct Entry {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        std::uint32_t frameIndex   = 0;
        long long     timestampHns = 0;
    };

    mutable std::mutex mutex_;
    std::condition_variable notFull_;
    std::condition_variable notEmpty_;
    std::vector<Entry> buffer_;
    std::uint32_t capacity_;
    std::uint32_t head_ = 0; // next read position
    std::uint32_t tail_ = 0; // next write position
    std::uint32_t count_ = 0;
    std::atomic<bool> stopped_{ false };
};
