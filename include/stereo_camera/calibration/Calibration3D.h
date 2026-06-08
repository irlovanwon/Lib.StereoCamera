#pragma once

#include "stereo_camera/calibration/Calibration2D.h"
#include <vector>

namespace stereo_camera {

struct StereoCalibrationParams {
    CalibrationParams2D left;
    CalibrationParams2D right;
    struct Rotation { double data[9]; };
    struct Translation { double data[3]; };
    Rotation R;
    Translation T;
};

class Calibration3D {
public:
    Calibration3D() = default;

    StereoCalibrationParams calibrate(
        const std::vector<std::vector<Point2D>>& left_points,
        const std::vector<std::vector<Point2D>>& right_points,
        const std::vector<std::vector<Point3D>>& object_points,
        uint32_t image_width, uint32_t image_height);

    StereoCalibrationParams params() const;

private:
    StereoCalibrationParams params_;
};

} // namespace stereo_camera
