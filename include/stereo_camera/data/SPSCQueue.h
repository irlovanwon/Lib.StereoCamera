#pragma once
#include <atomic>
#include <cstddef>
#include <new>
#include <utility>

namespace stereo_camera {

template <typename T, size_t Capacity>
class SPSCQueue {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be power of 2");
public:
    SPSCQueue() : max_depth_(Capacity) {}

    void set_max_depth(size_t depth) { max_depth_ = depth > Capacity ? Capacity : depth; }

    // Drop-NEWEST: returns false if full (incoming item rejected)
    bool try_push(const T& item) {
        const size_t wp = write_pos_.load(std::memory_order_relaxed);
        const size_t rp = read_pos_.load(std::memory_order_acquire);
        if (wp - rp >= max_depth_.load(std::memory_order_relaxed)) return false;
        buffer_[wp & kMask] = item;
        write_pos_.store(wp + 1, std::memory_order_release);
        return true;
    }
    bool try_push(T&& item) {
        const size_t wp = write_pos_.load(std::memory_order_relaxed);
        const size_t rp = read_pos_.load(std::memory_order_acquire);
        if (wp - rp >= max_depth_.load(std::memory_order_relaxed)) return false;
        buffer_[wp & kMask] = std::move(item);
        write_pos_.store(wp + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out) {
        const size_t rp = read_pos_.load(std::memory_order_relaxed);
        const size_t wp = write_pos_.load(std::memory_order_acquire);
        if (rp >= wp) return false;
        out = std::move(buffer_[rp & kMask]);
        read_pos_.store(rp + 1, std::memory_order_release);
        return true;
    }
    bool empty() const {
        return read_pos_.load(std::memory_order_acquire) >= write_pos_.load(std::memory_order_acquire);
    }
    size_t size() const {
        const size_t wp = write_pos_.load(std::memory_order_acquire);
        const size_t rp = read_pos_.load(std::memory_order_acquire);
        return wp >= rp ? wp - rp : 0;
    }
    static constexpr size_t capacity() { return Capacity; }
private:
    static constexpr size_t kMask = Capacity - 1;
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};
    alignas(64) std::atomic<size_t> max_depth_;
    alignas(64) T buffer_[Capacity];
};
} // namespace stereo_camera
