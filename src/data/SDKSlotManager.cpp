#include "stereo_camera/data/SDKSlotManager.h"
#include "stereo_camera/common/Logger.h"
#include <zmq.h>
#include <cstring>

namespace stereo_camera {
static std::vector<std::string> types_to_channels(const std::vector<DataType>& types) {
    std::vector<std::string> chans;
    for (auto t : types) {
        switch (t) {
            case DataType::StereoImage:
                chans.push_back("left_image");
                chans.push_back("right_image");
                break;
            case DataType::DepthMap:      chans.push_back("depth_map"); break;
            case DataType::PointCloud:    chans.push_back("point_cloud"); break;
            case DataType::IMU:           chans.push_back("imu"); break;
            case DataType::DisparityMap:  chans.push_back("disparity_map"); break;
            case DataType::ConfidenceMap: chans.push_back("confidence_map"); break;
            case DataType::Temperature:   chans.push_back("temperature"); break;
            default: break;
        }
    }
    return chans;
}



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
        slot->channels = cfg.channels;
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
        std::vector<std::string> chan_names;
        for (const auto& cn : types_to_channels(types)) {
            if (slot->channels.count(cn)) {
                slot->client->activate_channel(cn);
                chan_names.push_back(cn);
            }
        }

        auto resp = slot->client->start_capture_by_channels(chan_names);
        if (resp.code != ResponseCode::Success && resp.code != ResponseCode::AlreadyInit) {
            Logger::instance().warn("SDKSlotManager", "SDK StartCapture returned " + std::to_string(static_cast<int>(resp.code)) + " for " + camera_id + ", continuing with dealer thread");
        }

        slot->capturing.store(true);
        running_.store(true);
        std::unordered_map<std::string, std::string> chans;
        for (const auto& [k, v] : slot->channels) {
            if (k != "base") chans[k] = v;
        }
        slot->dealer_thread = std::make_unique<std::thread>(&SDKSlotManager::dealer_loop, this, camera_id, chans);
        Logger::instance().info("SDKSlotManager", "Dealer thread started for " + camera_id);
    }

    Logger::instance().info("SDKSlotManager", "Capture started for " + camera_id + " subscribers=" + std::to_string(slot->subscriber_count));
    return true;
}

bool SDKSlotManager::stop_capture(const std::string& camera_id, const std::vector<DataType>& types) {
    std::unique_ptr<std::thread> thread_to_join;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = slots_.find(camera_id);
        if (it == slots_.end()) return false;

        auto& slot = it->second;
        if (slot->subscriber_count > 0) {
            slot->subscriber_count--;
        }

        if (slot->subscriber_count <= 0 && slot->capturing.load()) {
            slot->capturing.store(false);
            thread_to_join = std::move(slot->dealer_thread);
            std::vector<std::string> chan_names;
            for (const auto& cn : types_to_channels(types)) {
                if (slot->channels.count(cn)) {
                    chan_names.push_back(cn);
                }
            }
            slot->client->stop_capture_by_channels(chan_names);
            for (const auto& cn : chan_names) {
                slot->client->deactivate_channel(cn);
            }
            for (const auto& type : types) {
                buffer_->remove_slot(camera_id, type);
            }
            Logger::instance().info("SDKSlotManager", "Dealer thread stopping for " + camera_id);
        }

        Logger::instance().info("SDKSlotManager", "Capture stopped for " + camera_id);
    }
    if (thread_to_join && thread_to_join->joinable()) {
        thread_to_join->join();
    }
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
    running_.store(true);
    for (const auto& [id, slot] : slots_) {
        auto resp = slot->client->init();
        if (resp.code == ResponseCode::Success || resp.code == ResponseCode::AlreadyInit) {
            resp = slot->client->connect();
            if (resp.code == ResponseCode::Success || resp.code == ResponseCode::AlreadyInit) {
                Logger::instance().info("SDKSlotManager", "Connected: " + id);
            } else {
                Logger::instance().warn("SDKSlotManager", "Connect failed for " + id + ": code=" + std::to_string(static_cast<int>(resp.code)));
            }
        } else {
            Logger::instance().warn("SDKSlotManager", "Init failed for " + id + ": code=" + std::to_string(static_cast<int>(resp.code)));
        }
    }
}

void SDKSlotManager::stop_all() {
    running_.store(false);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, slot] : slots_) {
        slot->capturing.store(false);
        slot->subscriber_count = 0;
        if (slot->dealer_thread && slot->dealer_thread->joinable()) {
            slot->dealer_thread->join();
        }
        slot->client->disconnect();
        slot->client->dispose();
    }
}

