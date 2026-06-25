#include "stereo_camera/data/ConfigManager.h"
#include "stereo_camera/common/Logger.h"
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace stereo_camera {

namespace fs = std::filesystem;

ConfigManager::ConfigManager(const std::string& config_dir)
    : config_dir_(config_dir) {
    fs::create_directories(config_dir_);
}

void ConfigManager::set_parameter_manager(std::shared_ptr<ParameterManager> param_mgr) {
    param_mgr_ = std::move(param_mgr);
}

bool ConfigManager::load(const std::string& filename) {
    std::string full_path = config_dir_ + "/" + filename;
    std::ifstream file(full_path);
    if (!file) {
        Logger::instance().warn("ConfigManager", "Config file not found: " + full_path);
        return false;
    }
    try {
        auto j = nlohmann::json::parse(file);
        if (param_mgr_) {
            param_mgr_->from_json(j);
        }
        Logger::instance().info("ConfigManager", "Loaded config: " + full_path);
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("ConfigManager", std::string("Parse error: ") + e.what());
        return false;
    }
}

bool ConfigManager::save(const std::string& filename) {
    if (!param_mgr_) return false;
    std::string full_path = config_dir_ + "/" + filename;
    std::ofstream file(full_path);
    if (!file) {
        Logger::instance().error("ConfigManager", "Failed to write: " + full_path);
        return false;
    }
    file << param_mgr_->to_json().dump(4);
    Logger::instance().info("ConfigManager", "Saved config: " + full_path);
    return true;
}

bool ConfigManager::sync() {
    return save("parameters.json");
}

bool ConfigManager::load_app_config() {
    std::string path = config_dir_ + "/config.json";
    std::ifstream file(path);
    if (!file) {
        Logger::instance().warn("ConfigManager", "config.json not found, using defaults");
        return false;
    }
    try {
        auto j = nlohmann::json::parse(file);

        if (j.contains("api1")) {
            auto& a1 = j["api1"];
            if (a1.contains("camera_sdk")) {
                auto& cs = a1["camera_sdk"];
                app_config_.api1.host = cs.value("host", "127.0.0.1");
                app_config_.api1.port = cs.value("port", 8443);
                app_config_.api1.cert_path = cs.value("cert_path", "");
                app_config_.api1.timeout_ms = cs.value("timeout_ms", 5000);
            }
            if (a1.contains("data")) {
                auto& d = a1["data"];
                app_config_.api1.transport = d.value("transport", "ipc");
                if (d.contains("channels") && d["channels"].is_object()) {
                    for (auto& [key, val] : d["channels"].items()) {
                        app_config_.api1.channels[key] = val.get<std::string>();
                    }
                }
            }
        }

        if (j.contains("api2")) {
            auto& a2 = j["api2"];
            if (a2.contains("server")) {
                auto& s = a2["server"];
                app_config_.api2.server.host = s.value("host", "0.0.0.0");
                app_config_.api2.server.port = s.value("port", 8444);
                app_config_.api2.server.cert_path = s.value("cert_path", "");
                app_config_.api2.server.key_path = s.value("key_path", "");
                app_config_.api2.server.thread_strategy = s.value("thread_strategy", "hybrid");
                app_config_.api2.server.worker_threads = s.value("worker_threads", 4);
            }
            if (a2.contains("data")) {
                auto& d = a2["data"];
                app_config_.api2.data.transport = d.value("transport", "ipc");
                app_config_.api2.data.zmq_hwm = d.value("zmq_hwm", 2);
                if (d.contains("pub_endpoints") && d["pub_endpoints"].is_object()) {
                    for (auto& [key, val] : d["pub_endpoints"].items()) {
                        app_config_.api2.data.pub_endpoints[key] = val.get<std::string>();
                    }
                }
            }
        }

        if (j.contains("api3")) {
            auto& a3 = j["api3"];
            if (a3.contains("admin_server")) {
                auto& s = a3["admin_server"];
                app_config_.api3.admin_server.host = s.value("host", "127.0.0.1");
                app_config_.api3.admin_server.port = s.value("port", 9443);
                app_config_.api3.admin_server.cert_path = s.value("cert_path", "");
                app_config_.api3.admin_server.key_path = s.value("key_path", "");
                app_config_.api3.admin_server.thread_strategy = s.value("thread_strategy", "hybrid");
                app_config_.api3.admin_server.worker_threads = s.value("worker_threads", 4);
            }
            if (a3.contains("data")) {
                auto& d = a3["data"];
                app_config_.api3.data.zmq_hwm = d.value("zmq_hwm", 1);
                if (d.contains("wss_server")) {
                    auto& w = d["wss_server"];
                    app_config_.api3.data.wss_server.host = w.value("host", "127.0.0.1");
                    app_config_.api3.data.wss_server.port = w.value("port", 9444);
                    app_config_.api3.data.wss_server.cert_path = w.value("cert_path", "");
                    app_config_.api3.data.wss_server.key_path = w.value("key_path", "");
                    app_config_.api3.data.wss_server.thread_strategy = w.value("thread_strategy", "one_loop_per_thread");
                    app_config_.api3.data.wss_server.worker_threads = w.value("worker_threads", 2);
                }
                if (d.contains("sub_endpoints") && d["sub_endpoints"].is_object()) {
                    for (auto& [key, val] : d["sub_endpoints"].items()) {
                        app_config_.api3.data.sub_endpoints[key] = val.get<std::string>();
                    }
                }
            }
        }

        app_config_.log_level = j.value("log_level", "info");
        Logger::instance().info("ConfigManager", "Loaded config.json");
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("ConfigManager", std::string("config.json parse error: ") + e.what());
        return false;
    }
}

} // namespace stereo_camera
