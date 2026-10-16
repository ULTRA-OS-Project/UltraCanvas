// Tests/UltraMail/test_scheduler.cpp
// The sync scheduler's due-account logic, the offline grace period and the
// contact auto-collector, the wake-from-sleep detector, and the quiet period
// after the computer was switched on with its last-connected file.
// Version: 0.4.0 - SwitchOnQuiet, ReadLastConnected / WriteLastConnected
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailSyncScheduler.h"
#include "UltraMailContactCollector.h"
#include "UltraMailContactStore.h"
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace UltraMail;

// ---- scheduler -------------------------------------------------------------

TEST(scheduler_never_synced_is_due) {
    SyncScheduler s;
    s.SetAccount("erika", "imaps://x/", 300);
    s.SetAccount("work",  "imaps://y/", 600);

    auto due = s.DueAccounts(1000);
    REQUIRE_EQ(due.size(), (size_t)2);   // neither synced yet
}

TEST(scheduler_respects_interval) {
    SyncScheduler s;
    s.SetAccount("erika", "imaps://x/", 300);
    s.MarkSynced("erika", 1000);

    REQUIRE_EQ(s.DueAccounts(1100).size(), (size_t)0);   // 100s < 300s interval
    REQUIRE_EQ(s.DueAccounts(1300).size(), (size_t)1);   // exactly at interval -> due
    REQUIRE_EQ(s.DueAccounts(2000).size(), (size_t)1);   // well past

    s.MarkSynced("erika", 2000);
    REQUIRE_EQ(s.DueAccounts(2100).size(), (size_t)0);   // reset
}

// A new interval from Settings keeps the time of the last sync: an account
// that has waited that long already is due at once, the others later.
TEST(scheduler_new_interval_keeps_the_last_sync) {
    SyncScheduler s;
    s.SetAccount("erika", "imaps://x/", 300);
    s.MarkSynced("erika", 1000);
    REQUIRE_EQ(s.DueAccounts(1030).size(), (size_t)0);
    s.SetAccount("erika", "imaps://x/", 20);
    REQUIRE_EQ(s.DueAccounts(1030).size(), (size_t)1);   // 30 s waited, 20 s asked
    REQUIRE_EQ(s.DueAccounts(1010).size(), (size_t)0);
    s.SetAccount("erika", "imaps://x/", 600);
    REQUIRE_EQ(s.DueAccounts(1500).size(), (size_t)0);
    REQUIRE_EQ(s.DueAccounts(1600).size(), (size_t)1);
}

TEST(scheduler_remove) {
    SyncScheduler s;
    s.SetAccount("erika", "imaps://x/", 300);
    REQUIRE_EQ(s.Count(), (size_t)1);
    s.Remove("erika");
    REQUIRE_EQ(s.Count(), (size_t)0);
}

// ---- offline grace ---------------------------------------------------------

TEST(offline_grace_holds_the_first_failure) {
    OfflineGrace g(600);
    // Right after boot: the first failure is never reported.
    REQUIRE(!g.Unreachable("erika", 1000));
    REQUIRE(g.InGrace("erika", 1000));
    // Still failing a minute later: held, still in grace.
    REQUIRE(!g.Unreachable("erika", 1060));
    REQUIRE(g.InGrace("erika", 1300));
}

TEST(offline_grace_reports_once_the_period_has_passed) {
    OfflineGrace g(600);
    REQUIRE(!g.Unreachable("erika", 1000));
    REQUIRE(!g.Unreachable("erika", 1599));    // one second short
    REQUIRE(g.Unreachable("erika", 1600));     // the whole period: report
    REQUIRE(g.Unreachable("erika", 2000));     // and it stays reportable
    // An expired period is not "in grace": the regular cadence takes over.
    REQUIRE(!g.InGrace("erika", 1600));
    REQUIRE_EQ(g.AccountsInGrace(1600).size(), (size_t)0);
}

TEST(offline_grace_restarts_after_the_server_answered) {
    OfflineGrace g(600);
    REQUIRE(!g.Unreachable("erika", 1000));
    g.Reached("erika");                          // a sync succeeded
    REQUIRE(!g.InGrace("erika", 1000));
    // The next gap starts a new period from its own first failure.
    REQUIRE(!g.Unreachable("erika", 1500));
    REQUIRE(!g.Unreachable("erika", 2099));
    REQUIRE(g.Unreachable("erika", 2100));
}

TEST(offline_grace_tracks_accounts_apart) {
    OfflineGrace g(600);
    REQUIRE(!g.Unreachable("erika", 1000));
    REQUIRE(!g.Unreachable("work",  1400));
    auto in = g.AccountsInGrace(1500);
    REQUIRE_EQ(in.size(), (size_t)2);
    // erika's period has run out at 1600, work's has not.
    in = g.AccountsInGrace(1700);
    REQUIRE_EQ(in.size(), (size_t)1);
    REQUIRE_EQ(in[0], std::string("work"));
    REQUIRE(g.Unreachable("erika", 1700));
    REQUIRE(!g.Unreachable("work", 1700));
}

