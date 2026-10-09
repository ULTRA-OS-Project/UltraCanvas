// UltraCloud/include/UltraCloud/UltraCloudHttp.h
// The HTTP base every network provider builds on: one injectable request
// function (UltraNet_HttpRequest by default, a fake in tests), Basic /
// Bearer auth from the credentials, and the HTTP → Result mapping.
// Version: 0.3.0 - Send logs each request; FromHttp keeps the service's reason
// Last Modified: 2026-10-09
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
    // One request with Bearer (token) or Basic (password) auth applied. Its
    // method and URL, and the answer, go to the thread's log (UltraCloudLog.h).
    UltraNetResult Send(const Credentials& credentials, UltraNetHttpRequest request,
                        UltraNetResponse& response) const;
    // Map an HTTP outcome onto a Result (2xx → Ok; 429, or a 503 / Google 403
    // that says so → RateLimited; 401/403 → AuthFailed; 404 → NotFound; other
    // 4xx/5xx → Server; transport failure → Network). A refusal's message
    // carries the status and the service's own reason ("list /x: not found
    // (HTTP 404 - path/not_found)"), and its diagnostics the request ID, the
    // wait asked for and what the transport knew about the connection.
    static Result FromHttp(const UltraNetResult& net, const UltraNetResponse& response,
                           const std::string& what);
    static std::string BodyText(const UltraNetResponse& response) {
        return std::string(response.body.begin(), response.body.end());
    }

    HttpFn http_;
};

} // namespace UltraCloud
