// Tests/SettingsWatcherTest.cpp
// UltraCanvasSettingsWatcher in poll mode, without a display: a handler per
// file name, run only for the file that changed - created, written again,
// removed - and once per change; a file nobody watches changes nothing; a
// handler replaced is the new one; Start remembers the files as they are, so
// nothing fires for what was already there. The poster queues what the
// native watch posts and the test drains it, as the UI thread would: every
// look at the files is on this thread, as in an application, so a change
// fires once whether the native watch or the poll saw it first.
// Version: 1.1.0 - the posted checks queued and drained on this thread: a poster that
//                  ran them on the watcher's thread raced the test's own Poll
// Version: 1.0.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework
#include "UltraCanvasSettingsWatcher.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace UltraCanvas;

static int testCount = 0, failCount = 0;
static void Check(bool ok, const char* what) {
    ++testCount;
    if (!ok) { ++failCount; std::printf("FAIL: %s\n", what); }
}
static void WriteFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path, std::ios::trunc) << text;
    // a second write of the same size inside the file time's resolution
    // would look unchanged: let the clock move
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

int main() {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / ("uc-settings-watcher-test-" + std::to_string(getpid()));
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    WriteFile(folder / "Emoji.conf", "style=colour\n");

    int appearance = 0, emoji = 0, posted = 0;
    // What the native watch posts, run here when the test says so - the UI
    // thread's queue. The poster is called on the watcher's thread.
    std::mutex queueMutex;
    std::vector<std::function<void()>> queue;
    auto drain = [&] {
        std::vector<std::function<void()>> tasks;
        { std::lock_guard<std::mutex> lock(queueMutex); tasks.swap(queue); }
        for (auto& task : tasks) task();
    };
    UltraCanvasSettingsWatcher watcher;
    watcher.Watch("Appearance.conf", [&] { ++appearance; });
    watcher.Watch("Emoji.conf", [&] { ++emoji; });
    const bool native = watcher.Start(folder, [&](std::function<void()> task) {
        std::lock_guard<std::mutex> lock(queueMutex);
        ++posted;
        queue.push_back(std::move(task));
    });
    std::printf("native watch: %s\n", native ? "yes" : "no");
    // Every look: what the native watch posted, then the poll - on this
    // thread, as the application's UI thread does both.
    auto poll = [&] { drain(); watcher.Poll(); };
    poll();
    Check(appearance == 0 && emoji == 0, "Start remembers the files as they are: nothing fires at the first look");

    WriteFile(folder / "Appearance.conf", "look=dark\n");
    poll();
    Check(appearance == 1 && emoji == 0, "a file created fires its handler, not another's");
    poll();
    Check(appearance == 1, "and once only");

    WriteFile(folder / "Appearance.conf", "look=light\n");
    poll();
    Check(appearance == 2, "a file written again fires again");

    WriteFile(folder / "Appearance.conf", "look=dark\n");   // the same size as the last text: the time tells
    poll();
    Check(appearance == 3, "a write of the same size is seen by its time");

    WriteFile(folder / "Language.conf", "language=de\n");
    poll();
    Check(appearance == 3 && emoji == 0, "a file nobody watches changes nothing");

    WriteFile(folder / "Emoji.conf", "style=monochrome\n");
    poll();
    Check(emoji == 1 && appearance == 3, "the other file's handler for the other file");

    int replaced = 0;
    watcher.Watch("Emoji.conf", [&] { ++replaced; });
    WriteFile(folder / "Emoji.conf", "style=system\n");
    poll();
    Check(emoji == 1 && replaced == 1, "a second Watch of a name replaces the handler");

    std::filesystem::remove(folder / "Appearance.conf");
    poll();
    Check(appearance == 4, "a file removed fires its handler");

    if (native) {
        // the native watch posts one look per burst through the poster; the
        // queue is drained without a poll of this thread's own
        const int postedBefore = [&] { std::lock_guard<std::mutex> lock(queueMutex); return posted; }();
        WriteFile(folder / "Appearance.conf", "look=dark\n");
        for (int i = 0; i < 100; ++i) {
            { std::lock_guard<std::mutex> lock(queueMutex); if (posted > postedBefore) break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        drain();
        Check(appearance == 5, "the native watch reports a change through the poster");
    }
    watcher.Stop();
    Check(!watcher.IsNative(), "Stop ends the watch");
    drain();   // anything posted meanwhile runs harmlessly after Stop

    std::filesystem::remove_all(folder);
    std::printf("%d/%d passed\n", testCount - failCount, testCount);
    return failCount ? 1 : 0;
}
