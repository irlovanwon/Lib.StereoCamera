#pragma once

#include "stereo_camera/data/DataBuffer.h"
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
#include <zmq.h>

namespace stereo_camera {

class LoopbackSubscriber {
public:
    LoopbackSubscriber(std::shared_ptr<DataBuffer> buffer,
                       const std::unordered_map<std::string, std::string>& sub_endpoints);
    ~LoopbackSubscriber();

    void start();
    void stop();
    bool is_running() const;

private:
    void sub_loop();
    static DataType channel_to_type(const std::string& channel);

    std::shared_ptr<DataBuffer> buffer_;
    std::unordered_map<std::string, std::string> sub_endpoints_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    void* zmq_ctx_ = nullptr;
    std::vector<void*> zmq_sockets_;
};

} // namespace stereo_camera
