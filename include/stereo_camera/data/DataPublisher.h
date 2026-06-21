#pragma once

#include "stereo_camera/data/DataPipeline.h"
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
    DataPublisher(std::shared_ptr<DataPipeline> pipeline,
                  const std::unordered_map<std::string, std::string>& pub_endpoints,
                  int zmq_hwm = 2);
    ~DataPublisher();

    void start();
    void stop();
    bool is_running() const;
    void set_zmq_context(void* ctx) { shared_ctx_ = ctx; }
    void notify_new_data();

private:
    void pub_loop();
    void publish_group(const std::string& channel, void* sock, ChannelFrame& frame);

    std::shared_ptr<DataPipeline> pipeline_;
    std::unordered_map<std::string, std::string> pub_endpoints_;
    int zmq_hwm_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    void* zmq_ctx_ = nullptr;
    void* shared_ctx_ = nullptr;
    bool owns_ctx_ = false;
    std::unordered_map<std::string, void*> zmq_sockets_;
    std::mutex sockets_mutex_;
};

} // namespace stereo_camera
