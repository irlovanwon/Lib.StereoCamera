#pragma once

#include "stereo_camera/data/DataBuffer.h"
#include "stereo_camera/data/SPSCQueue.h"
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <openssl/ssl.h>
#include <webp/encode.h>
#include <functional>

namespace stereo_camera {

struct SendItem {
    DataType type;
    std::shared_ptr<DataBundle> bundle;
};

struct WSSClient {
    SSL* ssl = nullptr;
    int fd = -1;
    std::mutex send_mutex;
    std::unordered_set<std::string> subscriptions;
    std::atomic<bool> active{true};
    std::unordered_map<std::string, const void*> last_sent;
    std::unique_ptr<std::thread> loop_thread;

    std::deque<SendItem> send_queue;
    std::mutex send_queue_mutex;
    std::condition_variable send_queue_cv;
    std::unique_ptr<std::thread> send_thread;
    std::atomic<bool> send_running{false};
};

class WSServer {
public:
    WSServer(std::shared_ptr<DataBuffer> buffer,
             const std::string& host, uint16_t port,
             const std::string& cert_path, const std::string& key_path);
    ~WSServer();

    void start();
    void stop();
    bool is_running() const;
    size_t client_count() const;
    uint32_t total_frames() const;

    using DisconnectCallback = std::function<void()>;
    void set_on_all_disconnected(DisconnectCallback cb) { on_all_disconnected_ = std::move(cb); }

    // SPSC-fed encode path (3 queues, one per data group).
    // Drop-NEWEST: returns silently if the queue is full.
    void push_encode(DataGroup group, const ChannelFrame& frame);
    void set_encode_queue_depth(size_t depth);
    void broadcast(const std::string& message);

private:
    void accept_loop();
    void client_loop(WSSClient* session);
    void send_loop(WSSClient* session);
    void encode_loop();
    bool has_encode_data() const;

    bool ws_handshake(SSL* ssl);
    bool ws_send_binary(SSL* ssl, const uint8_t* data, size_t len);
    bool ws_read_frame(SSL* ssl, uint8_t& opcode, std::vector<uint8_t>& payload);
    void send_to_one(WSSClient* session, DataType type,
                     const std::shared_ptr<DataBundle>& bundle);
    bool encode_stereo_image(const std::vector<uint8_t>& raw_concat,
                             std::vector<uint8_t>& encoded_out);

    std::shared_ptr<DataBuffer> buffer_;
    std::string host_;
    uint16_t port_;
    std::string cert_path_;
    std::string key_path_;

    SSL_CTX* ssl_ctx_ = nullptr;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};

    std::unique_ptr<std::thread> accept_thread_;
    std::unique_ptr<std::thread> encode_thread_;

    mutable std::mutex clients_mutex_;
    std::vector<std::unique_ptr<WSSClient>> clients_;

    // Encoded stereo cache (encode once, all clients read)
    std::mutex encoded_mutex_;
    std::unordered_map<std::string, std::shared_ptr<DataBundle>> encoded_stereo_;
    const void* last_encoded_ptr_ = nullptr;

    // SPSC encode queues (fed by LoopbackSubscriber) + CV-driven encode loop.
    // Same type/width as the API2 DataPipeline queues.
    SPSCQueue<ChannelFrame, 16> encode_queue_2d_;
    SPSCQueue<ChannelFrame, 16> encode_queue_3d_;
    SPSCQueue<ChannelFrame, 16> encode_queue_sensor_;
    std::mutex encode_mutex_;
    std::condition_variable encode_cv_;

    int webp_quality_ = 80;
    std::atomic<uint32_t> frame_count_{0};

    DisconnectCallback on_all_disconnected_;
    bool had_clients_ = false;
};

} // namespace stereo_camera
