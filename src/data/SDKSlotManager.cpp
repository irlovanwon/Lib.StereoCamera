#include "stereo_camera/data/SDKSlotManager.h"
#include "stereo_camera/common/Logger.h"
#include <zmq.h>
#include <cstring>
#include <nlohmann/json.hpp>

namespace stereo_camera {
static std::vector<std::string> types_to_channels(const std::vector<DataType>& types) {
    bool has_2d = false, has_3d = false, has_sensor = false;
    for (auto t : types) {
        switch (data_type_to_group(t)) {
            case DataGroup::VisualGeometric2D: has_2d = true; break;
            case DataGroup::VisualGeometric3D: has_3d = true; break;
            case DataGroup::SensorTracking:    has_sensor = true; break;
        }
    }
    std::vector<std::string> chans;
    if (has_2d) chans.push_back("visual_2d");
    if (has_3d) chans.push_back("visual_3d");
    if (has_sensor) chans.push_back("sensor_data");
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
            chan_names.push_back(cn);
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

nlohmann::json SDKSlotManager::get_status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json cameras = nlohmann::json::array();
    for (const auto& [id, slot] : slots_) {
        nlohmann::json cam;
        cam["camera_id"] = id;
        cam["capturing"] = slot->capturing.load();
        cam["subscriber_count"] = slot->subscriber_count;
        nlohmann::json types = nlohmann::json::array();
        for (auto t : slot->active_types)
            types.push_back(data_type_to_channel(t));
        cam["active_types"] = types;
        nlohmann::json chans = nlohmann::json::array();
        for (const auto& [name, endpoint] : slot->channels)
            chans.push_back({{"name", name}, {"endpoint", endpoint}});
        cam["channels"] = chans;
        cameras.push_back(cam);
    }
    return cameras;
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
    std::vector<std::unique_ptr<std::thread>> threads_to_join;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, slot] : slots_) {
            slot->capturing.store(false);
            slot->subscriber_count = 0;
            if (slot->dealer_thread && slot->dealer_thread->joinable()) {
                threads_to_join.push_back(std::move(slot->dealer_thread));
            }
        }
    }
    // Join outside mutex to avoid deadlock with dealer_loop → capturing_active()
    for (auto& t : threads_to_join) {
        if (t && t->joinable()) t->join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, slot] : slots_) {
        slot->client->disconnect();
        slot->client->dispose();
    }
}

void SDKSlotManager::set_data_callback(DataReceivedCallback callback) {
    data_callback_ = std::move(callback);
}

