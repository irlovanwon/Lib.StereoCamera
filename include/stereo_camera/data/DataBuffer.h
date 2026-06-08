#pragma once

#include "stereo_camera/common/Types.h"
#include <vector>
#include <mutex>
#include <memory>
#include <unordered_map>
#include <string>
#include <atomic>
#include <cstddef>

namespace stereo_camera {

struct BufferSlotKey {
    std::string camera_id;
    DataType type;

    bool operator==(const BufferSlotKey& other) const {
        return camera_id == other.camera_id && type == other.type;
    }
};

struct BufferSlotKeyHash {
    size_t operator()(const BufferSlotKey& key) const {
        return std::hash<std::string>()(key.camera_id) ^ (std::hash<int>()(static_cast<int>(key.type)) << 1);
    }
};

template<typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 4)
        : capacity_(next_power_of_two(capacity)),
          mask_(capacity_ - 1),
          ring_(capacity_),
          write_idx_(0),
          read_idx_(0) {}

    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t idx = write_idx_.load(std::memory_order_relaxed);
        ring_[idx & mask_] = item;
        write_idx_.store(idx + 1, std::memory_order_release);

        size_t r = read_idx_.load(std::memory_order_relaxed);
        if (idx + 1 - r > capacity_) {
            read_idx_.store(idx + 1 - capacity_, std::memory_order_release);
        }
    }

    T get_latest() const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t w = write_idx_.load(std::memory_order_acquire);
        size_t r = read_idx_.load(std::memory_order_acquire);
        if (w == r) return T{};
        return ring_[(w - 1) & mask_];
    }

    std::vector<T> get_latest_n(size_t count) const {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t w = write_idx_.load(std::memory_order_acquire);
        size_t r = read_idx_.load(std::memory_order_acquire);
        size_t available = w - r;
        size_t n = std::min(count, available);
        std::vector<T> result;
        result.reserve(n);
        for (size_t i = n; i > 0; --i) {
            result.push_back(ring_[(w - i) & mask_]);
        }
        return result;
    }

    size_t size() const {
        size_t w = write_idx_.load(std::memory_order_acquire);
        size_t r = read_idx_.load(std::memory_order_acquire);
        return w - r;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        write_idx_.store(0, std::memory_order_relaxed);
        read_idx_.store(0, std::memory_order_relaxed);
        std::fill(ring_.begin(), ring_.end(), T{});
    }

    size_t capacity() const { return capacity_; }

private:
    static size_t next_power_of_two(size_t v) {
        if (v == 0) return 1;
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        return v + 1;
    }

    size_t capacity_;
    size_t mask_;
    std::vector<T> ring_;
    std::atomic<size_t> write_idx_;
    std::atomic<size_t> read_idx_;
    mutable std::mutex mutex_;
};

class DataBuffer {
public:
    explicit DataBuffer(size_t max_frames_per_slot = 3);

    void push(const std::string& camera_id, const std::shared_ptr<DataBundle>& bundle);

    std::shared_ptr<DataBundle> get_latest(const std::string& camera_id, DataType type) const;
    std::vector<std::shared_ptr<DataBundle>> get_latest_n(const std::string& camera_id, DataType type, size_t count) const;

    void create_slot(const std::string& camera_id, DataType type);
    void remove_slot(const std::string& camera_id, DataType type);
    void remove_all_slots(const std::string& camera_id);

    size_t slot_size(const std::string& camera_id, DataType type) const;
    void clear();
    void clear_slot(const std::string& camera_id, DataType type);

    std::vector<BufferSlotKey> active_slots() const;

private:
    mutable std::mutex mutex_;
    size_t max_frames_per_slot_;
    std::unordered_map<BufferSlotKey, std::unique_ptr<RingBuffer<std::shared_ptr<DataBundle>>>, BufferSlotKeyHash> slots_;
};

} // namespace stereo_camera
