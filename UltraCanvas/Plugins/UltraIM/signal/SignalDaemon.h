// Plugins/UltraIM/signal/SignalDaemon.h
// signal-cli as a child process of the Signal provider: found (the
// ULTRAIM_SIGNAL_CLI environment variable, signal-cli/bin/signal-cli next to
// the provider modules, the PATH), started as a daemon on a loopback port,
// and stopped with the provider. The one place of the provider that is
// written per platform: POSIX fork/exec, a process group, SIGTERM then
// SIGKILL and waitpid; Windows CreateProcessW in a job object that ends the
// whole tree (signal-cli.bat starts java) with TerminateJobObject.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace UltraIMSignal {

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
    // logPath (UTF-8). False with a sentence in error when it could not be
    // started at all; a program that starts and then exits (no Java, say)
    // shows as !IsRunning().
    bool Start(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
               std::string& error);
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
