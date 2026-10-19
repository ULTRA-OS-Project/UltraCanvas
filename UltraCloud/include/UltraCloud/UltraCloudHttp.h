// UltraCloud/include/UltraCloud/UltraCloudHttp.h
// The HTTP base every network provider builds on: one injectable request
// function (UltraNet_HttpRequest by default, a fake in tests), Basic /
// Bearer auth from the credentials, the HTTP → Result mapping, and the wait
// and retry when a service limits requests (RateLimitPolicy).
// Version: 0.4.0 - a throttled request is sent again after the wait the
//                  service asks for (RateLimitPolicy, per thread)
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCloudProvider.h"

#include <UltraNet/UltraNetHttp.h>

#include <functional>
#include <string>

namespace UltraCloud {

// The HTTP seam: UltraNet_HttpRequest by default, a fake in tests.
using HttpFn = std::function<UltraNetResult(const UltraNetHttpRequest&, UltraNetResponse&)>;

// What HttpProviderBase::Send does when a service limits requests - a 429, a
// 503 that says when to come back, Google Drive's rate-limit 403: it waits as
// long as the service asks (Retry-After), or 1, 2, 4 s when it does not say,
// and sends the request again. The service did not act on a throttled
// request, so sending it again is safe for every method. Each wait is a line
// in the thread's log (UltraCloudLog.h).
struct RateLimitPolicy {
    // Tries after the first. 0 reports the first refusal as it is.
    int maxRetries = 3;
    // A longer wait than this is not waited out: the request fails as
    // ResultCode::RateLimited, its message saying how long was asked for.
    long long maxWaitSeconds = 30;
    // Waits `seconds` and returns true, or returns false to stop waiting and
    // fail now - a worker being shut down. Empty: the thread sleeps.
    std::function<bool(long long seconds)> wait;
};

// Sets the policy for requests made on the CALLING THREAD and returns the
// previous one, so a caller can put it back; a thread that never sets one
// gets RateLimitPolicy{}. Per thread, like the log sink: a worker that can be
// stopped passes a wait it can interrupt, and work nobody waits on (a folder
// read ahead) can choose not to wait at all.
RateLimitPolicy SetThreadRateLimitPolicy(RateLimitPolicy policy);

class HttpProviderBase : public ICloudProvider {
public:
    explicit HttpProviderBase(HttpFn http = nullptr);

protected:
    // One request with Bearer (token) or Basic (password) auth applied. Its
    // method and URL, and the answer, go to the thread's log (UltraCloudLog.h).
    // A throttled request is sent again as the thread's RateLimitPolicy says;
    // `response` is the last answer.
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
