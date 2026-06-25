#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>

namespace stereo_camera {

enum class DataType : uint8_t {
    StereoImage   = 0,
    DepthMap      = 1,
    PointCloud    = 2,
    IMU           = 3,
    DisparityMap  = 4,
    ConfidenceMap = 5,
    Temperature   = 6,
    Magnetometer  = 7,
    Barometer     = 8,
};

NLOHMANN_JSON_SERIALIZE_ENUM(DataType, {
    {DataType::StereoImage, "stereo_image"},
    {DataType::DepthMap, "depth_map"},
    {DataType::PointCloud, "point_cloud"},
    {DataType::IMU, "imu"},
    {DataType::DisparityMap, "disparity_map"},
    {DataType::ConfidenceMap, "confidence_map"},
    {DataType::Temperature, "temperature"},
    {DataType::Magnetometer, "magnetometer"},
    {DataType::Barometer, "barometer"},
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
    ParameterType type = ParameterType::Integer;
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

inline std::string data_type_to_channel(DataType dt) {
    switch (dt) {
        case DataType::StereoImage:  return "stereo_image";
        case DataType::DepthMap:     return "depth_map";
        case DataType::PointCloud:   return "point_cloud";
        case DataType::IMU:          return "imu";
        case DataType::DisparityMap: return "disparity_map";
        case DataType::ConfidenceMap: return "confidence_map";
        case DataType::Temperature:  return "temperature";
        case DataType::Magnetometer: return "magnetometer";
        case DataType::Barometer:    return "barometer";
    }
    return "unknown";
}


enum class DataGroup : uint8_t {
    VisualGeometric2D = 0,
    VisualGeometric3D = 1,
    SensorTracking    = 2,
};

inline DataGroup data_type_to_group(DataType dt) {
    switch (dt) {
        case DataType::StereoImage:
        case DataType::DepthMap:
            return DataGroup::VisualGeometric2D;
        case DataType::PointCloud:
        case DataType::DisparityMap:
        case DataType::ConfidenceMap:
            return DataGroup::VisualGeometric3D;
        default:
            return DataGroup::SensorTracking;
    }
}

inline std::string data_group_to_channel(DataGroup g) {
    switch (g) {
        case DataGroup::VisualGeometric2D: return "visual_geometric_2d";
        case DataGroup::VisualGeometric3D: return "visual_geometric_3d";
        case DataGroup::SensorTracking:    return "sensor_tracking";
    }
    return "unknown";
}

struct ChannelFrame {
    std::string camera_id;
    Timestamp timestamp;
    std::vector<std::shared_ptr<DataBundle>> bundles;
};

} // namespace stereo_camera
