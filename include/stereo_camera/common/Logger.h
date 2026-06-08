#pragma once

#include "Types.h"
#include <string>
#include <nlohmann/json.hpp>

namespace stereo_camera {

class Logger {
public:
    enum class Level { Debug, Info, Warn, Error };

    static Logger& instance();

    void set_level(Level level);
    void log(Level level, const std::string& category, const std::string& message);

    void debug(const std::string& category, const std::string& msg);
    void info(const std::string& category, const std::string& msg);
    void warn(const std::string& category, const std::string& msg);
    void error(const std::string& category, const std::string& msg);

private:
    Logger() = default;
    Level level_ = Level::Info;
};

} // namespace stereo_camera
