#include "stereo_camera/common/Response.h"

namespace stereo_camera {

Response make_response(ResponseCode code, const std::string& message) {
    return Response{code, message, nlohmann::json::object()};
}

Response make_response(ResponseCode code, const std::string& message, nlohmann::json detail) {
    return Response{code, message, std::move(detail)};
}

} // namespace stereo_camera
