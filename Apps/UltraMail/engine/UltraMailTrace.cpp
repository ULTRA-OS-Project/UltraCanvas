// Apps/UltraMail/engine/UltraMailTrace.cpp
// Version: 1.0.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailTrace.h"

#include "UltraCanvasPathUtf8.h"   // OpenFileUtf8

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>
#elif defined(__linux__)
#include <fstream>
#include <sstream>
#include <unistd.h>
#endif

namespace UltraMail {
namespace Trace {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxFileBytes    = 4u * 1024 * 1024;
constexpr std::size_t kMaxPendingBytes = 256u * 1024;

// How long the process had been running when this is called, from the
// platform's record of when it started; -1 where there is none.
double ProcessAgeMs() {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER c, n;
    c.LowPart = created.dwLowDateTime; c.HighPart = created.dwHighDateTime;
    n.LowPart = now.dwLowDateTime;     n.HighPart = now.dwHighDateTime;
    if (n.QuadPart < c.QuadPart) return -1;
    return static_cast<double>(n.QuadPart - c.QuadPart) / 10000.0;   // 100 ns units
#elif defined(__APPLE__)
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(getpid()) };
    struct kinfo_proc info{};
    size_t size = sizeof(info);
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size == 0) return -1;
    struct timeval now{};
    gettimeofday(&now, nullptr);
    const struct timeval& start = info.kp_proc.p_starttime;
    const double ms = (now.tv_sec - start.tv_sec) * 1000.0 + (now.tv_usec - start.tv_usec) / 1000.0;
    return ms >= 0 ? ms : -1;
#elif defined(__linux__)
    // Field 22 of /proc/self/stat: the start in clock ticks after boot;
    // /proc/uptime: seconds since boot. The command name (field 2) may hold
    // spaces, so the fields are counted from its closing parenthesis.
    std::ifstream statFile("/proc/self/stat");
    std::string stat((std::istreambuf_iterator<char>(statFile)), std::istreambuf_iterator<char>());
    const std::size_t close = stat.rfind(')');
    if (close == std::string::npos) return -1;
    std::istringstream fields(stat.substr(close + 1));
    std::string field;
    unsigned long long startTicks = 0;
    for (int i = 3; i <= 22 && (fields >> field); ++i)
        if (i == 22) startTicks = std::strtoull(field.c_str(), nullptr, 10);
    std::ifstream uptimeFile("/proc/uptime");
    std::string uptimeText;
    if (!(uptimeFile >> uptimeText)) return -1;
    // "12345.67": whole seconds and hundredths, read without the locale.
    const std::size_t dot = uptimeText.find('.');
    const long long whole = std::strtoll(uptimeText.substr(0, dot).c_str(), nullptr, 10);
    long long hundredths = 0;
    if (dot != std::string::npos)
        hundredths = std::strtoll((uptimeText.substr(dot + 1) + "00").substr(0, 2).c_str(), nullptr, 10);
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    if (startTicks == 0 || ticksPerSecond <= 0) return -1;
    const double uptimeMs = whole * 1000.0 + hundredths * 10.0;
    const double startMs  = static_cast<double>(startTicks) * 1000.0 / ticksPerSecond;
    return uptimeMs >= startMs ? uptimeMs - startMs : -1;
#else
    return -1;
#endif
}

struct ClockOrigin {
    Clock::time_point origin = Clock::now();
    double processAgeMs = ProcessAgeMs();
};

const ClockOrigin& Origin() {
    static const ClockOrigin origin;
    return origin;
}

struct State {
    std::atomic<bool> enabled{false};
    std::atomic<bool> console{true};
    std::mutex writeMutex;
    std::FILE* file = nullptr;
    std::size_t fileBytes = 0;
    bool fileFull = false;
    bool fileNamed = false;
    std::string pending;              // lines before SetLogFile named the file

    std::atomic<std::thread::id> uiThread{};
    std::atomic<int> nextThreadIndex{1};

    std::mutex uiStagesMutex;
    std::vector<std::string> uiStages;

    std::mutex watchdogMutex;         // the watchdog thread's start and stop
    std::thread watchdog;
    std::atomic<bool> stopWatchdog{false};
    std::mutex stopMutex;
    std::condition_variable stopSignal;
};

State& S() {
    // Leaked on purpose: a line from a detached worker during shutdown must
    // not meet a destroyed mutex.
    static State* state = new State();
    return *state;
}

thread_local int t_depth = 0;
thread_local int t_index = 0;
// The lines held back by the quiet stages open on this thread, innermost last.
thread_local std::vector<std::vector<std::string>> t_heldBack;
constexpr std::size_t kMaxHeldBackLines = 4000;

