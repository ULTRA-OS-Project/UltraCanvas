// Apps/UltraMail/engine/UltraMailSyncScheduler.h
// Decides which accounts are due for a background sync. Pure bookkeeping over
// per-account intervals and last-sync timestamps; the app drives it from a UI
// timer and runs the due accounts through the SyncService.
// Version: 0.1.0 (Phase 2)
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

} // namespace UltraMail
