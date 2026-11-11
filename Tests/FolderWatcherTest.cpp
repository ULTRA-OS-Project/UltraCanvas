// Tests/FolderWatcherTest.cpp
// Unit tests for UltraCanvasFolderWatcher: that a watch reports real changes,
// that it refuses what it cannot watch, and - the part a leak or a crash hides
// behind - that Stop() is final and repeatable.
//
// Framework-independent: builds from the watcher's own sources (core plus this
// platform's backend), no display connection and no UltraCanvas library.
//
// On a platform with no native backend every Watch() fails by design; the
// change-reporting checks are then skipped rather than failed, because the
// caller's documented answer there is to poll instead.
// Version: 1.2.0 - UltraCanvasFolderWatchHold: watches below a held folder let go,
//                  none starts there, the hold's end lifts it
// Version: 1.1.0 - a change right after Watch() returns is reported
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework

#include "UltraCanvasFolderWatcher.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using UltraCanvas::UltraCanvasFolderWatcher;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, what) do { \
    ++checks; \
    if (cond) { \
        std::printf("  PASS  %s\n", what); \
    } else { \
        ++failures; \
        std::printf("  FAIL  %s (%s:%d)\n", what, __FILE__, __LINE__); \
    } \
} while (0)

// Waits for the hit counter to move past `from`, up to a second. Filesystem
// notifications are asynchronous everywhere, so a fixed sleep would either be
// flaky or slow; this is neither.
static bool WaitForHit(const std::atomic<int>& hits, int from) {
    for (int i = 0; i < 200; ++i) {
        if (hits.load() > from) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

int main() {
    const bool native = UltraCanvasFolderWatcher::NativeBackendAvailable();
    std::printf("── UltraCanvasFolderWatcher (native backend: %s) ──\n",
                native ? "yes" : "no - watching falls back to polling");

    // ===== WHAT CANNOT BE WATCHED =====
    {
        UltraCanvasFolderWatcher watcher;
        CHECK(!watcher.Watch("/ultracanvas/no/such/folder", []() {}),
              "a folder that does not exist is refused");
        CHECK(!watcher.IsWatching(), "and the watcher reports itself idle");
        CHECK(watcher.WatchedPath().empty(), "and remembers no path");
    }
    {
        UltraCanvasFolderWatcher watcher;
        CHECK(!watcher.Watch(fs::temp_directory_path().string(), nullptr),
              "a watch without a callback is refused");
    }
    {
        UltraCanvasFolderWatcher watcher;
        CHECK(!watcher.Watch("", []() {}), "an empty path is refused");
    }

    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "ultracanvas_folderwatcher_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    if (ec) {
        std::printf("  could not create %s: %s\n", dir.string().c_str(),
                    ec.message().c_str());
        return 1;
    }

    std::atomic<int> hits{0};
    UltraCanvasFolderWatcher watcher;
    const bool started = watcher.Watch(dir.string(), [&hits]() { ++hits; });

    CHECK(started == native, "a real folder is watched exactly when a backend exists");
    CHECK(watcher.IsWatching() == started, "IsWatching() agrees with the result");
    CHECK(watcher.WatchedPath() == (started ? dir.string() : std::string()),
          "the watched path is reported only while watching");

    if (started) {
        // ===== WHAT A WATCH MUST REPORT =====
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        int before = hits.load();
        { std::ofstream(dir / "created.txt") << "hello"; }
        CHECK(WaitForHit(hits, before), "a created file is reported");

        before = hits.load();
        { std::ofstream(dir / "created.txt", std::ios::app) << " again"; }
        CHECK(WaitForHit(hits, before), "a rewritten file is reported");

        before = hits.load();
        fs::rename(dir / "created.txt", dir / "renamed.txt", ec);
        CHECK(WaitForHit(hits, before), "a renamed file is reported");

        before = hits.load();
        fs::remove(dir / "renamed.txt", ec);
        CHECK(WaitForHit(hits, before), "a deleted file is reported");

        // ===== STOP IS FINAL =====
        // The callback captures the counter by reference: one arriving after
        // Stop() would be a use-after-free in any real caller.
        watcher.Stop();
        CHECK(!watcher.IsWatching(), "Stop() clears the watch");
        CHECK(watcher.WatchedPath().empty(), "Stop() clears the path");

        const int afterStop = hits.load();
        { std::ofstream(dir / "ignored.txt") << "no one is listening"; }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        CHECK(hits.load() == afterStop, "no callback arrives after Stop()");

        watcher.Stop();
        CHECK(true, "Stop() twice is safe");

        // ===== RE-TARGETING =====
        // Navigating between folders is Watch() on a live watcher; the old
        // folder must go quiet and the new one must report.
        const fs::path other = dir / "other";
        fs::create_directories(other, ec);
        std::atomic<int> otherHits{0};
        CHECK(watcher.Watch(other.string(), [&otherHits]() { ++otherHits; }),
              "a stopped watcher can be started again on another folder");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const int otherBefore = otherHits.load();
        { std::ofstream(other / "here.txt") << "x"; }
        CHECK(WaitForHit(otherHits, otherBefore), "the new folder is reported");

        // ===== LISTENING WHEN WATCH() RETURNS =====
        // A caller starts the watch and then lists the folder, so a change in
        // between must be reported - no settling sleep here. Windows used to
        // issue its first read on the backend's thread, after Watch() had
        // returned, and lost exactly this change.
        const fs::path eager = dir / "eager";
        fs::create_directories(eager, ec);
        std::atomic<int> eagerHits{0};
        CHECK(watcher.Watch(eager.string(), [&eagerHits]() { ++eagerHits; }),
              "a fresh folder is watched");
        { std::ofstream(eager / "at-once.txt") << "x"; }
        CHECK(WaitForHit(eagerHits, 0),
              "a change made right after Watch() returns is reported");
        watcher.Stop();

        // ===== STOP IS NOT A FAILURE =====
        // The failure callback exists so a caller can fall back to polling
        // when a watch dies on its own. A Stop() the caller performed is not
        // that, and reporting it would send every navigation through the
        // recovery path.
        std::atomic<int> quietFailures{0};
        CHECK(watcher.Watch(other.string(), [&otherHits]() { ++otherHits; },
                            [&quietFailures]() { ++quietFailures; }),
              "a watch takes a failure callback as well");
        watcher.Stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(quietFailures.load() == 0, "Stop() is not reported as a failure");

        // ===== A WATCH THAT DIES ON ITS OWN =====
        // Deleting the watched folder is the reachable stand-in for what a
        // pulled USB stick does to the watch on a folder of that volume: the
        // descriptor / handle is retired under the watcher. Without this the
        // caller never hears, and a file display simply stops noticing
        // changes - which is exactly the bug this callback was added for.
        const fs::path doomed = dir / "doomed";
        fs::create_directories(doomed, ec);
        std::atomic<int> lost{0};
        CHECK(watcher.Watch(doomed.string(), []() {},
                            [&lost]() { ++lost; }),
              "a folder about to be deleted can be watched");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        fs::remove_all(doomed, ec);
        CHECK(WaitForHit(lost, 0), "losing the watched folder is reported once");
        const int lostOnce = lost.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CHECK(lost.load() == lostOnce, "and reported only once");
        watcher.Stop();

        // ===== HOLDS =====
        // A folder about to be deleted, renamed or moved: every watch on it
        // or below lets go (on Windows a watch holds the folder open, and a
        // second file display showing a subfolder kept a deleted folder
        // "delete pending" on FAT and exFAT), and none starts there until
        // the hold ends.
        const fs::path parent = dir / "Lei";
        const fs::path inside = parent / "Album";
        const fs::path elsewhere = dir / "elsewhere";
        const fs::path lookalike = dir / "Lei2";
        fs::create_directories(inside, ec);
        fs::create_directories(elsewhere, ec);
        fs::create_directories(lookalike, ec);
        UltraCanvasFolderWatcher below, outside;
        std::atomic<int> belowLost{0}, outsideLost{0};
        CHECK(below.Watch(inside.string(), []() {}, [&belowLost]() { ++belowLost; }) &&
              outside.Watch(elsewhere.string(), []() {}, [&outsideLost]() { ++outsideLost; }),
              "two watches, one below the folder about to go");
        {
            UltraCanvas::UltraCanvasFolderWatchHold hold({parent.string() + "/"});
            CHECK(hold.Released() == 1, "a hold releases the watch below its folder, and only that one");
            CHECK(!below.IsWatching(), "the released watcher is idle");
            CHECK(belowLost.load() == 1, "and reports itself failed, at once");
            CHECK(below.WatchedPath() == inside.string(),
                  "and keeps its path for the owner to poll");
            CHECK(outside.IsWatching() && outsideLost.load() == 0, "a watch elsewhere is untouched");

            UltraCanvasFolderWatcher late;
            CHECK(!late.Watch(inside.string(), []() {}), "no new watch starts below a held folder");
            CHECK(!late.Watch(parent.string(), []() {}), "nor on the held folder itself");
            CHECK(late.Watch(lookalike.string(), []() {}),
                  "a folder whose name merely starts the same is not below it");
            {
                UltraCanvas::UltraCanvasFolderWatchHold again({parent.string()});
                CHECK(again.Released() == 0, "a second hold on it finds nothing left to release");
            }
            CHECK(!late.Watch(inside.string(), []() {}),
                  "the end of the second hold does not lift the first");
            late.Stop();

            fs::remove_all(parent, ec);
            CHECK(!ec && !fs::exists(parent, ec), "the held folder can be deleted");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            CHECK(belowLost.load() == 1, "the failure is reported once, not again as the folder goes");
            below.Stop();
        }
        fs::create_directories(inside, ec);
        UltraCanvasFolderWatcher after;
        CHECK(after.Watch(inside.string(), []() {}), "once the hold ends, the folder is watched again");
        after.Stop();
        outside.Stop();
    }

    fs::remove_all(dir, ec);

    std::printf("%s: %d checks, %d failure(s)\n",
                failures ? "FAILURE" : "OK", checks, failures);
    return failures ? 1 : 0;
}
