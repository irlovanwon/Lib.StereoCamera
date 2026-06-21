#include "stereo_camera/vision/DisparityMap.h"
#include <algorithm>
#include <cmath>

namespace stereo_camera {

DisparityMap::DisparityMap(uint32_t width, uint32_t height)
    : width_(width), height_(height), data_(width * height, 0.0f) {}

std::vector<uint8_t> DisparityMap::to_gray(const uint8_t* bgra, uint32_t w, uint32_t h) {
    std::vector<uint8_t> gray(w * h);
    for (uint32_t i = 0; i < w * h; ++i) {
        const uint8_t* p = bgra + i * 4;
        gray[i] = static_cast<uint8_t>(0.299f * p[2] + 0.587f * p[1] + 0.114f * p[0]);
    }
    return gray;
}

float DisparityMap::compute_sad(const std::vector<uint8_t>& left_gray,
                                  const std::vector<uint8_t>& right_gray,
                                  uint32_t w, uint32_t h,
                                  int x, int y, int d,
                                  uint32_t block, uint32_t max_disp) {
    int half = block / 2;
    uint32_t sad = 0;
    uint32_t count = 0;

    for (int dy = -half; dy <= half; ++dy) {
        int ry = y + dy;
        if (ry < 0 || ry >= (int)h) continue;

        for (int dx = -half; dx <= half; ++dx) {
            int rx = x + dx;
            int rxr = rx - d;
            if (rx < 0 || rx >= (int)w || rxr < 0 || rxr >= (int)w) continue;

            int diff = static_cast<int>(left_gray[ry * w + rx]) -
                       static_cast<int>(right_gray[ry * w + rxr]);
            sad += static_cast<uint32_t>(std::abs(diff));
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sad) / count : 1e30f;
}

void DisparityMap::compute(const StereoImagePair& pair, const DisparityConfig& config) {
    width_ = pair.width;
    height_ = pair.height;
    data_.assign(width_ * height_, 0.0f);

    if (pair.left.size() < width_ * height_ * 4 ||
        pair.right.size() < width_ * height_ * 4) return;

    auto left_gray = to_gray(pair.left.data(), width_, height_);
    auto right_gray = to_gray(pair.right.data(), width_, height_);

    uint32_t block = std::max(config.block_size, 1u);
    uint32_t max_disp = std::min(config.max_disparity, width_);
    int half = block / 2;

    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            if (x < static_cast<uint32_t>(half) + max_disp ||
                x >= width_ - static_cast<uint32_t>(half) ||
                y < static_cast<uint32_t>(half) ||
                y >= height_ - static_cast<uint32_t>(half)) {
                continue;
            }

            float best_cost = 1e30f;
            float best_disp = 0.0f;
            float prev_cost = 1e30f;
            float next_cost = 1e30f;

            for (uint32_t d = 0; d < max_disp; ++d) {
                float cost = compute_sad(left_gray, right_gray,
                                         width_, height_,
                                         static_cast<int>(x),
                                         static_cast<int>(y),
                                         static_cast<int>(d),
                                         block, max_disp);
                if (cost < best_cost) {
                    best_cost = cost;
                    best_disp = static_cast<float>(d);
                    if (d > 0) {
                        prev_cost = compute_sad(left_gray, right_gray,
                                                width_, height_,
                                                static_cast<int>(x),
                                                static_cast<int>(y),
                                                static_cast<int>(d - 1),
                                                block, max_disp);
                    }
                    if (d < max_disp - 1) {
                        next_cost = compute_sad(left_gray, right_gray,
                                                width_, height_,
                                                static_cast<int>(x),
                                                static_cast<int>(y),
                                                static_cast<int>(d + 1),
                                                block, max_disp);
                    }
                }
            }

            if (config.subpixel && best_disp > 0 && best_disp < max_disp - 1) {
                float denom = prev_cost - 2.0f * best_cost + next_cost;
                if (std::abs(denom) > 1e-6f) {
                    best_disp -= (prev_cost - next_cost) / (2.0f * denom);
                }
            }

            data_[y * width_ + x] = best_disp;
        }
    }
}

const std::vector<float>& DisparityMap::data() const { return data_; }
uint32_t DisparityMap::width() const { return width_; }
uint32_t DisparityMap::height() const { return height_; }

} // namespace stereo_camera
