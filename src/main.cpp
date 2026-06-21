#include "stereo_camera/api/CameraSDKClient.h"
#include "stereo_camera/api/AdminServer.h"
#include "stereo_camera/api/ClientHandler.h"
#include "stereo_camera/common/Parameter.h"
#include "stereo_camera/common/Logger.h"
#include "stereo_camera/data/DataBuffer.h"
#include "stereo_camera/data/DataPipeline.h"
#include "stereo_camera/data/DataPublisher.h"
#include "stereo_camera/data/LoopbackSubscriber.h"
#include "stereo_camera/data/WSServer.h"
#include "stereo_camera/data/SDKSlotManager.h"
#include "stereo_camera/data/ConfigManager.h"
#include <csignal>
#include <thread>
#include <atomic>
#include <iostream>
#include <fstream>
#include <zmq.h>

static std::atomic<bool> g_running{true};
static void signal_handler(int) { g_running = false; }

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    stereo_camera::Logger::instance().set_level(stereo_camera::Logger::Level::Debug);

    const std::string config_dir = (argc > 1) ? argv[1] : "./config";
    auto config_mgr = std::make_shared<stereo_camera::ConfigManager>(config_dir);
    config_mgr->load_app_config();
    auto& cfg = config_mgr->app_config();

    if (cfg.log_level == "info")
        stereo_camera::Logger::instance().set_level(stereo_camera::Logger::Level::Info);
    else if (cfg.log_level == "warn")
        stereo_camera::Logger::instance().set_level(stereo_camera::Logger::Level::Warn);

    auto param_mgr = std::make_shared<stereo_camera::ParameterManager>();
    config_mgr->set_parameter_manager(param_mgr);
    config_mgr->load("parameters.json");

    // DataBuffer #1 — dealer_loop pushes here (direct access)
    auto buffer1 = std::make_shared<stereo_camera::DataBuffer>(cfg.data_buffer_depth);

    // SPSC Pipeline — dealer_loop → DataPublisher
    auto pipeline = std::make_shared<stereo_camera::DataPipeline>();
    int qs = cfg.spsc.queue_size;
    pipeline->queue_2d.set_max_depth(qs);
    pipeline->queue_3d.set_max_depth(qs);
    pipeline->queue_sensor.set_max_depth(qs);

    // SDKSlotManager — pushes to DataBuffer #1 + SPSC Pipeline
    auto sdk_manager = std::make_shared<stereo_camera::SDKSlotManager>(buffer1);
    sdk_manager->set_data_pipeline(pipeline);

    std::vector<stereo_camera::CameraSDKConfig> sdk_configs;
    stereo_camera::CameraSDKConfig sdk_cfg;
    sdk_cfg.id = "cam1";
    sdk_cfg.base_url = "https://" + cfg.api1.host + ":" + std::to_string(cfg.api1.port);
    auto it_base = cfg.api1.channels.find("base");
    sdk_cfg.zmq_endpoint = (it_base != cfg.api1.channels.end()) ? it_base->second : "ipc:///tmp/zed_vision";
    sdk_cfg.channels = cfg.api1.channels;
    sdk_configs.push_back(sdk_cfg);
    sdk_manager->configure(sdk_configs);

    void* shared_zmq_ctx = zmq_ctx_new();
    sdk_manager->set_zmq_context(shared_zmq_ctx);

    // DataPublisher — reads from SPSC Pipeline, publishes 3 grouped ZMQ PUB channels
    auto publisher = std::make_shared<stereo_camera::DataPublisher>(
        pipeline, cfg.api2.data.pub_endpoints, cfg.api2.data.zmq_hwm);
    publisher->set_zmq_context(shared_zmq_ctx);

    // DataBuffer #2 — LoopbackSubscriber → WSServer
    auto buffer2 = std::make_shared<stereo_camera::DataBuffer>(cfg.data_buffer_depth);

    // LoopbackSubscriber — subscribes to 3 grouped PUB channels → DataBuffer #2
    stereo_camera::LoopbackSubscriber loopback(buffer2, cfg.api3.data.sub_endpoints,
                                                cfg.api3.data.zmq_hwm);
    loopback.set_zmq_context(shared_zmq_ctx);

    // TLS certs
    const std::string& api3_cert = cfg.api3.admin_server.cert_path;
    const std::string& api3_key = cfg.api3.admin_server.key_path;
    std::string cert_path = api3_cert.empty() ? config_dir + "/server.crt" : api3_cert;
    std::string key_path = api3_key.empty() ? config_dir + "/server.key" : api3_key;

    // WSServer — reads from DataBuffer #2
    stereo_camera::WSServer wss_server(buffer2,
        cfg.api3.data.wss_server.host,
        static_cast<uint16_t>(cfg.api3.data.wss_server.port),
        cert_path, key_path);

    auto client_handler = std::make_shared<stereo_camera::ClientHandler>();
    client_handler->set_sdk_manager(sdk_manager);

    stereo_camera::AdminServer server(
        cfg.api3.admin_server.host,
        static_cast<uint16_t>(cfg.api3.admin_server.port),
        cert_path, key_path,
        cfg.api3.admin_server.worker_threads);
    server.set_client_handler(client_handler);
    server.set_parameter_manager(param_mgr);
    server.start();

    stereo_camera::AdminServer api2_server(
        cfg.api2.server.host,
        static_cast<uint16_t>(cfg.api2.server.port),
        cert_path, key_path,
        cfg.api2.server.worker_threads);
    api2_server.set_client_handler(client_handler);
    api2_server.set_parameter_manager(param_mgr);
    api2_server.start();

    publisher->start();
    loopback.start();
    wss_server.start();

    stereo_camera::Logger::instance().info("Main", "StereoCamera node started");
    stereo_camera::Logger::instance().info("Main", "API 3a HTTPS: " + cfg.api3.admin_server.host + ":" + std::to_string(cfg.api3.admin_server.port));
    stereo_camera::Logger::instance().info("Main", "API 2a HTTPS: " + cfg.api2.server.host + ":" + std::to_string(cfg.api2.server.port));
    stereo_camera::Logger::instance().info("Main", "API 2b PUB: " + std::to_string(cfg.api2.data.pub_endpoints.size()) + " grouped channels (HWM=" + std::to_string(cfg.api2.data.zmq_hwm) + ")");
    stereo_camera::Logger::instance().info("Main", "API 1a Camera SDK: " + sdk_cfg.base_url);
    stereo_camera::Logger::instance().info("Main", "API 3b-1 LoopbackSubscriber: " + std::to_string(cfg.api3.data.sub_endpoints.size()) + " grouped channels");
    stereo_camera::Logger::instance().info("Main", "API 3b-2 WSS: " + cfg.api3.data.wss_server.host + ":" + std::to_string(cfg.api3.data.wss_server.port));
    stereo_camera::Logger::instance().info("Main", "SPSC: queue_size=" + std::to_string(qs) + ", drop_policy=" + cfg.spsc.drop_policy);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    wss_server.stop();
    loopback.stop();
    publisher->stop();
    sdk_manager->stop_all();
    if (shared_zmq_ctx) {
        zmq_ctx_destroy(shared_zmq_ctx);
        shared_zmq_ctx = nullptr;
    }
    config_mgr->sync();
    api2_server.stop();
    server.stop();
    stereo_camera::Logger::instance().info("Main", "StereoCamera node stopped");
    return 0;
}
