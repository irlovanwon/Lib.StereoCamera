#include "stereo_camera/api/AdminServer.h"
#include "stereo_camera/common/Logger.h"
#include <nlohmann/json.hpp>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>

namespace stereo_camera {

static std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

static std::string path_to_command(const std::string& path) {
    std::string p = path;
    if (!p.empty() && p[0] == '/') p = p.substr(1);
    std::string lower = to_lower(p);
    if (lower == "checkstatus")    return "check_status";
    if (lower == "init")           return "init";
    if (lower == "dispose")        return "dispose";
    if (lower == "connect")        return "connect";
    if (lower == "disconnect")     return "disconnect";
    if (lower == "startcapture")   return "start_capture";
    if (lower == "stopcapture")    return "stop_capture";
    if (lower == "setparameter")   return "set_parameter";
    if (lower == "getparameter")   return "get_parameter";
    return lower;
}

struct AdminServer::Impl {
    std::string host;
    uint16_t port;
    std::string cert_path;
    std::string key_path;
    std::shared_ptr<ClientHandler> handler;
    std::shared_ptr<ParameterManager> param_mgr;
    int server_fd = -1;
    bool running = false;
    int worker_count = 4;

    // Worker pool
    std::vector<std::thread> workers;
    std::queue<std::pair<SSL*, int>> pending;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    SSL_CTX* ssl_ctx = nullptr;

    void handle_connection(SSL* ssl, int client_fd) {
        char buf[65536] = {};
        std::string raw;
        bool got_headers = false;
        size_t content_length = 0;
        size_t header_end_pos = std::string::npos;

        while (true) {
            int n = SSL_read(ssl, buf, sizeof(buf) - 1);
            if (n > 0) {
                raw.append(buf, n);
                if (!got_headers) {
                    header_end_pos = raw.find("\r\n\r\n");
                    if (header_end_pos != std::string::npos) {
                        got_headers = true;
                        std::string header_block = raw.substr(0, header_end_pos);
                        auto cl_pos = header_block.find("Content-Length:");
                        if (cl_pos == std::string::npos)
                            cl_pos = header_block.find("content-length:");
                        if (cl_pos != std::string::npos) {
                            auto eol = header_block.find("\r\n", cl_pos);
                            content_length = std::stoul(header_block.substr(cl_pos + 15, eol - cl_pos - 15));
                        }
                    }
                }
                if (got_headers) {
                    size_t body_len = raw.size() - header_end_pos - 4;
                    if (body_len >= content_length) break;
                }
            } else {
                break;
            }
        }

        if (!raw.empty() && handler) {
            std::string method = "GET";
            std::string path = "/";
            std::string body;

            if (header_end_pos != std::string::npos) {
                std::string header = raw.substr(0, header_end_pos);
                body = raw.substr(header_end_pos + 4);
                auto first_space = header.find(' ');
                if (first_space != std::string::npos) {
                    method = header.substr(0, first_space);
                    auto second_space = header.find(' ', first_space + 1);
                    if (second_space != std::string::npos)
                        path = header.substr(first_space + 1, second_space - first_space - 1);
                }
            } else {
                body = raw;
            }

            std::string cmd = path_to_command(path);
            if (!body.empty() && body[0] == '{') {
                try {
                    auto j = nlohmann::json::parse(body);
                    if (j.contains("command")) cmd = j.value("command", cmd);
                } catch (...) {}
            }

            auto resp = route(cmd, body);
            std::string json_str = resp.dump();

            std::string http_resp =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: " + std::to_string(json_str.size()) + "\r\n"
                "Connection: close\r\n"
                "\r\n" + json_str;

            SSL_write(ssl, http_resp.c_str(), http_resp.size());
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client_fd);
    }

    void worker_loop() {
        while (running) {
            SSL* ssl = nullptr;
            int client_fd = -1;
            {
                std::unique_lock<std::mutex> lk(queue_mutex);
                queue_cv.wait_for(lk, std::chrono::milliseconds(200),
                    [this] { return !running || !pending.empty(); });
                if (!running && pending.empty()) break;
                if (pending.empty()) continue;
                auto front = pending.front();
                pending.pop();
                ssl = front.first;
                client_fd = front.second;
            }
            handle_connection(ssl, client_fd);
        }
    }

