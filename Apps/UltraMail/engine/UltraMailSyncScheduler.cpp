// Apps/UltraMail/engine/UltraMailSyncScheduler.cpp
// Version: 0.1.0 (Phase 2)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSyncScheduler.h"

namespace UltraMail {

void SyncScheduler::SetAccount(const std::string& accountId, const std::string& serverUrl,
                               int64_t intervalSec) {
    auto& a = accounts_[accountId];
    a.accountId = accountId;
    a.serverUrl = serverUrl;
    a.intervalSec = intervalSec > 0 ? intervalSec : 300;
    // lastSync preserved across updates.
}

void SyncScheduler::Remove(const std::string& accountId) {
    accounts_.erase(accountId);
}

void SyncScheduler::MarkSynced(const std::string& accountId, int64_t nowEpoch) {
    auto it = accounts_.find(accountId);
    if (it != accounts_.end()) it->second.lastSync = nowEpoch;
}

std::vector<ScheduledAccount> SyncScheduler::DueAccounts(int64_t nowEpoch) const {
    std::vector<ScheduledAccount> due;
    for (const auto& [id, a] : accounts_) {
        if (a.lastSync == 0 || a.lastSync + a.intervalSec <= nowEpoch)
            due.push_back(a);
    }
    return due;
}

} // namespace UltraMail