TEST(offline_grace_default_period_and_bad_values) {
    REQUIRE_EQ(OfflineGrace().GraceSec(), OfflineGrace::kDefaultGraceSec);
    REQUIRE_EQ(OfflineGrace(0).GraceSec(), OfflineGrace::kDefaultGraceSec);
    REQUIRE_EQ(OfflineGrace(-5).GraceSec(), OfflineGrace::kDefaultGraceSec);
}

// ---- contact collector -----------------------------------------------------

namespace {
ContactStore FreshContacts(const std::string& tag) {
    ContactStore s;
    REQUIRE(s.Open("collect-" + tag, ":memory:").success);
    return s;
}
int TotalContacts(ContactStore& s) {
    std::vector<SectionCount> counts;
    s.GetSectionCounts(counts);
    int total = 0;
    for (auto& c : counts) total += c.count;
    return total;
}
} // namespace

TEST(collector_adds_new_and_skips_existing) {
    ContactStore store = FreshContacts("basic");

    REQUIRE(ContactCollector::Collect(store, "Anna Schmidt", "anna@example.com"));
    REQUIRE_EQ(TotalContacts(store), 1);

    // Same email again -> not added.
    REQUIRE(!ContactCollector::Collect(store, "Anna S.", "anna@example.com"));
    REQUIRE_EQ(TotalContacts(store), 1);

    // Different email -> added.
    REQUIRE(ContactCollector::Collect(store, "Max Weber", "max@example.com"));
    REQUIRE_EQ(TotalContacts(store), 2);
}

TEST(collector_ignores_blank_and_uses_email_when_no_name) {
    ContactStore store = FreshContacts("blank");
    REQUIRE(!ContactCollector::Collect(store, "Nobody", ""));   // blank email ignored
    REQUIRE_EQ(TotalContacts(store), 0);

    REQUIRE(ContactCollector::Collect(store, "", "noname@example.com"));
    std::vector<Contact> other;
    store.ListBySection(ContactSection::Other, other);
    REQUIRE_EQ(other.size(), (size_t)1);
    REQUIRE_EQ(other[0].displayName, std::string("noname@example.com"));  // falls back to email
}

TEST(collector_does_not_reclassify_existing) {
    ContactStore store = FreshContacts("reclass");
    Contact c; c.displayName = "Carol"; c.section = ContactSection::Work;
    ContactEmail e; e.address = "carol@acme.com"; e.primary = true; c.emails.push_back(e);
    REQUIRE(store.Save(c).success);

    // Collecting the same address must not move Carol out of Work.
    REQUIRE(!ContactCollector::Collect(store, "Carol", "carol@acme.com", ContactSection::Other));
    std::vector<Contact> work;
    store.ListBySection(ContactSection::Work, work);
    REQUIRE_EQ(work.size(), (size_t)1);
}

// ---- WakeDetector: a wake from sleep, seen as a gap between timer ticks ----

TEST(wake_detector_first_tick_starts_the_clock) {
    WakeDetector w(30, 90);
    REQUIRE(!w.Tick(1000000));          // nothing to compare with yet
    REQUIRE(!w.Tick(1000030));          // one period later: running normally
}

TEST(wake_detector_late_ticks_are_not_a_sleep) {
    WakeDetector w(30, 90);
    w.Tick(1000000);
    REQUIRE(!w.Tick(1000060));          // a busy loop, a tick delayed by 30 s
    REQUIRE(!w.Tick(1000060 + 120));    // exactly period + slack: still not a sleep
}

TEST(wake_detector_sees_a_sleep) {
    WakeDetector w(30, 90);
    w.Tick(1000000);
    REQUIRE(w.Tick(1000000 + 3600));    // an hour went by between two ticks
    REQUIRE_EQ(w.LastSleepSec(), static_cast<int64_t>(3570));
    REQUIRE(!w.Tick(1000000 + 3630));   // and afterwards it runs normally again
}

TEST(wake_detector_ignores_the_clock_going_back) {
    WakeDetector w(30, 90);
    w.Tick(1000000);
    REQUIRE(!w.Tick(990000));           // NTP set the clock back
    REQUIRE(!w.Tick(990030));
}

TEST(offline_grace_clear_forgets_running_periods) {
    OfflineGrace g(600);
    REQUIRE(!g.Unreachable("a", 100));  // first failure: held back
    REQUIRE(g.InGrace("a", 200));
    g.Clear();                          // the computer slept and woke
    REQUIRE(!g.InGrace("a", 5000));
    REQUIRE(!g.Unreachable("a", 5000)); // the first failure after waking is held back again
}

