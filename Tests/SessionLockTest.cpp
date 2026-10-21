// Tests/SessionLockTest.cpp
// UltraCanvasSessionLock: listeners hear a lock and an unlock once each, a
// listener may remove itself while it is called, and on Linux the D-Bus
// backend turns a screensaver's ActiveChanged signal into those calls.
//
// The first half needs nothing. The second needs a display (the application
// it watches through), a session bus and gdbus to send the signal; CTest runs
// it under dbus-run-session where that is installed, and it skips otherwise.
// Version: 1.0.0
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasSessionLock.h"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0;
static int failCount = 0;

#define TEST(name, condition)                                                 \
    do {                                                                      \
        bool passed = (condition);                                            \
        std::cerr << (passed ? "PASS" : "FAIL") << ": " << name << std::endl; \
        if (!passed) failCount++;                                             \
        testCount++;                                                          \
    } while (0)

namespace {

int Finish() {
    std::cerr << testCount - failCount << "/" << testCount << " passed" << std::endl;
    return failCount == 0 ? 0 : 1;
}

void Listeners() {
    TEST("no callback, no listener", AddSessionLockListener(nullptr) == 0);

    std::vector<bool> heard;
    const SessionLockListenerId a = AddSessionLockListener([&](bool locked) { heard.push_back(locked); });
    TEST("a listener gets an id", a != 0);

    NotifySessionLockChanged(true);
    NotifySessionLockChanged(true);   // a second source reporting the same lock
    NotifySessionLockChanged(false);
    TEST("a lock and an unlock, once each", heard == std::vector<bool>({true, false}));

    // One that removes itself while it is called, beside one that stays.
    int selfCalls = 0;
    SessionLockListenerId self = 0;
    self = AddSessionLockListener([&](bool) {
        ++selfCalls;
        RemoveSessionLockListener(self);
    });
    NotifySessionLockChanged(true);
    NotifySessionLockChanged(false);
    TEST("a listener that removed itself is called once", selfCalls == 1);
    TEST("the other still hears both", heard == std::vector<bool>({true, false, true, false}));

    // One that removes another before it is reached.
    SessionLockListenerId later = 0;
    int laterCalls = 0;
    const SessionLockListenerId first = AddSessionLockListener([&](bool) { RemoveSessionLockListener(later); });
    later = AddSessionLockListener([&](bool) { ++laterCalls; });
    NotifySessionLockChanged(true);
    TEST("a listener removed during the call is not called", laterCalls == 0);

    RemoveSessionLockListener(first);
    RemoveSessionLockListener(a);
    RemoveSessionLockListener(a);   // twice is harmless
    TEST("nothing is watched once the last listener is gone", !IsWatchingSessionLock());

    // The state is forgotten with the last listener: a new one hears the next
    // lock even though the last report was a lock.
    std::vector<bool> fresh;
    const SessionLockListenerId b = AddSessionLockListener([&](bool locked) { fresh.push_back(locked); });
    NotifySessionLockChanged(true);
    TEST("a new listener hears the next lock", fresh == std::vector<bool>({true}));
    RemoveSessionLockListener(b);
}

#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
void PumpUntil(UltraCanvasApplication& app, const std::function<bool()>& done, int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!done() && std::chrono::steady_clock::now() < deadline) app.RunOnce();
}

bool Emit(const std::string& path, const std::string& iface, bool active) {
    const std::string command = "gdbus emit --session --object-path " + path + " --signal " + iface +
                                ".ActiveChanged " + (active ? "true" : "false") + " >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
}

void DBusBackend() {
    if (!std::getenv("DISPLAY")) { std::cerr << "SKIP: D-Bus backend (no DISPLAY)" << std::endl; return; }
    const char* bus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (!bus || !*bus) { std::cerr << "SKIP: D-Bus backend (no session bus)" << std::endl; return; }
    if (std::system("command -v gdbus >/dev/null 2>&1") != 0) {
        std::cerr << "SKIP: D-Bus backend (no gdbus)" << std::endl;
        return;
    }
    UltraCanvasApplication app;
    if (!app.Initialize("SessionLockTest")) { std::cerr << "SKIP: D-Bus backend (no application)" << std::endl; return; }
    // Without a timer the loop waits for an event however long it takes; this
    // one lets PumpUntil look at its deadline.
    const TimerId tick = app.StartTimer(20, true, [](TimerId) {});

    std::vector<bool> heard;
    const SessionLockListenerId id = AddSessionLockListener([&](bool locked) { heard.push_back(locked); });
    TEST("the backend watches with a session bus", IsWatchingSessionLock());
    // The subscription reaches the bus daemon asynchronously; let it settle
    // before the first signal is sent.
    PumpUntil(app, []() { return false; }, 300);

    TEST("freedesktop screensaver: signal sent", Emit("/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", true));
    PumpUntil(app, [&]() { return heard.size() >= 1; });
    TEST("ActiveChanged(true) is a lock", heard == std::vector<bool>({true}));

    TEST("GNOME screensaver: signal sent", Emit("/org/gnome/ScreenSaver", "org.gnome.ScreenSaver", false));
    PumpUntil(app, [&]() { return heard.size() >= 2; });
    TEST("ActiveChanged(false) is an unlock", heard == std::vector<bool>({true, false}));

    RemoveSessionLockListener(id);
    TEST("stopped with the last listener", !IsWatchingSessionLock());
    // Nothing reaches a listener once the backend has stopped.
    Emit("/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", true);
    PumpUntil(app, []() { return false; }, 300);
    TEST("no call after stopping", heard.size() == 2);
    app.StopTimer(tick);
}
#endif

} // namespace

int main() {
    std::cerr << "=== Session lock ===" << std::endl;
    Listeners();
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    DBusBackend();
#endif
    return Finish();
}
