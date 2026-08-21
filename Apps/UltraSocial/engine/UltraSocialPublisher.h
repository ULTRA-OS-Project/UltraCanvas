// Apps/UltraSocial/engine/UltraSocialPublisher.h
// The one publish path shared by "Post now" and the scheduled outbox:
// adapt the draft to the target's network, validate, fetch credentials,
// publish through the connector, persist rotated tokens back to the vault,
// and record a history row. FlushDueOutbox drives every due queue entry
// through the same path with bounded retries and linear backoff.
// Blocking (network) — run on a worker thread.
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraSocialCredentialVault.h"
#include "UltraSocialStore.h"

#include <cstdint>

namespace UltraSocial {

// Publishes one draft to one account and appends the history row (with
// entry.succeeded / url / error filled). Returns the recorded entry.
HistoryEntry PublishOne(Store& store,
                        CredentialVault& vault,
                        const Account& account,
                        const PostDraft& draft);

// Retry policy for scheduled posts: a failed attempt is retried
// kOutboxRetryBackoffSeconds * attempts later, up to kOutboxMaxAttempts;
// then the entry leaves the queue as a failed history row.
constexpr int     kOutboxMaxAttempts         = 5;
constexpr int64_t kOutboxRetryBackoffSeconds = 300;

struct FlushStats {
    int published = 0;   // queue entries that posted successfully
    int rescheduled = 0; // failed, will retry later
    int abandoned = 0;   // failed for the last time -> failed history row
};

// Publishes every outbox entry due at `now` (epoch seconds). Entries whose
// account has disappeared are abandoned. Returns what happened.
FlushStats FlushDueOutbox(Store& store, CredentialVault& vault, int64_t now);

} // namespace UltraSocial