bool OnUiThread() {
    return S().uiThread.load() == std::this_thread::get_id();
}

std::string ThreadTag() {
    if (OnUiThread()) return "ui";
    if (t_index == 0) t_index = S().nextThreadIndex.fetch_add(1);
    std::string tag = "w" + std::to_string(t_index);
    return tag;
}

// "  12.345" - seconds with milliseconds, no locale.
std::string SecondsText(double seconds) {
    const long long ms = static_cast<long long>(seconds * 1000.0 + 0.5);
    std::string whole = std::to_string(ms / 1000);
    std::string frac  = std::to_string(ms % 1000);
    while (frac.size() < 3) frac.insert(frac.begin(), '0');
    while (whole.size() < 4) whole.insert(whole.begin(), ' ');
    return whole + "." + frac;
}

// "17:46:12.345" - the wall clock, to set the trace beside another log or
// beside what was seen on screen.
std::string TimeOfDayText() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    auto two = [](int v) { return std::string(v < 10 ? "0" : "") + std::to_string(v); };
    std::string msText = std::to_string(ms);
    while (msText.size() < 3) msText.insert(msText.begin(), '0');
    return two(tm.tm_hour) + ":" + two(tm.tm_min) + ":" + two(tm.tm_sec) + "." + msText;
}

void WriteLine(const std::string& line) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.writeMutex);
    if (s.console.load()) {
        std::fputs(line.c_str(), stderr);
        std::fflush(stderr);
    }
    if (!s.fileNamed) {
        if (s.pending.size() + line.size() <= kMaxPendingBytes) s.pending += line;
        return;
    }
    if (!s.file || s.fileFull) return;
    if (s.fileBytes + line.size() > kMaxFileBytes) {
        std::fputs("(trace.log is full; the console has the rest)\n", s.file);
        std::fflush(s.file);
        s.fileFull = true;
        return;
    }
    std::fputs(line.c_str(), s.file);
    std::fflush(s.file);
    s.fileBytes += line.size();
}

std::string Indent(int depth) {
    return std::string(static_cast<std::size_t>(depth > 0 ? depth * 2 : 0), ' ');
}

void Watch(PostToUi post, int stallMs, int intervalMs) {
    State& s = S();
    struct Ping {
        std::mutex mutex;
        std::condition_variable answered;
        bool done = false;
    };
    while (!s.stopWatchdog.load()) {
        auto ping = std::make_shared<Ping>();
        const Clock::time_point sent = Clock::now();
        post([ping]() {
            { std::lock_guard<std::mutex> lock(ping->mutex); ping->done = true; }
            ping->answered.notify_all();
        });
        double nextReportMs = stallMs;
        bool reported = false;
        std::string lastWhere;
        double elapsedMs = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(ping->mutex);
                ping->answered.wait_for(lock, std::chrono::milliseconds(50),
                                        [&]() { return ping->done; });
                if (ping->done) break;
            }
            if (s.stopWatchdog.load()) return;
            elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - sent).count();
            if (elapsedMs < nextReportMs) continue;
            std::string where = CurrentUiStages();
            if (where.empty())
                where = "no UltraMail stage (the framework: an event, a timer, layout or painting)";
            // The same place again: say only that it goes on.
            Line("! UI thread blocked " + FormatMs(elapsedMs) + " so far, in: " +
                 (reported && where == lastWhere ? "(the same)" : where));
            lastWhere = where;
            reported = true;
            nextReportMs = elapsedMs < 1000 ? 1000 : nextReportMs + (nextReportMs < 5000 ? 1000 : 5000);
        }
        if (reported) {
            elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - sent).count();
            Line("! UI thread answered again after " + FormatMs(elapsedMs));
        }
        std::unique_lock<std::mutex> lock(s.stopMutex);
        s.stopSignal.wait_for(lock, std::chrono::milliseconds(intervalMs),
                              [&]() { return s.stopWatchdog.load(); });
    }
}

} // namespace

void Enable(bool on) {
    Origin();   // the clock starts no later than this
    S().enabled.store(on);
}

bool Enabled() {
    return S().enabled.load(std::memory_order_relaxed);
}

void SetLogFile(const std::string& utf8Path) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.writeMutex);
    if (s.file) { std::fclose(s.file); s.file = nullptr; }
    s.fileBytes = 0;
    s.fileFull = false;
    s.fileNamed = !utf8Path.empty();
    if (!s.fileNamed) return;
    s.file = UltraCanvas::OpenFileUtf8(utf8Path, "w");
    if (s.file && !s.pending.empty()) {
        std::fputs(s.pending.c_str(), s.file);
        std::fflush(s.file);
        s.fileBytes = s.pending.size();
    }
    s.pending.clear();
    s.pending.shrink_to_fit();
}

