// UltraAI/adapters/_shared/include/UltraAIJobPoll.h
// Poll loop for adapters whose provider runs generation as an asynchronous
// job: submit once, then ask "is it done yet?" until it is. MiniMax video
// (task_id -> /query/video_generation) and the ComfyUI polling fallback
// (prompt_id -> /history) both work this way.
//
// Pure control flow: the caller supplies the one-shot poll and the
// cancellation predicate, so the same loop serves blocking Generate() and
// the worker thread behind GenerateJob().
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

#include <functional>

namespace UltraAI {

// What one poll learned about the job.
enum class JobPollState {
    Pending,     // still queued or running — keep polling
    Completed,   // finished successfully; the callback kept the payload
    Failed       // the provider reported failure; *outError describes it
};

struct JobPollOptions {
    int    initialDelayMs = 1000;   // wait before the first poll
    int    intervalMs     = 2000;   // wait between polls
    int    maxIntervalMs  = 10000;  // ceiling once backoff is applied
    double backoff        = 1.0;    // interval growth per poll (1.0 = flat)
    int    timeoutMs      = 600000; // give up after this much wall time
    int    cancelSliceMs  = 100;    // cancellation is noticed within this
};

enum class JobPollOutcome {
    Completed,
    Failed,      // poll reported Failed, or a poll call errored terminally
    Cancelled,
    TimedOut
};

// Drive `poll` until it reports something terminal. Runs on the calling
// thread and sleeps in `cancelSliceMs` slices so Cancel() is honoured
// promptly. `isCancelled` may be null (never cancelled).
//
// A poll that returns Pending with a non-empty *outError is treated as a
// transient failure and retried; return Failed to stop. *outError is
// filled for the Failed / TimedOut / Cancelled outcomes.
JobPollOutcome RunJobPoll(const JobPollOptions& options,
                          const std::function<bool()>& isCancelled,
                          const std::function<JobPollState(Error*)>& poll,
                          Error* outError);

} // namespace UltraAI
