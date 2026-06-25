#pragma once
#include "stereo_camera/data/DataBuffer.h"
#include "stereo_camera/common/Types.h"
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>
#include <zmq.h>

namespace stereo_camera {

class LoopbackSubscriber {
public:
    // Called per grouped frame after pushing to DataBuffer, to feed the
    // API3 encode SPSC queues (group selected from the bundle type).
    using EncodeCallback = std::function<void(DataGroup, const ChannelFrame&)>;

    LoopbackSubscriber(std::shared_ptr<DataBuffer> buffer,
                       const std::unordered_map<std::string, std::string>& sub_endpoints,
                       int zmq_hwm = 1);
    ~LoopbackSubscriber();
    void start();
    void stop();
    bool is_running() const;
    void set_zmq_context(void* ctx) { shared_ctx_ = ctx; }
    void set_encode_callback(EncodeCallback cb) { encode_callback_ = std::move(cb); }
private:
    void sub_loop();
    static DataType channel_to_type(const std::string& id);

    std::shared_ptr<DataBuffer> buffer_;
    std::unordered_map<std::string, std::string> sub_endpoints_;
    int zmq_hwm_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    void* zmq_ctx_ = nullptr;
    void* shared_ctx_ = nullptr;
    bool owns_ctx_ = false;
    std::vector<void*> zmq_sockets_;
    std::atomic<uint64_t> total_frames_{0};
    EncodeCallback encode_callback_;
};

} // namespace stereo_camera
