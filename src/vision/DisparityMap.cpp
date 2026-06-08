#include "stereo_camera/vision/DisparityMap.h"

namespace stereo_camera {

DisparityMap::DisparityMap(uint32_t width, uint32_t height)
    : width_(width), height_(height), data_(width * height, 0.0f) {}

void DisparityMap::compute(const StereoImagePair& pair) {
    width_ = pair.width;
    height_ = pair.height;
    data_.assign(width_ * height_, 0.0f);
    // ... stereo matching algorithm placeholder ...
}

const std::vector<float>& DisparityMap::data() const { return data_; }
uint32_t DisparityMap::width() const { return width_; }
uint32_t DisparityMap::height() const { return height_; }

} // namespace stereo_camera
