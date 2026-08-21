// UltraAI/adapters/_shared/src/ScriptedTransport.cpp
// Implementation of TransportResponse::GetHeader and ScriptedTransport.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAITransport.h"

#include <algorithm>
#include <cctype>

namespace UltraAI {

namespace {
bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}
} // namespace

std::string TransportResponse::GetHeader(const std::string& name) const {
    for (const auto& kv : headers) {
        if (EqualsIgnoreCase(kv.first, name)) return kv.second;
    }
    return {};
}

void ScriptedTransport::ScriptResponse(TransportResponse response) {
    Exchange ex;
    ex.response = std::move(response);
    script_.push_back(std::move(ex));
}

void ScriptedTransport::ScriptError(Error error) {
    Exchange ex;
    ex.isError = true;
    ex.error   = std::move(error);
    script_.push_back(std::move(ex));
}

void ScriptedTransport::ScriptSse(std::vector<TransportSseEvent> events,
                                  Error finalError, int statusCode) {
    Exchange ex;
    ex.isSse         = true;
    ex.events        = std::move(events);
    ex.error         = std::move(finalError);
    ex.sseStatusCode = statusCode;
    script_.push_back(std::move(ex));
}

TransportResponse ScriptedTransport::Request(const TransportRequest& request,
                                             Error* outError) {
    requests_.push_back(request);

    if (script_.empty() || script_.front().isSse) {
        if (outError) {
            outError->code    = ErrorCode::ProviderError;
            outError->message = "ScriptedTransport: unscripted Request to " +
                                request.url;
        }
        return {};
    }

    Exchange ex = std::move(script_.front());
    script_.pop_front();

    if (ex.isError) {
        if (outError) *outError = ex.error;
        return {};
    }
    if (outError) *outError = {};
    return ex.response;
}

CancelFn ScriptedTransport::SseStream(const TransportRequest& request,
                                      SseEventCallback onEvent,
                                      SseCompleteCallback onComplete) {
    requests_.push_back(request);

    if (script_.empty() || !script_.front().isSse) {
        Error e;
        e.code    = ErrorCode::ProviderError;
        e.message = "ScriptedTransport: unscripted SseStream to " + request.url;
        if (onComplete) onComplete(e, 0);
        return [this] { cancelled_ = true; };
    }

    Exchange ex = std::move(script_.front());
    script_.pop_front();

    if (onEvent) {
        for (const auto& ev : ex.events) onEvent(ev);
    }
    if (onComplete) onComplete(ex.error, ex.sseStatusCode);
    return [this] { cancelled_ = true; };
}

} // namespace UltraAI
