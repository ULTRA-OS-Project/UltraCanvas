// UltraCloud/core/UltraCloudHttp.cpp
// Version: 0.7.0 - DownloadToFile continues a partial copy (SetThreadDownloadContinues)
// Version: 0.6.0 - DownloadToFile: a download is written to disk as it arrives
// Version: 0.5.0 - a request called off (the thread's abort check) is Cancelled,
//                  not a network failure
// Version: 0.4.0 - a throttled request is sent again after the wait the
//                  service asks for (RateLimitPolicy)
// Last Modified: 2026-10-20
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudHttp.h>
#include <UltraCloud/UltraCloudLog.h>

#include <UltraCanvasPathUtf8.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

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

long long AskedWait(const std::string& retryAfter) {
    return RetryAfterSeconds(retryAfter, static_cast<long long>(std::time(nullptr)));
}

// "it asks to wait 30 s" / "it gives no time to wait".
std::string WaitText(const std::string& retryAfter) {
    const long long seconds = AskedWait(retryAfter);
    if (seconds < 0) return "it gives no time to wait";
    return "it asks to wait " + std::to_string(seconds) + " s";
}

thread_local RateLimitPolicy t_rateLimit;

} // namespace

RateLimitPolicy SetThreadRateLimitPolicy(RateLimitPolicy policy) {
    RateLimitPolicy previous = std::move(t_rateLimit);
    t_rateLimit = std::move(policy);
    return previous;
}

HttpProviderBase::HttpProviderBase(HttpFn http) : http_(std::move(http)) {
    if (!http_) http_ = [](const UltraNetHttpRequest& req, UltraNetResponse& resp) {
        return UltraNet_HttpRequest(req, resp);
    };
}

UltraNetResult HttpProviderBase::Send(const Credentials& credentials, UltraNetHttpRequest request,
                                      UltraNetResponse& response) const {
    return SendWith(credentials, std::move(request), response, nullptr);
}

UltraNetResult HttpProviderBase::SendWith(
        const Credentials& credentials, UltraNetHttpRequest request,
        UltraNetResponse& response,
        const std::function<void(UltraNetResponse&)>& afterAnswer) const {
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
    // A copy: the wait may run code that sets another policy on this thread.
    const RateLimitPolicy policy = t_rateLimit;
    const int tries = 1 + (policy.maxRetries > 0 ? policy.maxRetries : 0);

    UltraNetResult net;
    for (int attempt = 1;; ++attempt) {
        if (logging)
            LogToThread(LogKind::Request, MethodText(request) + " " + LoggableUrl(request.url));
        response = UltraNetResponse{};
        net = http_(request, response);
        if (afterAnswer) afterAnswer(response);

        const int status = response.statusCode;
        const std::string took = Duration(response.elapsedTime);
        if (status <= 0) {
            if (logging) {
                std::string line = "No answer: " + (net.message.empty() ? std::string("the request failed")
                                                                        : net.message);
                if (!took.empty()) line += " (after " + took + ")";
                LogToThread(LogKind::Error, line);
            }
            return net;
        }
        const std::string reason = status >= 400 ? ServerErrorReason(BodyText(response)) : "";
        const std::string retryAfter = response.headers.Get("Retry-After");
        if (logging) {
            std::string line = std::to_string(status);
            if (!response.statusMessage.empty()) line += " " + response.statusMessage;
            if (!reason.empty()) line += " - " + reason;
            if (!took.empty()) line += reason.empty() ? " - " + took : " (" + took + ")";
            LogToThread(LogKind::Response, line, status);
        }
        if (!IsThrottled(status, retryAfter, reason)) return net;

        // Throttled: wait as long as the service asks - or 1, 2, 4 s when it
        // does not say - and send the same request again.
        const std::string limiting = "The service is limiting requests: " + WaitText(retryAfter);
        long long wait = AskedWait(retryAfter);
        if (wait < 0) wait = 1LL << (attempt - 1 < 10 ? attempt - 1 : 10);
        if (wait < 1) wait = 1;   // "0": come back at once - not in the same breath
        if (tries == 1) {
            if (logging) LogToThread(LogKind::Step, limiting);
            return net;
        }
        if (attempt >= tries) {
            if (logging)
                LogToThread(LogKind::Step, limiting + " - not trying again after " +
                                           std::to_string(tries) + " tries");
            return net;
        }
        if (wait > policy.maxWaitSeconds) {
            if (logging)
                LogToThread(LogKind::Step, limiting + " - longer than a request waits (" +
                                           std::to_string(policy.maxWaitSeconds) +
                                           " s), so not trying again");
            return net;
        }
        if (logging)
            LogToThread(LogKind::Step, limiting + " - trying again in " + std::to_string(wait) +
                                       " s (try " + std::to_string(attempt + 1) + " of " +
                                       std::to_string(tries) + ")");
        bool waited = true;
        if (policy.wait) {
            waited = policy.wait(wait);
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(wait));
        }
        if (!waited) {
            if (logging) LogToThread(LogKind::Step, "Stopped waiting - the request is given up");
            return net;
        }
    }
}

