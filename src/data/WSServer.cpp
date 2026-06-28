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
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <turbojpeg.h>

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

// SC-H3: SSL_read may return fewer bytes than requested on TLS; loop until the
// full frame header/mask is read to avoid parsing uninitialized bytes.
static int ssl_read_full(SSL* ssl, void* buf, int len) {
    int total = 0;
    while (total < len) {
        int n = SSL_read(ssl, (char*)buf + total, len - total);
        if (n <= 0) return n;  // error or connection closed
        total += n;
    }
    return total;
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
    // Wake the CV-driven encode loop so it can observe !running_ and exit.
    encode_cv_.notify_all();
    if (accept_thread_ && accept_thread_->joinable()) accept_thread_->join();
    if (encode_thread_ && encode_thread_->joinable()) encode_thread_->join();
    if (gst_pipeline_) { gst_element_set_state(gst_pipeline_, GST_STATE_NULL); gst_object_unref(gst_appsink_); gst_object_unref(gst_pipeline_); gst_pipeline_ = nullptr; gst_appsink_ = nullptr; }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto& c : clients_) {
            c->active.store(false);
            c->send_running.store(false);
            c->send_queue_cv.notify_all();
            if (c->send_thread && c->send_thread->joinable()) c->send_thread->join();
            if (c->loop_thread && c->loop_thread->joinable()) c->loop_thread->join();
            if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); c->ssl = nullptr; }
        }
        clients_.clear();
    }
    if (ssl_ctx_) { SSL_CTX_free(ssl_ctx_); ssl_ctx_ = nullptr; }
    Logger::instance().info("WSServer", "Stopped");
}

void WSServer::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lk(clients_mutex_);
    size_t payload_len = message.size();
    for (auto& c : clients_) {
        if (!c->active.load() || !c->ssl) continue;
        uint8_t hdr[10];
        size_t hdr_len = 0;
        hdr[0] = 0x81; // FIN + text opcode
        if (payload_len < 126) {
            hdr[1] = static_cast<uint8_t>(payload_len);
            hdr_len = 2;
        } else if (payload_len < 65536) {
            hdr[1] = 126;
            hdr[2] = (payload_len >> 8) & 0xFF;
            hdr[3] = payload_len & 0xFF;
            hdr_len = 4;
        } else {
            hdr[1] = 127;
            for (int i = 7; i >= 0; --i)
                hdr[2 + (7 - i)] = (payload_len >> (i * 8)) & 0xFF;
            hdr_len = 10;
        }
        std::lock_guard<std::mutex> slk(c->send_mutex);
        SSL_write(c->ssl, hdr, hdr_len);
        SSL_write(c->ssl, message.c_str(), message.size());
    }
}


size_t WSServer::client_count() const {
    std::lock_guard<std::mutex> lock(clients_mutex_);
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
        bool all_disconnected = false;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            for (auto it = clients_.begin(); it != clients_.end();) {
                if (!(*it)->active.load()) {
                    (*it)->send_running.store(false);
                    (*it)->send_queue_cv.notify_all();
                    if ((*it)->send_thread && (*it)->send_thread->joinable())
                        (*it)->send_thread->join();
                    if ((*it)->loop_thread && (*it)->loop_thread->joinable())
                        (*it)->loop_thread->join();
                    if ((*it)->ssl) {
                        SSL_shutdown((*it)->ssl);
                        SSL_free((*it)->ssl);
                        (*it)->ssl = nullptr;
                    }
                    if ((*it)->fd >= 0) {
                        close((*it)->fd);
                        (*it)->fd = -1;
                    }
                    it = clients_.erase(it);
                } else {
                    ++it;
                }
            }

            // Detect transition: had clients → no clients
            if (had_clients_ && clients_.empty()) {
                had_clients_ = false;
                all_disconnected = true;
            }
        }
        // SC-H5: run heavy callback (HTTP calls + buffer clear) outside clients_mutex_
        if (all_disconnected) {
            Logger::instance().info("WSServer", "All WebSocket clients disconnected — triggering cleanup");
            if (on_all_disconnected_) {
                on_all_disconnected_();
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
            had_clients_ = true;
            Logger::instance().info("WSServer", "Client connected (total=" + std::to_string(clients_.size()) + ")");
        }

        raw->loop_thread = std::make_unique<std::thread>(&WSServer::client_loop, this, raw);
        raw->send_running.store(true);
        raw->send_thread = std::make_unique<std::thread>(&WSServer::send_loop, this, raw);
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
    uint8_t hdr[10];
    int hdr_len = 0;
    hdr[0] = 0x82;
    if (len < 126) {
        hdr[1] = static_cast<uint8_t>(len);
        hdr_len = 2;
    } else if (len < 65536) {
        hdr[1] = 126;
        hdr[2] = (len >> 8) & 0xFF;
        hdr[3] = len & 0xFF;
        hdr_len = 4;
    } else {
        hdr[1] = 127;
        for (int i = 7; i >= 0; --i)
            hdr[2 + (7 - i)] = (len >> (i * 8)) & 0xFF;
        hdr_len = 10;
    }

    int sent = 0;
    while (sent < hdr_len) {
        int n = SSL_write(ssl, hdr + sent, hdr_len - sent);
        if (n <= 0) return false;
        sent += n;
    }

    const size_t CHUNK = 16384;
    size_t total = 0;
    while (total < len) {
        size_t chunk = std::min(CHUNK, len - total);
        int n = SSL_write(ssl, data + total, chunk);
        if (n <= 0) return false;
        total += n;
    }
    return true;
}

