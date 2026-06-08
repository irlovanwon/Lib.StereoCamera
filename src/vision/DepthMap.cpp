#include "stereo_camera/vision/DepthMap.h"
#include <cmath>

namespace stereo_camera {

DepthMap::DepthMap(uint32_t width, uint32_t height)
    : width_(width), height_(height), data_(width * height, 0.0f) {}

void DepthMap::compute(const DisparityMap& disparity, float baseline, float focal_length) {
    width_ = disparity.width();
    height_ = disparity.height();
    data_.resize(width_ * height_);

    const auto& disp = disparity.data();
    for (size_t i = 0; i < disp.size(); ++i) {
        if (disp[i] > 0.0f) {
            data_[i] = (baseline * focal_length) / disp[i];
        } else {
            data_[i] = 0.0f;
        }
    }
}

const std::vector<float>& DepthMap::data() const { return data_; }
uint32_t DepthMap::width() const { return width_; }
uint32_t DepthMap::height() const { return height_; }

} // namespace stereo_camera
