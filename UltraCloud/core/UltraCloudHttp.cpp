// UltraCloud/core/UltraCloudHttp.cpp
// Version: 0.3.0 - every request logged (UltraCloudLog.h); a refusal keeps the
//                  service's own reason, a throttled request says so, and a
//                  failure carries its diagnostics
// Last Modified: 2026-10-09
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudHttp.h>
#include <UltraCloud/UltraCloudLog.h>

#include <cmath>
#include <ctime>

namespace UltraCloud {

namespace {

std::string MethodText(const UltraNetHttpRequest& request) {
    switch (request.method) {
        case UltraNetHttpMethod::Get:     return "GET";
        case UltraNetHttpMethod::Post:    return "POST";
        case UltraNetHttpMethod::Put:     return "PUT";
        case UltraNetHttpMethod::Delete:  return "DELETE";
        case UltraNetHttpMethod::Head:    return "HEAD";
        case UltraNetHttpMethod::Patch:   return "PATCH";
        case UltraNetHttpMethod::Options: return "OPTIONS";
        case UltraNetHttpMethod::Connect: return "CONNECT";
        case UltraNetHttpMethod::Trace:   return "TRACE";
        case UltraNetHttpMethod::Custom:
            return request.customMethod.empty() ? "?" : request.customMethod;
    }
    return "?";
}

// "420 ms" / "3.25 s", from integers: a time in the log is not a number
// for the locale to put a comma in.
std::string Duration(double seconds) {
    if (seconds <= 0) return "";
    const long long ms = std::llround(seconds * 1000.0);
    if (ms < 1000) return std::to_string(ms) + " ms";
    const long long hundredths = (ms % 1000) / 10;
    return std::to_string(ms / 1000) + "." + (hundredths < 10 ? "0" : "") +
           std::to_string(hundredths) + " s";
}

// The header a service names the request by in its own logs - what its
// support asks for.
std::string RequestId(const UltraNetResponse& response) {
    for (const char* name : {"x-dropbox-request-id", "request-id", "x-ms-request-id",
                             "x-request-id", "x-guploader-uploadid", "x-amz-request-id",
                             "x-nextcloud-request-id"}) {
        const std::string id = response.headers.Get(name);
        if (!id.empty()) return id;
    }
    return {};
}

// Whether the service is limiting requests: 429, a 503 that says when to come
// back, or Google Drive's 403 with a rate-limit reason.
bool IsThrottled(int status, const std::string& retryAfter, const std::string& reason) {
    if (status == 429) return true;
    if (status == 503 && !retryAfter.empty()) return true;
    if (status == 403 && (reason.rfind("rateLimitExceeded", 0) == 0 ||
                          reason.rfind("userRateLimitExceeded", 0) == 0))
        return true;
    return false;
}

// "it asks to wait 30 s" / "it gives no time to wait".
std::string WaitText(const std::string& retryAfter) {
    const long long seconds =
            RetryAfterSeconds(retryAfter, static_cast<long long>(std::time(nullptr)));
    if (seconds < 0) return "it gives no time to wait";
    return "it asks to wait " + std::to_string(seconds) + " s";
}

} // namespace

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

    // The method and the URL go to the log; the headers - Authorization among
    // them - and the body do not.
    const bool logging = ThreadLogActive();
    if (logging) LogToThread(LogKind::Request, MethodText(request) + " " + LoggableUrl(request.url));

    UltraNetResult net = http_(request, response);

    if (logging) {
        const std::string took = Duration(response.elapsedTime);
        const int status = response.statusCode;
        if (status > 0) {
            std::string line = std::to_string(status);
            if (!response.statusMessage.empty()) line += " " + response.statusMessage;
            std::string reason;
            if (status >= 400) reason = ServerErrorReason(BodyText(response));
            if (!reason.empty()) line += " - " + reason;
            if (!took.empty()) line += reason.empty() ? " - " + took : " (" + took + ")";
            LogToThread(LogKind::Response, line, status);
            const std::string retryAfter = response.headers.Get("Retry-After");
            if (IsThrottled(status, retryAfter, reason))
                LogToThread(LogKind::Step, "The service is limiting requests: " +
                                           WaitText(retryAfter));
        } else {
            std::string line = "No answer: " + (net.message.empty() ? std::string("the request failed")
                                                                    : net.message);
            if (!took.empty()) line += " (after " + took + ")";
            LogToThread(LogKind::Error, line);
        }
    }
    return net;
}

Result HttpProviderBase::FromHttp(const UltraNetResult& net, const UltraNetResponse& response,
                                  const std::string& what) {
    const int status = response.statusCode;
    const std::string reason = status >= 400 ? ServerErrorReason(BodyText(response)) : "";
    const std::string retryAfter = response.headers.Get("Retry-After");
    // "(HTTP 409 - path/not_found)": the status and, when the service gave
    // one, its own reason - the part of a refusal that says what to change.
    auto statusText = [&]() {
        return "HTTP " + std::to_string(status) + (reason.empty() ? "" : " - " + reason);
    };

    Result r;
    if (status >= 400 && IsThrottled(status, retryAfter, reason)) {
        r = Result::Error(ResultCode::RateLimited,
                          what + ": the service is limiting requests - " + WaitText(retryAfter) +
                                  " (" + statusText() + ")",
                          status);
    } else if (status == 401) {
        r = Result::Error(ResultCode::AuthFailed, what + ": sign-in rejected (" + statusText() + ")",
                          status);
    } else if (status == 403) {
        r = Result::Error(ResultCode::AuthFailed, what + ": not allowed (" + statusText() + ")",
                          status);
    } else if (status == 404) {
        r = Result::Error(ResultCode::NotFound, what + ": not found (" + statusText() + ")", status);
    } else if (status >= 400) {
        r = Result::Error(ResultCode::Server, what + ": " + statusText(), status);
    } else if (!net) {
        r = Result::Error(ResultCode::Network, what + ": " + net.message, status);
    } else {
        return Result::Ok();
    }

    // How it ended, for a Details view and a bug report: what the service
    // said and how to find the request in its logs, then whatever the
    // transport knew about the connection.
    std::string d;
    auto line = [&d](const char* name, const std::string& value) {
        if (!value.empty()) d += std::string(name) + ": " + value + "\n";
    };
    if (status > 0)
        line("HTTP status", std::to_string(status) +
                                    (response.statusMessage.empty() ? "" : " " + response.statusMessage));
    line("Service's reason", reason);
    line("Retry-After", retryAfter);
    line("Request ID", RequestId(response));
    line("Answered by", LoggableUrl(response.finalUrl));
    line("Took", Duration(response.elapsedTime));
    r.diagnostics = d + net.diagnostics;
    return r;
}

} // namespace UltraCloud
