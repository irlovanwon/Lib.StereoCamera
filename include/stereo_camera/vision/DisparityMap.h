#pragma once

#include "stereo_camera/common/Types.h"
#include <vector>
#include <cstdint>

namespace stereo_camera {

struct StereoImagePair {
    std::vector<uint8_t> left;
    std::vector<uint8_t> right;
    uint32_t width;
    uint32_t height;
};

class DisparityMap {
public:
    DisparityMap() = default;
    explicit DisparityMap(uint32_t width, uint32_t height);

    void compute(const StereoImagePair& pair);
    const std::vector<float>& data() const;
    uint32_t width() const;
    uint32_t height() const;

private:
    std::vector<float> data_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace stereo_camera
