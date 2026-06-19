#include "stereo_camera/data/LoopbackSubscriber.h"
#include "stereo_camera/common/Logger.h"
#include <chrono>
#include <cstring>

namespace stereo_camera {

LoopbackSubscriber::LoopbackSubscriber(
    std::shared_ptr<DataBuffer> buffer,
    const std::unordered_map<std::string, std::string>& sub_endpoints)
    : buffer_(std::move(buffer)), sub_endpoints_(sub_endpoints) {}

LoopbackSubscriber::~LoopbackSubscriber() {
    stop();
}

void LoopbackSubscriber::start() {
    if (running_.load()) return;

    zmq_ctx_ = zmq_ctx_new();
    for (auto& [channel, endpoint] : sub_endpoints_) {
        void* sock = zmq_socket(zmq_ctx_, ZMQ_SUB);
        int rcvhwm = 2;
        zmq_setsockopt(sock, ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
        int linger = 0;
        zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(linger));

        zmq_setsockopt(sock, ZMQ_SUBSCRIBE, "", 0);

        int rc = zmq_connect(sock, endpoint.c_str());
        if (rc != 0) {
            Logger::instance().error("LoopbackSubscriber",
                "ZMQ connect failed for " + channel + ": " + zmq_strerror(zmq_errno()));
            zmq_close(sock);
            continue;
        }
        zmq_sockets_.push_back(sock);
        Logger::instance().info("LoopbackSubscriber",
            "SUB connected: " + channel + " -> " + endpoint);
    }

    if (zmq_sockets_.empty()) {
        Logger::instance().warn("LoopbackSubscriber", "No sockets connected, not starting");
        zmq_ctx_destroy(zmq_ctx_);
        zmq_ctx_ = nullptr;
        return;
    }

    running_.store(true);
    thread_ = std::make_unique<std::thread>(&LoopbackSubscriber::sub_loop, this);
}

void LoopbackSubscriber::stop() {
    running_.store(false);
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    for (void* sock : zmq_sockets_) {
        zmq_close(sock);
    }
    zmq_sockets_.clear();
    if (zmq_ctx_) {
        zmq_ctx_destroy(zmq_ctx_);
        zmq_ctx_ = nullptr;
    }
    Logger::instance().info("LoopbackSubscriber", "Stopped");
}

bool LoopbackSubscriber::is_running() const {
    return running_.load();
}

void LoopbackSubscriber::sub_loop() {
    Logger::instance().info("LoopbackSubscriber", "Sub loop started");

    uint64_t frame_count = 0;

    while (running_.load()) {
        std::vector<zmq_pollitem_t> items;
        items.reserve(zmq_sockets_.size());
        for (size_t i = 0; i < zmq_sockets_.size(); ++i) {
            zmq_pollitem_t item;
            item.socket = zmq_sockets_[i];
            item.events = ZMQ_POLLIN;
            item.revents = 0;
            items.push_back(item);
        }

        int rc = zmq_poll(items.data(), static_cast<int>(items.size()), 100);
        if (rc <= 0) continue;

        for (size_t i = 0; i < items.size(); ++i) {
            if (!(items[i].revents & ZMQ_POLLIN)) continue;

            zmq_msg_t topic_msg;
            zmq_msg_t payload_msg;
            zmq_msg_init(&topic_msg);
            zmq_msg_init(&payload_msg);

            zmq_msg_recv(&topic_msg, zmq_sockets_[i], 0);
            zmq_msg_recv(&payload_msg, zmq_sockets_[i], 0);

            char* topic_data = static_cast<char*>(zmq_msg_data(&topic_msg));
            size_t topic_len = zmq_msg_size(&topic_msg);

            std::string topic(topic_data, topic_len);

            size_t slash = topic.find('/');
            std::string camera_id = (slash != std::string::npos) ? topic.substr(0, slash) : "default";
            std::string channel = (slash != std::string::npos) ? topic.substr(slash + 1) : topic;

            DataType dt = channel_to_type(channel);

            auto bundle = std::make_shared<DataBundle>();
            bundle->timestamp = Timestamp::now();
            bundle->type = dt;
            bundle->payload.resize(zmq_msg_size(&payload_msg));
            std::memcpy(bundle->payload.data(), zmq_msg_data(&payload_msg), zmq_msg_size(&payload_msg));

            buffer_->push(camera_id, bundle);

            frame_count++;
            if (frame_count % 100 == 0) {
                Logger::instance().info("LoopbackSubscriber",
                    "Received " + std::to_string(frame_count) + " frames total");
            }

            zmq_msg_close(&topic_msg);
            zmq_msg_close(&payload_msg);
        }
    }

    Logger::instance().info("LoopbackSubscriber", "Sub loop exited");
}

DataType LoopbackSubscriber::channel_to_type(const std::string& channel) {
    if (channel == "stereo_image")   return DataType::StereoImage;
    if (channel == "depth_map")      return DataType::DepthMap;
    if (channel == "point_cloud")    return DataType::PointCloud;
    if (channel == "imu")            return DataType::IMU;
    if (channel == "disparity_map")  return DataType::DisparityMap;
    if (channel == "confidence_map") return DataType::ConfidenceMap;
    if (channel == "temperature")    return DataType::Temperature;
    if (channel == "magnetometer")   return DataType::Magnetometer;
    if (channel == "barometer")      return DataType::Barometer;
    return DataType::StereoImage;
}

} // namespace stereo_camera
