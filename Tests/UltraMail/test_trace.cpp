// Tests/UltraMail/test_trace.cpp
// The timing trace (UltraMailTrace.h): its lines and their times, quiet
// stages that speak only when slow, and the watchdog that reports a blocked
// UI thread and the stage it is in.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"

#include "UltraMailTrace.h"
#include "UltraCanvasPathUtf8.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

using namespace UltraMail;
namespace fs = std::filesystem;

namespace {

// Starts a trace written only to a file of its own; ReadBack stops it and
// returns what it wrote.
struct TraceToFile {
    std::string path;
    explicit TraceToFile(const std::string& name) {
        path = UltraCanvas::PathToUtf8(fs::temp_directory_path() / ("ultramail_trace_" + name + ".log"));
        Trace::SetConsoleOutput(false);
        Trace::SetLogFile(path);
        Trace::Enable(true);
    }
    std::string ReadBack() {
        Trace::Enable(false);
        Trace::SetLogFile("");
        std::ifstream in(UltraCanvas::PathFromUtf8(path));
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::error_code ec;
        fs::remove(UltraCanvas::PathFromUtf8(path), ec);
        return text;
    }
};

bool Has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

TEST(trace_off_writes_nothing) {
    TraceToFile trace("off");
    Trace::Enable(false);
    Trace::Line("not said");
    { Trace::Stage stage("not timed"); }
    REQUIRE(trace.ReadBack().empty());
}

TEST(trace_line_carries_time_of_day_seconds_and_thread) {
    TraceToFile trace("line");
    Trace::Line("hello");
    const std::string text = trace.ReadBack();
    // "[UltraMail 17:46:12.345 +   1.234 s w1] hello"
    REQUIRE(text.rfind("[UltraMail ", 0) == 0);
    REQUIRE(text.size() > 24 && text[13] == ':' && text[16] == ':' && text[19] == '.');
    REQUIRE(Has(text, " +"));
    REQUIRE(Has(text, " s "));
    REQUIRE(Has(text, "] hello\n"));
}

TEST(trace_stage_reports_begin_and_end_with_its_time) {
    TraceToFile trace("stage");
    {
        Trace::Stage outer("Switch to account work");
        Trace::Stage inner("Folder tree", 0);
    }
    const std::string text = trace.ReadBack();
    REQUIRE(Has(text, "] > Switch to account work\n"));
    REQUIRE(Has(text, "]   < Folder tree: "));          // nested: indented, one line
    REQUIRE(!Has(text, "> Folder tree"));
    REQUIRE(Has(text, "] < Switch to account work: "));
    REQUIRE(Has(text, " ms\n"));
}

TEST(trace_quiet_stage_says_nothing_when_fast) {
    TraceToFile trace("quiet_fast");
    {
        Trace::Stage quiet("Refresh", 10000);
        Trace::Line("inside");
        Trace::Stage step("Account bar", 0);
    }
    REQUIRE(trace.ReadBack().empty());
}

TEST(trace_quiet_stage_says_all_when_slow) {
    TraceToFile trace("quiet_slow");
    {
        Trace::Stage quiet("Refresh", 5);
        { Trace::Stage step("Account bar", 0); }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const std::string text = trace.ReadBack();
    const auto begin = text.find("> Refresh");
    const auto step  = text.find("< Account bar: ");
    const auto end   = text.find("< Refresh: ");
    REQUIRE(begin != std::string::npos);
    REQUIRE(step != std::string::npos);
    REQUIRE(end != std::string::npos);
    REQUIRE(begin < step);
    REQUIRE(step < end);
}

TEST(trace_formats_durations) {
    REQUIRE_EQ(Trace::FormatMs(3.24), std::string("3.2 ms"));
    REQUIRE_EQ(Trace::FormatMs(0), std::string("0.0 ms"));
    REQUIRE_EQ(Trace::FormatMs(12.4), std::string("12 ms"));
    REQUIRE_EQ(Trace::FormatMs(999.4), std::string("999 ms"));
    REQUIRE_EQ(Trace::FormatMs(1940), std::string("1.94 s"));
    REQUIRE_EQ(Trace::FormatMs(12050), std::string("12.05 s"));
}

// A stand-in for the event loop: a thread that runs posted tasks in order.
TEST(trace_watchdog_reports_a_blocked_ui_thread_and_its_stage) {
    TraceToFile trace("watchdog");
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> tasks;
    bool quit = false;
    std::thread ui([&]() {
        Trace::MarkUiThread();
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&]() { return quit || !tasks.empty(); });
                if (tasks.empty()) return;
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            task();
        }
    });
    auto post = [&](std::function<void()> task) {
        { std::lock_guard<std::mutex> lock(mutex); tasks.push_back(std::move(task)); }
        wake.notify_all();
    };

    Trace::StartUiWatchdog(post, /*stallMs=*/100, /*intervalMs=*/20);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));   // answered in time: silence
    std::atomic<bool> blocked{false};
    post([&]() {
        Trace::Stage stage("Show its stored mail");
        Trace::Stage step("Messages from the store", 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        blocked = true;
    });
    while (!blocked) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));   // the late answer arrives
    Trace::StopUiWatchdog();
    { std::lock_guard<std::mutex> lock(mutex); quit = true; }
    wake.notify_all();
    ui.join();

    const std::string text = trace.ReadBack();
    REQUIRE(Has(text, "! UI thread blocked "));
    REQUIRE(Has(text, "in: Show its stored mail > Messages from the store"));
    REQUIRE(Has(text, "! UI thread answered again after "));
    // The UI thread's own lines are tagged as such.
    REQUIRE(Has(text, " s ui] > Show its stored mail"));
}
