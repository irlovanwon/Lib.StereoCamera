#pragma once

#include "stereo_camera/data/DataBuffer.h"
#include <string>
#include <thread>
#include <atomic>
#include <memory>

namespace stereo_camera {

class DataPublisher {
public:
    explicit DataPublisher(std::shared_ptr<DataBuffer> buffer, const std::string& pub_endpoint = "tcp://*:5556");
    ~DataPublisher();

    void start();
    void stop();
    bool is_running() const;

private:
    void pub_loop();

    std::shared_ptr<DataBuffer> buffer_;
    std::string pub_endpoint_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
};

} // namespace stereo_camera
