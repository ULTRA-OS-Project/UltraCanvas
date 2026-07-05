// core/UltraNet/UltraNetSse.cpp
// SSE parser + HTTP streamers. The parser is the meaningful part; the two
// streamer functions are thin wrappers that feed bytes from
// UltraNet_HttpRequest / UltraNet_HttpRequestAsync into the parser.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetSse.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

// ============================================================================
// UltraNetSseParser
// ============================================================================
void UltraNetSseParser::Feed(
    const std::vector<uint8_t>& chunk,
    const std::function<void(const UltraNetSseEvent&)>& onEvent) {
    buffer_.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());

    std::size_t pos = 0;
    while (true) {
        std::size_t nl = buffer_.find('\n', pos);
        if (nl == std::string::npos) break;

        std::string line = buffer_.substr(pos, nl - pos);
        pos = nl + 1;
        // Strip trailing CR for CRLF servers.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        DispatchLine(line, onEvent);
    }
    buffer_.erase(0, pos);
}

void UltraNetSseParser::Flush(
    const std::function<void(const UltraNetSseEvent&)>& onEvent) {
    if (!buffer_.empty()) {
        if (!buffer_.empty() && buffer_.back() == '\r') buffer_.pop_back();
        DispatchLine(buffer_, onEvent);
        buffer_.clear();
    }
    // Terminating blank line equivalent: dispatch any in-progress event.
    if (!current_.data.empty() || !current_.event.empty() || !current_.id.empty()) {
        if (!current_.data.empty() && current_.data.back() == '\n') {
            current_.data.pop_back();
        }
        if (onEvent) onEvent(current_);
        current_ = {};
    }
}

void UltraNetSseParser::DispatchLine(
    const std::string& line,
    const std::function<void(const UltraNetSseEvent&)>& onEvent) {

    if (line.empty()) {
        // Blank line - dispatch the event we've been accumulating.
        if (!current_.data.empty() || !current_.event.empty() || !current_.id.empty() ||
            current_.retry > 0) {
            // SSE spec: the trailing '\n' in data is implied by the next
            // field separator; we add '\n' after every data: line and strip
            // exactly one at dispatch time.
            if (!current_.data.empty() && current_.data.back() == '\n') {
                current_.data.pop_back();
            }
            if (onEvent) onEvent(current_);
        }
        current_ = {};
        return;
    }
    if (line[0] == ':') return;     // comment - ignore per spec

    std::size_t colon = line.find(':');
    std::string field, value;
    if (colon == std::string::npos) {
        field = line;
    } else {
        field = line.substr(0, colon);
        value = line.substr(colon + 1);
        // Spec: a single leading space in value is stripped.
        if (!value.empty() && value.front() == ' ') value.erase(0, 1);
    }
    if (field == "data") {
        current_.data += value;
        current_.data.push_back('\n');
    } else if (field == "event") {
        current_.event = value;
    } else if (field == "id") {
        // Spec: if the value contains a NUL the field is ignored.
        if (value.find('\0') == std::string::npos) current_.id = value;
    } else if (field == "retry") {
        // Spec: only set if the value is all ASCII digits.
        bool digits = !value.empty();
        for (char c : value) if (c < '0' || c > '9') { digits = false; break; }
        if (digits) current_.retry = std::atoi(value.c_str());
    }
    // Any other field name is silently ignored per spec.
}

// ============================================================================
// HTTP front-ends
// ============================================================================
namespace {

void DecorateForSse(UltraNetHttpRequest& req) {
    req.method = UltraNetHttpMethod::Get;
    // Set default SSE-friendly headers if the caller didn't supply them.
    if (!req.options.headers.Has("Accept")) {
        req.options.headers.Set("Accept", "text/event-stream");
    }
    if (!req.options.headers.Has("Cache-Control")) {
        req.options.headers.Set("Cache-Control", "no-cache");
    }
    // SSE expects long-lived connections.
    if (req.options.timeoutMs == 0) {
        req.options.timeoutMs = 0;   // 0 here means "use config default"; SSE
                                     // is unbounded anyway — most users
                                     // override via the request.
    }
}

} // namespace

UltraNetResult UltraNet_SseStream(
    const std::string& url,
    std::function<void(const UltraNetSseEvent&)> onEvent,
    const UltraNetHttpOptions& options) {
    if (!onEvent) {
        return UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                     "onEvent callback is required");
    }
    auto parser = std::make_shared<UltraNetSseParser>();

    UltraNetHttpRequest req;
    req.url     = url;
    req.options = options;
    DecorateForSse(req);
    req.onDataChunk = [parser, onEvent](const std::vector<uint8_t>& chunk) {
        parser->Feed(chunk, onEvent);
    };

    UltraNetResponse resp;
    UltraNetResult r = UltraNet_HttpRequest(req, resp);
    // Drain any trailing partial line — most servers terminate with a blank
    // line but explicit Flush ensures last-event-before-disconnect is seen.
    parser->Flush(onEvent);
    return r;
}

UltraNetHandle UltraNet_SseStreamAsync(
    const UltraNetHttpRequest& request,
    std::function<void(const UltraNetSseEvent&)> onEvent,
    std::function<void(const UltraNetResult&)>    onComplete) {
    if (!onEvent) {
        if (onComplete) {
            onComplete(UltraNetResult::Error(UltraNetResultCode::InvalidState,
                                             "onEvent callback is required"));
        }
        return UltraNetInvalidHandle;
    }
    auto parser = std::make_shared<UltraNetSseParser>();

    UltraNetHttpRequest req = request;
    DecorateForSse(req);
    req.onDataChunk = [parser, onEvent](const std::vector<uint8_t>& chunk) {
        parser->Feed(chunk, onEvent);
    };

    return UltraNet_HttpRequestAsync(req,
        [parser, onEvent, onComplete](const UltraNetResponse& resp) {
            parser->Flush(onEvent);
            if (onComplete) {
                UltraNetResult r;
                r.code        = (resp.statusCode >= 200 && resp.statusCode < 300)
                                    ? UltraNetResultCode::Success
                                    : UltraNetResultCode::HttpError;
                r.success     = (r.code == UltraNetResultCode::Success);
                r.httpStatus  = resp.statusCode;
                r.message     = resp.statusMessage;
                r.processingTime = resp.elapsedTime;
                onComplete(r);
            }
        });
}
