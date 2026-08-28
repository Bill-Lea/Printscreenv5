#include "PCH.h"
#include "FrameQueue.h"
#include "logger.h"

FrameQueue::FrameQueue(std::uint32_t capacity)
    : capacity_(std::max(std::uint32_t(1), capacity)) {
    buffer_.resize(capacity_);
}

bool FrameQueue::Push(ID3D11Texture2D* texture, std::uint32_t frameIndex,
                      long long timestampHns) {
    std::unique_lock<std::mutex> lk(mutex_);

    // Wait until there is space or we are stopped
    notFull_.wait(lk, [this] { return count_ < capacity_ || stopped_.load(); });

    if (stopped_.load()) {
        return false;
    }

    auto& entry = buffer_[tail_];
    entry.texture = texture;
    entry.frameIndex = frameIndex;
    entry.timestampHns = timestampHns;

    tail_ = (tail_ + 1) % capacity_;
    ++count_;

    lk.unlock();
    notEmpty_.notify_one();
    return true;
}

bool FrameQueue::Pop(Microsoft::WRL::ComPtr<ID3D11Texture2D>& outTexture,
                     std::uint32_t& outFrameIndex,
                     long long& outTimestampHns) {
    std::unique_lock<std::mutex> lk(mutex_);

    // Wait until there is data or we are stopped
    notEmpty_.wait(lk, [this] { return count_ > 0 || stopped_.load(); });

    if (count_ == 0 && stopped_.load()) {
        return false;
    }

    auto& entry = buffer_[head_];
    outTexture = entry.texture;
    outFrameIndex = entry.frameIndex;
    outTimestampHns = entry.timestampHns;
    entry.texture.Reset(); // Release reference immediately

    head_ = (head_ + 1) % capacity_;
    --count_;

    lk.unlock();
    notFull_.notify_one();
    return true;
}

void FrameQueue::Stop() noexcept {
    stopped_.store(true);
    notFull_.notify_all();
    notEmpty_.notify_all();
}

bool FrameQueue::IsEmpty() const noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    return count_ == 0;
}

bool FrameQueue::IsFull() const noexcept {
    std::lock_guard<std::mutex> lk(mutex_);
    return count_ == capacity_;
}
