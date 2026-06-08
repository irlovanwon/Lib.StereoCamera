#include "stereo_camera/api/ClientHandler.h"
#include "stereo_camera/common/Logger.h"

namespace stereo_camera {

Response ClientHandler::handle_init(const std::string& client_id) {
    if (module_initialized_) {
        Logger::instance().info("ClientHandler", "Init: already initialized, returning success for " + client_id);
        return make_response(ResponseCode::AlreadyInit, "Already initialized");
    }
    module_initialized_ = true;
    Logger::instance().info("ClientHandler", "Init: module initialized by " + client_id);
    return make_response(ResponseCode::Success, "Initialized");
}

Response ClientHandler::handle_dispose(const std::string& client_id) {
    if (!module_initialized_) {
        return make_response(ResponseCode::NotReady, "Not initialized");
    }
    module_initialized_ = false;
    sessions_.clear();
    Logger::instance().info("ClientHandler", "Dispose by " + client_id);
    return make_response(ResponseCode::Success, "Disposed");
}

Response ClientHandler::handle_connect(const std::string& client_id) {
    if (!module_initialized_) {
        return make_response(ResponseCode::NotReady, "Not initialized");
    }
    auto& session = sessions_[client_id];
    if (session.connected) {
        return make_response(ResponseCode::AlreadyInit, "Already connected");
    }
    session.id = client_id;
    session.connected = true;
    Logger::instance().info("ClientHandler", "Connect: " + client_id);
    return make_response(ResponseCode::Success, "Connected");
}

Response ClientHandler::handle_disconnect(const std::string& client_id) {
    auto it = sessions_.find(client_id);
    if (it == sessions_.end() || !it->second.connected) {
        return make_response(ResponseCode::NotReady, "Not connected");
    }
    it->second.connected = false;
    Logger::instance().info("ClientHandler", "Disconnect: " + client_id);
    return make_response(ResponseCode::Success, "Disconnected");
}

Response ClientHandler::handle_start_capture(const std::string& client_id, const std::vector<DataType>& types) {
    auto it = sessions_.find(client_id);
    if (it == sessions_.end() || !it->second.connected) {
        return make_response(ResponseCode::NotReady, "Not connected");
    }
    Logger::instance().info("ClientHandler", "StartCapture: " + client_id);
    return make_response(ResponseCode::Success, "Capture started");
}

Response ClientHandler::handle_stop_capture(const std::string& client_id, const std::vector<DataType>& types) {
    auto it = sessions_.find(client_id);
    if (it == sessions_.end() || !it->second.connected) {
        return make_response(ResponseCode::NotReady, "Not connected");
    }
    Logger::instance().info("ClientHandler", "StopCapture: " + client_id);
    return make_response(ResponseCode::Success, "Capture stopped");
}

Response ClientHandler::handle_check_status(const std::string& client_id) {
    nlohmann::json detail;
    detail["initialized"] = module_initialized_;
    detail["sessions"] = sessions_.size();
    return make_response(ResponseCode::Success, "Status", detail);
}

Response ClientHandler::handle_set_parameter(const std::string& client_id, const std::string& name, const ParameterValue& value) {
    auto it = sessions_.find(client_id);
    if (it == sessions_.end() || !it->second.connected) {
        return make_response(ResponseCode::NotReady, "Not connected");
    }
    return make_response(ResponseCode::Success, "Parameter set");
}

Response ClientHandler::handle_get_parameter(const std::string& client_id, const std::string& name) {
    auto it = sessions_.find(client_id);
    if (it == sessions_.end() || !it->second.connected) {
        return make_response(ResponseCode::NotReady, "Not connected");
    }
    return make_response(ResponseCode::Success, "Parameter get");
}

} // namespace stereo_camera
