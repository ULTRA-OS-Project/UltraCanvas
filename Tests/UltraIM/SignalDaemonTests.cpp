// Tests/UltraIM/SignalDaemonTests.cpp
// SignalDaemon on its own: the one place of the Signal provider that is
// written per platform (fork/exec and a process group; CreateProcessW in a
// job object), run for real on each of them. SignalProviderTests reaches it
// through the provider, on POSIX only - its stand-in for signal-cli is a
// shell script - so the Windows half was compiled and never run.
//
// The program started is this test itself ("--child ..."): it reports the
// arguments and the environment it was given, or stays until it is stopped.
// What is checked is what the provider relies on:
//   - the variables handed to Start reach the child exactly (the proxy goes
//     to signal-cli as JAVA_TOOL_OPTIONS): spaces, quotes, '|' and '&', Thai
//     and an emoji; a variable the environment already has is replaced, not
//     doubled; the rest of the environment is kept
//   - the arguments reach it exactly: a folder with spaces and a trailing
//     backslash, quotes, an empty one, Thai
//   - standard output and error are appended to the log, whose name has a
//     space, Thai and an emoji
//   - through a wrapper script, as signal-cli is one (a .bat run by cmd.exe
//     on Windows, a shell script elsewhere), in a folder with a space and
//     Thai in its name: the same environment and arguments, and Stop ends
//     the script and the program it started
//   - a program that ends by itself, and one that is not there
//   - FindSignalCli, JavaToolOptionsWith, JvmTookOptions, FreeLoopbackPort
// Version: 0.1.0
// Last Modified: 2026-11-19
// Author: UltraCanvas Framework

#include "SignalDaemon.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <fcntl.h>
#include <io.h>
#else
#include <cerrno>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
extern "C" char** environ;
#endif

using namespace UltraIMSignal;

namespace {

namespace fs = std::filesystem;

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

// The variables the child reports. The last is the fourth again in another
// spelling: one variable on Windows, where names have no case, two elsewhere.
const char* const kNames[] = {"ULTRAIM_DAEMON_TEST_KEEP", "ULTRAIM_DAEMON_TEST_REPLACE", "ULTRAIM_DAEMON_TEST_NEW",
                              "ULTRAIM_DAEMON_TEST_CASE", "Ultraim_Daemon_Test_Case"};
const char* const kSelfVariable = "ULTRAIM_DAEMON_TEST_SELF";

#if defined(_WIN32)
std::wstring Wide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), n);
    return wide;
}

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(std::max(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), n, nullptr, nullptr);
    return utf8;
}
#endif

void SetEnv(const std::string& name, const std::string& value) {
#if defined(_WIN32)
    SetEnvironmentVariableW(Wide(name).c_str(), Wide(value).c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

void UnsetEnv(const std::string& name) {
#if defined(_WIN32)
    SetEnvironmentVariableW(Wide(name).c_str(), nullptr);
#else
    unsetenv(name.c_str());
#endif
}

// Every "NAME=value" of this process's environment, UTF-8.
std::vector<std::string> EnvironmentEntries() {
    std::vector<std::string> entries;
#if defined(_WIN32)
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* e = block; *e; e += wcslen(e) + 1) entries.push_back(Narrow(e));
        FreeEnvironmentStringsW(block);
    }
#else
    for (char** e = environ; e && *e; ++e) entries.emplace_back(*e);
#endif
    return entries;
}

bool SameName(const std::string& a, const std::string& b) {
#if defined(_WIN32)
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
#else
    return a == b;
#endif
}

long OwnProcessId() {
#if defined(_WIN32)
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}

// ----------------------------------------------------------------------------
// The child
// ----------------------------------------------------------------------------

// The arguments after "--child <mode>", UTF-8. On Windows from the wide
// command line: main's argv is in the ANSI code page, which has no Thai.
std::vector<std::string> ChildArguments(int argc, char** argv) {
    std::vector<std::string> args;
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    int count = 0;
    if (wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 3; i < count; ++i) args.push_back(Narrow(wide[i]));
        LocalFree(wide);
    }
