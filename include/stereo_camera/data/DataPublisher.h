#pragma once

#include "stereo_camera/data/DataBuffer.h"
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <zmq.h>

namespace stereo_camera {

class DataPublisher {
public:
    explicit DataPublisher(std::shared_ptr<DataBuffer> buffer,
                           const std::unordered_map<std::string, std::string>& pub_endpoints);
    ~DataPublisher();

    void start();
    void stop();
    bool is_running() const;

private:
    void pub_loop();

    std::shared_ptr<DataBuffer> buffer_;
    std::unordered_map<std::string, std::string> pub_endpoints_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    void* zmq_ctx_ = nullptr;
    std::unordered_map<std::string, void*> zmq_sockets_;
    std::mutex sockets_mutex_;
};

} // namespace stereo_camera