bool WSServer::ws_read_frame(SSL* ssl, uint8_t& opcode, std::vector<uint8_t>& payload) {
    uint8_t hdr[2];
    int n = ssl_read_full(ssl, hdr, 2);
    if (n <= 0) return false;
    opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t payload_len = hdr[1] & 0x7F;
    if (payload_len == 126) {
        uint8_t ext[2];
        if (ssl_read_full(ssl, ext, 2) <= 0) return false;
        payload_len = (ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (ssl_read_full(ssl, ext, 8) <= 0) return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) payload_len = (payload_len << 8) | ext[i];
    }
    uint8_t mask[4] = {0};
    if (masked) { if (ssl_read_full(ssl, mask, 4) <= 0) return false; }
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


static GstFlowReturn wss_gst_on_sample(GstAppSink* sink, gpointer udata) {
    auto* self = static_cast<WSServer*>(udata);
    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_ERROR;
    GstBuffer* buf = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buf && gst_buffer_map(buf, &map, GST_MAP_READ)) {
        std::lock_guard<std::mutex> lk(self->gst_mutex_);
        self->gst_output_.assign(map.data, map.data + map.size);
        gst_buffer_unmap(buf, &map);
        self->gst_cv_.notify_one();
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
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

    // Lazy-init GStreamer pipeline (reuse for lifetime)
    if (!gst_pipeline_) {
        std::string pipe =
            "appsrc name=src is-live=true format=time "
            "caps=video/x-raw,format=BGRA,width=" + std::to_string(w) +
            ",height=" + std::to_string(h) + ",framerate=30/1 "
            "! videoconvert ! video/x-raw,format=I420 "
            "! jpegenc quality=" + std::to_string(jpeg_quality_) + " "
            "! appsink name=sink emit-signals=true sync=false max-buffers=2 drop=true";
        GError* err = nullptr;
        gst_pipeline_ = gst_parse_launch(pipe.c_str(), &err);
        if (!gst_pipeline_) { if (err) g_error_free(err); return false; }
        gst_appsink_ = gst_bin_get_by_name(GST_BIN(gst_pipeline_), "sink");
        g_signal_connect(gst_appsink_, "new-sample", G_CALLBACK(wss_gst_on_sample), this);
        gst_element_set_state(gst_pipeline_, GST_STATE_PLAYING);
    }

    GstElement* appsrc = gst_bin_get_by_name(GST_BIN(gst_pipeline_), "src");
    if (!appsrc) return false;

    uint32_t left_sz = 0;
    for (int img = 0; img < 2; img++) {
        const uint8_t* src_data = raw_concat.data() + (img == 0 ? 0 : half);

        GstBuffer* buf = gst_buffer_new_allocate(nullptr, half, nullptr);
        GstMapInfo map;
        gst_buffer_map(buf, &map, GST_MAP_WRITE);
        std::memcpy(map.data, src_data, half);
        gst_buffer_unmap(buf, &map);

        {
            std::unique_lock<std::mutex> lk(gst_mutex_);
            gst_output_.clear();
            gst_app_src_push_buffer(GST_APP_SRC(appsrc), buf);
            if (gst_cv_.wait_for(lk, std::chrono::milliseconds(1000)) == std::cv_status::timeout) {
                gst_object_unref(appsrc);
                return false;
            }
        }

        if (gst_output_.empty()) { gst_object_unref(appsrc); return false; }

        if (img == 0) {
            left_sz = static_cast<uint32_t>(gst_output_.size());
            encoded_out.reserve(4 + gst_output_.size() * 2);
            uint8_t* p = reinterpret_cast<uint8_t*>(&left_sz);
            encoded_out.insert(encoded_out.end(), p, p + 4);
        }
        encoded_out.insert(encoded_out.end(), gst_output_.begin(), gst_output_.end());
    }
    gst_object_unref(appsrc);
    return true;
}

void WSServer::push_encode(DataGroup group, const ChannelFrame& frame) {
    bool pushed = false;
    switch (group) {
        case DataGroup::VisualGeometric2D: pushed = encode_queue_2d_.try_push(frame); break;
        case DataGroup::VisualGeometric3D: pushed = encode_queue_3d_.try_push(frame); break;
        case DataGroup::SensorTracking:    pushed = encode_queue_sensor_.try_push(frame); break;
    }
    if (pushed) {
        std::lock_guard<std::mutex> lk(encode_mutex_);
        encode_cv_.notify_one();
    }
}

void WSServer::set_encode_queue_depth(size_t depth) {
    encode_queue_2d_.set_max_depth(depth);
    encode_queue_3d_.set_max_depth(depth);
    encode_queue_sensor_.set_max_depth(depth);
}

bool WSServer::has_encode_data() const {
    return !encode_queue_2d_.empty() ||
           !encode_queue_3d_.empty() ||
           !encode_queue_sensor_.empty();
}

void WSServer::encode_loop() {
    Logger::instance().info("WSServer", "Encode loop started (SPSC-fed, CV-driven)");

    while (running_.load()) {
        bool got_data = false;

        // 2D group: encode StereoImage bundles (DepthMap/2D pass through unencoded for now)
        ChannelFrame frame2d;
        while (encode_queue_2d_.pop(frame2d)) {
            got_data = true;
            for (const auto& bundle : frame2d.bundles) {
                if (!bundle || bundle->type != DataType::StereoImage) continue;
                if (bundle->payload.empty()) continue;
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
                        encoded_stereo_[frame2d.camera_id] = std::move(out);
                    }
                }
            }
        }

        // 3D & sensor: no encoding needed yet — drain to avoid queue buildup.
        ChannelFrame tmp;
        while (encode_queue_3d_.pop(tmp)) { got_data = true; }
        while (encode_queue_sensor_.pop(tmp)) { got_data = true; }

        if (!got_data) {
            std::unique_lock<std::mutex> lk(encode_mutex_);
            encode_cv_.wait_for(lk, std::chrono::milliseconds(100),
                [this] { return !running_.load() || has_encode_data(); });
        }
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
            bool ok;
            {
                std::lock_guard<std::mutex> lk(session->send_mutex);
                ok = ws_read_frame(session->ssl, opcode, payload);
            }
            if (!ok) break;
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

            {
                std::lock_guard<std::mutex> slk(session->send_queue_mutex);
                for (auto qit = session->send_queue.begin(); qit != session->send_queue.end(); ) {
                    if (qit->type == slot.type) qit = session->send_queue.erase(qit);
                    else ++qit;
                }
                session->send_queue.push_back({slot.type, bundle});
            }
            session->send_queue_cv.notify_one();
            frame_count_.fetch_add(1);
        }
    }

    session->active.store(false);
    session->send_running.store(false);
    session->send_queue_cv.notify_all();
    Logger::instance().info("WSServer", "Client disconnected (cleanup deferred to accept_loop)");

    if (frame_count_.load() % 500 == 0 && frame_count_.load() > 0) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        Logger::instance().info("WSServer",
            "Sent " + std::to_string(frame_count_.load()) + " frames, " +
            std::to_string(clients_.size()) + " clients");
    }
}

void WSServer::send_loop(WSSClient* session) {
    Logger::instance().info("WSServer", "Send loop started");
    while (session->send_running.load() && session->active.load()) {
        SendItem item;
        {
            std::unique_lock<std::mutex> lk(session->send_queue_mutex);
            session->send_queue_cv.wait_for(lk, std::chrono::milliseconds(100),
                [&] { return !session->send_queue.empty() || !session->send_running.load(); });
            if (session->send_queue.empty()) continue;
            item = session->send_queue.front();
            session->send_queue.pop_front();
        }
        send_to_one(session, item.type, item.bundle);
    }
    Logger::instance().info("WSServer", "Send loop exited");
}

} // namespace stereo_camera
