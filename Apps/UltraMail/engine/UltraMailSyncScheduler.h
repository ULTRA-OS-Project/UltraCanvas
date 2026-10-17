// Apps/UltraMail/engine/UltraMailSyncScheduler.h
// Decides which accounts are due for a background sync. Pure bookkeeping over
// per-account intervals and last-sync timestamps; the app drives it from a UI
// timer and runs the due accounts through the SyncService.
// Version: 0.4.0 - SwitchOnQuiet holds back every failure alert for a while after the
//                  computer was switched on; ReadLastConnected / WriteLastConnected
// Version: 0.3.0 - WakeDetector notices the computer woke from sleep; OfflineGrace::Clear
// Version: 0.2.0 - OfflineGrace holds back a not-yet-online failure
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <map>
#include <set>
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

    // Forget every running period - after the computer woke from sleep, time
    // spent offline before it slept says nothing about the network now.
    void Clear() { since_.clear(); }

private:
    int64_t graceSec_;
    std::map<std::string, int64_t> since_;   // accountId -> first failure, steady seconds
};

// Tells a computer that was just switched on from one whose connection just
// dropped, so that a mail check failing while the network is still coming up
// raises no alert. UltraMail remembers when it last reached a mail server (the
// last-connected file, kept across runs). When it starts, or the computer
// wakes, and that was more than kSwitchedOffSec ago, the computer was most
// likely switched off - or asleep - and its network may not be up yet. For
// the quiet period that follows (Settings > Mail > Connection, two minutes by
// default) a background check that fails is shown on the status line only,
// whatever the reason; the accounts held back that way are checked once more
// when the period ends, and what still fails then is reported.
//
// It covers every failure, not only "the server could not be reached", which
// OfflineGrace holds back anyway: a network that is only half up also breaks
// connections off in the TLS handshake or in the middle of a read, and a
// clock not yet set right after boot can make a good certificate look expired.
//
// Pure bookkeeping, like OfflineGrace: the wall clock (epoch seconds) for the
// last connection, which has to survive a restart, and a steady clock for the
// period itself, which a clock correction right after boot must not stretch
// or cut short.
class SwitchOnQuiet {
public:
    static constexpr int64_t kSwitchedOffSec = 600;   // ten minutes without a connection

    // Whether a last connection at `lastConnectedEpoch` (0 = none on record)
    // means the computer was most likely off until `wallNowSec`: it was more
    // than kSwitchedOffSec ago, there is none, or it lies in the future (the
    // clock is wrong - as it can be right after boot).
    static bool LooksSwitchedOff(int64_t lastConnectedEpoch, int64_t wallNowSec);

    // At start and after a wake: when LooksSwitchedOff says so and `quietSec`
    // is positive, the quiet period runs from `steadyNowSec` for `quietSec`.
    // A period already running is never cut short. Returns whether one began.
    bool Begin(int64_t lastConnectedEpoch, int64_t wallNowSec,
               int64_t steadyNowSec, int64_t quietSec);

    // True while the quiet period runs.
    bool Quiet(int64_t steadyNowSec) const { return steadyNowSec < until_; }
    // Seconds until it ends; 0 when none runs.
    int64_t RemainingSec(int64_t steadyNowSec) const {
        return Quiet(steadyNowSec) ? until_ - steadyNowSec : 0;
    }
    // End the period now (the setting was turned off). The accounts held back
    // stay for TakeHeld.
    void End() { until_ = 0; }

    // A failed check of the account raised no alert: check it again when the
    // period ends.
    void Hold(const std::string& accountId) { held_.insert(accountId); }
    bool Held(const std::string& accountId) const { return held_.count(accountId) != 0; }
    // The account was reached: there is nothing to check again for it.
    void Reached(const std::string& accountId) { held_.erase(accountId); }
    // The accounts held back, handed over once.
    std::vector<std::string> TakeHeld();

private:
    int64_t until_ = 0;   // steady seconds; 0 = no period
    std::set<std::string> held_;
};

// The file in the data folder that keeps when UltraMail last reached a mail
// server - wall-clock epoch seconds, one line - for SwitchOnQuiet at the next
// start. An internal file, read and written directly.
inline constexpr const char* kLastConnectedFileName = "last-connected";
// 0 when the file is missing or holds no time. A write torn by the computer
// going off reads the same, which counts as "switched off": the safe side.
int64_t ReadLastConnected(const std::string& path);
// False when the file could not be written.
bool WriteLastConnected(const std::string& path, int64_t epoch);

// Notices that the computer slept (or hibernated) and has just woken up, so
// the app can check for mail at once instead of when its five-minute timer
// next fires - which, after a wake, can be minutes away and leaves the inbox
// showing mail from before the sleep.
//
// Portable, with no system notification: the app calls Tick() from a short
// periodic timer with the wall clock. While the computer runs, consecutive
// ticks are about one period apart; across a sleep the timer cannot fire, so
// the first tick afterwards sees far more wall time pass than a period. A
// gap longer than the period plus `slackSec` is taken as a wake. (Setting the
// clock forward by as much looks the same and costs one early mail check;
// setting it back is ignored.)
class WakeDetector {
public:
    static constexpr int64_t kDefaultTickSec  = 15;
    static constexpr int64_t kDefaultSlackSec = 90;

    explicit WakeDetector(int64_t tickSec = kDefaultTickSec,
                          int64_t slackSec = kDefaultSlackSec)
        : tickSec_(tickSec > 0 ? tickSec : kDefaultTickSec),
          slackSec_(slackSec >= 0 ? slackSec : kDefaultSlackSec) {}

    // Called every tickSec with the wall clock (epoch seconds). True when the
    // time since the previous call shows the computer was asleep. The first
    // call only starts the clock.
    bool Tick(int64_t wallNowSec);

    int64_t TickSec() const { return tickSec_; }
    // How long the last detected sleep lasted (seconds, approximately).
    int64_t LastSleepSec() const { return lastSleepSec_; }

private:
    int64_t tickSec_;
    int64_t slackSec_;
    int64_t last_ = 0;
    int64_t lastSleepSec_ = 0;
};

} // namespace UltraMail
