#pragma once

#include "stereo_camera/data/DataBuffer.h"
#include <string>
#include <thread>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <openssl/ssl.h>
#include <openssl/evp.h>

namespace stereo_camera {

struct WSSClient {
    SSL* ssl = nullptr;
    int fd = -1;
    std::mutex send_mutex;
    std::unordered_set<std::string> subscriptions;
    std::atomic<bool> active{true};
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

private:
    void accept_loop();
    void client_loop(int fd, SSL* ssl);
    void broadcast_loop();
    bool ws_handshake(SSL* ssl);
    bool ws_send_binary(SSL* ssl, const uint8_t* data, size_t len);
    bool ws_read_frame(SSL* ssl, uint8_t& opcode, std::vector<uint8_t>& payload);
    void send_to_subscribers(const std::string& camera_id, DataType type,
                             const std::shared_ptr<DataBundle>& bundle);

    std::shared_ptr<DataBuffer> buffer_;
    std::string host_;
    uint16_t port_;
    std::string cert_path_;
    std::string key_path_;

    SSL_CTX* ssl_ctx_ = nullptr;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};

    std::unique_ptr<std::thread> accept_thread_;
    std::unique_ptr<std::thread> bcast_thread_;

    std::mutex clients_mutex_;
    std::vector<std::unique_ptr<WSSClient>> clients_;
    std::atomic<uint32_t> frame_count_{0};
};

} // namespace stereo_camera
