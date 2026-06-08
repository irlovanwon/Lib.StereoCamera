#pragma once

#include "Types.h"
#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace stereo_camera {

struct Parameter {
    std::string name;
    ParameterType type;
    ParameterValue value;
    bool is_readonly;
    bool is_available;

    nlohmann::json to_json() const;
    static Parameter from_json(const nlohmann::json& j);
};

class ParameterManager {
public:
    void define(const Parameter& param);
    Parameter get(const std::string& name) const;
    bool set(const std::string& name, const ParameterValue& value);
    std::vector<Parameter> list() const;
    nlohmann::json to_json() const;
    void from_json(const nlohmann::json& j);

private:
    std::unordered_map<std::string, Parameter> params_;
};

} // namespace stereo_camera
