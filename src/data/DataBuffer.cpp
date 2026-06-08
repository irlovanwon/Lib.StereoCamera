#include "stereo_camera/data/DataBuffer.h"

namespace stereo_camera {

DataBuffer::DataBuffer(size_t max_frames_per_slot)
    : max_frames_per_slot_(max_frames_per_slot) {}

void DataBuffer::push(const std::string& camera_id, const std::shared_ptr<DataBundle>& bundle) {
    std::lock_guard<std::mutex> lock(mutex_);
    BufferSlotKey key{camera_id, bundle->type};
    auto it = slots_.find(key);
    if (it == slots_.end()) {
        auto rb = std::make_unique<RingBuffer<std::shared_ptr<DataBundle>>>(max_frames_per_slot_);
        auto result = slots_.emplace(key, std::move(rb));
        it = result.first;
    }
    it->second->push(bundle);
}

std::shared_ptr<DataBundle> DataBuffer::get_latest(const std::string& camera_id, DataType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find({camera_id, type});
    if (it == slots_.end()) return nullptr;
    return it->second->get_latest();
}

std::vector<std::shared_ptr<DataBundle>> DataBuffer::get_latest_n(const std::string& camera_id, DataType type, size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find({camera_id, type});
    if (it == slots_.end()) return {};
    return it->second->get_latest_n(count);
}

void DataBuffer::create_slot(const std::string& camera_id, DataType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    BufferSlotKey key{camera_id, type};
    if (slots_.find(key) == slots_.end()) {
        slots_.emplace(key, std::make_unique<RingBuffer<std::shared_ptr<DataBundle>>>(max_frames_per_slot_));
    }
}

void DataBuffer::remove_slot(const std::string& camera_id, DataType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    slots_.erase({camera_id, type});
}

void DataBuffer::remove_all_slots(const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (it->first.camera_id == camera_id) {
            it = slots_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t DataBuffer::slot_size(const std::string& camera_id, DataType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find({camera_id, type});
    return it == slots_.end() ? 0 : it->second->size();
}

void DataBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    slots_.clear();
}

void DataBuffer::clear_slot(const std::string& camera_id, DataType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find({camera_id, type});
    if (it != slots_.end()) {
        it->second->clear();
    }
}

std::vector<BufferSlotKey> DataBuffer::active_slots() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BufferSlotKey> result;
    result.reserve(slots_.size());
    for (const auto& [key, _] : slots_) {
        result.push_back(key);
    }
    return result;
}

} // namespace stereo_camera
