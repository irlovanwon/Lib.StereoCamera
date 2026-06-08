#include "stereo_camera/api/CameraSDKClient.h"
#include "stereo_camera/api/AdminServer.h"
#include "stereo_camera/api/ClientHandler.h"
#include "stereo_camera/common/Parameter.h"
#include "stereo_camera/common/Logger.h"
#include "stereo_camera/data/DataBuffer.h"
#include "stereo_camera/data/DataPublisher.h"
#include "stereo_camera/data/SDKSlotManager.h"
#include "stereo_camera/data/ConfigManager.h"
#include <csignal>
#include <thread>
#include <atomic>
#include <iostream>
#include <fstream>

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

static std::vector<stereo_camera::CameraSDKConfig> load_sdk_configs(const std::string& config_dir) {
    std::vector<stereo_camera::CameraSDKConfig> configs;
    std::string path = config_dir + "/default.json";
    std::ifstream file(path);
    if (!file.is_open()) {
        stereo_camera::Logger::instance().warn("Main", "No config file: " + path);
        return configs;
    }
    try {
        auto json = nlohmann::json::parse(file);
        if (json.contains("camera_sdks") && json["camera_sdks"].is_array()) {
            for (const auto& sdk : json["camera_sdks"]) {
                stereo_camera::CameraSDKConfig cfg;
                cfg.id = sdk.value("id", "");
                cfg.base_url = sdk.value("base_url", "");
                cfg.zmq_endpoint = sdk.value("zmq_endpoint", "");
                if (!cfg.id.empty() && !cfg.base_url.empty() && !cfg.zmq_endpoint.empty()) {
                    configs.push_back(cfg);
                }
            }
        }
    } catch (const std::exception& e) {
        stereo_camera::Logger::instance().error("Main", std::string("Config parse error: ") + e.what());
    }
    return configs;
}

static size_t load_buffer_depth(const std::string& config_dir) {
    std::string path = config_dir + "/default.json";
    std::ifstream file(path);
    if (!file.is_open()) return 3;
    try {
        auto json = nlohmann::json::parse(file);
        if (json.contains("buffer")) {
            return json["buffer"].value("max_frames_per_slot", 3);
        }
    } catch (...) {}
    return 3;
}

static std::string load_pub_endpoint(const std::string& config_dir) {
    std::string path = config_dir + "/default.json";
    std::ifstream file(path);
    if (!file.is_open()) return "tcp://*:5556";
    try {
        auto json = nlohmann::json::parse(file);
        if (json.contains("pub")) {
            return json["pub"].value("endpoint", "tcp://*:5556");
        }
    } catch (...) {}
    return "tcp://*:5556";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    stereo_camera::Logger::instance().set_level(stereo_camera::Logger::Level::Debug);

    const std::string config_dir = (argc > 1) ? argv[1] : "./config";
    const std::string cert_path = (argc > 2) ? argv[2] : "./config/server.crt";
    const std::string key_path  = (argc > 3) ? argv[3] : "./config/server.key";

    size_t buffer_depth = load_buffer_depth(config_dir);
    std::string pub_endpoint = load_pub_endpoint(config_dir);
    auto sdk_configs = load_sdk_configs(config_dir);

    auto param_mgr = std::make_shared<stereo_camera::ParameterManager>();
    stereo_camera::Parameter fps_param;
    fps_param.name = "fps";
    fps_param.type = stereo_camera::ParameterType::Integer;
    fps_param.value.int_val = 30;
    fps_param.is_readonly = false;
    fps_param.is_available = true;
    param_mgr->define(fps_param);

    stereo_camera::Parameter exposure_param;
    exposure_param.name = "exposure_time";
    exposure_param.type = stereo_camera::ParameterType::Integer;
    exposure_param.value.int_val = 10000000;
    exposure_param.is_readonly = false;
    exposure_param.is_available = true;
    param_mgr->define(exposure_param);

    stereo_camera::Parameter gain_param;
    gain_param.name = "gain";
    gain_param.type = stereo_camera::ParameterType::Float;
    gain_param.value.float_val = 1.0;
    gain_param.is_readonly = false;
    gain_param.is_available = true;
    param_mgr->define(gain_param);

    stereo_camera::Parameter auto_exposure;
    auto_exposure.name = "auto_exposure";
    auto_exposure.type = stereo_camera::ParameterType::Enum;
    auto_exposure.value.enum_val = "Off";
    auto_exposure.is_readonly = false;
    auto_exposure.is_available = true;
    param_mgr->define(auto_exposure);

    stereo_camera::Parameter auto_gain;
    auto_gain.name = "auto_gain";
    auto_gain.type = stereo_camera::ParameterType::Enum;
    auto_gain.value.enum_val = "Off";
    auto_gain.is_readonly = false;
    auto_gain.is_available = true;
    param_mgr->define(auto_gain);

    auto config_mgr = std::make_shared<stereo_camera::ConfigManager>(config_dir);
    config_mgr->set_parameter_manager(param_mgr);
    config_mgr->load("parameters.json");

    auto buffer = std::make_shared<stereo_camera::DataBuffer>(buffer_depth);
    auto sdk_manager = std::make_shared<stereo_camera::SDKSlotManager>(buffer);
    auto publisher = std::make_shared<stereo_camera::DataPublisher>(buffer, pub_endpoint);

    sdk_manager->configure(sdk_configs);
    sdk_manager->set_data_callback([](const std::string& cam_id, const std::shared_ptr<stereo_camera::DataBundle>&) {
    });

    auto client_handler = std::make_shared<stereo_camera::ClientHandler>();

    stereo_camera::AdminServer server("0.0.0.0", 9443, cert_path, key_path);
    server.set_client_handler(client_handler);
    server.set_parameter_manager(param_mgr);
    server.start();

    publisher->start();

    stereo_camera::Logger::instance().info("Main", "StereoCamera node started");
    stereo_camera::Logger::instance().info("Main", "Buffer depth: " + std::to_string(buffer_depth));
    stereo_camera::Logger::instance().info("Main", "PUB endpoint: " + pub_endpoint);
    stereo_camera::Logger::instance().info("Main", "SDKs configured: " + std::to_string(sdk_configs.size()));
    std::cout << "StereoCamera module running. Press Ctrl+C to stop." << std::endl;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    publisher->stop();
    sdk_manager->stop_all();
    config_mgr->sync();
    server.stop();
    stereo_camera::Logger::instance().info("Main", "StereoCamera node stopped");
    return 0;
}
