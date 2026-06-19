#pragma once

#include "stereo_camera/common/Types.h"
#include "stereo_camera/common/Response.h"
#include <string>
#include <memory>

namespace stereo_camera {

class SDKSlotManager;

struct ClientSession {
    std::string id;
    bool connected = false;
};

class ClientHandler {
public:
    ClientHandler() = default;

    void set_sdk_manager(std::shared_ptr<SDKSlotManager> mgr);

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
    std::unordered_map<std::string, ClientSession> sessions_;
    bool module_initialized_ = false;
};

} // namespace stereo_camera
