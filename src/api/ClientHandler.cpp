#include "stereo_camera/api/ClientHandler.h"
#include "stereo_camera/data/DataPipeline.h"
#include "stereo_camera/data/DataBuffer.h"
#include "stereo_camera/data/WSServer.h"
#include "stereo_camera/data/SDKSlotManager.h"
#include "stereo_camera/common/Logger.h"
#include <algorithm>

namespace stereo_camera {

void ClientHandler::set_sdk_manager(std::shared_ptr<SDKSlotManager> mgr) {
    sdk_manager_ = std::move(mgr);
}

Response ClientHandler::handle_init(const std::string& client_id) {
    // Module is initialized at service startup. init/dispose are internal lifecycle
    // operations — clients should use connect/disconnect for session management.
    return make_response(ResponseCode::AlreadyInit, "Already initialized — use connect to start a session");
}

Response ClientHandler::handle_dispose(const std::string& client_id) {
    // Dispose is an internal lifecycle operation handled at service shutdown.
    // Clients must not stop the module via API.
    return make_response(ResponseCode::Error, "Dispose not permitted via API — shutdown the service instead");
}

Response ClientHandler::handle_connect(const std::string& client_id) {
    if (!module_initialized_.load()) {
        return make_response(ResponseCode::NotReady, "Not initialized");
    }
    std::lock_guard<std::mutex> lk(sessions_mutex_);
    auto& session = sessions_[client_id];
    if (session.connected) {
        return make_response(ResponseCode::AlreadyInit, "Already connected");
    }
    if (sdk_manager_) { sdk_manager_->start_all(); }
    session.id = client_id;
    session.connected = true;
    Logger::instance().info("ClientHandler", "Connect: " + client_id);
    return make_response(ResponseCode::Success, "Connected");
}

Response ClientHandler::handle_disconnect(const std::string& client_id) {
    std::vector<DataType> types_to_stop;
    std::string cam_id;
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = sessions_.find(client_id);
        if (it == sessions_.end() || !it->second.connected) {
            return make_response(ResponseCode::NotReady, "Not connected");
        }
        types_to_stop = it->second.active_types;
        cam_id = it->second.camera_id;
        it->second.connected = false;
        it->second.active_types.clear();
    }
    if (sdk_manager_ && !types_to_stop.empty() && !cam_id.empty()) {
        sdk_manager_->stop_capture(cam_id, types_to_stop);
        Logger::instance().info("ClientHandler", "Disconnect cleanup: stopped " + std::to_string(types_to_stop.size()) + " types for " + client_id);
    }
    Logger::instance().info("ClientHandler", "Disconnect: " + client_id);
    return make_response(ResponseCode::Success, "Disconnected");
}

Response ClientHandler::handle_start_capture(const std::string& client_id, const std::vector<DataType>& types) {
    std::vector<DataType> capture_types = types;
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = sessions_.find(client_id);
        if (it == sessions_.end() || !it->second.connected) {
            return make_response(ResponseCode::NotReady, "Not connected");
        }
        if (capture_types.empty()) {
            capture_types = {DataType::StereoImage, DataType::IMU};
        }
        for (const auto& t : capture_types) {
            if (std::find(it->second.active_types.begin(), it->second.active_types.end(), t) == it->second.active_types.end()) {
                it->second.active_types.push_back(t);
            }
        }
    }
    if (sdk_manager_) {
        std::string camera_id = "cam1";
        auto camera_ids = sdk_manager_->get_camera_ids();
        if (!camera_ids.empty()) {
            camera_id = camera_ids[0];
        }
        {
            std::lock_guard<std::mutex> lk(sessions_mutex_);
            sessions_[client_id].camera_id = camera_id;
        }
        bool ok = sdk_manager_->start_capture(camera_id, capture_types);
        if (!ok) {
            return make_response(ResponseCode::Error, "StartCapture failed");
        }
    }
    Logger::instance().info("ClientHandler", "StartCapture: " + client_id);
    return make_response(ResponseCode::Success, "Capture started");
}

Response ClientHandler::handle_stop_capture(const std::string& client_id, const std::vector<DataType>& types) {
    std::vector<DataType> capture_types = types;
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = sessions_.find(client_id);
        if (it == sessions_.end() || !it->second.connected) {
            return make_response(ResponseCode::NotReady, "Not connected");
        }
        if (capture_types.empty()) {
            capture_types = {DataType::StereoImage, DataType::IMU};
        }
        for (const auto& t : capture_types) {
            it->second.active_types.erase(
                std::remove(it->second.active_types.begin(), it->second.active_types.end(), t),
                it->second.active_types.end());
        }
    }
    if (sdk_manager_) {
        std::string camera_id = "cam1";
        auto camera_ids = sdk_manager_->get_camera_ids();
        if (!camera_ids.empty()) {
            camera_id = camera_ids[0];
        }
        sdk_manager_->stop_capture(camera_id, capture_types);
    }
    Logger::instance().info("ClientHandler", "StopCapture: " + client_id);
    return make_response(ResponseCode::Success, "Capture stopped");
}

