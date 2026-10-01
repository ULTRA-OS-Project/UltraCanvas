// Apps/UltraMail/engine/UltraMailSyncScheduler.cpp
// Version: 0.3.0 - WakeDetector
// Version: 0.2.0 - OfflineGrace holds back a not-yet-online failure
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

// ---- OfflineGrace -----------------------------------------------------------

bool OfflineGrace::Unreachable(const std::string& accountId, int64_t nowSec) {
    auto it = since_.find(accountId);
    if (it == since_.end()) {
        since_[accountId] = nowSec;
        return false;                       // the first failure of a run: hold it
    }
    return nowSec - it->second >= graceSec_;
}

void OfflineGrace::Reached(const std::string& accountId) {
    since_.erase(accountId);
}

bool OfflineGrace::InGrace(const std::string& accountId, int64_t nowSec) const {
    auto it = since_.find(accountId);
    return it != since_.end() && nowSec - it->second < graceSec_;
}

std::vector<std::string> OfflineGrace::AccountsInGrace(int64_t nowSec) const {
    std::vector<std::string> out;
    for (const auto& [id, since] : since_)
        if (nowSec - since < graceSec_) out.push_back(id);
    return out;
}

// ---- WakeDetector -----------------------------------------------------------

bool WakeDetector::Tick(int64_t wallNowSec) {
    const int64_t previous = last_;
    last_ = wallNowSec;
    if (previous == 0) return false;                // the first tick starts the clock
    const int64_t gap = wallNowSec - previous;
    if (gap <= tickSec_ + slackSec_) return false;  // ran normally (or the clock went back)
    lastSleepSec_ = gap - tickSec_;
    return true;
}

} // namespace UltraMail
