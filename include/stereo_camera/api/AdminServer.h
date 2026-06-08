#pragma once

#include "stereo_camera/api/ClientHandler.h"
#include "stereo_camera/common/Parameter.h"
#include <string>
#include <cstdint>
#include <memory>

namespace stereo_camera {

class AdminServer {
public:
    AdminServer(const std::string& host, uint16_t port,
                const std::string& cert_path, const std::string& key_path);
    ~AdminServer();

    void set_client_handler(std::shared_ptr<ClientHandler> handler);
    void set_parameter_manager(std::shared_ptr<ParameterManager> param_mgr);
    void start();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace stereo_camera
