#include "stereo_camera/data/WSServer.h"
#include "stereo_camera/common/Logger.h"
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <openssl/sha.h>
#include <algorithm>

namespace stereo_camera {

static std::string base64_encode(const uint8_t* data, size_t len) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data, static_cast<int>(len));
    BIO_flush(b64);
    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

WSServer::WSServer(std::shared_ptr<DataBuffer> buffer,
                   const std::string& host, uint16_t port,
                   const std::string& cert_path, const std::string& key_path)
    : buffer_(std::move(buffer)), host_(host), port_(port),
      cert_path_(cert_path), key_path_(key_path) {}

WSServer::~WSServer() { stop(); }

void WSServer::start() {
    if (running_.load()) return;

    ssl_ctx_ = SSL_CTX_new(TLS_server_method());
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, nullptr);
    SSL_CTX_use_certificate_file(ssl_ctx_, cert_path_.c_str(), SSL_FILETYPE_PEM);
    SSL_CTX_use_PrivateKey_file(ssl_ctx_, key_path_.c_str(), SSL_FILETYPE_PEM);

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Logger::instance().error("WSServer", "Bind failed: " + std::string(strerror(errno)));
        close(listen_fd_); listen_fd_ = -1;
        SSL_CTX_free(ssl_ctx_); ssl_ctx_ = nullptr;
        return;
    }
    listen(listen_fd_, 8);

    running_.store(true);
    accept_thread_ = std::make_unique<std::thread>(&WSServer::accept_loop, this);
    encode_thread_ = std::make_unique<std::thread>(&WSServer::encode_loop, this);

    Logger::instance().info("WSServer", "Listening on " + host_ + ":" + std::to_string(port_) + " (One Loop Per Thread)");
}

void WSServer::stop() {
    running_.store(false);
    if (listen_fd_ >= 0) { close(listen_fd_); listen_fd_ = -1; }
    if (accept_thread_ && accept_thread_->joinable()) accept_thread_->join();
    if (encode_thread_ && encode_thread_->joinable()) encode_thread_->join();

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& c : clients_) {
            c->active.store(false);
            if (c->loop_thread && c->loop_thread->joinable()) c->loop_thread->join();
            if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); c->ssl = nullptr; }
        }
        clients_.clear();
    }
    if (ssl_ctx_) { SSL_CTX_free(ssl_ctx_); ssl_ctx_ = nullptr; }
    Logger::instance().info("WSServer", "Stopped");
}


size_t WSServer::client_count() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(clients_mutex_));
    size_t n = 0;
    for (const auto& c : clients_) if (c->active.load()) ++n;
    return n;
}

uint32_t WSServer::total_frames() const {
    return frame_count_.load();
}

bool WSServer::is_running() const { return running_.load(); }

void WSServer::accept_loop() {
    while (running_.load()) {
        // Cleanup inactive clients: join thread, then erase
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (auto it = clients_.begin(); it != clients_.end();) {
                if (!(*it)->active.load()) {
                    if ((*it)->loop_thread && (*it)->loop_thread->joinable())
                        (*it)->loop_thread->join();
                    it = clients_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        struct timeval tv{1, 0};
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd_, &rfds);
        int rc = select(listen_fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (rc <= 0) continue;

        int client_fd = accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) continue;

        SSL* ssl = SSL_new(ssl_ctx_);
        SSL_set_fd(ssl, client_fd);
        SSL_set_verify(ssl, SSL_VERIFY_NONE, nullptr);

        if (SSL_accept(ssl) <= 0) {
            SSL_free(ssl); close(client_fd); continue;
        }
        if (!ws_handshake(ssl)) {
            SSL_free(ssl); close(client_fd); continue;
        }

        auto client = std::make_unique<WSSClient>();
        client->ssl = ssl;
        client->fd = client_fd;
        WSSClient* raw = client.get();

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            clients_.push_back(std::move(client));
            Logger::instance().info("WSServer", "Client connected (total=" + std::to_string(clients_.size()) + ")");
        }

        raw->loop_thread = std::make_unique<std::thread>(&WSServer::client_loop, this, raw);
    }
}

bool WSServer::ws_handshake(SSL* ssl) {
    char buf[4096];
    int n = SSL_read(ssl, buf, sizeof(buf) - 1);
    if (n <= 0) return false;
    buf[n] = '\0';
    std::string request(buf, n);
    size_t key_pos = request.find("Sec-WebSocket-Key: ");
    if (key_pos == std::string::npos) return false;
    size_t key_start = key_pos + 19;
    size_t key_end = request.find("\r\n", key_start);
    std::string ws_key = request.substr(key_start, key_end - key_start);

    std::string magic = ws_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t sha1_hash[20];
    SHA1((const unsigned char*)magic.c_str(), magic.size(), sha1_hash);
    std::string accept_key = base64_encode(sha1_hash, 20);

    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";
    SSL_write(ssl, response.c_str(), response.size());
    return true;
}

