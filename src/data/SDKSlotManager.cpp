#include "stereo_camera/data/SDKSlotManager.h"
#include "stereo_camera/common/Logger.h"
#include <zmq.h>
#include <cstring>

namespace stereo_camera {

SDKSlotManager::SDKSlotManager(std::shared_ptr<DataBuffer> buffer)
    : buffer_(std::move(buffer)) {}

SDKSlotManager::~SDKSlotManager() {
    stop_all();
}

void SDKSlotManager::configure(const std::vector<CameraSDKConfig>& configs) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& cfg : configs) {
        auto slot = std::make_unique<SDKSlot>();
        slot->camera_id = cfg.id;
        slot->zmq_endpoint = cfg.zmq_endpoint;
        slot->client = std::make_shared<CameraSDKClient>(cfg.base_url);
        slots_[cfg.id] = std::move(slot);
        Logger::instance().info("SDKSlotManager", "Configured SDK: " + cfg.id + " -> " + cfg.base_url);
    }
}

bool SDKSlotManager::start_capture(const std::string& camera_id, const std::vector<DataType>& types) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find(camera_id);
    if (it == slots_.end()) {
        Logger::instance().error("SDKSlotManager", "Unknown camera: " + camera_id);
        return false;
    }

    auto& slot = it->second;
    slot->subscriber_count++;
    slot->active_types = types;

    for (const auto& type : types) {
        buffer_->create_slot(camera_id, type);
    }

    if (!slot->capturing.load()) {
        auto resp = slot->client->start_capture(types);
        if (resp.code != ResponseCode::Success && resp.code != ResponseCode::AlreadyInit) {
            Logger::instance().error("SDKSlotManager", "StartCapture failed for " + camera_id);
            slot->subscriber_count--;
            return false;
        }

        slot->capturing.store(true);
        running_.store(true);
        slot->dealer_thread = std::make_unique<std::thread>(&SDKSlotManager::dealer_loop, this, camera_id, slot->zmq_endpoint);
        Logger::instance().info("SDKSlotManager", "Dealer thread started for " + camera_id);
    }

    Logger::instance().info("SDKSlotManager", "Capture started for " + camera_id + " subscribers=" + std::to_string(slot->subscriber_count));
    return true;
}

bool SDKSlotManager::stop_capture(const std::string& camera_id, const std::vector<DataType>& types) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find(camera_id);
    if (it == slots_.end()) return false;

    auto& slot = it->second;
    if (slot->subscriber_count > 0) {
        slot->subscriber_count--;
    }

    if (slot->subscriber_count <= 0 && slot->capturing.load()) {
        slot->capturing.store(false);
        if (slot->dealer_thread && slot->dealer_thread->joinable()) {
            slot->dealer_thread->join();
        }
        slot->client->stop_capture(types);
        for (const auto& type : types) {
            buffer_->remove_slot(camera_id, type);
        }
        Logger::instance().info("SDKSlotManager", "Dealer thread stopped for " + camera_id);
    }

    Logger::instance().info("SDKSlotManager", "Capture stopped for " + camera_id + " subscribers=" + std::to_string(slot->subscriber_count));
    return true;
}

std::shared_ptr<CameraSDKClient> SDKSlotManager::get_client(const std::string& camera_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find(camera_id);
    return it == slots_.end() ? nullptr : it->second->client;
}

std::vector<std::string> SDKSlotManager::get_camera_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(slots_.size());
    for (const auto& [id, _] : slots_) {
        ids.push_back(id);
    }
    return ids;
}

void SDKSlotManager::start_all() {
    for (const auto& [id, slot] : slots_) {
        auto resp = slot->client->init();
        if (resp.code == ResponseCode::Success) {
            resp = slot->client->connect();
            if (resp.code == ResponseCode::Success) {
                Logger::instance().info("SDKSlotManager", "Connected: " + id);
            }
        }
    }
}

void SDKSlotManager::stop_all() {
    running_.store(false);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, slot] : slots_) {
        slot->capturing.store(false);
        if (slot->dealer_thread && slot->dealer_thread->joinable()) {
            slot->dealer_thread->join();
        }
        slot->client->disconnect();
        slot->client->dispose();
    }
    slots_.clear();
}

void SDKSlotManager::set_data_callback(DataReceivedCallback callback) {
    data_callback_ = std::move(callback);
}

void SDKSlotManager::dealer_loop(const std::string& camera_id, const std::string& zmq_endpoint) {
    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_DEALER);
    int timeout_ms = 100;
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    int linger_val = 0; zmq_setsockopt(socket, ZMQ_LINGER, &linger_val, sizeof(linger_val));

    int rc = zmq_connect(socket, zmq_endpoint.c_str());
    if (rc != 0) {
        Logger::instance().error("SDKSlotManager", "ZMQ connect failed for " + camera_id + ": " + zmq_strerror(zmq_errno()));
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return;
    }

    Logger::instance().info("SDKSlotManager", "Dealer connected: " + camera_id + " -> " + zmq_endpoint);

    while (running_.load() && capturing_active(camera_id)) {
        char topic[256] = {};
        int rc = zmq_recv(socket, topic, sizeof(topic) - 1, 0);
        if (rc < 0) {
            int err = zmq_errno();
            if (err == EAGAIN) continue;
            Logger::instance().error("SDKSlotManager", "ZMQ recv error: " + std::string(zmq_strerror(err)));
            break;
        }
        if (rc == 0) continue;

        zmq_msg_t msg;
        zmq_msg_init(&msg);
        rc = zmq_msg_recv(&msg, socket, 0);
        if (rc < 0) {
            zmq_msg_close(&msg);
            continue;
        }

        auto bundle = std::make_shared<DataBundle>();
        size_t msg_size = zmq_msg_size(&msg);
        const uint8_t* data = static_cast<const uint8_t*>(zmq_msg_data(&msg));
        bundle->payload.assign(data, data + msg_size);
        bundle->timestamp = Timestamp::now();

        std::string topic_str(topic);
        for (int i = 0; i < static_cast<int>(DataType::Temperature) + 1; ++i) {
            DataType dt = static_cast<DataType>(i);
            std::string dt_name;
            switch (dt) {
                case DataType::StereoImage:  dt_name = "stereo_image"; break;
                case DataType::DepthMap:     dt_name = "depth_map"; break;
                case DataType::PointCloud:   dt_name = "point_cloud"; break;
                case DataType::IMU:          dt_name = "imu"; break;
                case DataType::DisparityMap: dt_name = "disparity_map"; break;
                case DataType::ConfidenceMap: dt_name = "confidence_map"; break;
                case DataType::Temperature:   dt_name = "temperature"; break;
            }
            if (topic_str.find(dt_name) != std::string::npos) {
                bundle->type = dt;
                break;
            }
        }

        buffer_->push(camera_id, bundle);

        if (data_callback_) {
            data_callback_(camera_id, bundle);
        }

        zmq_msg_close(&msg);
    }

    zmq_close(socket);
    zmq_ctx_destroy(context);
    Logger::instance().info("SDKSlotManager", "Dealer loop exited: " + camera_id);
}

bool SDKSlotManager::capturing_active(const std::string& camera_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find(camera_id);
    return it != slots_.end() && it->second->capturing.load();
}

} // namespace stereo_camera
