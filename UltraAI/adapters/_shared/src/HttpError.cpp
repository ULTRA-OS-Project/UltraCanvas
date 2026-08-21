// UltraAI/adapters/_shared/src/HttpError.cpp
// Implementation of MapHttpStatus / ParseRetryAfterMs.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAIHttpError.h"

#include <cctype>
#include <cstdlib>

namespace UltraAI {

Error MapHttpStatus(int statusCode, const std::string& detail) {
    Error e;
    if (statusCode < 400) {
        return e;                               // ErrorCode::None
    }

    switch (statusCode) {
        case 400:
        case 413:
        case 422: e.code = ErrorCode::InvalidRequest;       break;
        case 401:
        case 403: e.code = ErrorCode::AuthenticationFailed; break;
        case 402: e.code = ErrorCode::QuotaExceeded;        break;
        case 404: e.code = ErrorCode::ModelNotFound;        break;
        case 408: e.code = ErrorCode::Timeout;              break;
        case 429: e.code = ErrorCode::RateLimited;          break;
        default:
            e.code = (statusCode >= 500) ? ErrorCode::ProviderError
                                         : ErrorCode::Unknown;
            break;
    }

    e.providerCode = std::to_string(statusCode);
    e.message      = "HTTP " + e.providerCode;
    if (!detail.empty()) {
        e.message += ": " + detail;
    }
    return e;
}

int ParseRetryAfterMs(const std::string& headerValue) {
    if (headerValue.empty()) return -1;

    size_t i = 0;
    while (i < headerValue.size() &&
           std::isspace(static_cast<unsigned char>(headerValue[i]))) ++i;
    size_t start = i;
    while (i < headerValue.size() &&
           std::isdigit(static_cast<unsigned char>(headerValue[i]))) ++i;
    if (i == start) return -1;                  // HTTP-date or garbage
    size_t rest = i;
    while (rest < headerValue.size() &&
           std::isspace(static_cast<unsigned char>(headerValue[rest]))) ++rest;
    if (rest != headerValue.size()) return -1;  // trailing junk => HTTP-date

    long seconds = std::strtol(headerValue.c_str() + start, nullptr, 10);
    if (seconds < 0 || seconds > 24L * 3600L) return -1;
    return static_cast<int>(seconds * 1000L);
}

} // namespace UltraAI
