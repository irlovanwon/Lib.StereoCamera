#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <nlohmann/json.hpp>

namespace stereo_camera {

enum class DataType : uint8_t {
    StereoImage  = 0,
    DepthMap     = 1,
    PointCloud   = 2,
    IMU          = 3,
    DisparityMap = 4,
    ConfidenceMap = 5,
    Temperature   = 6,
};

NLOHMANN_JSON_SERIALIZE_ENUM(DataType, {
    {DataType::StereoImage, "stereo_image"},
    {DataType::DepthMap, "depth_map"},
    {DataType::PointCloud, "point_cloud"},
    {DataType::IMU, "imu"},
    {DataType::DisparityMap, "disparity_map"},
    {DataType::ConfidenceMap, "confidence_map"},
    {DataType::Temperature, "temperature"},
})

struct Timestamp {
    int64_t sec;
    int64_t nsec;

    static Timestamp now() {
        auto tp = std::chrono::system_clock::now();
        auto dur = tp.time_since_epoch();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(dur);
        auto nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(dur - sec);
        return {sec.count(), nsec.count()};
    }
};

struct DataBundle {
    Timestamp timestamp;
    DataType type;
    std::vector<uint8_t> payload;
};

enum class ParameterType : uint8_t {
    Integer,
    Float,
    Enum,
};

struct ParameterValue {
    int int_val = 0;
    double float_val = 0.0;
    std::string enum_val;
};

enum class ResponseCode {
    Success       = 0,
    Error         = 1,
    NotReady      = 2,
    AlreadyInit   = 3,
    InvalidParam  = 4,
    Unavailable   = 5,
};

struct Response {
    ResponseCode code;
    std::string message;
    nlohmann::json detail;
};

} // namespace stereo_camera
