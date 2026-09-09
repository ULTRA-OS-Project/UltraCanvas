// UltraAI/adapters/_shared/src/JobPoll.cpp
// Implementation of RunJobPoll.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "UltraAIJobPoll.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace UltraAI {

namespace {

using Clock = std::chrono::steady_clock;

// Sleep `totalMs`, waking every `sliceMs` to re-check cancellation.
// Returns false as soon as cancellation is observed.
bool SleepUnlessCancelled(int totalMs, int sliceMs,
                          const std::function<bool()>& isCancelled) {
    const int slice = std::max(1, sliceMs);
    int remaining   = std::max(0, totalMs);
    while (remaining > 0) {
        if (isCancelled && isCancelled()) return false;
        const int step = std::min(slice, remaining);
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
        remaining -= step;
    }
    return !(isCancelled && isCancelled());
}

} // namespace

JobPollOutcome RunJobPoll(const JobPollOptions& options,
                          const std::function<bool()>& isCancelled,
                          const std::function<JobPollState(Error*)>& poll,
                          Error* outError) {
    Error scratch;
    Error* err = outError ? outError : &scratch;
    *err = {};

    const auto deadline =
        Clock::now() + std::chrono::milliseconds(std::max(0, options.timeoutMs));

    // The last transient error seen, so a timeout can say what kept failing
    // instead of reporting a bare deadline.
    Error lastTransient;

    double intervalMs = static_cast<double>(std::max(0, options.intervalMs));
    int delayMs = std::max(0, options.initialDelayMs);

    for (;;) {
        if (!SleepUnlessCancelled(delayMs, options.cancelSliceMs, isCancelled)) {
            err->code    = ErrorCode::Cancelled;
            err->message = "job cancelled";
            return JobPollOutcome::Cancelled;
        }
        if (Clock::now() >= deadline) break;

        Error pollError;
        const JobPollState state = poll(&pollError);
        if (state == JobPollState::Completed) {
            *err = {};
            return JobPollOutcome::Completed;
        }
        if (state == JobPollState::Failed) {
            *err = pollError;
            if (err->IsOk()) {
                err->code    = ErrorCode::ProviderError;
                err->message = "job failed without a provider message";
            }
            return JobPollOutcome::Failed;
        }
        if (!pollError.IsOk()) lastTransient = pollError;

        delayMs    = static_cast<int>(intervalMs);
        intervalMs = std::min(intervalMs * std::max(1.0, options.backoff),
                              static_cast<double>(std::max(0, options.maxIntervalMs)));
    }

    if (!lastTransient.IsOk()) {
        *err = lastTransient;
        err->code = ErrorCode::Timeout;
        err->message = "job did not finish in time; last poll error: " +
                       lastTransient.message;
    } else {
        err->code    = ErrorCode::Timeout;
        err->message = "job did not finish within " +
                       std::to_string(options.timeoutMs) + " ms";
    }
    return JobPollOutcome::TimedOut;
}

} // namespace UltraAI