void SetConsoleOutput(bool on) {
    S().console.store(on);
}

void MarkUiThread() {
    S().uiThread.store(std::this_thread::get_id());
}

double NowSeconds() {
    const ClockOrigin& o = Origin();
    const double sinceOrigin = std::chrono::duration<double>(Clock::now() - o.origin).count();
    return sinceOrigin + (o.processAgeMs > 0 ? o.processAgeMs / 1000.0 : 0.0);
}

double ProcessStartToTraceMs() {
    return Origin().processAgeMs;
}

namespace {

// The whole line: time of day, seconds since the start, thread, text.
std::string Format(const std::string& text) {
    std::string tag = ThreadTag();
    while (tag.size() < 3) tag.insert(tag.begin(), ' ');
    return "[UltraMail " + TimeOfDayText() + " +" + SecondsText(NowSeconds()) + " s" + tag +
           "] " + text + "\n";
}

// To the innermost quiet stage holding lines back, or out.
void Emit(std::string line) {
    if (!t_heldBack.empty()) {
        if (t_heldBack.back().size() < kMaxHeldBackLines) t_heldBack.back().push_back(std::move(line));
        return;
    }
    WriteLine(line);
}

} // namespace

void Line(const std::string& text) {
    if (!Enabled()) return;
    Emit(Format(Indent(t_depth) + text));
}

std::string FormatMs(double ms) {
    if (ms < 0) ms = 0;
    if (ms < 10) {
        // One decimal: "3.2 ms".
        const long long tenths = static_cast<long long>(ms * 10.0 + 0.5);
        return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " ms";
    }
    if (ms < 1000) return std::to_string(static_cast<long long>(ms + 0.5)) + " ms";
    // Two decimals of a second: "1.94 s".
    const long long hundredths = static_cast<long long>(ms / 10.0 + 0.5);
    std::string frac = std::to_string(hundredths % 100);
    if (frac.size() < 2) frac.insert(frac.begin(), '0');
    return std::to_string(hundredths / 100) + "." + frac + " s";
}

Stage::Stage(std::string name, double reportAboveMs)
    : name_(std::move(name)), reportAboveMs_(reportAboveMs) {
    if (!Enabled()) return;
    active_ = true;
    onUiThread_ = OnUiThread();
    if (reportAboveMs_ < 0) {
        Line("> " + name_);
    } else {
        beginLine_ = Format(Indent(t_depth) + "> " + name_);
        t_heldBack.emplace_back();
    }
    ++t_depth;
    if (onUiThread_) {
        std::lock_guard<std::mutex> lock(S().uiStagesMutex);
        S().uiStages.push_back(name_);
    }
    start_ = Clock::now();
}

Stage::~Stage() {
    if (!active_) return;
    const double ms = ElapsedMs();
    if (onUiThread_) {
        std::lock_guard<std::mutex> lock(S().uiStagesMutex);
        if (!S().uiStages.empty()) S().uiStages.pop_back();
    }
    --t_depth;
    const std::string endText = "< " + name_ + ": " + FormatMs(ms);
    if (reportAboveMs_ < 0) {
        Line(endText);
        return;
    }
    std::vector<std::string> inside;
    if (!t_heldBack.empty()) {
        inside = std::move(t_heldBack.back());
        t_heldBack.pop_back();
    }
    if (ms < reportAboveMs_ || !Enabled()) return;
    if (!inside.empty()) {
        Emit(std::move(beginLine_));
        for (auto& line : inside) Emit(std::move(line));
    }
    Emit(Format(Indent(t_depth) + endText));
}

double Stage::ElapsedMs() const {
    if (!active_) return 0;
    return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
}

void StartUiWatchdog(PostToUi post, int stallMs, int intervalMs) {
    if (!Enabled() || !post) return;
    State& s = S();
    std::lock_guard<std::mutex> lock(s.watchdogMutex);
    if (s.watchdog.joinable()) return;
    s.stopWatchdog.store(false);
    s.watchdog = std::thread(Watch, std::move(post), stallMs, intervalMs);
}

void StopUiWatchdog() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.watchdogMutex);
    if (!s.watchdog.joinable()) return;
    {
        std::lock_guard<std::mutex> stopLock(s.stopMutex);
        s.stopWatchdog.store(true);
    }
    s.stopSignal.notify_all();
    s.watchdog.join();
}

std::string CurrentUiStages() {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.uiStagesMutex);
    std::string path;
    for (const auto& stage : s.uiStages) {
        if (!path.empty()) path += " > ";
        path += stage;
    }
    return path;
}

} // namespace Trace
} // namespace UltraMail
