#include "stereo_camera/data/DataPublisher.h"
#include "stereo_camera/common/Logger.h"
#include <chrono>

namespace stereo_camera {

DataPublisher::DataPublisher(std::shared_ptr<DataBuffer> buffer,
                             const std::unordered_map<std::string, std::string>& pub_endpoints)
    : buffer_(std::move(buffer)), pub_endpoints_(pub_endpoints) {}

DataPublisher::~DataPublisher() {
    stop();
}

void DataPublisher::start() {
    if (running_.load()) return;

    zmq_ctx_ = zmq_ctx_new();
    for (auto& [channel, endpoint] : pub_endpoints_) {
        void* sock = zmq_socket(zmq_ctx_, ZMQ_PUB);
        int sndhwm = 1;
        zmq_setsockopt(sock, ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
        int linger = 0;
        zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(linger));

        int rc = zmq_bind(sock, endpoint.c_str());
        if (rc != 0) {
            Logger::instance().error("DataPublisher",
                "ZMQ bind failed for " + channel + ": " + zmq_strerror(zmq_errno()));
            zmq_close(sock);
            continue;
        }
        zmq_sockets_[channel] = sock;
        Logger::instance().info("DataPublisher", "PUB bound: " + channel + " -> " + endpoint);
    }

    running_.store(true);
    thread_ = std::make_unique<std::thread>(&DataPublisher::pub_loop, this);
}

void DataPublisher::stop() {
    running_.store(false);
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    std::lock_guard<std::mutex> lock(sockets_mutex_);
    for (auto& [ch, sock] : zmq_sockets_) {
        zmq_close(sock);
    }
    zmq_sockets_.clear();
    if (zmq_ctx_) {
        zmq_ctx_destroy(zmq_ctx_);
        zmq_ctx_ = nullptr;
    }
    Logger::instance().info("DataPublisher", "Stopped");
}

bool DataPublisher::is_running() const {
    return running_.load();
}

void DataPublisher::pub_loop() {
    Logger::instance().info("DataPublisher", "Pub loop started");

    while (running_.load()) {
        auto slots = buffer_->active_slots();
        for (const auto& slot : slots) {
            auto bundle = buffer_->get_latest(slot.camera_id, slot.type);
            if (!bundle || bundle->payload.empty()) continue;

            std::string channel = data_type_to_channel(slot.type);

            std::lock_guard<std::mutex> lock(sockets_mutex_);
            auto it = zmq_sockets_.find(channel);
            if (it == zmq_sockets_.end()) continue;

            std::string topic = slot.camera_id + "/" + channel;
            zmq_send(it->second, topic.c_str(), topic.size(), ZMQ_SNDMORE);
            zmq_send(it->second, bundle->payload.data(), bundle->payload.size(), 0);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    Logger::instance().info("DataPublisher", "Pub loop exited");
}

} // namespace stereo_camera