void SDKSlotManager::set_data_callback(DataReceivedCallback callback) {
    data_callback_ = std::move(callback);
}

void SDKSlotManager::dealer_loop(const std::string& camera_id, std::unordered_map<std::string, std::string> channels) {
    void* context = zmq_ctx_new();

    struct SubEntry { std::string channel; void* sock; };
    std::vector<SubEntry> subs;

    for (const auto& [ch, ep] : channels) {
        void* s = zmq_socket(context, ZMQ_SUB);
        int timeout_ms = 100;
        zmq_setsockopt(s, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
        int linger_val = 0;
        zmq_setsockopt(s, ZMQ_LINGER, &linger_val, sizeof(linger_val));
        if (zmq_connect(s, ep.c_str()) != 0) {
            Logger::instance().error("SDKSlotManager", "ZMQ connect failed for " + camera_id + "/" + ch + ": " + zmq_strerror(zmq_errno()));
            zmq_close(s);
            continue;
        }
        zmq_setsockopt(s, ZMQ_SUBSCRIBE, "", 0);
        subs.push_back({ch, s});
        Logger::instance().info("SDKSlotManager", "SUB connected: " + camera_id + "/" + ch + " -> " + ep);
    }

    if (subs.empty()) {
        zmq_ctx_destroy(context);
        Logger::instance().warn("SDKSlotManager", "Dealer loop: no channels for " + camera_id);
        return;
    }

    std::vector<zmq_pollitem_t> items;
    for (auto& su : subs) {
        zmq_pollitem_t it{};
        it.socket = su.sock;
        it.events = ZMQ_POLLIN;
        items.push_back(it);
    }

    while (running_.load() && capturing_active(camera_id)) {
        int rc = zmq_poll(items.data(), static_cast<int>(items.size()), 100);
        if (rc <= 0) continue;

        for (size_t i = 0; i < items.size(); ++i) {
            if (!(items[i].revents & ZMQ_POLLIN)) continue;

            zmq_msg_t hdr;
            zmq_msg_t body;
            zmq_msg_init(&hdr);
            zmq_msg_init(&body);
            zmq_msg_recv(&hdr, subs[i].sock, 0);

            int64_t more = 0;
            size_t more_sz = sizeof(more);
            zmq_getsockopt(subs[i].sock, ZMQ_RCVMORE, &more, &more_sz);
            if (more) {
                zmq_msg_recv(&body, subs[i].sock, 0);
            }

            auto bundle = std::make_shared<DataBundle>();
            bundle->type = channel_to_datatype(subs[i].channel);

            if (more) {
                size_t bsz = zmq_msg_size(&body);
                const uint8_t* d = static_cast<const uint8_t*>(zmq_msg_data(&body));
                bundle->payload.assign(d, d + bsz);
            } else {
                size_t hsz = zmq_msg_size(&hdr);
                const uint8_t* d = static_cast<const uint8_t*>(zmq_msg_data(&hdr));
                bundle->payload.assign(d, d + hsz);
            }
            bundle->timestamp = Timestamp::now();

            buffer_->push(camera_id, bundle);
            if (data_callback_) {
                data_callback_(camera_id, bundle);
            }

            zmq_msg_close(&hdr);
            zmq_msg_close(&body);
        }
    }

    for (auto& su : subs) zmq_close(su.sock);
    zmq_ctx_destroy(context);
    Logger::instance().info("SDKSlotManager", "Dealer loop exited: " + camera_id);
}

DataType SDKSlotManager::channel_to_datatype(const std::string& channel) {
    if (channel == "left_image" || channel == "right_image" || channel == "stereo_image") return DataType::StereoImage;
    if (channel == "depth_map") return DataType::DepthMap;
    if (channel == "point_cloud") return DataType::PointCloud;
    if (channel == "imu") return DataType::IMU;
    if (channel == "disparity_map") return DataType::DisparityMap;
    if (channel == "confidence_map") return DataType::ConfidenceMap;
    if (channel == "temperature") return DataType::Temperature;
    if (channel == "magnetometer") return DataType::Magnetometer;
    if (channel == "barometer") return DataType::Barometer;
    return DataType::StereoImage;
}

bool SDKSlotManager::capturing_active(const std::string& camera_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find(camera_id);
    return it != slots_.end() && it->second->capturing.load();
}

} // namespace stereo_camera
