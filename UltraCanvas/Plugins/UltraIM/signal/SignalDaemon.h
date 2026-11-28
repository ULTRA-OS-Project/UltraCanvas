// Plugins/UltraIM/signal/SignalDaemon.h
// signal-cli as a child process of the Signal provider: found (the
// ULTRAIM_SIGNAL_CLI environment variable, signal-cli/bin/signal-cli next to
// the provider modules, the PATH), started as a daemon on a loopback port,
// and stopped with the provider. The one place of the provider that is
// written per platform: POSIX fork/exec, a process group, SIGTERM then
// SIGKILL and waitpid; Windows CreateProcessW in a job object that ends the
// whole tree (signal-cli.bat starts java) with TerminateJobObject.
//
// The proxy: signal-cli connects to Signal by itself - nothing of UltraNet's
// is between it and Signal's servers - so the proxy every UltraCanvas
// application connects through (UltraCanvasProxySettings.h) is handed to it
// where it reads one: the Java runtime's proxy properties, which signal-cli
// (0.13.13 and later - SignalCliTakesProxy) passes on to its network library as well. They go in
// JAVA_TOOL_OPTIONS in the daemon's environment, after what that variable
// already holds, so the setting wins over a proxy named there. See
// DaemonProxy below.
// Version: 0.2.0 - DaemonProxy: the shared proxy setting as the Java options signal-cli reads;
//                  Start takes variables for the child's environment; JvmTookOptions, SignalCliTakesProxy
// Version: 0.1.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#pragma once

#include "UltraNet/UltraNetCore.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace UltraIMSignal {

// How the daemon is to connect, from the proxy setting, as the Java options
// that say it (https.proxyHost / socksProxyHost and the rest):
//   AsItIs    nothing is passed: the setting is the operating system's and it
//             names no proxy for Signal - the environment stays as it is
//   Direct    the setting says no proxy (or excepts Signal's servers): the
//             options take out a proxy the environment's own options name
//   Through   an HTTP proxy (it tunnels: CONNECT) or a SOCKS 5 one
//   Unusable  a proxy signal-cli has no means for - one that asks for a user
//             name and password (signal-cli passes none on), SOCKS 4, a proxy
//             reached over TLS. `text` says so; the daemon is not started,
//             rather than started to connect directly.
struct DaemonProxy {
    enum class Kind { AsItIs, Direct, Through, Unusable };
    Kind        kind = Kind::AsItIs;
    std::string jvmOptions;     // "" for AsItIs and Unusable
    std::string text;           // Through: "socks5h://127.0.0.1:10808"; Unusable: why, for the person

    bool operator==(const DaemonProxy&) const = default;
};

// `setting` is the module-wide proxy setting, `resolved` what it comes to for
// Signal's servers (UltraNet_ResolveProxy). Pure, for the tests.
DaemonProxy DaemonProxyFor(const UltraNetProxyConfig& setting, const UltraNetProxyConfig& resolved);
// For the setting as it is now. Asks the operating system when the setting
// is its proxy, which may take a moment: not on the UI thread.
DaemonProxy ChooseDaemonProxy();
// The daemon's JAVA_TOOL_OPTIONS: what the environment has, then `options`.
std::string JavaToolOptionsWith(const std::string& options);
// Whether a Java runtime started after the log at logPath was `from` bytes
// long says it read JAVA_TOOL_OPTIONS ("Picked up JAVA_TOOL_OPTIONS: ...",
// which it writes to standard error before anything else). A signal-cli that
// is no Java program - its native build - writes no such line, and nothing
// says it reads the options.
bool JvmTookOptions(const std::string& logPath, uint64_t from);
// Whether a signal-cli of this version ("0.13.24", as its `version` call
// says it) sends everything through the proxy Java names: 0.13.13 and later
// do. Before that its network library was told nothing of the proxy, and
// what went by it - asking Signal which numbers are registered, for one
// (signal-cli's changelog for 0.13.13) - connected directly. False for a
// version that cannot be read.
bool SignalCliTakesProxy(const std::string& version);

// The signal-cli program to start (UTF-8), or "" when there is none:
// ULTRAIM_SIGNAL_CLI when set (a file), else <pluginDirectory>/signal-cli/
// bin/signal-cli (signal-cli.bat on Windows) when the application ships one,
// else the first signal-cli on the PATH.
std::string FindSignalCli(const std::string& pluginDirectory);

// A TCP port on 127.0.0.1 that is free now (0 when none could be had).
int FreeLoopbackPort();

class SignalDaemon {
public:
    SignalDaemon() = default;
    ~SignalDaemon();
    SignalDaemon(const SignalDaemon&) = delete;
    SignalDaemon& operator=(const SignalDaemon&) = delete;

    // Starts program with args, its standard output and error appended to
    // logPath (UTF-8). `environment`: variables set in its environment, on
    // top of this program's own. False with a sentence in error when it could
    // not be started at all; a program that starts and then exits (no Java,
    // say) shows as !IsRunning().
    bool Start(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
               std::string& error, const std::vector<std::pair<std::string, std::string>>& environment = {});
    // Asks it to end (SIGTERM; Windows: the job is ended), waits up to
    // graceMs, then ends it for good; the process is reaped. Nothing when
    // none was started.
    void Stop(int graceMs = 5000);
    // Started and not yet exited.
    bool IsRunning();
    // The operating system's id of the process (0 when none).
    int64_t ProcessId() const;

private:
    mutable std::mutex mutex_;
#if defined(_WIN32)
    void* process_ = nullptr;      // HANDLE
    void* job_ = nullptr;          // HANDLE
    uint32_t pid_ = 0;
#else
    int pid_ = -1;
#endif
};

} // namespace UltraIMSignal
