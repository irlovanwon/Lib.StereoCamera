#include "stereo_camera/api/CameraSDKClient.h"
#include "stereo_camera/common/Logger.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace stereo_camera {

struct CameraSDKClient::Impl {
    std::string base_url;
    std::function<void(const DataBundle&)> data_callback;
    CURL* curl = nullptr;

    Impl(const std::string& url) : base_url(url) {
        curl = curl_easy_init();
    }

    ~Impl() {
        if (curl) curl_easy_cleanup(curl);
    }

    Response http_post(const std::string& endpoint, const nlohmann::json& body = {}) {
        if (!curl) return make_response(ResponseCode::Error, "CURL not initialized");

        std::string url = base_url + endpoint;
        std::string json_str = body.dump();

        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        std::string response_data;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
            auto* str = static_cast<std::string*>(userdata);
            str->append(ptr, size * nmemb);
            return size * nmemb;
        });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            return make_response(ResponseCode::Error, curl_easy_strerror(res));
        }

        try {
            auto resp_json = nlohmann::json::parse(response_data);
            ResponseCode code = static_cast<ResponseCode>(resp_json.value("code", 1));
            return make_response(code, resp_json.value("message", ""), resp_json.value("detail", nlohmann::json::object()));
        } catch (const std::exception& e) {
            return make_response(ResponseCode::Error, std::string("Parse error: ") + e.what());
        }
    }
};

CameraSDKClient::CameraSDKClient(const std::string& base_url)
    : impl_(std::make_unique<Impl>(base_url)) {}

CameraSDKClient::~CameraSDKClient() = default;

Response CameraSDKClient::init() {
    Logger::instance().info("CameraSDKClient", "Sending Init");
    return impl_->http_post("/api/init");
}

Response CameraSDKClient::dispose() {
    Logger::instance().info("CameraSDKClient", "Sending Dispose");
    return impl_->http_post("/api/dispose");
}

Response CameraSDKClient::connect() {
    Logger::instance().info("CameraSDKClient", "Sending Connect");
    return impl_->http_post("/api/connect");
}

Response CameraSDKClient::disconnect() {
    Logger::instance().info("CameraSDKClient", "Sending Disconnect");
    return impl_->http_post("/api/disconnect");
}

Response CameraSDKClient::start_capture(const std::vector<DataType>& types) {
    nlohmann::json j;
    j["data_types"] = types;
    Logger::instance().info("CameraSDKClient", "Sending StartCapture");
    return impl_->http_post("/api/start_capture", j);
}

Response CameraSDKClient::stop_capture(const std::vector<DataType>& types) {
    nlohmann::json j;
    j["data_types"] = types;
    Logger::instance().info("CameraSDKClient", "Sending StopCapture");
    return impl_->http_post("/api/stop_capture", j);
}

Response CameraSDKClient::check_status() {
    return impl_->http_post("/api/check_status");
}

Response CameraSDKClient::set_parameter(const std::string& name, const ParameterValue& value) {
    nlohmann::json j;
    j["name"] = name;
    j["value"] = value.int_val;
    return impl_->http_post("/api/set_parameter", j);
}

Response CameraSDKClient::get_parameter(const std::string& name) {
    nlohmann::json j;
    j["name"] = name;
    return impl_->http_post("/api/get_parameter", j);
}

void CameraSDKClient::set_data_callback(std::function<void(const DataBundle&)> callback) {
    impl_->data_callback = std::move(callback);
}

} // namespace stereo_camera
