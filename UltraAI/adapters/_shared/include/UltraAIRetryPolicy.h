// UltraAI/adapters/_shared/include/UltraAIRetryPolicy.h
// Retry decision + exponential backoff shared by network adapters.
// Pure policy: it decides whether and how long to wait; the adapter owns
// the actual sleeping / rescheduling so async code can use it too.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

namespace UltraAI {

struct RetryPolicy {
    int    maxAttempts   = 4;      // total tries, including the first
    int    baseDelayMs   = 500;    // delay before the first retry
    double multiplier    = 2.0;    // backoff growth per retry
    int    maxDelayMs    = 30000;  // ceiling for any single delay
    bool   retryOnTimeout = true;

    // True when `error` is transient and `attempt` (0-based count of tries
    // already made) leaves room for another try. Retryable codes:
    // RateLimited, NetworkError, ProviderError (5xx), and Timeout when
    // retryOnTimeout is set. Everything else — auth, quota, invalid
    // request, content filter, cancellation — is permanent.
    bool ShouldRetry(const Error& error, int attempt) const;

    // Delay in ms before retry number `attempt` (0-based: attempt 0 is the
    // first retry). A non-negative retryAfterMs (from a Retry-After header)
    // takes precedence over the computed backoff; both are capped at
    // maxDelayMs.
    int NextDelayMs(int attempt, int retryAfterMs = -1) const;
};

} // namespace UltraAI