#else
    for (int i = 3; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return args;
}

int ChildMain(int argc, char** argv) {
#if defined(_WIN32)
    // Bytes as written: no "\r\n" for "\n" in what the test compares.
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    const std::string mode = argc > 2 ? argv[2] : "";
    if (mode == "stay") {
        std::printf("PID:%ld\nREADY\n", OwnProcessId());
        std::fflush(stdout);
        // Until it is stopped; not for ever, should the test die first.
        for (int i = 0; i < 1200; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 0;
    }
    if (mode != "report") return 2;
    for (const std::string& arg : ChildArguments(argc, argv)) std::printf("ARG:%s\n", arg.c_str());
    const std::vector<std::string> entries = EnvironmentEntries();
    for (const char* name : kNames) {
        int count = 0;
        std::string value;
        for (const std::string& entry : entries) {
            const size_t eq = entry.find('=', entry.empty() ? 0 : 1);
            if (eq == std::string::npos || !SameName(entry.substr(0, eq), name)) continue;
            if (count++ == 0) value = entry.substr(eq + 1);
        }
        std::printf("ENV:%s:%d:%s\n", name, count, value.c_str());
    }
    std::printf("DONE\n");
    std::fflush(stdout);
    std::fprintf(stderr, "ERR:to standard error\n");
    std::fflush(stderr);
    return 0;
}

// ----------------------------------------------------------------------------
// The test
// ----------------------------------------------------------------------------

// This program's own file, UTF-8.
std::string SelfPath(const char* argv0) {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (n > 0 && n < buffer.size()) return Narrow(buffer.substr(0, n));
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        std::error_code ec;
        const fs::path real = fs::canonical(UltraCanvas::PathFromUtf8(buffer), ec);
        return ec ? std::string(buffer) : UltraCanvas::PathToUtf8(real);
    }
#else
    {
        std::error_code ec;
        const fs::path real = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) return UltraCanvas::PathToUtf8(real);
    }
#endif
    std::error_code ec;
    const fs::path fallback = fs::absolute(UltraCanvas::PathFromUtf8(argv0 ? argv0 : ""), ec);
    return UltraCanvas::PathToUtf8(fallback);
}

bool Eventually(const std::function<bool()>& holds, int seconds = 30) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!holds()) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

std::string ReadText(const std::string& path) {
    std::string out;
    std::FILE* f = UltraCanvas::OpenFileUtf8(path, "rb");
    if (!f) return out;
    char buffer[4096];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, n);
    std::fclose(f);
    return out;
}

bool WriteText(const fs::path& path, const std::string& text, bool program = false) {
    std::FILE* f = UltraCanvas::OpenFileUtf8(UltraCanvas::PathToUtf8(path), "wb");
    if (!f) return false;
    const bool whole = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
#if !defined(_WIN32)
    if (program) chmod(path.c_str(), 0755);
#else
    (void)program;
#endif
    return whole;
}

// The lines of `text` that begin with `prefix`, without it (and without the
// "\r" a batch file's own lines end with).
std::vector<std::string> Lines(const std::string& text, const std::string& prefix) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(prefix, 0) == 0) out.push_back(line.substr(prefix.size()));
        start = end + 1;
    }
    return out;
}

bool Has(const std::string& text, const std::string& line) {
    const std::vector<std::string> all = Lines(text, "");
    return std::find(all.begin(), all.end(), line) != all.end();
}

