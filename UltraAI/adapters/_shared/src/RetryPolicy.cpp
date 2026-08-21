// UltraAI/adapters/_shared/src/RetryPolicy.cpp
// Implementation of RetryPolicy.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAIRetryPolicy.h"

namespace UltraAI {

bool RetryPolicy::ShouldRetry(const Error& error, int attempt) const {
    if (attempt + 1 >= maxAttempts) return false;

    switch (error.code) {
        case ErrorCode::RateLimited:
        case ErrorCode::NetworkError:
        case ErrorCode::ProviderError:
            return true;
        case ErrorCode::Timeout:
            return retryOnTimeout;
        default:
            return false;
    }
}

int RetryPolicy::NextDelayMs(int attempt, int retryAfterMs) const {
    if (retryAfterMs >= 0) {
        return retryAfterMs < maxDelayMs ? retryAfterMs : maxDelayMs;
    }
    double delay = static_cast<double>(baseDelayMs);
    for (int i = 0; i < attempt; ++i) {
        delay *= multiplier;
        if (delay >= static_cast<double>(maxDelayMs)) return maxDelayMs;
    }
    if (delay >= static_cast<double>(maxDelayMs)) return maxDelayMs;
    return static_cast<int>(delay);
}

} // namespace UltraAI
