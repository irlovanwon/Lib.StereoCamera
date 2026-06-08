#pragma once

#include "stereo_camera/vision/DepthMap.h"
#include <vector>

namespace stereo_camera {

struct Point3D {
    float x, y, z;
};

class PointCloud {
public:
    PointCloud() = default;

    void generate(const DepthMap& depth, float fx, float fy, float cx, float cy);
    const std::vector<Point3D>& points() const;
    void clear();

private:
    std::vector<Point3D> points_;
};

} // namespace stereo_camera
