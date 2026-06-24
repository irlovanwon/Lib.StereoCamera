#pragma once

#include "stereo_camera/common/Types.h"
#include "stereo_camera/common/Response.h"
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>

namespace stereo_camera {

class SDKSlotManager;
class DataPipeline;
class DataBuffer;
class WSServer;

struct ClientSession {
    std::string id;
    bool connected = false;
    std::vector<DataType> active_types;
    std::string camera_id;
};

class ClientHandler {
public:
    ClientHandler() = default;

    void set_sdk_manager(std::shared_ptr<SDKSlotManager> mgr);
    void set_data_pipeline(std::shared_ptr<DataPipeline> p) { pipeline_ = std::move(p); }
    void set_buffer2(std::shared_ptr<DataBuffer> b) { buffer2_ = std::move(b); }
    void set_wss_server(WSServer* ws) { wss_server_ = ws; }

    Response handle_init(const std::string& client_id);
    Response handle_dispose(const std::string& client_id);
    Response handle_connect(const std::string& client_id);
    Response handle_disconnect(const std::string& client_id);
    Response handle_start_capture(const std::string& client_id, const std::vector<DataType>& types);
    Response handle_stop_capture(const std::string& client_id, const std::vector<DataType>& types);
    Response handle_check_status(const std::string& client_id);
    Response handle_set_parameter(const std::string& client_id, const std::string& name, const ParameterValue& value);
    Response handle_get_parameter(const std::string& client_id, const std::string& name);

private:
    std::shared_ptr<SDKSlotManager> sdk_manager_;
    std::shared_ptr<DataPipeline> pipeline_;
    std::shared_ptr<DataBuffer> buffer2_;
    WSServer* wss_server_ = nullptr;
    std::mutex sessions_mutex_;
    std::unordered_map<std::string, ClientSession> sessions_;
    std::atomic<bool> module_initialized_{true};
};

} // namespace stereo_camera