bool WSServer::ws_send_binary(SSL* ssl, const uint8_t* data, size_t len) {
    std::vector<uint8_t> frame;
    frame.push_back(0x82);
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len < 65536) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back((len >> (i * 8)) & 0xFF);
    }
    frame.insert(frame.end(), data, data + len);

    int total_sent = 0;
    while (total_sent < (int)frame.size()) {
        int n = SSL_write(ssl, frame.data() + total_sent, frame.size() - total_sent);
        if (n <= 0) return false;
        total_sent += n;
    }
    return true;
}

bool WSServer::ws_read_frame(SSL* ssl, uint8_t& opcode, std::vector<uint8_t>& payload) {
    uint8_t hdr[2];
    int n = SSL_read(ssl, hdr, 2);
    if (n <= 0) return false;
    opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t payload_len = hdr[1] & 0x7F;
    if (payload_len == 126) {
        uint8_t ext[2];
        if (SSL_read(ssl, ext, 2) <= 0) return false;
        payload_len = (ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (SSL_read(ssl, ext, 8) <= 0) return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) payload_len = (payload_len << 8) | ext[i];
    }
    uint8_t mask[4] = {0};
    if (masked) { if (SSL_read(ssl, mask, 4) <= 0) return false; }
    if (payload_len > 0) {
        payload.resize(payload_len);
        size_t total = 0;
        while (total < payload_len) {
            int chunk = std::min((uint64_t)4096, payload_len - total);
            n = SSL_read(ssl, payload.data() + total, chunk);
            if (n <= 0) return false;
            total += n;
        }
        if (masked)
            for (size_t i = 0; i < payload_len; ++i) payload[i] ^= mask[i % 4];
    }
    return true;
}

void WSServer::send_to_one(WSSClient* session, DataType type,
                            const std::shared_ptr<DataBundle>& bundle) {
    std::vector<uint8_t> frame;
    uint32_t type_tag = static_cast<uint32_t>(type);
    uint32_t ts_sec = static_cast<uint32_t>(bundle->timestamp.sec);
    uint32_t ts_nsec = static_cast<uint32_t>(bundle->timestamp.nsec);

    frame.insert(frame.end(), (uint8_t*)&type_tag, (uint8_t*)&type_tag + 4);
    frame.insert(frame.end(), (uint8_t*)&ts_sec, (uint8_t*)&ts_sec + 4);
    frame.insert(frame.end(), (uint8_t*)&ts_nsec, (uint8_t*)&ts_nsec + 4);
    frame.insert(frame.end(), bundle->payload.begin(), bundle->payload.end());

    std::lock_guard<std::mutex> send_lock(session->send_mutex);
    if (!ws_send_binary(session->ssl, frame.data(), frame.size())) {
        session->active.store(false);
    }
}

bool WSServer::encode_stereo_image(const std::vector<uint8_t>& raw_concat,
                                    std::vector<uint8_t>& encoded_out) {
    if (raw_concat.size() < 8) return false;
    size_t half = raw_concat.size() / 2;
    size_t pixels = half / 4;
    if (pixels == 0 || half % 4 != 0) return false;

    int w = 0, h = 0;
    static const struct { int pw, w, h; } res[] = {
        {(int)(1280*720),  1280, 720},  {(int)(1920*1080), 1920, 1080},
        {(int)(1920*1200), 1920, 1200}, {(int)(2208*1242), 2208, 1242},
        {(int)(672*376),   672,  376},  {(int)(1280*480),  1280, 480},
    };
    for (auto& r : res)
        if ((int)pixels == r.pw) { w = r.w; h = r.h; break; }
    if (w == 0) return false;

    tjhandle compressor = tjInitCompress();
    if (!compressor) return false;

    uint32_t left_sz = 0;
    for (int img = 0; img < 2; img++) {
        const uint8_t* src = raw_concat.data() + (img == 0 ? 0 : half);
        unsigned char* jpeg_buf = nullptr;
        unsigned long jpeg_size = 0;
        int rc = tjCompress2(compressor, src, w, w * 4, h,
                             TJPF_BGRA, &jpeg_buf, &jpeg_size,
                             TJSAMP_420, jpeg_quality_, TJFLAG_FASTDCT);
        if (rc != 0 || !jpeg_buf || jpeg_size == 0) {
            if (jpeg_buf) tjFree(jpeg_buf);
            tjDestroy(compressor);
            return false;
        }
        if (img == 0) {
            left_sz = static_cast<uint32_t>(jpeg_size);
            encoded_out.reserve(4 + jpeg_size + jpeg_size / 2);
            uint8_t* p = reinterpret_cast<uint8_t*>(&left_sz);
            encoded_out.insert(encoded_out.end(), p, p + 4);
        }
        encoded_out.insert(encoded_out.end(), jpeg_buf, jpeg_buf + jpeg_size);
        tjFree(jpeg_buf);
    }
    tjDestroy(compressor);
    return true;
}

