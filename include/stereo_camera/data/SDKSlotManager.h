#pragma once

#include "stereo_camera/common/Types.h"
#include "stereo_camera/api/CameraSDKClient.h"
#include "stereo_camera/data/DataBuffer.h"
#include "stereo_camera/data/DataPipeline.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

namespace stereo_camera {

struct SDKSlot {
    std::string camera_id;
    std::string zmq_endpoint;
    std::unordered_map<std::string, std::string> channels;
    std::shared_ptr<CameraSDKClient> client;
    std::unique_ptr<std::thread> dealer_thread;
    std::atomic<bool> capturing{false};
    int subscriber_count = 0;
    std::vector<DataType> active_types;
    std::vector<std::string> active_channels;
};

struct CameraSDKConfig {
    std::string id;
    std::string base_url;
    std::string zmq_endpoint;
    std::unordered_map<std::string, std::string> channels;
};

class SDKSlotManager {
public:
    explicit SDKSlotManager(std::shared_ptr<DataBuffer> buffer);
    ~SDKSlotManager();

    void configure(const std::vector<CameraSDKConfig>& configs);

    bool start_capture(const std::string& camera_id, const std::vector<DataType>& types);
    bool stop_capture(const std::string& camera_id, const std::vector<DataType>& types);
    void force_stop_all_captures();

    std::shared_ptr<CameraSDKClient> get_client(const std::string& camera_id) const;
    std::vector<std::string> get_camera_ids() const;
    nlohmann::json get_status() const;

    void start_all();
    void stop_all();

    using DataReceivedCallback = std::function<void(const std::string& camera_id, const std::shared_ptr<DataBundle>&)>;
    void set_data_callback(DataReceivedCallback callback);
    void set_zmq_context(void* ctx) { shared_zmq_ctx_ = ctx; }
    void set_data_pipeline(std::shared_ptr<DataPipeline> p) { pipeline_ = std::move(p); }

private:
    void dealer_loop(const std::string& camera_id, std::unordered_map<std::string, std::string> channels);
    bool capturing_active(const std::string& camera_id) const;
    static DataType channel_to_datatype(const std::string& channel);

    mutable std::mutex mutex_;
    std::shared_ptr<DataBuffer> buffer_;
    std::unordered_map<std::string, std::unique_ptr<SDKSlot>> slots_;
    std::atomic<bool> running_{false};
    DataReceivedCallback data_callback_;
    void* shared_zmq_ctx_ = nullptr;
    std::shared_ptr<DataPipeline> pipeline_;
};

} // namespace stereo_camera
