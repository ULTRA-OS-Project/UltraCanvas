// Apps/UltraClaude/engine/ClaudeCliProcess.h
// Runs one child process with all three standard streams piped: writes a
// block of text to its standard input and closes it (or, with
// InputMode::KeepOpen, keeps it open for WriteInput - how a login code is
// pasted into `claude auth login`), then hands its standard output back one
// line at a time while it runs, and its standard error when it exits.
// UltraCanvas::RunProcessCaptured (UltraCanvasUtils.h) waits for the child and
// returns everything at the end; a chat has to show the reply as it is
// written, so this streams instead.
//
// argv is a list, never a command line: POSIX executes it with execvp, so no
// shell sees the strings. On Windows the arguments are quoted for
// CreateProcessW; a `claude.cmd` (the npm install) is started through cmd.exe,
// which is why the prompt never travels as an argument - it goes through
// standard input.
//
// The callbacks run on the reader thread, not the UI thread.
//
// Version: 0.1.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraClaude {

class ClaudeCliProcess {
public:
    using LineCallback = std::function<void(const std::string& line)>;
    using ExitCallback = std::function<void(int exitCode, const std::string& standardError)>;

    enum class InputMode {
        CloseAfterWrite,   // standardInput is all the child gets
        KeepOpen           // more can follow through WriteInput, until CloseInput
    };

    ClaudeCliProcess() = default;
    ~ClaudeCliProcess();
    ClaudeCliProcess(const ClaudeCliProcess&) = delete;
    ClaudeCliProcess& operator=(const ClaudeCliProcess&) = delete;

    // Starts argv[0] in workingDirectory (empty: inherit ours), writes
    // standardInput to it and closes the pipe. onLine receives each line of
    // standard output without its line ending; onExit is called once, after
    // the last line, with the exit code (-1 when the child was killed by a
    // signal). Returns false with outError when nothing could be started,
    // and then calls neither callback. Fails while a child is still running.
    bool Start(const std::vector<std::string>& argv,
               const std::string& workingDirectory,
               const std::string& standardInput,
               LineCallback onLine,
               ExitCallback onExit,
               std::string& outError,
               InputMode inputMode = InputMode::CloseAfterWrite);

    // InputMode::KeepOpen only: writes text to the child's standard input
    // (any thread). Returns false when the input is closed or the child is
    // gone.
    bool WriteInput(const std::string& text);
    // Closes the child's standard input (it reads end-of-file).
    void CloseInput();

    // Asks the child (and the processes it started) to end. Returns at once;
    // onExit still follows when it has.
    void Stop();

    // Stops the child and waits for the reader thread to finish.
    void StopAndWait();

    bool IsRunning() const { return running_.load(); }

private:
    void Pump(std::string standardInput, LineCallback onLine, ExitCallback onExit);

    std::thread reader_;
    std::atomic<bool> running_{false};
    std::mutex childMutex_;   // guards the child handle against Stop racing the reap
    std::mutex inputMutex_;   // guards the standard-input end in KeepOpen mode
    bool keepInputOpen_ = false;

#if defined(_WIN32)
    void* process_ = nullptr;   // HANDLE
    void* job_ = nullptr;       // HANDLE: the job the child and its children run in
    void* stdinWrite_ = nullptr;
    void* stdoutRead_ = nullptr;
    void* stderrRead_ = nullptr;
#else
    int pid_ = -1;
    int stdinFd_ = -1;
    int stdoutFd_ = -1;
    int stderrFd_ = -1;
#endif
};

} // namespace UltraClaude
