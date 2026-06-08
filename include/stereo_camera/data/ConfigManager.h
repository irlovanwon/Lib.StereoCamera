#pragma once

#include "stereo_camera/common/Parameter.h"
#include <string>
#include <memory>

namespace stereo_camera {

class ConfigManager {
public:
    explicit ConfigManager(const std::string& config_dir);

    void set_parameter_manager(std::shared_ptr<ParameterManager> param_mgr);
    bool load(const std::string& filename);
    bool save(const std::string& filename);
    bool sync();

private:
    std::string config_dir_;
    std::shared_ptr<ParameterManager> param_mgr_;
};

} // namespace stereo_camera
