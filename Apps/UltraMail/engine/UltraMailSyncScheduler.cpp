// Apps/UltraMail/engine/UltraMailSyncScheduler.cpp
// Version: 0.4.0 - SwitchOnQuiet; the last-connected file
// Version: 0.3.0 - WakeDetector
// Version: 0.2.0 - OfflineGrace holds back a not-yet-online failure
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailSyncScheduler.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <fstream>

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

// ---- SwitchOnQuiet ----------------------------------------------------------

bool SwitchOnQuiet::LooksSwitchedOff(int64_t lastConnectedEpoch, int64_t wallNowSec) {
    if (lastConnectedEpoch <= 0) return true;            // none on record
    if (wallNowSec < lastConnectedEpoch) return true;    // the clock is wrong
    return wallNowSec - lastConnectedEpoch > kSwitchedOffSec;
}

bool SwitchOnQuiet::Begin(int64_t lastConnectedEpoch, int64_t wallNowSec,
                          int64_t steadyNowSec, int64_t quietSec) {
    if (quietSec <= 0 || !LooksSwitchedOff(lastConnectedEpoch, wallNowSec)) return false;
    if (steadyNowSec + quietSec > until_) until_ = steadyNowSec + quietSec;
    return true;
}

std::vector<std::string> SwitchOnQuiet::TakeHeld() {
    std::vector<std::string> out(held_.begin(), held_.end());
    held_.clear();
    return out;
}

int64_t ReadLastConnected(const std::string& path) {
    std::ifstream file(UltraCanvas::PathFromUtf8(path));
    if (!file.is_open()) return 0;
    long long epoch = 0;
    if (!(file >> epoch) || epoch < 0) return 0;
    return static_cast<int64_t>(epoch);
}

bool WriteLastConnected(const std::string& path, int64_t epoch) {
    std::ofstream file(UltraCanvas::PathFromUtf8(path), std::ios::trunc);
    if (!file.is_open()) return false;
    file << static_cast<long long>(epoch) << "\n";
    file.flush();
    return static_cast<bool>(file);
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