// ---- SwitchOnQuiet: no alerts while the network comes up after a switch-on ----

TEST(switch_on_quiet_tells_a_switch_on_from_a_restart) {
    const int64_t now = 1700000000;
    // Connected a minute ago: UltraMail was only restarted.
    REQUIRE(!SwitchOnQuiet::LooksSwitchedOff(now - 60, now));
    REQUIRE(!SwitchOnQuiet::LooksSwitchedOff(now - 600, now));     // ten minutes: not yet
    REQUIRE(SwitchOnQuiet::LooksSwitchedOff(now - 601, now));      // more than ten
    REQUIRE(SwitchOnQuiet::LooksSwitchedOff(now - 86400, now));    // overnight
    REQUIRE(SwitchOnQuiet::LooksSwitchedOff(0, now));              // none on record
    // The clock is behind the last connection (not yet set after boot).
    REQUIRE(SwitchOnQuiet::LooksSwitchedOff(now + 3600, now));
}

TEST(switch_on_quiet_runs_for_the_chosen_time) {
    const int64_t wall = 1700000000;
    SwitchOnQuiet q;
    REQUIRE(!q.Quiet(50));
    REQUIRE(q.Begin(wall - 3600, wall, 50, 120));    // off for an hour
    REQUIRE(q.Quiet(50));
    REQUIRE(q.Quiet(169));
    REQUIRE_EQ(q.RemainingSec(110), static_cast<int64_t>(60));
    REQUIRE(!q.Quiet(170));                          // two minutes are up
    REQUIRE_EQ(q.RemainingSec(170), static_cast<int64_t>(0));
}

TEST(switch_on_quiet_does_not_begin_after_a_recent_connection_or_when_off) {
    const int64_t wall = 1700000000;
    SwitchOnQuiet q;
    REQUIRE(!q.Begin(wall - 120, wall, 50, 120));    // connected two minutes ago
    REQUIRE(!q.Quiet(50));
    REQUIRE(!q.Begin(wall - 3600, wall, 50, 0));     // the setting is off
    REQUIRE(!q.Quiet(50));
}

TEST(switch_on_quiet_is_never_cut_short) {
    const int64_t wall = 1700000000;
    SwitchOnQuiet q;
    REQUIRE(q.Begin(0, wall, 100, 600));             // until 700
    REQUIRE(q.Begin(0, wall, 200, 120));             // a wake: until 320 would be shorter
    REQUIRE(q.Quiet(699));
    REQUIRE(q.Begin(0, wall, 650, 120));             // and this one lasts longer: until 770
    REQUIRE(q.Quiet(769));
    REQUIRE(!q.Quiet(770));
    q.End();                                         // turned off in Settings
    REQUIRE(!q.Quiet(0));
}

TEST(switch_on_quiet_hands_over_the_accounts_held_back) {
    SwitchOnQuiet q;
    q.Hold("erika");
    q.Hold("work");
    q.Hold("erika");
    REQUIRE(q.Held("erika"));
    q.Reached("work");                               // reached meanwhile: no second look
    REQUIRE(!q.Held("work"));
    const auto held = q.TakeHeld();
    REQUIRE_EQ(held.size(), (size_t)1);
    REQUIRE_EQ(held[0], std::string("erika"));
    REQUIRE(!q.Held("erika"));
    REQUIRE_EQ(q.TakeHeld().size(), (size_t)0);      // handed over once
}

TEST(last_connected_file_round_trip) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
                         UltraCanvas::PathFromUtf8("ultramail-last-connected-ทดสอบ-🚀");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string path = UltraCanvas::PathToUtf8(dir / kLastConnectedFileName);

    REQUIRE_EQ(ReadLastConnected(path), static_cast<int64_t>(0));   // no file yet
    REQUIRE(WriteLastConnected(path, 1700000123));
    REQUIRE_EQ(ReadLastConnected(path), static_cast<int64_t>(1700000123));
    REQUIRE(WriteLastConnected(path, 1700000456));                  // replaced, not appended
    REQUIRE_EQ(ReadLastConnected(path), static_cast<int64_t>(1700000456));
    {
        // A write torn by the power going off: no time, read as none.
        std::ofstream f(dir / kLastConnectedFileName, std::ios::trunc);
        f << "garbage";
    }
    REQUIRE_EQ(ReadLastConnected(path), static_cast<int64_t>(0));
    {
        std::ofstream f(dir / kLastConnectedFileName, std::ios::trunc);
    }
    REQUIRE_EQ(ReadLastConnected(path), static_cast<int64_t>(0));    // empty
    // A folder that does not exist cannot be written.
    REQUIRE(!WriteLastConnected(UltraCanvas::PathToUtf8(dir / "missing" / "x"), 1));
    fs::remove_all(dir, ec);
}
