// Lock-free single-producer/single-consumer ring. UI thread pushes,
// audio render thread pops. Pure C++.
#pragma once
#include <atomic>
#include <cstdint>

namespace dsp {

template <typename T, uint32_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");

public:
    bool push(const T& v) {
        const uint32_t h = head_.load(std::memory_order_relaxed);
        const uint32_t n = (h + 1) & (N - 1);
        if (n == tail_.load(std::memory_order_acquire)) return false;  // full
        buf_[h] = v;
        head_.store(n, std::memory_order_release);
        return true;
    }

    bool pop(T& v) {
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;  // empty
        v = buf_[t];
        tail_.store((t + 1) & (N - 1), std::memory_order_release);
        return true;
    }

    // Consumer-side peek: read the head without consuming it. Lets the
    // consumer hold a not-yet-due scheduled item in place.
    bool peek(T& v) const {
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;  // empty
        v = buf_[t];
        return true;
    }

private:
    T buf_[N];
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
};

}  // namespace dsp
