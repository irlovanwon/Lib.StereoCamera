#include "stereo_camera/calibration/Calibration3D.h"
#include "stereo_camera/common/Logger.h"
#include <fstream>
#include <cmath>

namespace stereo_camera {

nlohmann::json StereoCalibrationParams::to_json() const {
    nlohmann::json j;
    j["left"] = left.to_json();
    j["right"] = right.to_json();
    j["R"] = std::vector<double>(R.data, R.data + 9);
    j["T"] = std::vector<double>(T.data, T.data + 3);
    return j;
}

StereoCalibrationParams StereoCalibrationParams::from_json(const nlohmann::json& j) {
    StereoCalibrationParams p;
    p.left = CalibrationParams2D::from_json(j.value("left", nlohmann::json::object()));
    p.right = CalibrationParams2D::from_json(j.value("right", nlohmann::json::object()));
    auto R = j.value("R", std::vector<double>{1,0,0,0,1,0,0,0,1});
    auto T = j.value("T", std::vector<double>{0,0,0});
    for (int i = 0; i < 9 && i < (int)R.size(); ++i) p.R.data[i] = R[i];
    for (int i = 0; i < 3 && i < (int)T.size(); ++i) p.T.data[i] = T[i];
    return p;
}

StereoCalibrationParams Calibration3D::calibrate(
    const std::vector<std::vector<Point2D>>& left_points,
    const std::vector<std::vector<Point2D>>& right_points,
    const std::vector<std::vector<Point3D>>& object_points,
    uint32_t image_width, uint32_t image_height) {

    if (left_points.empty() || left_points.size() != right_points.size()) {
        Logger::instance().warn("Calibration3D", "Invalid input for stereo calibration");
        return params_;
    }

    Calibration2D cal_left, cal_right;
    params_.left = cal_left.calibrate(left_points, object_points, image_width, image_height);
    params_.right = cal_right.calibrate(right_points, object_points, image_width, image_height);

    size_t n_views = left_points.size();
    double sum_dx = 0;
    for (size_t v = 0; v < n_views; ++v)
        for (size_t i = 0; i < left_points[v].size(); ++i)
            sum_dx += (left_points[v][i].x - right_points[v][i].x);
    double mean_disp = sum_dx / (n_views * left_points[0].size());

    if (params_.left.fx > 0 && mean_disp > 0.1) {
        params_.T.data[0] = params_.left.fx / mean_disp;
    }

    Logger::instance().info("Calibration3D",
        "Estimated baseline=" + std::to_string(params_.baseline()) +
        "mm, mean disparity=" + std::to_string(mean_disp) + "px");

    return params_;
}

StereoCalibrationParams Calibration3D::params() const { return params_; }

void Calibration3D::set_params(const StereoCalibrationParams& p) { params_ = p; }

bool Calibration3D::save(const std::string& path) const {
    std::ofstream file(path);
    if (!file) return false;
    file << params_.to_json().dump(2);
    Logger::instance().info("Calibration3D", "Saved to " + path);
    return true;
}

bool Calibration3D::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(file);
        params_ = StereoCalibrationParams::from_json(j);
        Logger::instance().info("Calibration3D", "Loaded from " + path);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace stereo_camera
