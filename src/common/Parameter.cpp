#include "stereo_camera/common/Parameter.h"

namespace stereo_camera {

nlohmann::json Parameter::to_json() const {
    nlohmann::json j;
    j["name"] = name;
    j["is_readonly"] = is_readonly;
    j["is_available"] = is_available;
    switch (type) {
        case ParameterType::Integer:
            j["value"] = value.int_val;
            j["type"] = "integer";
            break;
        case ParameterType::Float:
            j["value"] = value.float_val;
            j["type"] = "float";
            break;
        case ParameterType::Enum:
            j["value"] = value.enum_val;
            j["type"] = "enum";
            break;
    }
    return j;
}

Parameter Parameter::from_json(const nlohmann::json& j) {
    Parameter p;
    p.name = j.at("name").get<std::string>();
    p.is_readonly = j.at("is_readonly").get<bool>();
    p.is_available = j.at("is_available").get<bool>();
    std::string t = j.at("type").get<std::string>();
    if (t == "integer") {
        p.type = ParameterType::Integer;
        p.value.int_val = j.at("value").get<int>();
        p.value.type = ParameterType::Integer;
    } else if (t == "float") {
        p.type = ParameterType::Float;
        p.value.float_val = j.at("value").get<double>();
        p.value.type = ParameterType::Float;
    } else if (t == "enum") {
        p.type = ParameterType::Enum;
        p.value.enum_val = j.at("value").get<std::string>();
        p.value.type = ParameterType::Enum;
    }
    return p;
}

void ParameterManager::define(const Parameter& param) {
    params_[param.name] = param;
}

Parameter ParameterManager::get(const std::string& name) const {
    auto it = params_.find(name);
    if (it == params_.end()) {
        Parameter p;
        p.name = name;
        p.is_available = false;
        return p;
    }
    return it->second;
}

bool ParameterManager::set(const std::string& name, const ParameterValue& value) {
    auto it = params_.find(name);
    if (it == params_.end()) return false;
    if (it->second.is_readonly) return false;
    if (!it->second.is_available) return false;
    it->second.value = value;
    return true;
}

std::vector<Parameter> ParameterManager::list() const {
    std::vector<Parameter> result;
    result.reserve(params_.size());
    for (const auto& [_, p] : params_) {
        result.push_back(p);
    }
    return result;
}

nlohmann::json ParameterManager::to_json() const {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& [_, p] : params_) {
        j.push_back(p.to_json());
    }
    return j;
}

void ParameterManager::from_json(const nlohmann::json& j) {
    for (const auto& item : j) {
        auto p = Parameter::from_json(item);
        params_[p.name] = p;
    }
}

} // namespace stereo_camera
