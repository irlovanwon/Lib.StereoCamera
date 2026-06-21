#include "stereo_camera/calibration/Calibration2D.h"
#include "stereo_camera/common/Logger.h"
#include <fstream>
#include <cmath>
#include <nlohmann/json.hpp>

namespace stereo_camera {

nlohmann::json CalibrationParams2D::to_json() const {
    nlohmann::json j;
    j["fx"] = fx;
    j["fy"] = fy;
    j["cx"] = cx;
    j["cy"] = cy;
    j["distortion"] = distortion;
    return j;
}

CalibrationParams2D CalibrationParams2D::from_json(const nlohmann::json& j) {
    CalibrationParams2D p;
    p.fx = j.value("fx", 0.0);
    p.fy = j.value("fy", 0.0);
    p.cx = j.value("cx", 0.0);
    p.cy = j.value("cy", 0.0);
    p.distortion = j.value("distortion", std::vector<double>{});
    return p;
}

CalibrationParams2D Calibration2D::calibrate(
    const std::vector<std::vector<Point2D>>& image_points,
    const std::vector<std::vector<Point3D>>& object_points,
    uint32_t image_width, uint32_t image_height) {

    if (image_points.empty() || image_points.size() != object_points.size()) {
        Logger::instance().warn("Calibration2D", "Invalid input for calibration");
        return params_;
    }

    size_t n_views = image_points.size();
    size_t n_pts = image_points[0].size();

    double sum_x = 0, sum_y = 0;
    for (const auto& view : image_points)
        for (const auto& pt : view) { sum_x += pt.x; sum_y += pt.y; }
    double total_pts = n_views * n_pts;

    params_.cx = sum_x / total_pts;
    params_.cy = sum_y / total_pts;

    double mean_dist = 0;
    for (const auto& view : image_points)
        for (const auto& pt : view) {
            double dx = pt.x - params_.cx;
            double dy = pt.y - params_.cy;
            mean_dist += std::sqrt(dx * dx + dy * dy);
        }
    mean_dist /= total_pts;

    double scale = mean_dist / std::sqrt(2.0);
    params_.fx = scale;
    params_.fy = scale;
    params_.distortion.assign(5, 0.0);

    Logger::instance().info("Calibration2D",
        "Estimated intrinsics: fx=" + std::to_string(params_.fx) +
        " fy=" + std::to_string(params_.fy) +
        " cx=" + std::to_string(params_.cx) +
        " cy=" + std::to_string(params_.cy) +
        " (approximate — " + std::to_string(n_views) + " views, " + std::to_string(n_pts) + " pts)");

    return params_;
}

CalibrationParams2D Calibration2D::params() const { return params_; }

void Calibration2D::set_params(const CalibrationParams2D& p) { params_ = p; }

bool Calibration2D::save(const std::string& path) const {
    std::ofstream file(path);
    if (!file) return false;
    file << params_.to_json().dump(2);
    Logger::instance().info("Calibration2D", "Saved to " + path);
    return true;
}

bool Calibration2D::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(file);
        params_ = CalibrationParams2D::from_json(j);
        Logger::instance().info("Calibration2D", "Loaded from " + path);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace stereo_camera