void WSServer::encode_loop() {
    Logger::instance().info("WSServer", "Encode loop started (polls DataBuffer, caches encoded)");

    while (running_.load()) {
        auto slots = buffer_->active_slots();
        for (const auto& slot : slots) {
            if (slot.type != DataType::StereoImage) continue;
            auto bundle = buffer_->get_latest(slot.camera_id, slot.type);
            if (!bundle || bundle->payload.empty()) continue;
            if (bundle.get() == last_encoded_ptr_) continue;
            last_encoded_ptr_ = bundle.get();

            std::vector<uint8_t> encoded;
            if (encode_stereo_image(bundle->payload, encoded)) {
                auto out = std::make_shared<DataBundle>();
                out->timestamp = bundle->timestamp;
                out->type = bundle->type;
                out->payload = std::move(encoded);
                {
                    std::lock_guard<std::mutex> lk(encoded_mutex_);
                    encoded_stereo_[slot.camera_id] = std::move(out);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Logger::instance().info("WSServer", "Encode loop exited");
}

void WSServer::client_loop(WSSClient* session) {
    // Send subscribed response
    std::string resp = R"({"status":"ready"})";
    {
        std::lock_guard<std::mutex> lk(session->send_mutex);
        ws_send_binary(session->ssl, (const uint8_t*)resp.data(), resp.size());
    }

    while (running_.load() && session->active.load()) {
        // 1. Check for incoming frames (subscription updates)
        struct timeval tv{0, 10000}; // 10ms
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(session->fd, &rfds);
        int rc = select(session->fd + 1, &rfds, nullptr, nullptr, &tv);

        if (rc > 0 && FD_ISSET(session->fd, &rfds)) {
            uint8_t opcode;
            std::vector<uint8_t> payload;
            if (!ws_read_frame(session->ssl, opcode, payload)) break;
            if (opcode == 0x8) break;
            if (opcode == 0x1 && !payload.empty()) {
                std::string msg(payload.begin(), payload.end());
                try {
                    auto j = nlohmann::json::parse(msg);
                    std::string action = j.value("action", "");
                    if (action == "subscribe" && j.contains("topics")) {
                        for (auto& t : j["topics"])
                            session->subscriptions.insert(t.get<std::string>());
                        Logger::instance().info("WSServer", "Client subscribed: " + msg);
                    } else if (action == "unsubscribe" && j.contains("topics")) {
                        for (auto& t : j["topics"])
                            session->subscriptions.erase(t.get<std::string>());
                    }
                } catch (...) {
                    session->subscriptions.insert("all");
                }
            }
        }

        // 2. Send data (One Loop Per Thread — each client sends independently)
        if (session->subscriptions.empty()) continue;

        auto slots = buffer_->active_slots();
        for (const auto& slot : slots) {
            // Filter by subscribed topics
            std::string channel = data_type_to_channel(slot.type);
            std::string full_topic = slot.camera_id + "/" + channel;
            bool is_subscribed = false;
            for (const auto& sub : session->subscriptions) {
                if (sub == "all" || sub == channel || sub == full_topic) {
                    is_subscribed = true;
                    break;
                }
            }
            if (!is_subscribed) continue;

            std::shared_ptr<DataBundle> bundle;

            if (slot.type == DataType::StereoImage) {
                std::lock_guard<std::mutex> lk(encoded_mutex_);
                auto it = encoded_stereo_.find(slot.camera_id);
                if (it != encoded_stereo_.end()) bundle = it->second;
            } else {
                bundle = buffer_->get_latest(slot.camera_id, slot.type);
            }

            if (!bundle || bundle->payload.empty()) continue;

            // Dedup per client
            std::string key = slot.camera_id + "/" + std::to_string(static_cast<int>(slot.type));
            if (session->last_sent[key] == bundle.get()) continue;
            session->last_sent[key] = bundle.get();

            send_to_one(session, slot.type, bundle);
            frame_count_.fetch_add(1);
        }
    }

    session->active.store(false);
    if (session->ssl) {
        SSL_shutdown(session->ssl);
        SSL_free(session->ssl);
        session->ssl = nullptr;
    }
    if (session->fd >= 0) {
        close(session->fd);
        session->fd = -1;
    }
    Logger::instance().info("WSServer", "Client disconnected (cleanup deferred to accept_loop)");

    if (frame_count_.load() % 500 == 0 && frame_count_.load() > 0) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        Logger::instance().info("WSServer",
            "Sent " + std::to_string(frame_count_.load()) + " frames, " +
            std::to_string(clients_.size()) + " clients");
    }
}

} // namespace stereo_camera
