#pragma once
// Single-producer, single-consumer FIFO from the audio thread to the display. The display may lose data,
// audio is never delayed: when full, new samples are dropped. Holds its memory inline, so it never allocates.

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace drosera {

class SpectrumTap {
public:
    static constexpr size_t kCapacity = 4096;

    /// Consumer thread. Writes happen only while active, so a closed window costs the audio thread nothing.
    /// Activating drops what an earlier window left behind.
    void setActive(bool on) noexcept {
        if (on) read_.store(write_.load(std::memory_order_acquire), std::memory_order_release);
        active_.store(on, std::memory_order_release);
    }
    bool isActive() const noexcept { return active_.load(std::memory_order_acquire); }

    /// Any thread. Only the consumer may move the read position, so it applies the request on its next pop.
    void requestFlush() noexcept { flush_.store(true, std::memory_order_release); }

    /// Audio thread. Stores the mono sum; a non-finite sample becomes silence.
    void push(const float* l, const float* r, size_t n) noexcept {
        if (!isActive()) return;
        const uint32_t w = write_.load(std::memory_order_relaxed);
        const uint32_t used = w - read_.load(std::memory_order_acquire);
        const size_t count = n < kCapacity - used ? n : kCapacity - used;
        for (size_t i = 0; i < count; ++i) {
            const float s = 0.5f * (l[i] + r[i]);
            buffer_[(w + i) & (kCapacity - 1)] = std::isfinite(s) ? s : 0.0f;
        }
        write_.store(w + (uint32_t) count, std::memory_order_release);
    }

    /// Message thread. Returns how many samples it copied.
    size_t pop(float* dst, size_t max) noexcept {
        const uint32_t w = write_.load(std::memory_order_acquire);
        if (flush_.exchange(false, std::memory_order_acq_rel)) read_.store(w, std::memory_order_release);
        const uint32_t r = read_.load(std::memory_order_relaxed);
        const size_t available = w - r;
        const size_t count = max < available ? max : available;
        for (size_t i = 0; i < count; ++i) dst[i] = buffer_[(r + i) & (kCapacity - 1)];
        read_.store(r + (uint32_t) count, std::memory_order_release);
        return count;
    }

private:
    static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity must be a power of two");
    std::array<float, kCapacity> buffer_ {};
    std::atomic<uint32_t> write_ { 0 }, read_ { 0 };
    std::atomic<bool> active_ { false }, flush_ { false };
};

} // namespace drosera
