#include "stereo_camera/api/CameraSDKClient.h"
#include "stereo_camera/api/AdminServer.h"
#include "stereo_camera/api/ClientHandler.h"
#include "stereo_camera/common/Parameter.h"
#include "stereo_camera/common/Logger.h"
#include "stereo_camera/data/DataBuffer.h"
#include "stereo_camera/data/DataPublisher.h"
#include "stereo_camera/data/LoopbackSubscriber.h"
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

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    stereo_camera::Logger::instance().set_level(stereo_camera::Logger::Level::Debug);

    const std::string config_dir = (argc > 1) ? argv[1] : "./config";

    auto config_mgr = std::make_shared<stereo_camera::ConfigManager>(config_dir);
    config_mgr->load_app_config();
    auto& cfg = config_mgr->app_config();

    if (cfg.log_level == "info") {
        stereo_camera::Logger::instance().set_level(stereo_camera::Logger::Level::Info);
    } else if (cfg.log_level == "warn") {
        stereo_camera::Logger::instance().set_level(stereo_camera::Logger::Level::Warn);
    }

    size_t buffer_depth = 3;

    auto param_mgr = std::make_shared<stereo_camera::ParameterManager>();
    config_mgr->set_parameter_manager(param_mgr);
    config_mgr->load("parameters.json");

    auto buffer = std::make_shared<stereo_camera::DataBuffer>(buffer_depth);

    auto sdk_manager = std::make_shared<stereo_camera::SDKSlotManager>(buffer);

    std::vector<stereo_camera::CameraSDKConfig> sdk_configs;
    stereo_camera::CameraSDKConfig sdk_cfg;
    sdk_cfg.id = "cam1";
    sdk_cfg.base_url = "https://" + cfg.api1.host + ":" + std::to_string(cfg.api1.port);
    auto it_base = cfg.api1.channels.find("base");
    if (it_base != cfg.api1.channels.end()) {
        sdk_cfg.zmq_endpoint = it_base->second;
    } else {
        sdk_cfg.zmq_endpoint = "ipc:///tmp/zed_vision";
    }
    sdk_cfg.channels = cfg.api1.channels;
    sdk_configs.push_back(sdk_cfg);
    sdk_manager->configure(sdk_configs);

    auto publisher = std::make_shared<stereo_camera::DataPublisher>(buffer, cfg.api2.data.pub_endpoints);

    auto loopback_buffer = std::make_shared<stereo_camera::DataBuffer>(buffer_depth);
    auto loopback_sub = std::make_shared<stereo_camera::LoopbackSubscriber>(
        loopback_buffer, cfg.api3.data.sub_endpoints);

    sdk_manager->set_data_callback([&publisher](const std::string& cam_id,
        const std::shared_ptr<stereo_camera::DataBundle>& bundle) {
        (void)cam_id;
        (void)bundle;
    });

    auto client_handler = std::make_shared<stereo_camera::ClientHandler>();
    client_handler->set_sdk_manager(sdk_manager);

    const std::string& api3_cert = cfg.api3.admin_server.cert_path;
    const std::string& api3_key = cfg.api3.admin_server.key_path;
    std::string cert_path = api3_cert.empty() ? config_dir + "/server.crt" : api3_cert;
    std::string key_path = api3_key.empty() ? config_dir + "/server.key" : api3_key;

    stereo_camera::AdminServer server(
        cfg.api3.admin_server.host,
        static_cast<uint16_t>(cfg.api3.admin_server.port),
        cert_path, key_path);
    server.set_client_handler(client_handler);
    server.set_parameter_manager(param_mgr);
    server.start();

    stereo_camera::AdminServer api2_server(
        cfg.api2.server.host,
        static_cast<uint16_t>(cfg.api2.server.port),
        cert_path, key_path);
    api2_server.set_client_handler(client_handler);
    api2_server.set_parameter_manager(param_mgr);
    api2_server.start();

    publisher->start();
    loopback_sub->start();

    stereo_camera::Logger::instance().info("Main", "StereoCamera node started");
    stereo_camera::Logger::instance().info("Main", std::string("API 3a HTTPS: ") + cfg.api3.admin_server.host + ":" + std::to_string(cfg.api3.admin_server.port));
    stereo_camera::Logger::instance().info("Main", std::string("API 2a HTTPS: ") + cfg.api2.server.host + ":" + std::to_string(cfg.api2.server.port));
    stereo_camera::Logger::instance().info("Main", "API 2b PUB endpoints: " +
        std::to_string(cfg.api2.data.pub_endpoints.size()));
    stereo_camera::Logger::instance().info("Main", "API 1a Camera SDK: " +
        sdk_cfg.base_url);
    stereo_camera::Logger::instance().info("Main", "API 3b-1 LoopbackSubscriber: " +
        std::to_string(cfg.api3.data.sub_endpoints.size()) + " channels");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    loopback_sub->stop();
    publisher->stop();
    sdk_manager->stop_all();
    config_mgr->sync();
    api2_server.stop();
    server.stop();
    stereo_camera::Logger::instance().info("Main", "StereoCamera node stopped");
    return 0;
}
