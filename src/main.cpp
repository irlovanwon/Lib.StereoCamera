#include "stereo_camera/api/CameraSDKClient.h"
#include "stereo_camera/api/AdminServer.h"
#include "stereo_camera/api/ClientHandler.h"
#include "stereo_camera/common/Parameter.h"
#include "stereo_camera/common/Logger.h"
#include "stereo_camera/data/DataBuffer.h"
#include "stereo_camera/data/DataPipeline.h"
#include "stereo_camera/data/DataPublisher.h"
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
    publisher->set_publish_cv_timeout_ms(cfg.spsc.publish_cv_timeout_ms);

    auto buffer2 = std::make_shared<stereo_camera::DataBuffer>(cfg.data_buffer_depth);


    // TLS certs
    const std::string& api3_cert = cfg.api3.admin_server.cert_path;
    const std::string& api3_key = cfg.api3.admin_server.key_path;
    std::string cert_path = api3_cert.empty() ? config_dir + "/server.crt" : api3_cert;
    std::string key_path = api3_key.empty() ? config_dir + "/server.key" : api3_key;

    // WSServer — reads from DataBuffer #2 + 3 encode SPSC queues
    stereo_camera::WSServer wss_server(buffer2,
        cfg.api3.data.wss_server.host,
        static_cast<uint16_t>(cfg.api3.data.wss_server.port),
        cert_path, key_path);
    wss_server.set_encode_queue_depth(qs);
    wss_server.set_cv_timeout_ms(cfg.spsc.publish_cv_timeout_ms, cfg.spsc.encode_cv_timeout_ms, cfg.spsc.send_cv_timeout_ms);


    auto client_handler = std::make_shared<stereo_camera::ClientHandler>();
    client_handler->set_sdk_manager(sdk_manager);
    client_handler->set_data_pipeline(pipeline);
    client_handler->set_buffer2(buffer2);
    client_handler->set_wss_server(&wss_server);

    stereo_camera::AdminServer server(
        cfg.api3.admin_server.host,
        static_cast<uint16_t>(cfg.api3.admin_server.port),
        cert_path, key_path,
        cfg.api3.admin_server.worker_threads);
    server.set_client_handler(client_handler);
    server.set_parameter_manager(param_mgr);
    // Auto-initialize: connect to all camera SDKs at startup (Init/Dispose are internal)
    sdk_manager->start_all();
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

    // Feed API3 encode SPSC + DataBuffer #2 directly from DataPublisher (no ZMQ loopback)
    publisher->set_pub_callback([&](const stereo_camera::ChannelFrame& frame) {
        for (const auto& b : frame.bundles) {
            buffer2->push(frame.camera_id, b);
        }
        if (!frame.bundles.empty()) {
            wss_server.push_encode(stereo_camera::data_type_to_group(frame.bundles[0]->type), frame);
        }
    });
    wss_server.start();

    // Auto-cleanup: when all WebSocket clients disconnect, force-stop all captures
    // to prevent orphaned subscriber counts and wasted CPU
    wss_server.set_on_all_disconnected([&sdk_manager, &buffer2]() {
        stereo_camera::Logger::instance().info("Main", "WS cleanup callback — force stopping all captures");
        // force_stop removed
        buffer2->clear();
    });

    stereo_camera::Logger::instance().info("Main", "StereoCamera node started");
    stereo_camera::Logger::instance().info("Main", "API 3a HTTPS: " + cfg.api3.admin_server.host + ":" + std::to_string(cfg.api3.admin_server.port));
    stereo_camera::Logger::instance().info("Main", "API 2a HTTPS: " + cfg.api2.server.host + ":" + std::to_string(cfg.api2.server.port));
    stereo_camera::Logger::instance().info("Main", "API 2b PUB: " + std::to_string(cfg.api2.data.pub_endpoints.size()) + " grouped channels (HWM=" + std::to_string(cfg.api2.data.zmq_hwm) + ")");
    stereo_camera::Logger::instance().info("Main", "API 1a Camera SDK: " + sdk_cfg.base_url);
    stereo_camera::Logger::instance().info("Main", "API 3b-2 WSS: " + cfg.api3.data.wss_server.host + ":" + std::to_string(cfg.api3.data.wss_server.port));
    stereo_camera::Logger::instance().info("Main", "SPSC: queue_size=" + std::to_string(qs) + ", drop_policy=" + cfg.spsc.drop_policy);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // === Service restart notification ===
    stereo_camera::Logger::instance().info("Main", "Broadcasting shutdown notification to all clients...");
    wss_server.broadcast(R"({"event":"server_shutdown","message":"Service restarting"})");
    publisher->publish_shutdown();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    wss_server.stop();
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
