#pragma once

#include <vector>
#include <string>
#include <nlohmann/json.hpp>

namespace stereo_camera {

struct CalibrationParams2D {
    double fx = 0;
    double fy = 0;
    double cx = 0;
    double cy = 0;
    std::vector<double> distortion;

    bool valid() const { return fx > 0 && fy > 0; }

    nlohmann::json to_json() const;
    static CalibrationParams2D from_json(const nlohmann::json& j);
};

struct Point2D { double x, y; };
struct Point3D { double x, y, z; };

class Calibration2D {
public:
    Calibration2D() = default;

    CalibrationParams2D calibrate(
        const std::vector<std::vector<Point2D>>& image_points,
        const std::vector<std::vector<Point3D>>& object_points,
        uint32_t image_width, uint32_t image_height);

    CalibrationParams2D params() const;
    void set_params(const CalibrationParams2D& p);

    bool save(const std::string& path) const;
    bool load(const std::string& path);

private:
    CalibrationParams2D params_;
};

} // namespace stereo_camera
