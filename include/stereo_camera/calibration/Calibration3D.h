#pragma once

#include "stereo_camera/calibration/Calibration2D.h"
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

namespace stereo_camera {

struct StereoCalibrationParams {
    CalibrationParams2D left;
    CalibrationParams2D right;
    struct Rotation { double data[9] = {1,0,0,0,1,0,0,0,1}; };
    struct Translation { double data[3] = {0,0,0}; };
    Rotation R;
    Translation T;

    double baseline() const {
        return std::sqrt(T.data[0]*T.data[0] + T.data[1]*T.data[1] + T.data[2]*T.data[2]);
    }

    nlohmann::json to_json() const;
    static StereoCalibrationParams from_json(const nlohmann::json& j);
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
    void set_params(const StereoCalibrationParams& p);

    bool save(const std::string& path) const;
    bool load(const std::string& path);

private:
    StereoCalibrationParams params_;
};

} // namespace stereo_camera
