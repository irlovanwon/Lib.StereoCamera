#include "stereo_camera/data/LoopbackSubscriber.h"
#include "stereo_camera/common/Logger.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstring>

namespace stereo_camera {

DataType LoopbackSubscriber::channel_to_type(const std::string& id) {
    try { return nlohmann::json(id).get<DataType>(); }
    catch (...) { return DataType::IMU; }
}

LoopbackSubscriber::LoopbackSubscriber(std::shared_ptr<DataBuffer> buffer,
                                       const std::unordered_map<std::string, std::string>& sub_endpoints,
                                       int zmq_hwm)
    : buffer_(std::move(buffer)), sub_endpoints_(sub_endpoints), zmq_hwm_(zmq_hwm) {}

LoopbackSubscriber::~LoopbackSubscriber() { stop(); }

void LoopbackSubscriber::start() {
    if (running_.load()) return;
    if (shared_ctx_) { zmq_ctx_ = shared_ctx_; owns_ctx_ = false; }
    else { zmq_ctx_ = zmq_ctx_new(); owns_ctx_ = true; }

    for (auto& [channel, endpoint] : sub_endpoints_) {
        void* sock = zmq_socket(zmq_ctx_, ZMQ_SUB);
        int hwm = zmq_hwm_;
        zmq_setsockopt(sock, ZMQ_RCVHWM, &hwm, sizeof(hwm));
        int linger = 0;
        zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_connect(sock, endpoint.c_str());
        zmq_setsockopt(sock, ZMQ_SUBSCRIBE, "", 0);
        zmq_sockets_.push_back(sock);
        Logger::instance().info("LoopbackSubscriber",
            "SUB connected: " + channel + " -> " + endpoint + " (HWM=" + std::to_string(zmq_hwm_) + ")");
    }
    running_.store(true);
    thread_ = std::make_unique<std::thread>(&LoopbackSubscriber::sub_loop, this);
}

void LoopbackSubscriber::stop() {
    running_.store(false);
    if (thread_ && thread_->joinable()) thread_->join();
    for (auto* sock : zmq_sockets_) zmq_close(sock);
    zmq_sockets_.clear();
    if (zmq_ctx_ && owns_ctx_) zmq_ctx_destroy(zmq_ctx_);
    zmq_ctx_ = nullptr;
    Logger::instance().info("LoopbackSubscriber", "Stopped");
}

bool LoopbackSubscriber::is_running() const { return running_.load(); }

void LoopbackSubscriber::sub_loop() {
    Logger::instance().info("LoopbackSubscriber", "Sub loop started (grouped channels)");

    while (running_.load()) {
        for (size_t i = 0; i < zmq_sockets_.size(); i++) {
            zmq_pollitem_t poll;
            poll.socket = zmq_sockets_[i];
            poll.events = ZMQ_POLLIN;
            int rc = zmq_poll(&poll, 1, 10);
            if (rc <= 0 || !(poll.revents & ZMQ_POLLIN)) continue;

            std::vector<std::vector<uint8_t>> zparts;
            while (true) {
                zmq_msg_t msg;
                zmq_msg_init(&msg);
                if (zmq_msg_recv(&msg, zmq_sockets_[i], ZMQ_DONTWAIT) < 0) {
                    zmq_msg_close(&msg);
                    break;
                }
                uint8_t* d = static_cast<uint8_t*>(zmq_msg_data(&msg));
                size_t s = zmq_msg_size(&msg);
                zparts.emplace_back(d, d + s);
                zmq_msg_close(&msg);

                int more = 0;
                size_t more_sz = sizeof(more);
                zmq_getsockopt(zmq_sockets_[i], ZMQ_RCVMORE, &more, &more_sz);
                if (!more) break;
            }
            if (zparts.size() < 2) continue;

            try {
                // zparts[0] = topic, zparts[1] = JSON header, zparts[2..] = payloads
                std::string hdr_str(zparts[1].begin(), zparts[1].end());
                auto hdr = nlohmann::json::parse(hdr_str);
                auto& parts_arr = hdr["parts"];
                Timestamp ts;
                ts.sec = hdr.value("ts_sec", (int64_t)0);
                ts.nsec = hdr.value("ts_nsec", (int64_t)0);

                ChannelFrame frame;
                frame.camera_id = hdr.value("camera_id", "default");
                frame.timestamp = ts;

                for (size_t j = 0; j < parts_arr.size() && j + 2 < zparts.size(); j++) {
                    std::string id = parts_arr[j].value("id", "");
                    auto b = std::make_shared<DataBundle>();
                    b->type = channel_to_type(id);
                    b->payload = zparts[j + 2];
                    b->timestamp = ts;
                    buffer_->push(frame.camera_id, b);
                    frame.bundles.push_back(b);
                }

                // Feed the API3 encode SPSC queues (drop-newest) in addition to
                // the DataBuffer, so encode_loop runs CV-driven instead of polling.
                if (encode_callback_ && !frame.bundles.empty()) {
                    encode_callback_(data_type_to_group(frame.bundles[0]->type), frame);
                }
                total_frames_.fetch_add(1);
                if (total_frames_.load() % 1000 == 0) {
                    Logger::instance().info("LoopbackSubscriber",
                        "Received " + std::to_string(total_frames_.load()) + " group frames total");
                }
            } catch (...) {}
        }
    }
    Logger::instance().info("LoopbackSubscriber", "Sub loop exited");
}

} // namespace stereo_camera
