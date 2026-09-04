// UltraCloud/core/UltraCloudHttp.cpp
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudHttp.h>

namespace UltraCloud {

HttpProviderBase::HttpProviderBase(HttpFn http) : http_(std::move(http)) {
    if (!http_) http_ = [](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        return UltraNet_HttpRequest(req, resp);
    };
}

UltraNetResult HttpProviderBase::Send(const Credentials& credentials, UltraNetHttpRequest request,
                                      UltraNetResponse& response) const {
    if (!credentials.token.empty()) {
        request.options.authType = UltraNetAuthType::Bearer;
        request.options.credentials.type = UltraNetAuthType::Bearer;
        request.options.credentials.token = credentials.token;
        // Some servers only honour the explicit header; set both.
        if (!request.headers.Has("Authorization"))
            request.headers.Set("Authorization", "Bearer " + credentials.token);
    } else if (!credentials.username.empty()) {
        request.options.authType = UltraNetAuthType::Basic;
        request.options.credentials.type = UltraNetAuthType::Basic;
        request.options.credentials.username = credentials.username;
        request.options.credentials.password = credentials.password;
    }
    if (request.options.timeoutMs == 0) request.options.timeoutMs = 120000;
    return http_(request, response);
}

Result HttpProviderBase::FromHttp(const UltraNetResult& net, const UltraNetResponse& response,
                                  const std::string& what) {
    const int status = response.statusCode;
    if (status == 401 || status == 403)
        return Result::Error(ResultCode::AuthFailed, what + ": sign-in rejected", status);
    if (status == 404)
        return Result::Error(ResultCode::NotFound, what + ": not found", status);
    if (status >= 400)
        return Result::Error(ResultCode::Server, what + ": HTTP " + std::to_string(status), status);
    if (!net)
        return Result::Error(ResultCode::Network, what + ": " + net.message, status);
    return Result::Ok();
}

} // namespace UltraCloud
