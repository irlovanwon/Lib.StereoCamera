#pragma once

#include "stereo_camera/common/Types.h"
#include "stereo_camera/common/Parameter.h"
#include "stereo_camera/common/Response.h"
#include <string>
#include <memory>
#include <functional>

namespace stereo_camera {

class CameraSDKClient {
public:
    explicit CameraSDKClient(const std::string& base_url);
    ~CameraSDKClient();

    Response init();
    Response dispose();
    Response connect();
    Response disconnect();
    Response start_capture(const std::vector<DataType>& types);
    Response stop_capture(const std::vector<DataType>& types);
    Response activate_channel(const std::string& channel_name);
    Response deactivate_channel(const std::string& channel_name);
    Response start_capture_by_channels(const std::vector<std::string>& channels);
    Response stop_capture_by_channels(const std::vector<std::string>& channels);
    Response check_status();
    Response set_parameter(const std::string& name, const ParameterValue& value);
    Response get_parameter(const std::string& name);

    void set_data_callback(std::function<void(const DataBundle&)> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace stereo_camera
