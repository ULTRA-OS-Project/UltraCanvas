// Apps/UltraMail/engine/UltraMailTrace.h
// Timing trace: what UltraMail spends its time on while starting and while
// switching accounts, written as it happens to the console the application
// was started with (stderr - on Windows the console window that opens with
// UltraMail) and to trace.log in the data folder.
//
//   [UltraMail 17:46:12.345 +   1.234 s ui] > Switch to account work
//   [UltraMail 17:46:12.347 +   1.236 s ui]   < Folder tree: 2.1 ms
//   [UltraMail 17:46:12.413 +   1.302 s ui] < Switch to account work: 68 ms
//   [UltraMail 17:46:22.413 +  11.302 s w1] ! UI thread blocked 1.00 s so far, in: Refresh > Account counts
//
// Every line carries the time of day, the seconds since the process started
// and the thread ("ui", or "w<n>" for a worker); every stage its duration.
//
// A Stage times a block: "> name" when it begins and "< name: N ms" when it
// ends, indented by how deep it is nested on its thread. A quiet stage
// (reportAboveMs >= 0) - the refresh after every sync, or one step of a
// larger stage - holds back everything said inside it and says it only when
// it took at least that long: then its "> name", the lines from inside it and
// its "< name: N ms"; a quiet stage with nothing inside it only the last. So
// a fast refresh prints nothing, and a slow one prints where its time went.
//
// The UI watchdog finds what no stage covers: it
// asks the UI thread to answer every quarter second and, when no answer comes,
// says for how long and in which UltraMail stage the UI thread is - or that it
// is in none, which is the framework's own work: handling an event, laying
// out or painting a frame.
//
// Off until the application turns it on (Enable), so the engine's tests and
// any other host stay quiet; every call is a branch when it is off.
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <chrono>
#include <functional>
#include <string>

namespace UltraMail {
namespace Trace {

// On or off; off by default.
void Enable(bool on);
bool Enabled();

// Also write every line to this file (UTF-8 path), emptied first. The lines
// written before it was named go into it too, so a trace that starts before
// the data folder is known is still complete. An empty path stops it. The file
// stops growing at a few megabytes and says so.
void SetLogFile(const std::string& utf8Path);

// Whether the lines also go to stderr (the console); on by default. The
// tests write only to their file.
void SetConsoleOutput(bool on);

// The thread that calls this is the UI thread: its lines are tagged "ui", and
// its stages are what the watchdog names.
void MarkUiThread();

// Seconds of the trace clock: 0 when the process started (where the platform
// says when that was), otherwise the first use of the trace.
double NowSeconds();

// How long the process ran before the trace clock was first read - loading
// the program and its libraries - or -1 where the platform cannot say.
double ProcessStartToTraceMs();

// One line, with the time and thread in front, indented as deep as the
// stages open on this thread.
void Line(const std::string& text);

// "12 ms", "1.9 s".
std::string FormatMs(double ms);

class Stage {
public:
    // reportAboveMs < 0: "> name" now and "< name: N ms" at the end.
    // reportAboveMs >= 0: quiet; nothing unless it took at least that long
    // (0: always, as one line when nothing was said inside it).
    explicit Stage(std::string name, double reportAboveMs = -1);
    ~Stage();
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

    // Milliseconds since this stage began.
    double ElapsedMs() const;

private:
    std::string name_;
    double reportAboveMs_ = -1;
    bool active_ = false;
    bool onUiThread_ = false;
    std::string beginLine_;           // a quiet stage's "> name", said only if it reports
    std::chrono::steady_clock::time_point start_{};
};

// The watchdog's way to the UI thread: run this task there (the application's
// PostToUIThread).
using PostToUi = std::function<void(std::function<void()>)>;

// Starts the UI watchdog: a thread that posts an empty task every
// `intervalMs` and reports when the UI thread takes longer than `stallMs` to
// run it - at once, then every second or two while it lasts, and once more
// when the UI thread answers again. Does nothing while the trace is off.
void StartUiWatchdog(PostToUi post, int stallMs = 250, int intervalMs = 250);
void StopUiWatchdog();

// The UI thread's open stages, outermost first ("Refresh > Account counts"),
// or "" when none is open.
std::string CurrentUiStages();

} // namespace Trace
} // namespace UltraMail