    void start() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) { Logger::instance().error("AdminServer", "socket() failed"); return; }
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            Logger::instance().error("AdminServer", "bind() failed"); return;
        }
        if (listen(server_fd, 16) < 0) {
            Logger::instance().error("AdminServer", "listen() failed"); return;
        }

        ssl_ctx = SSL_CTX_new(TLS_server_method());
        if (!ssl_ctx) { Logger::instance().error("AdminServer", "SSL_CTX_new failed"); return; }
        if (SSL_CTX_use_certificate_file(ssl_ctx, cert_path.c_str(), SSL_FILETYPE_PEM) <= 0 ||
            SSL_CTX_use_PrivateKey_file(ssl_ctx, key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
            Logger::instance().error("AdminServer", "Failed to load cert/key");
            SSL_CTX_free(ssl_ctx); ssl_ctx = nullptr; return;
        }

        running = true;
        Logger::instance().info("AdminServer",
            "Listening on " + host + ":" + std::to_string(port) +
            " (Hybrid Pools: 1 accept + " + std::to_string(worker_count) + " workers)");

        // Start worker pool
        for (int i = 0; i < worker_count; i++)
            workers.emplace_back(&Impl::worker_loop, this);

        // Accept thread
        std::thread([this]() {
            while (running) {
                struct sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) { if (running) continue; break; }

                SSL* ssl = SSL_new(ssl_ctx);
                SSL_set_fd(ssl, client_fd);
                if (SSL_accept(ssl) <= 0) {
                    SSL_free(ssl);
                    close(client_fd);
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lk(queue_mutex);
                    pending.push({ssl, client_fd});
                }
                queue_cv.notify_one();
            }
        }).detach();
    }

    nlohmann::json route(const std::string& cmd, const std::string& body) {
        std::string client_id = "default";
        nlohmann::json j;
        if (!body.empty() && body[0] == '{') {
            try {
                j = nlohmann::json::parse(body);
                client_id = j.value("client_id", "default");
            } catch (...) {
                return {{"code", static_cast<int>(ResponseCode::Error)},
                        {"message", "Invalid JSON body"}};
            }
        }

        Response resp;
        if (cmd == "init")          resp = handler->handle_init(client_id);
        else if (cmd == "dispose")   resp = handler->handle_dispose(client_id);
        else if (cmd == "connect")   resp = handler->handle_connect(client_id);
        else if (cmd == "disconnect") resp = handler->handle_disconnect(client_id);
        else if (cmd == "check_status") resp = handler->handle_check_status(client_id);
        else if (cmd == "start_capture") {
            std::vector<DataType> types;
            if (j.contains("data_types"))
                for (auto& dt : j["data_types"]) types.push_back(dt.get<DataType>());
            resp = handler->handle_start_capture(client_id, types);
        }
        else if (cmd == "stop_capture") {
            std::vector<DataType> types;
            if (j.contains("data_types"))
                for (auto& dt : j["data_types"]) types.push_back(dt.get<DataType>());
            resp = handler->handle_stop_capture(client_id, types);
        }
        else if (cmd == "set_parameter") {
            std::string pname = j.value("name", "");
            ParameterValue pv;
            if (j.contains("value")) {
                auto& v = j["value"];
                if (v.is_number_integer()) pv.int_val = v.get<int>();
                else if (v.is_number_float()) pv.float_val = v.get<double>();
                else if (v.is_string()) pv.enum_val = v.get<std::string>();
            }
            resp = handler->handle_set_parameter(client_id, pname, pv);
        }
        else if (cmd == "get_parameter") resp = handler->handle_get_parameter(client_id, j.value("name", ""));
        else resp = make_response(ResponseCode::Error, "Unknown command: " + cmd);

        nlohmann::json result;
        result["code"] = static_cast<int>(resp.code);
        result["message"] = resp.message;
        result["detail"] = resp.detail;
        return result;
    }

    void stop() {
        running = false;
        if (server_fd >= 0) {
            shutdown(server_fd, SHUT_RDWR);
            close(server_fd);
            server_fd = -1;
        }
        queue_cv.notify_all();
        for (auto& w : workers)
            if (w.joinable()) w.join();
        workers.clear();
        if (ssl_ctx) { SSL_CTX_free(ssl_ctx); ssl_ctx = nullptr; }
        Logger::instance().info("AdminServer", "Stopped");
    }
};

AdminServer::AdminServer(const std::string& host, uint16_t port,
                         const std::string& cert_path, const std::string& key_path,
                         int worker_count)
    : impl_(std::make_unique<Impl>()) {
    impl_->host = host;
    impl_->port = port;
    impl_->cert_path = cert_path;
    impl_->key_path = key_path;
    impl_->worker_count = worker_count > 0 ? worker_count : 4;
}

AdminServer::~AdminServer() { stop(); }

void AdminServer::set_client_handler(std::shared_ptr<ClientHandler> handler) {
    impl_->handler = std::move(handler);
}

void AdminServer::set_parameter_manager(std::shared_ptr<ParameterManager> param_mgr) {
    impl_->param_mgr = std::move(param_mgr);
}

void AdminServer::start() { impl_->start(); }
void AdminServer::stop() { impl_->stop(); }

} // namespace stereo_camera
