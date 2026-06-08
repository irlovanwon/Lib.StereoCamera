#pragma once

#include "Types.h"
#include <string>
#include <functional>

namespace stereo_camera {

using ResponseCallback = std::function<void(const Response&)>;

Response make_response(ResponseCode code, const std::string& message = "");
Response make_response(ResponseCode code, const std::string& message, nlohmann::json detail);

} // namespace stereo_camera