Result HttpProviderBase::DownloadToFile(const Credentials& credentials,
                                        UltraNetHttpRequest request,
                                        const std::string& localPath,
                                        const std::string& what) const {
    if (localPath.empty())
        return Result::Error(ResultCode::InvalidArgument, what + ": no local file given");
    // As asked, for a second try from the start (a 416 below).
    const UltraNetHttpRequest asked = request;

    // Continuing a partial copy (SetThreadDownloadContinues): the bytes past
    // its size are asked for (Range), and a 206 appends them. A server that
    // ignores the range answers 200 with the whole file, which replaces it.
    const bool continuing = DownloadContinues();
    int64_t have = 0;
    if (continuing) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(UltraCanvas::PathFromUtf8(localPath), ec);
        if (!ec) have = static_cast<int64_t>(size);
    }
    if (have > 0) request.options.resumeFromOffset = have;

    // Shared with the transfer's callbacks, which run on this thread inside
    // the request - once per try when a throttled request is sent again.
    struct Target {
        std::filesystem::path path;
        std::ofstream out;
        bool made = false;         // the file exists because of this download
        bool accepted = false;     // the answer being read is a 2xx
        bool writeFailed = false;
        int64_t have = 0;          // the partial copy being continued
        std::vector<uint8_t> refusal;   // a non-2xx answer's body, for its reason
        // 206 to a range: the rest, appended. Anything else is the whole
        // file, from the start.
        std::ios::openmode ModeFor(int status) const {
            return std::ios::binary | (status == 206 && have > 0 ? std::ios::app
                                                                 : std::ios::trunc);
        }
    };
    auto target = std::make_shared<Target>();
    target->path = UltraCanvas::PathFromUtf8(localPath);
    target->have = have;
    // A refusal's body is a few hundred bytes of JSON or XML; a service that
    // sends a page instead is not kept whole.
    static constexpr std::size_t kRefusalKept = 64 * 1024;

    request.onHeaders = [target](const UltraNetResponse& answer) {
        target->accepted = answer.statusCode >= 200 && answer.statusCode < 300;
        target->refusal.clear();
        if (!target->accepted || target->made) return true;
        target->out.open(target->path, target->ModeFor(answer.statusCode));
        target->made = true;
        if (!target->out) {
            target->writeFailed = true;
            return false;   // ends the transfer: nowhere to put it
        }
        return true;
    };
    request.onDataChunk = [target](const std::vector<uint8_t>& chunk) {
        if (!target->accepted) {
            const std::size_t room = kRefusalKept - std::min(kRefusalKept, target->refusal.size());
            target->refusal.insert(target->refusal.end(), chunk.begin(),
                                   chunk.begin() + std::min(room, chunk.size()));
            return;
        }
        if (target->writeFailed || !target->out.is_open()) return;
        target->out.write(reinterpret_cast<const char*>(chunk.data()),
                          static_cast<std::streamsize>(chunk.size()));
        if (!target->out) target->writeFailed = true;
    };
    // A download is as long as the file is large: no limit on the whole
    // transfer, only on a server that has stopped sending.
    if (request.options.timeoutMs == 0) request.options.timeoutMs = -1;
    if (request.options.stallTimeoutMs == 0) request.options.stallTimeoutMs = 30000;

    UltraNetResponse response;
    const UltraNetResult net = SendWith(
            credentials, std::move(request), response, [target](UltraNetResponse& answer) {
                // The refusal's body where Send and FromHttp read a reason
                // from - Google Drive's rate-limit 403 is told apart by it.
                if (!target->accepted && answer.body.empty())
                    answer.body = target->refusal;
            });
    if (target->out.is_open()) {
        target->out.close();
        if (!target->out) target->writeFailed = true;
    }
    // A transport that hands the body over whole instead of in chunks (a
    // test's fake, a stand-in): written now, the old way.
    if (!target->made && net && response.statusCode >= 200 && response.statusCode < 300) {
        std::ofstream out(target->path, target->ModeFor(response.statusCode));
        target->made = true;
        out.write(reinterpret_cast<const char*>(response.body.data()),
                  static_cast<std::streamsize>(response.body.size()));
        out.close();
        if (!out) target->writeFailed = true;
    }

    // 416 to a range: the partial copy is not the start of this file - it is
    // the whole of it (Content-Range "bytes */<size>" names its size), or the
    // file on the server has changed. Done, or fetched again from the start.
    if (have > 0 && response.statusCode == 416) {
        const std::string range = response.headers.Get("Content-Range");
        const std::size_t slash = range.find('/');
        const long long total =
                slash == std::string::npos ? -1 : std::atoll(range.c_str() + slash + 1);
        if (total == have) return Result::Ok();
        std::error_code ec;
        std::filesystem::remove(target->path, ec);
        const bool previous = SetThreadDownloadContinues(false);
        Result again = DownloadToFile(credentials, asked, localPath, what);
        SetThreadDownloadContinues(previous);
        return again;
    }

    // What a failure leaves behind: nothing - unless the download continues a
    // partial copy, whose caller keeps what has arrived for the next try.
    auto discard = [&target, continuing]() {
        if (!target->made || continuing) return;
        std::error_code ec;
        std::filesystem::remove(target->path, ec);
    };
    if (target->writeFailed) {
        discard();
        return Result::Error(ResultCode::IoError, what + ": cannot write " + localPath);
    }
    Result r = FromHttp(net, response, what);
    if (!r) {
        discard();   // half a file is no file
        return r;
    }
    return Result::Ok();
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
    } else if (!net && net.code == UltraNetResultCode::Cancelled) {
        // Called off by the caller (UltraNet_SetThreadAbortCheck): nothing
        // went wrong with the network.
        r = Result::Error(ResultCode::Cancelled, what + ": called off", status);
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
