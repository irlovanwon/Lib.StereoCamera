#include "stereo_camera/vision/PointCloud.h"

namespace stereo_camera {

void PointCloud::generate(const DepthMap& depth, float fx, float fy, float cx, float cy) {
    points_.clear();
    uint32_t w = depth.width();
    uint32_t h = depth.height();
    const auto& d = depth.data();

    points_.reserve(w * h);
    for (uint32_t v = 0; v < h; ++v) {
        for (uint32_t u = 0; u < w; ++u) {
            float z = d[v * w + u];
            if (z <= 0.0f) continue;
            float x = (static_cast<float>(u) - cx) * z / fx;
            float y = (static_cast<float>(v) - cy) * z / fy;
            points_.push_back({x, y, z});
        }
    }
}

const std::vector<Point3D>& PointCloud::points() const { return points_; }
void PointCloud::clear() { points_.clear(); }

} // namespace stereo_camera
