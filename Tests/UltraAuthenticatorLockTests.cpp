// Tests/UltraAuthenticatorLockTests.cpp
// Unit tests for the authenticator's unlock back-off and settings file.
//
// Both are pure: the throttle takes "now" as an argument, so the whole
// schedule is checked without sleeping, and the preferences round-trip goes
// through a string as well as a file. No crypto backend, no UI.
//
// Author: UltraCanvas Framework / ULTRA OS
#include "../Apps/UltraAuthenticator/LockPolicy.h"
#include "../Apps/UltraAuthenticator/Preferences.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace UltraCanvas::Authenticator;

static int g_failures = 0;
static int g_checks   = 0;

static void Check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

// ===========================================================================
// UnlockThrottle
// ===========================================================================

static void TestDelaySchedule() {
    std::printf("Delay schedule is free, then doubles, then caps\n");
    UnlockThrottle::Config cfg;   // 3 free, base 2 s, cap 300 s

    Check(UnlockThrottle::DelayAfterFailures(0, cfg) == 0, "no failures: no delay");
    Check(UnlockThrottle::DelayAfterFailures(1, cfg) == 0, "1st failure free");
    Check(UnlockThrottle::DelayAfterFailures(3, cfg) == 0, "3rd failure free");
    Check(UnlockThrottle::DelayAfterFailures(4, cfg) == 2, "4th failure: base delay");
    Check(UnlockThrottle::DelayAfterFailures(5, cfg) == 4, "5th: doubled");
    Check(UnlockThrottle::DelayAfterFailures(6, cfg) == 8, "6th: doubled again");
    Check(UnlockThrottle::DelayAfterFailures(10, cfg) == 128, "10th: 2^7 * base");
    Check(UnlockThrottle::DelayAfterFailures(11, cfg) == 256, "11th: still under the cap");
    Check(UnlockThrottle::DelayAfterFailures(12, cfg) == 300, "12th: capped");
    Check(UnlockThrottle::DelayAfterFailures(1000, cfg) == 300,
          "absurd failure count does not overflow past the cap");

    UnlockThrottle::Config strict;
    strict.freeAttempts = 0;
    strict.baseDelaySeconds = 1;
    strict.maxDelaySeconds = 4;
    Check(UnlockThrottle::DelayAfterFailures(1, strict) == 1, "zero free attempts: first failure delays");
    Check(UnlockThrottle::DelayAfterFailures(3, strict) == 4, "cap reached exactly");
    Check(UnlockThrottle::DelayAfterFailures(4, strict) == 4, "cap held");
}

static void TestThrottleStateMachine() {
    std::printf("Throttle refuses during a delay and resets on success\n");
    UnlockThrottle t;
    int64_t now = 1'000'000;

    Check(t.IsAllowed(now), "fresh throttle allows");
    t.RecordFailure(now); t.RecordFailure(now); t.RecordFailure(now);
    Check(t.ConsecutiveFailures() == 3, "three failures counted");
    Check(t.IsAllowed(now), "still allowed after the free attempts");

    t.RecordFailure(now);   // 4th
    Check(!t.IsAllowed(now), "refused immediately after the 4th failure");
    Check(t.SecondsUntilAllowed(now) == 2, "2 s to wait");
    Check(t.SecondsUntilAllowed(now + 1) == 1, "counts down");
    Check(t.IsAllowed(now + 2), "allowed once the delay has elapsed");

    // A failure during the wait restarts a longer wait from *that* moment.
    t.RecordFailure(now + 1);   // 5th, delay 4 s from now+1
    Check(!t.IsAllowed(now + 2), "earlier deadline no longer applies");
    Check(t.SecondsUntilAllowed(now + 1) == 4, "delay measured from the latest failure");
    Check(t.IsAllowed(now + 5), "allowed after the new delay");

    t.RecordSuccess();
    Check(t.ConsecutiveFailures() == 0, "success resets the count");
    Check(t.IsAllowed(now), "success clears any pending delay");
    t.RecordFailure(now);
    Check(t.IsAllowed(now), "free attempts are restored after a success");
}

// ===========================================================================
// Preferences
// ===========================================================================

static void TestPreferencesDefaults() {
    std::printf("Defaults\n");
    Preferences p;
    Check(p.idleLockSeconds == 300, "idle lock defaults to 5 minutes");
    Check(p.lockOnMinimize, "lock on minimise defaults on");
    Check(!p.hideCodes, "hide codes defaults off");

    Check(Preferences::Parse("") == Preferences{}, "empty text is the defaults");
    Check(Preferences::Parse("garbage\n= \n#comment\nfoo=bar") == Preferences{},
          "unrecognised text is the defaults");
    Check(Preferences::Load("/nonexistent/dir/settings.ini") == Preferences{},
          "missing file is the defaults");
}

static void TestPreferencesRoundTrip() {
    std::printf("Round trip through text and file\n");
    Preferences p;
    p.idleLockSeconds = 120;
    p.lockOnMinimize = false;
    p.hideCodes = true;

    const Preferences back = Preferences::Parse(p.Serialize());
    Check(back == p, "serialize/parse round-trips");

    const std::string path =
        (std::filesystem::temp_directory_path() / "ultraauth-prefs-test.ini").string();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    Check(p.Save(path), "save succeeds");
    Check(!std::filesystem::exists(path + ".tmp"), "temporary file is renamed away");
    Check(Preferences::Load(path) == p, "load reads back what was saved");

    // Saving over an existing file replaces it entirely.
    Preferences q;
    Check(q.Save(path), "second save succeeds");
    Check(Preferences::Load(path) == q, "second save replaced the first");
    std::filesystem::remove(path, ec);

    Check(!p.Save("/nonexistent/dir/settings.ini"), "save into a missing directory fails");
}

static void TestPreferencesParsing() {
    std::printf("Lenient parsing and clamping\n");
    Preferences p = Preferences::Parse(
        "  idle_lock_seconds =   60  \n"
        "lock_on_minimize = No\n"
        "hide_codes=YES\n"
        "unknown_key = 5\n");
    Check(p.idleLockSeconds == 60, "whitespace around key and value is ignored");
    Check(!p.lockOnMinimize, "No parses as false, case-insensitive");
    Check(p.hideCodes, "YES parses as true");

    p = Preferences::Parse("idle_lock_seconds = -5\nhide_codes = maybe\nlock_on_minimize = 0\n");
    Check(p.idleLockSeconds == 300, "a negative number keeps the default, not 0");
    Check(!p.hideCodes, "an unrecognised boolean keeps the default");
    Check(!p.lockOnMinimize, "0 parses as false");

    p = Preferences::Parse("idle_lock_seconds = 99999999999999999999\n");
    Check(p.idleLockSeconds == Preferences::kMaxIdleLockSeconds,
          "an oversized number clamps to the maximum, not to 0");

    p = Preferences::Parse("idle_lock_seconds = 0\n");
    Check(p.idleLockSeconds == 0, "0 is a valid value (idle lock off)");

    p = Preferences::Parse("idle_lock_seconds = 12abc\n");
    Check(p.idleLockSeconds == 300, "trailing garbage keeps the default");
}

int main() {
    std::printf("UltraAuthenticator lock-policy and preferences tests\n\n");

    TestDelaySchedule();
    TestThrottleStateMachine();
    TestPreferencesDefaults();
    TestPreferencesRoundTrip();
    TestPreferencesParsing();

    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