// No process of that id any more. (A process ended and not yet collected by
// whoever inherited it counts as gone: it runs nothing.)
bool ProcessGone(long pid) {
    if (pid <= 0) return false;
#if defined(_WIN32)
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (!h) return true;
    const bool ended = WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
    CloseHandle(h);
    return ended;
#else
    if (kill(static_cast<pid_t>(pid), 0) != 0) return errno == ESRCH;
#if defined(__linux__)
    const std::string stat = ReadText("/proc/" + std::to_string(pid) + "/stat");
    const size_t close = stat.rfind(')');
    if (close != std::string::npos && close + 2 < stat.size() && stat[close + 2] == 'Z') return true;
#endif
    return false;
#endif
}

// A wrapper as signal-cli is one - a batch file on Windows, a shell script
// elsewhere - that runs this program as the child named by kSelfVariable in
// the environment (so the file holds no path, and no character a batch file's
// code page lacks). `replaced`: the script becomes the program (exec); a
// batch file cannot, so there cmd.exe stays either way.
fs::path WriteWrapper(const fs::path& folder, const std::string& name, const std::string& mode, bool replaced) {
#if defined(_WIN32)
    (void)replaced;
    const fs::path path = folder / UltraCanvas::PathFromUtf8(name + ".bat");
    const std::string script = "@echo off\r\n\"%" + std::string(kSelfVariable) + "%\" --child " + mode +
                               " %*\r\necho WRAPPER-DONE\r\n";
#else
    const fs::path path = folder / UltraCanvas::PathFromUtf8(name);
    const std::string script = std::string("#!/bin/sh\n") + (replaced ? "exec " : "") + "\"$" + kSelfVariable +
                               "\" --child " + mode + " \"$@\"\necho WRAPPER-DONE\n";
#endif
    CHECK(WriteText(path, script, true));
    return path;
}

void ExpectEnvironment(const std::string& log, const std::string& newValue, bool withEnvironment) {
    const std::vector<std::string> env = Lines(log, "ENV:");
    auto has = [&](const std::string& line) { return std::find(env.begin(), env.end(), line) != env.end(); };
    // Inherited, untouched: Thai in an inherited value too.
    CHECK(has("ULTRAIM_DAEMON_TEST_KEEP:1:kept \xE0\xB9\x84\xE0\xB8\xA7\xE0\xB9\x89"));
    if (!withEnvironment) {
        CHECK(has("ULTRAIM_DAEMON_TEST_REPLACE:1:old"));
        CHECK(has("ULTRAIM_DAEMON_TEST_NEW:0:"));
        return;
    }
    // Replaced, not doubled: one entry, the new value.
    CHECK(has("ULTRAIM_DAEMON_TEST_REPLACE:1:new"));
    CHECK(has("ULTRAIM_DAEMON_TEST_NEW:1:" + newValue));
#if defined(_WIN32)
    // Names have no case: the variable set as ULTRAIM_DAEMON_TEST_CASE takes
    // the place of the one the environment spells Ultraim_Daemon_Test_Case.
    CHECK(has("ULTRAIM_DAEMON_TEST_CASE:1:new-case"));
    CHECK(has("Ultraim_Daemon_Test_Case:1:new-case"));
#else
    CHECK(has("ULTRAIM_DAEMON_TEST_CASE:1:new-case"));
    CHECK(has("Ultraim_Daemon_Test_Case:1:old-case"));
#endif
}

