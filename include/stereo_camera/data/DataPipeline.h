#pragma once

#include "stereo_camera/data/SPSCQueue.h"
#include "stereo_camera/common/Types.h"
#include <condition_variable>
#include <mutex>

namespace stereo_camera {

struct DataPipeline {
    using FrameQ = SPSCQueue<ChannelFrame, 16>;

    FrameQ queue_2d;      // Visual & Geometric 2D: Image, DepthMap
    FrameQ queue_3d;      // Visual & Geometric 3D: PointCloud, Disparity, Confidence
    FrameQ queue_sensor;  // Sensor & Tracking: IMU, Temperature, Magnetometer, Barometer

    std::mutex cv_mutex;
    std::condition_variable cv;

    std::atomic<bool> running{true};
    std::atomic<uint64_t> total_pushed{0};
    std::atomic<uint64_t> total_dropped{0};

    void notify() { cv.notify_one(); }

    FrameQ* queue_for_group(DataGroup g) {
        switch (g) {
            case DataGroup::VisualGeometric2D: return &queue_2d;
            case DataGroup::VisualGeometric3D: return &queue_3d;
            case DataGroup::SensorTracking:    return &queue_sensor;
        }
        return nullptr;
    }

    bool has_data() const {
        return !queue_2d.empty() || !queue_3d.empty() || !queue_sensor.empty();
    }
};

} // namespace stereo_camera
