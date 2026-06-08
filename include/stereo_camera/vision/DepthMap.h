#pragma once

#include "stereo_camera/vision/DisparityMap.h"
#include <vector>

namespace stereo_camera {

class DepthMap {
public:
    DepthMap() = default;
    explicit DepthMap(uint32_t width, uint32_t height);

    void compute(const DisparityMap& disparity, float baseline, float focal_length);
    const std::vector<float>& data() const;
    uint32_t width() const;
    uint32_t height() const;

private:
    std::vector<float> data_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace stereo_camera
