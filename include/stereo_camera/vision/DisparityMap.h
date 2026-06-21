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

struct DisparityConfig {
    uint32_t max_disparity = 64;
    uint32_t block_size = 7;
    bool subpixel = true;
};

class DisparityMap {
public:
    DisparityMap() = default;
    explicit DisparityMap(uint32_t width, uint32_t height);

    void compute(const StereoImagePair& pair, const DisparityConfig& config = {});
    const std::vector<float>& data() const;
    uint32_t width() const;
    uint32_t height() const;

private:
    std::vector<float> data_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    static std::vector<uint8_t> to_gray(const uint8_t* bgra, uint32_t w, uint32_t h);
    float compute_sad(const std::vector<uint8_t>& left_gray,
                      const std::vector<uint8_t>& right_gray,
                      uint32_t w, uint32_t h,
                      int x, int y, int d,
                      uint32_t block, uint32_t max_disp);
};

} // namespace stereo_camera
