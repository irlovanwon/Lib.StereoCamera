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
    int zmq_hwm = 2;
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
    int zmq_hwm = 1;
    std::unordered_map<std::string, std::string> sub_endpoints;
};

struct Api3Config {
    Api3AdminConfig admin_server;
    Api3DataConfig data;
};

struct SpscConfig {
    int queue_size = 4;
    std::string drop_policy = "newest";
    int publish_cv_timeout_ms = 10;
    int encode_cv_timeout_ms = 10;
    int send_cv_timeout_ms = 10;
    int gst_encode_timeout_ms = 1000;
};

struct AppConfig {
    Api1Config api1;
    Api2Config api2;
    Api3Config api3;
    SpscConfig spsc;
    int data_buffer_depth = 3;
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
