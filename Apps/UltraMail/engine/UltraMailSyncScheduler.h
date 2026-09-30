// Apps/UltraMail/engine/UltraMailSyncScheduler.h
// Decides which accounts are due for a background sync. Pure bookkeeping over
// per-account intervals and last-sync timestamps; the app drives it from a UI
// timer and runs the due accounts through the SyncService.
// Version: 0.2.0 - OfflineGrace holds back a not-yet-online failure
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraMail {

struct ScheduledAccount {
    std::string accountId;
    std::string serverUrl;      // imap(s)://host:port/
    int64_t     intervalSec = 300;
    int64_t     lastSync = 0;   // epoch seconds; 0 = never
};

class SyncScheduler {
public:
    // Register (or update) an account's sync cadence.
    void SetAccount(const std::string& accountId, const std::string& serverUrl,
                    int64_t intervalSec);
    void Remove(const std::string& accountId);

    // Record that an account was synced at `nowEpoch`.
    void MarkSynced(const std::string& accountId, int64_t nowEpoch);

    // Accounts whose next sync is due at `nowEpoch` (never-synced accounts are
    // always due).
    std::vector<ScheduledAccount> DueAccounts(int64_t nowEpoch) const;

    std::size_t Count() const { return accounts_.size(); }

private:
    std::map<std::string, ScheduledAccount> accounts_;
};

// Decides when a background sync that could not reach the server is worth
// telling the user about. Right after the computer starts, the network is
// often not up yet when the first sync runs, and the failure that produces
// ("could not resolve host") is a false alarm: the next attempt succeeds.
// So a connectivity failure is held back for a grace period and only reported
// when the account has stayed unreachable for the whole of it. A failure the
// server itself produced (a rejected password) is not a connectivity failure
// and never comes here.
//
// Pure bookkeeping on a clock the caller supplies (steady seconds, so a
// wall-clock jump - NTP correcting the time right after boot - cannot expire
// or extend the period). The app also polls the unreachable accounts more
// often than the regular cadence while the period runs, so mail arrives soon
// after the network does.
class OfflineGrace {
public:
    static constexpr int64_t kDefaultGraceSec = 600;   // ten minutes

    explicit OfflineGrace(int64_t graceSec = kDefaultGraceSec)
        : graceSec_(graceSec > 0 ? graceSec : kDefaultGraceSec) {}

    // Records that the account could not be reached at `nowSec`. Returns true
    // when the account has now been unreachable for the whole grace period,
    // measured from its first failure since it last succeeded - that is, when
    // the failure should be reported. The first failure of a run never is.
    bool Unreachable(const std::string& accountId, int64_t nowSec);

    // The account was reached (a sync succeeded, or failed for a reason that
    // proves the server answered): the next failure starts a new period.
    void Reached(const std::string& accountId);

    // True while the account's grace period is running: it has failed at
    // least once, has not been reached since, and the period has not expired
    // (an expired one has been reported and is left to the regular cadence).
    bool InGrace(const std::string& accountId, int64_t nowSec) const;

    // The accounts whose grace period is running at `nowSec` - the ones worth
    // retrying sooner than the regular cadence.
    std::vector<std::string> AccountsInGrace(int64_t nowSec) const;

    int64_t GraceSec() const { return graceSec_; }

private:
    int64_t graceSec_;
    std::map<std::string, int64_t> since_;   // accountId -> first failure, steady seconds
};

} // namespace UltraMail