void SDKSlotManager::dealer_loop(const std::string& camera_id, std::unordered_map<std::string, std::string> channels) {
    void* context = shared_zmq_ctx_ ? shared_zmq_ctx_ : zmq_ctx_new();
    bool owns_ctx = (shared_zmq_ctx_ == nullptr);

    struct SubEntry { std::string group; void* sock; };
    std::vector<SubEntry> subs;

    for (const auto& [ch, ep] : channels) {
        void* s = zmq_socket(context, ZMQ_SUB);
        int timeout_ms = 100;
        zmq_setsockopt(s, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
        int linger_val = 0;
        zmq_setsockopt(s, ZMQ_LINGER, &linger_val, sizeof(linger_val));
        int rcvhwm = 2;
        zmq_setsockopt(s, ZMQ_RCVHWM, &rcvhwm, sizeof(rcvhwm));
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
        if (owns_ctx) zmq_ctx_destroy(context);
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

            std::vector<std::vector<uint8_t>> zparts;
            while (true) {
                zmq_msg_t msg;
                zmq_msg_init(&msg);
                int r2 = zmq_msg_recv(&msg, subs[i].sock, 0);
                if (r2 < 0) { zmq_msg_close(&msg); break; }
                size_t sz = zmq_msg_size(&msg);
                auto* d = static_cast<const uint8_t*>(zmq_msg_data(&msg));
                zparts.emplace_back(d, d + sz);
                zmq_msg_close(&msg);

                int64_t more = 0;
                size_t more_sz = sizeof(more);
                zmq_getsockopt(subs[i].sock, ZMQ_RCVMORE, &more, &more_sz);
                if (!more) break;
            }
            if (zparts.empty()) continue;

            try {
                std::string hdr_str(zparts[0].begin(), zparts[0].end());
                auto hdr = nlohmann::json::parse(hdr_str);
                if (!hdr.value("active", true)) continue;
                auto& parts_arr = hdr["parts"];
                const std::string& grp = subs[i].group;

                if (grp == "visual_2d") {
                    ChannelFrame frame_2d;
                    frame_2d.camera_id = camera_id;
                    frame_2d.timestamp = {hdr.value("ts_sec", 0), hdr.value("ts_nsec", 0)};
                    for (size_t j = 0; j < parts_arr.size() && j + 1 < zparts.size(); j++) {
                        std::string id = parts_arr[j].value("id", "");
                        auto b = std::make_shared<DataBundle>();
                        b->payload = std::move(zparts[j+1]);
                        b->timestamp = frame_2d.timestamp;
                        try { b->type = nlohmann::json(id).get<DataType>(); }
                        catch (...) { continue; }
                        buffer_->push(camera_id, b);
                        frame_2d.bundles.push_back(b);
                        if (data_callback_) data_callback_(camera_id, b);
                    }
                    if (pipeline_ && !frame_2d.bundles.empty()) {
                        if (pipeline_->queue_2d.try_push(std::move(frame_2d)))
                            pipeline_->total_pushed.fetch_add(1);
                        else
                            pipeline_->total_dropped.fetch_add(1);
                        pipeline_->notify();
                    }
                } else if (grp == "visual_3d") {
                    ChannelFrame frame_3d;
                    frame_3d.camera_id = camera_id;
                    frame_3d.timestamp = {hdr.value("ts_sec", 0), hdr.value("ts_nsec", 0)};
                    for (size_t j = 0; j < parts_arr.size() && j + 1 < zparts.size(); j++) {
                        std::string id = parts_arr[j].value("id", "");
                        auto b = std::make_shared<DataBundle>();
                        b->payload = std::move(zparts[j+1]);
                        b->timestamp = frame_3d.timestamp;
                        try { b->type = nlohmann::json(id).get<DataType>(); }
                        catch (...) { continue; }
                        buffer_->push(camera_id, b);
                        frame_3d.bundles.push_back(b);
                        if (data_callback_) data_callback_(camera_id, b);
                    }
                    if (pipeline_ && !frame_3d.bundles.empty()) {
                        if (pipeline_->queue_3d.try_push(std::move(frame_3d)))
                            pipeline_->total_pushed.fetch_add(1);
                        else
                            pipeline_->total_dropped.fetch_add(1);
                        pipeline_->notify();
                    }
                } else if (grp == "sensor_data") {
                    ChannelFrame frame_s;
                    frame_s.camera_id = camera_id;
                    frame_s.timestamp = {hdr.value("ts_sec", 0), hdr.value("ts_nsec", 0)};
                    for (size_t j = 0; j < parts_arr.size() && j + 1 < zparts.size(); j++) {
                        std::string id = parts_arr[j].value("id", "");
                        auto b = std::make_shared<DataBundle>();
                        b->payload = std::move(zparts[j+1]);
                        b->timestamp = frame_s.timestamp;
                        try { b->type = nlohmann::json(id).get<DataType>(); }
                        catch (...) { continue; }
                        buffer_->push(camera_id, b);
                        frame_s.bundles.push_back(b);
                        if (data_callback_) data_callback_(camera_id, b);
                    }
                    if (pipeline_ && !frame_s.bundles.empty()) {
                        if (pipeline_->queue_sensor.try_push(std::move(frame_s)))
                            pipeline_->total_pushed.fetch_add(1);
                        else
                            pipeline_->total_dropped.fetch_add(1);
                        pipeline_->notify();
                    }
                }
            } catch (const std::exception& e) {
                Logger::instance().warn("SDKSlotManager", std::string("Parse error: ") + e.what());
            }
        }
    }

    for (auto& su : subs) zmq_close(su.sock);
    if (owns_ctx) zmq_ctx_destroy(context);
    Logger::instance().info("SDKSlotManager", "Dealer loop exited: " + camera_id);
}

DataType SDKSlotManager::channel_to_datatype(const std::string& channel) {
    try { return nlohmann::json(channel).get<DataType>(); }
    catch (...) { return DataType::StereoImage; }
}

bool SDKSlotManager::capturing_active(const std::string& camera_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = slots_.find(camera_id);
    return it != slots_.end() && it->second->capturing.load();
}

} // namespace stereo_camera
