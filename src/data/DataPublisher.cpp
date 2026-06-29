#include "stereo_camera/data/DataPublisher.h"
#include "stereo_camera/common/Logger.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstring>

namespace stereo_camera {

DataPublisher::DataPublisher(std::shared_ptr<DataPipeline> pipeline,
                             const std::unordered_map<std::string, std::string>& pub_endpoints,
                             int zmq_hwm)
    : pipeline_(std::move(pipeline)), pub_endpoints_(pub_endpoints), zmq_hwm_(zmq_hwm) {}

DataPublisher::~DataPublisher() { stop(); }

void DataPublisher::start() {
    if (running_.load()) return;
    if (shared_ctx_) { zmq_ctx_ = shared_ctx_; owns_ctx_ = false; }
    else { zmq_ctx_ = zmq_ctx_new(); owns_ctx_ = true; }

    for (auto& [channel, endpoint] : pub_endpoints_) {
        void* sock = zmq_socket(zmq_ctx_, ZMQ_PUB);
        int hwm = zmq_hwm_;
        zmq_setsockopt(sock, ZMQ_SNDHWM, &hwm, sizeof(hwm));
        int linger = 0;
        zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(linger));
        int immediate = 1;
        zmq_setsockopt(sock, ZMQ_IMMEDIATE, &immediate, sizeof(immediate));
        if (zmq_bind(sock, endpoint.c_str()) != 0) {
            Logger::instance().error("DataPublisher",
                "Bind failed " + channel + ": " + zmq_strerror(zmq_errno()));
            zmq_close(sock);
            continue;
        }
        zmq_sockets_[channel] = sock;
        Logger::instance().info("DataPublisher",
            "PUB bound: " + channel + " -> " + endpoint + " (HWM=" + std::to_string(zmq_hwm_) + ")");
    }
    running_.store(true);
    thread_ = std::make_unique<std::thread>(&DataPublisher::pub_loop, this);
}

void DataPublisher::stop() {
    running_.store(false);
    pipeline_->running.store(false);
    pipeline_->notify();
    if (thread_ && thread_->joinable()) thread_->join();
    std::lock_guard<std::mutex> lock(sockets_mutex_);
    for (auto& [ch, sock] : zmq_sockets_) zmq_close(sock);
    zmq_sockets_.clear();
    if (zmq_ctx_ && owns_ctx_) zmq_ctx_destroy(zmq_ctx_);
    zmq_ctx_ = nullptr;
    Logger::instance().info("DataPublisher", "Stopped");
}

bool DataPublisher::is_running() const { return running_.load(); }
void DataPublisher::notify_new_data() { pipeline_->notify(); }

void DataPublisher::publish_shutdown() {
    std::string msg = R"({"event":"server_shutdown","message":"Service restarting"})";
    std::lock_guard<std::mutex> lock(sockets_mutex_);
    for (auto& [channel, sock] : zmq_sockets_) {
        zmq_send(sock, msg.c_str(), msg.size(), ZMQ_DONTWAIT);
    }
}

void DataPublisher::publish_group(const std::string& channel, void* sock, ChannelFrame& frame) {
    if (frame.bundles.empty()) return;

    // Build JSON header with parts[]
    nlohmann::json hdr;
    hdr["camera_id"] = frame.camera_id;
    hdr["ts_sec"] = frame.timestamp.sec;
    hdr["ts_nsec"] = frame.timestamp.nsec;
    hdr["channel"] = channel;
    auto& parts = hdr["parts"] = nlohmann::json::array();

    for (auto& b : frame.bundles) {
        nlohmann::json p;
        p["id"] = data_type_to_channel(b->type);
        p["size"] = b->payload.size();
        parts.push_back(p);
    }
    std::string hdr_str = hdr.dump();

    std::string topic = frame.camera_id + "/" + channel;

    // Send topic frame
    zmq_send(sock, topic.c_str(), topic.size(), ZMQ_SNDMORE | ZMQ_DONTWAIT);
    // Send header frame
    zmq_send(sock, hdr_str.c_str(), hdr_str.size(), ZMQ_SNDMORE | ZMQ_DONTWAIT);
    // Send payload frames (zero-copy via zmq_msg_init_data)
    for (size_t i = 0; i < frame.bundles.size(); i++) {
        auto& bundle = frame.bundles[i];
        int flags = (i + 1 < frame.bundles.size()) ? (ZMQ_SNDMORE | ZMQ_DONTWAIT) : ZMQ_DONTWAIT;

        auto* keep_alive = new std::shared_ptr<DataBundle>(bundle);
        zmq_msg_t msg;
        zmq_msg_init_data(&msg, bundle->payload.data(), bundle->payload.size(),
            [](void*, void* hint) {
                delete static_cast<std::shared_ptr<DataBundle>*>(hint);
            }, keep_alive);
        if (zmq_msg_send(&msg, sock, flags) == -1) {
            zmq_msg_close(&msg);
            Logger::instance().warn("DataPublisher", "ZMQ send dropped (HWM)");
        }
    }
}

void DataPublisher::pub_loop() {
    Logger::instance().info("DataPublisher", "Pub loop started (SPSC + CV, zero-copy, grouped)");

    while (running_.load()) {
        {
            std::unique_lock<std::mutex> lk(pipeline_->cv_mutex);
            pipeline_->cv.wait_for(lk, std::chrono::milliseconds(publish_cv_timeout_ms_),
                [this] { return !running_.load() || pipeline_->has_data(); });
        }
        if (!running_.load()) break;

        std::lock_guard<std::mutex> lock(sockets_mutex_);

        ChannelFrame frame;
        // Pop from 2D queue
        while (pipeline_->queue_2d.pop(frame)) {
            auto it = zmq_sockets_.find("visual_geometric_2d");
            if (it != zmq_sockets_.end())
                publish_group("visual_geometric_2d", it->second, frame);
        }
        // Pop from 3D queue
        while (pipeline_->queue_3d.pop(frame)) {
            auto it = zmq_sockets_.find("visual_geometric_3d");
            if (it != zmq_sockets_.end())
                publish_group("visual_geometric_3d", it->second, frame);
        }
        // Pop from sensor queue
        while (pipeline_->queue_sensor.pop(frame)) {
            auto it = zmq_sockets_.find("sensor_tracking");
            if (it != zmq_sockets_.end())
                publish_group("sensor_tracking", it->second, frame);
        }
    }
    Logger::instance().info("DataPublisher", "Pub loop exited");
}

} // namespace stereo_camera
