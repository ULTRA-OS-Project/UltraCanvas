// Apps/UltraAuthenticator/LockPolicy.h
// Exponential back-off for unlock attempts.
//
// The vault's Argon2id parameters already make each guess cost real CPU time,
// but that is a cost per *derivation*, and a walk-up attacker at an unlocked
// desktop is not running a GPU cluster — they are typing guesses into the lock
// screen. This throttle addresses that case: a handful of attempts are free,
// because people mistype, and every failure after that doubles the wait
// before the next attempt is accepted, up to a cap.
//
// It is deliberately time-injected (callers pass "now") so the schedule can be
// tested without sleeping, and it holds no secret, so it can live in the
// headless core library and be enforced by AccountStore rather than by the UI.
// The gate lives with the data; a dialog that forgot to wait would still be
// refused.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once
#ifndef AUTHENTICATOR_LOCKPOLICY_H
#define AUTHENTICATOR_LOCKPOLICY_H

#include <cstdint>

namespace UltraCanvas {
namespace Authenticator {

class UnlockThrottle {
public:
    struct Config {
        unsigned freeAttempts     = 3;     // failures before any delay applies
        unsigned baseDelaySeconds = 2;     // first delay; doubles per failure
        unsigned maxDelaySeconds  = 300;   // ceiling for the doubling
    };

    UnlockThrottle() = default;
    explicit UnlockThrottle(const Config& config) : config_(config) {}

    // Seconds the caller must still wait before an attempt is accepted.
    // 0 means an attempt may be made now.
    uint32_t SecondsUntilAllowed(int64_t nowUnix) const;
    bool IsAllowed(int64_t nowUnix) const { return SecondsUntilAllowed(nowUnix) == 0; }

    // Records the outcome of an attempt. A failure recorded while a delay is
    // still running counts as a further failure — the delay is not a suggestion.
    void RecordFailure(int64_t nowUnix);
    void RecordSuccess();

    unsigned ConsecutiveFailures() const { return failures_; }

    // The delay that follows the given number of consecutive failures. Exposed
    // so the schedule can be tested as a pure function.
    static uint32_t DelayAfterFailures(unsigned failures, const Config& config);

private:
    Config   config_;
    unsigned failures_  = 0;
    int64_t  notBefore_ = 0;   // unix time before which attempts are refused
};

} // namespace Authenticator
} // namespace UltraCanvas

#endif // AUTHENTICATOR_LOCKPOLICY_H
