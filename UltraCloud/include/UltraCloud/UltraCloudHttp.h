// UltraCloud/include/UltraCloud/UltraCloudHttp.h
// The HTTP base every network provider builds on: one injectable request
// function (UltraNet_HttpRequest by default, a fake in tests), Basic /
// Bearer auth from the credentials, and the HTTP → Result mapping.
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudProvider.h"

#include <UltraNet/UltraNetHttp.h>

#include <functional>
#include <string>

namespace UltraCloud {

// The HTTP seam: UltraNet_HttpRequest by default, a fake in tests.
using HttpFn = std::function<UltraNetResult(const UltraNetHttpRequest&, UltraNetResponse&)>;

class HttpProviderBase : public ICloudProvider {
public:
    explicit HttpProviderBase(HttpFn http = nullptr);

protected:
    // One request with Bearer (token) or Basic (password) auth applied.
    UltraNetResult Send(const Credentials& credentials, UltraNetHttpRequest request,
                        UltraNetResponse& response) const;
    // Map an HTTP outcome onto a Result (2xx → Ok; 401/403 → AuthFailed;
    // 404 → NotFound; other 4xx/5xx → Server; transport failure → Network).
    static Result FromHttp(const UltraNetResult& net, const UltraNetResponse& response,
                           const std::string& what);
    static std::string BodyText(const UltraNetResponse& response) {
        return std::string(response.body.begin(), response.body.end());
    }

    HttpFn http_;
};

} // namespace UltraCloud
