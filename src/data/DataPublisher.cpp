#include "stereo_camera/data/DataPublisher.h"
#include "stereo_camera/common/Logger.h"
#include <zmq.h>
#include <chrono>

namespace stereo_camera {

DataPublisher::DataPublisher(std::shared_ptr<DataBuffer> buffer, const std::string& pub_endpoint)
    : buffer_(std::move(buffer)), pub_endpoint_(pub_endpoint) {}

DataPublisher::~DataPublisher() {
    stop();
}

void DataPublisher::start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::make_unique<std::thread>(&DataPublisher::pub_loop, this);
    Logger::instance().info("DataPublisher", "Started on " + pub_endpoint_);
}

void DataPublisher::stop() {
    running_.store(false);
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    Logger::instance().info("DataPublisher", "Stopped");
}

bool DataPublisher::is_running() const {
    return running_.load();
}

void DataPublisher::pub_loop() {
    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_PUB);

    int sndhwm = 1;
    zmq_setsockopt(socket, ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    int linger = 0;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));

    int rc = zmq_bind(socket, pub_endpoint_.c_str());
    if (rc != 0) {
        Logger::instance().error("DataPublisher", "ZMQ bind failed: " + std::string(zmq_strerror(zmq_errno())));
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return;
    }

    while (running_.load()) {
        auto slots = buffer_->active_slots();
        for (const auto& slot : slots) {
            auto bundle = buffer_->get_latest(slot.camera_id, slot.type);
            if (!bundle || bundle->payload.empty()) continue;

            std::string topic = slot.camera_id + "/";
            switch (slot.type) {
                case DataType::StereoImage:  topic += "stereo_image"; break;
                case DataType::DepthMap:     topic += "depth_map"; break;
                case DataType::PointCloud:   topic += "point_cloud"; break;
                case DataType::IMU:          topic += "imu"; break;
                case DataType::DisparityMap: topic += "disparity_map"; break;
                case DataType::ConfidenceMap: topic += "confidence_map"; break;
            }

            zmq_send(socket, topic.c_str(), topic.size(), ZMQ_SNDMORE);
            zmq_send(socket, bundle->payload.data(), bundle->payload.size(), 0);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    zmq_close(socket);
    zmq_ctx_destroy(context);
    Logger::instance().info("DataPublisher", "Pub loop exited");
}

} // namespace stereo_camera