Response ClientHandler::handle_check_status(const std::string& client_id) {
    nlohmann::json detail;
    detail["initialized"] = module_initialized_.load();
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        detail["sessions"] = sessions_.size();
    }

    // Camera status from SDKSlotManager
    if (sdk_manager_) {
        detail["cameras"] = sdk_manager_->get_status();
    }

    // Pipeline metrics from DataPipeline
    if (pipeline_) {
        nlohmann::json pipe;
        pipe["total_pushed"] = pipeline_->total_pushed.load();
        pipe["total_dropped"] = pipeline_->total_dropped.load();
        pipe["queue_2d_size"] = pipeline_->queue_2d.size();
        pipe["queue_3d_size"] = pipeline_->queue_3d.size();
        pipe["queue_sensor_size"] = pipeline_->queue_sensor.size();
        uint64_t pushed = pipeline_->total_pushed.load();
        uint64_t dropped = pipeline_->total_dropped.load();
        pipe["drop_rate_pct"] = (pushed + dropped > 0)
            ? (100.0 * dropped / (pushed + dropped)) : 0.0;
        detail["pipeline"] = pipe;
    }

    // DataBuffer #2 stats
    if (buffer2_) {
        nlohmann::json buf;
        auto slots = buffer2_->active_slots();
        nlohmann::json slot_info = nlohmann::json::array();
        for (const auto& s : slots) {
            nlohmann::json si;
            si["camera_id"] = s.camera_id;
            si["type"] = data_type_to_channel(s.type);
            si["buffered"] = buffer2_->slot_size(s.camera_id, s.type);
            slot_info.push_back(si);
        }
        buf["active_slots"] = slot_info;
        detail["data_buffer_2"] = buf;
    }

    // WSS stats
    if (wss_server_) {
        nlohmann::json wss;
        wss["running"] = wss_server_->is_running();
        wss["clients"] = wss_server_->client_count();
        wss["frames_sent"] = wss_server_->total_frames();
        detail["wss"] = wss;
    }

    return make_response(ResponseCode::Success, "Status", detail);
}

Response ClientHandler::handle_set_parameter(const std::string& client_id, const std::string& name, const ParameterValue& value) {
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = sessions_.find(client_id);
        if (it == sessions_.end() || !it->second.connected) {
            return make_response(ResponseCode::NotReady, "Not connected");
        }
    }
    if (!sdk_manager_) {
        return make_response(ResponseCode::Error, "No SDK manager");
    }
    auto camera_ids = sdk_manager_->get_camera_ids();
    if (camera_ids.empty()) {
        return make_response(ResponseCode::Error, "No camera available");
    }
    auto client = sdk_manager_->get_client(camera_ids[0]);
    if (!client) {
        return make_response(ResponseCode::Error, "No camera client");
    }
    auto resp = client->set_parameter(name, value);
    nlohmann::json detail;
    detail["name"] = name;
    static const std::unordered_map<std::string, bool> reopen_params = {
        {"fps", true}, {"resolution", true}, {"depth_mode", true}
    };
    auto rp = reopen_params.find(name);
    detail["needs_reopen"] = (rp != reopen_params.end() && rp->second);
    if (detail["needs_reopen"] == true) {
        detail["reopen_message"] = std::string("Parameter ") + name + " requires camera reopen to take effect";
    }
    return make_response(resp.code, resp.message, detail);
}

Response ClientHandler::handle_get_parameter(const std::string& client_id, const std::string& name) {
    {
        std::lock_guard<std::mutex> lk(sessions_mutex_);
        auto it = sessions_.find(client_id);
        if (it == sessions_.end() || !it->second.connected) {
            return make_response(ResponseCode::NotReady, "Not connected");
        }
    }
    if (!sdk_manager_) {
        return make_response(ResponseCode::Error, "No SDK manager");
    }
    auto camera_ids = sdk_manager_->get_camera_ids();
    if (camera_ids.empty()) {
        return make_response(ResponseCode::Error, "No camera available");
    }
    auto client = sdk_manager_->get_client(camera_ids[0]);
    if (!client) {
        return make_response(ResponseCode::Error, "No camera client");
    }
    auto resp = client->get_parameter(name);
    return make_response(resp.code, resp.message, resp.detail);
}

} // namespace stereo_camera
