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
// Version: 1.0.0
// Last Modified: 2026-08-25
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
        watcher.Stop();
    }

    fs::remove_all(dir, ec);

    std::printf("%s: %d checks, %d failure(s)\n",
                failures ? "FAILURE" : "OK", checks, failures);
    return failures ? 1 : 0;
}
