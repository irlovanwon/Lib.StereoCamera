#pragma once

#include "stereo_camera/common/Parameter.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace stereo_camera {

struct Api1Config {
    std::string host;
    int port;
    std::string cert_path;
    int timeout_ms;
    std::string transport;
    std::unordered_map<std::string, std::string> channels;
};

struct Api2ServerConfig {
    std::string host;
    int port;
    std::string cert_path;
    std::string key_path;
    std::string thread_strategy;
    int worker_threads;
};

struct Api2DataConfig {
    std::string transport;
    std::unordered_map<std::string, std::string> pub_endpoints;
};

struct Api2Config {
    Api2ServerConfig server;
    Api2DataConfig data;
};

struct Api3AdminConfig {
    std::string host;
    int port;
    std::string cert_path;
    std::string key_path;
    std::string thread_strategy;
    int worker_threads;
};

struct Api3WssConfig {
    std::string host;
    int port;
    std::string cert_path;
    std::string key_path;
    std::string thread_strategy;
    int worker_threads;
};

struct Api3DataConfig {
    Api3WssConfig wss_server;
    std::unordered_map<std::string, std::string> sub_endpoints;
};

struct Api3Config {
    Api3AdminConfig admin_server;
    Api3DataConfig data;
};

struct AppConfig {
    Api1Config api1;
    Api2Config api2;
    Api3Config api3;
    std::string log_level;
};

class ConfigManager {
public:
    explicit ConfigManager(const std::string& config_dir);

    void set_parameter_manager(std::shared_ptr<ParameterManager> param_mgr);
    bool load(const std::string& filename);
    bool save(const std::string& filename);
    bool sync();
    bool load_app_config();

    const AppConfig& app_config() const { return app_config_; }
    AppConfig& app_config() { return app_config_; }

private:
    std::string config_dir_;
    std::shared_ptr<ParameterManager> param_mgr_;
    AppConfig app_config_;
};

} // namespace stereo_camera
