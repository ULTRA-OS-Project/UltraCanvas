// Apps/UltraAuthenticator/LockPolicy.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "LockPolicy.h"

namespace UltraCanvas {
namespace Authenticator {

uint32_t UnlockThrottle::DelayAfterFailures(unsigned failures, const Config& config) {
    if (failures <= config.freeAttempts) return 0;
    const unsigned penalised = failures - config.freeAttempts;   // >= 1
    // base << (penalised - 1), computed without overflowing: once the shift
    // would exceed the cap there is no point shifting further.
    uint64_t delay = config.baseDelaySeconds;
    for (unsigned i = 1; i < penalised && delay < config.maxDelaySeconds; ++i) {
        delay *= 2;
    }
    if (delay > config.maxDelaySeconds) delay = config.maxDelaySeconds;
    return static_cast<uint32_t>(delay);
}

uint32_t UnlockThrottle::SecondsUntilAllowed(int64_t nowUnix) const {
    if (nowUnix >= notBefore_) return 0;
    return static_cast<uint32_t>(notBefore_ - nowUnix);
}

void UnlockThrottle::RecordFailure(int64_t nowUnix) {
    ++failures_;
    const uint32_t delay = DelayAfterFailures(failures_, config_);
    if (delay > 0) {
        // Measured from now, not from the previous deadline: an attempt made
        // during a running delay restarts a longer one.
        notBefore_ = nowUnix + static_cast<int64_t>(delay);
    }
}

void UnlockThrottle::RecordSuccess() {
    failures_  = 0;
    notBefore_ = 0;
}

} // namespace Authenticator
} // namespace UltraCanvas