void TestDaemon(const std::string& self, const fs::path& base) {
    // "ทดสอบ" (Thai, "test") and a space in the folder; an emoji in the log's name.
    const fs::path folder = base / UltraCanvas::PathFromUtf8("signal cli \xE0\xB8\x97\xE0\xB8\x94\xE0\xB8\xAA\xE0\xB8\xAD\xE0\xB8\x9A");
    std::error_code ec;
    fs::create_directories(folder, ec);
    CHECK(!ec);
    const std::string log = UltraCanvas::PathToUtf8(folder / UltraCanvas::PathFromUtf8("daemon \xF0\x9F\x99\x82.log"));

    SetEnv(kSelfVariable, self);
    SetEnv("ULTRAIM_DAEMON_TEST_KEEP", "kept \xE0\xB9\x84\xE0\xB8\xA7\xE0\xB9\x89");
    SetEnv("ULTRAIM_DAEMON_TEST_REPLACE", "old");
    SetEnv("Ultraim_Daemon_Test_Case", "old-case");
    UnsetEnv("ULTRAIM_DAEMON_TEST_NEW");
    // As the proxy reaches signal-cli: Java options in one variable - spaces,
    // '=', quotes, the '|' of http.nonProxyHosts, '&' and '%', a name in Thai.
    const std::string options = "-Dhttps.proxyHost=proxy.example -Dhttps.proxyPort=3128 "
                                "-Dhttp.nonProxyHosts=localhost|127.*|[::1] -Dname=\"\xE0\xB8\xAA\xE0\xB8\xA1\xE0\xB8\x8A\xE0\xB8\xB2\xE0\xB8\xA2 \xF0\x9F\x99\x82\" a&b 100%";
    const std::vector<std::pair<std::string, std::string>> environment = {
        {"ULTRAIM_DAEMON_TEST_REPLACE", "new"},
        {"ULTRAIM_DAEMON_TEST_NEW", options},
        {"ULTRAIM_DAEMON_TEST_CASE", "new-case"},
    };

    // ---- a program: arguments, environment, the log ------------------------------
    std::printf("  a program\n");
    CHECK(WriteText(UltraCanvas::PathFromUtf8(log), "earlier line\n"));
    const std::vector<std::string> arguments = {
        "plain",
        "two words",
        "C:\\Users\\Jane Doe\\signal cli\\",      // a folder with spaces, ending in a backslash
        "say \"hi\"",
        "back\\slash\\",
        "",
        "a\\\"b",
        "\xE0\xB8\x97\xE0\xB8\x94\xE0\xB8\xAA\xE0\xB8\xAD\xE0\xB8\x9A \xF0\x9F\x99\x82",
        "--receive-mode=manual",
    };
    {
        SignalDaemon daemon;
        CHECK(daemon.ProcessId() == 0 && !daemon.IsRunning());
        std::vector<std::string> args = {"--child", "report"};
        args.insert(args.end(), arguments.begin(), arguments.end());
        std::string error;
        CHECK(daemon.Start(self, args, log, error, environment));
        CHECK(error.empty());
        CHECK(Eventually([&]() { return !daemon.IsRunning(); }));
        // Ended by itself: no process is named any more.
        CHECK(daemon.ProcessId() == 0);
        const std::string text = ReadText(log);
        CHECK(text.rfind("earlier line\n", 0) == 0);           // appended to, not written over
        CHECK(Lines(text, "ARG:") == arguments);
        ExpectEnvironment(text, options, true);
        CHECK(Has(text, "DONE"));
        CHECK(Has(text, "ERR:to standard error"));              // standard error is in the log too
        daemon.Stop();                                           // nothing to stop: no harm
        CHECK(daemon.ProcessId() == 0 && !daemon.IsRunning());
    }

    // ---- nothing to set: the environment as it is --------------------------------
    {
        CHECK(WriteText(UltraCanvas::PathFromUtf8(log), ""));
        SignalDaemon daemon;
        std::string error;
        CHECK(daemon.Start(self, {"--child", "report"}, log, error));
        CHECK(Eventually([&]() { return !daemon.IsRunning(); }));
        const std::string text = ReadText(log);
        CHECK(Lines(text, "ARG:").empty());
        ExpectEnvironment(text, options, false);
    }

    // ---- through a wrapper script, as signal-cli is one --------------------------
    std::printf("  a wrapper script\n");
    {
        CHECK(WriteText(UltraCanvas::PathFromUtf8(log), ""));
        const fs::path wrapper = WriteWrapper(folder, "signal-cli-report", "report", true);
        // What the provider starts signal-cli with.
        const std::vector<std::string> daemonArguments = {
            "--config", UltraCanvas::PathToUtf8(folder / "signal-cli"), "daemon", "--http", "127.0.0.1:7583",
            "--receive-mode=manual",
        };
        SignalDaemon daemon;
        std::string error;
        CHECK(daemon.Start(UltraCanvas::PathToUtf8(wrapper), daemonArguments, log, error, environment));
        CHECK(error.empty());
        CHECK(Eventually([&]() { return !daemon.IsRunning(); }));
        const std::string text = ReadText(log);
        CHECK(Lines(text, "ARG:") == daemonArguments);
        ExpectEnvironment(text, options, true);
        CHECK(Has(text, "ERR:to standard error"));
    }

    // ---- running, and stopped: the script and what it started --------------------
    std::printf("  stopped with what it started\n");
    {
        CHECK(WriteText(UltraCanvas::PathFromUtf8(log), ""));
        const fs::path wrapper = WriteWrapper(folder, "signal-cli-stay", "stay", false);
        SignalDaemon daemon;
        std::string error;
        CHECK(daemon.Start(UltraCanvas::PathToUtf8(wrapper), {}, log, error, environment));
        CHECK(Eventually([&]() { return Has(ReadText(log), "READY"); }));
        CHECK(daemon.IsRunning());
        const long wrapperPid = static_cast<long>(daemon.ProcessId());
        const std::vector<std::string> pids = Lines(ReadText(log), "PID:");
        const long programPid = pids.empty() ? 0 : std::atol(pids[0].c_str());
        CHECK(wrapperPid > 0 && programPid > 0);
        // The script is one process, the program it runs another - as
        // signal-cli.bat and the Java runtime are.
        CHECK(wrapperPid != programPid);
        CHECK(!ProcessGone(wrapperPid) && !ProcessGone(programPid));
        const auto before = std::chrono::steady_clock::now();
        daemon.Stop(3000);
        const auto took = std::chrono::steady_clock::now() - before;
        CHECK(!daemon.IsRunning());
        CHECK(daemon.ProcessId() == 0);
        CHECK(Eventually([&]() { return ProcessGone(wrapperPid) && ProcessGone(programPid); }, 10));
        // Asked, it ends at once: the grace time is for one that does not.
        CHECK(took < std::chrono::seconds(3));
        // Stopped, not finished: the script never reached its last line.
        CHECK(!Has(ReadText(log), "WRAPPER-DONE"));
        // And once more from the start, on the same object.
        CHECK(daemon.Start(self, {"--child", "stay"}, log, error));
        CHECK(Eventually([&]() { return Lines(ReadText(log), "PID:").size() == 2; }));
        CHECK(daemon.IsRunning());
        const long again = static_cast<long>(daemon.ProcessId());
        CHECK(again > 0 && again != wrapperPid);
        // (Started directly, the process is the program itself.)
        CHECK(Lines(ReadText(log), "PID:").size() == 2 && std::atol(Lines(ReadText(log), "PID:")[1].c_str()) == again);
        // Start stops the one before it.
        CHECK(daemon.Start(self, {"--child", "stay"}, log, error));
        CHECK(Eventually([&]() { return ProcessGone(again); }, 10));
        const long third = static_cast<long>(daemon.ProcessId());
        CHECK(third > 0 && third != again && daemon.IsRunning());
        // Left to the destructor: it stops what is running.
        (void)third;
    }

    // ---- a program that is not there --------------------------------------------
    std::printf("  no program\n");
    {
        SignalDaemon daemon;
        std::string error;
        const std::string missing = UltraCanvas::PathToUtf8(folder / "no-such-signal-cli");
        // Either it cannot be started at all (Windows), or it is started and
        // ends at once (fork, then an exec that fails): never left "running".
        const bool started = daemon.Start(missing, {"daemon"}, log, error);
        if (!started) CHECK(error.find(missing) != std::string::npos);
        CHECK(Eventually([&]() { return !daemon.IsRunning(); }, 10));
        CHECK(daemon.ProcessId() == 0);
    }

    // ---- FindSignalCli ----------------------------------------------------------
    std::printf("  signal-cli found\n");
    {
#if defined(_WIN32)
        const std::string name = "signal-cli.bat";
#else
        const std::string name = "signal-cli";
#endif
        const fs::path bin = folder / "plugins" / "signal-cli" / "bin";
        fs::create_directories(bin, ec);
        CHECK(WriteText(bin / UltraCanvas::PathFromUtf8(name), "placeholder\n", true));
        const std::string shipped = UltraCanvas::PathToUtf8(bin / UltraCanvas::PathFromUtf8(name));
        const std::string plugins = UltraCanvas::PathToUtf8(folder / "plugins");
        // The one the application ships, next to the provider modules.
        UnsetEnv("ULTRAIM_SIGNAL_CLI");
        CHECK(FindSignalCli(plugins) == shipped);
        // ULTRAIM_SIGNAL_CLI comes first: a file that is there, or nothing -
        // not "the shipped one after all".
        const fs::path own = folder / UltraCanvas::PathFromUtf8("my signal-cli" + std::string(name == "signal-cli" ? "" : ".bat"));
        CHECK(WriteText(own, "placeholder\n", true));
        SetEnv("ULTRAIM_SIGNAL_CLI", UltraCanvas::PathToUtf8(own));
        CHECK(FindSignalCli(plugins) == UltraCanvas::PathToUtf8(own));
        SetEnv("ULTRAIM_SIGNAL_CLI", UltraCanvas::PathToUtf8(folder / "not-there"));
        CHECK(FindSignalCli(plugins).empty());
        UnsetEnv("ULTRAIM_SIGNAL_CLI");
    }

    // ---- the Java options -------------------------------------------------------
    std::printf("  Java options\n");
    {
        UnsetEnv("JAVA_TOOL_OPTIONS");
        CHECK(JavaToolOptionsWith("-Dhttps.proxyHost=proxy.example") == "-Dhttps.proxyHost=proxy.example");
        // After what the environment already holds, so that the setting wins.
        SetEnv("JAVA_TOOL_OPTIONS", "-Dinherited=\xE0\xB8\x97 -Dhttps.proxyHost=other.example");
        CHECK(JavaToolOptionsWith("-Dhttps.proxyHost=proxy.example") ==
              "-Dinherited=\xE0\xB8\x97 -Dhttps.proxyHost=other.example -Dhttps.proxyHost=proxy.example");
        UnsetEnv("JAVA_TOOL_OPTIONS");

        // What a Java runtime writes first when it read them - looked for
        // from where this start's lines begin.
        const std::string earlier = "Picked up JAVA_TOOL_OPTIONS: -Dfrom=an-earlier-start\n";
        CHECK(WriteText(UltraCanvas::PathFromUtf8(log), earlier + "INFO  DaemonCommand - Started HTTP server\n"));
        CHECK(JvmTookOptions(log, 0));
        CHECK(!JvmTookOptions(log, earlier.size()));
        CHECK(!JvmTookOptions(UltraCanvas::PathToUtf8(folder / "no.log"), 0));
    }

    // ---- a port for the daemon --------------------------------------------------
    {
        const int port = FreeLoopbackPort();
        CHECK(port > 0 && port < 65536);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--child") == 0) return ChildMain(argc, argv);

    UltraNet_Initialize();
    const std::string self = SelfPath(argc > 0 ? argv[0] : nullptr);
    std::printf("SignalDaemonTests (%s)\n", self.c_str());
    std::error_code ec;
    const fs::path base = fs::temp_directory_path() / ("ultraim-daemon-" + std::to_string(OwnProcessId()));
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    CHECK(!ec);
    TestDaemon(self, base);
    fs::remove_all(base, ec);
    UltraNet_Shutdown();
    std::printf("SignalDaemonTests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
