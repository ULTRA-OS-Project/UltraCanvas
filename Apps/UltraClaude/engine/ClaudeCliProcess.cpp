// Apps/UltraClaude/engine/ClaudeCliProcess.cpp
// See ClaudeCliProcess.h.
// Version: 0.1.0
// Last Modified: 2026-10-02
// Author: UltraCanvas Framework / ULTRA OS

#include "ClaudeCliProcess.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "UltraCanvasUtils.h"   // Utf8ToWide / WideToUtf8
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace UltraClaude {

namespace {
    // Standard error is kept for the message shown when the CLI fails; a
    // runaway child must not grow it without bound.
    constexpr size_t kMaxStandardError = 64 * 1024;

    // Splits buffered output into lines, keeping an unfinished last line.
    void EmitLines(std::string& buffer, const ClaudeCliProcess::LineCallback& onLine) {
        size_t start = 0;
        for (;;) {
            const size_t newline = buffer.find('\n', start);
            if (newline == std::string::npos) break;
            size_t end = newline;
            if (end > start && buffer[end - 1] == '\r') --end;
            if (onLine) onLine(buffer.substr(start, end - start));
            start = newline + 1;
        }
        buffer.erase(0, start);
    }

    void AppendBounded(std::string& target, const char* data, size_t length) {
        if (target.size() >= kMaxStandardError) return;
        target.append(data, std::min(length, kMaxStandardError - target.size()));
    }
} // namespace

ClaudeCliProcess::~ClaudeCliProcess() {
    StopAndWait();
}

void ClaudeCliProcess::StopAndWait() {
    Stop();
    // A child that ignores the polite request is ended after three seconds.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (running_.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
#if !defined(_WIN32)
    if (running_.load()) {
        std::lock_guard<std::mutex> lock(childMutex_);
        if (pid_ > 0) { ::kill(-pid_, SIGKILL); ::kill(pid_, SIGKILL); }
    }
#endif
    if (reader_.joinable()) reader_.join();
}

#if !defined(_WIN32)
// ============================================================== POSIX

namespace {
    bool MakePipe(int fds[2]) {
        if (::pipe(fds) != 0) return false;
        ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
        return true;
    }

    void CloseFd(int& fd) {
        if (fd >= 0) { ::close(fd); fd = -1; }
    }

    void SetNonBlocking(int fd) {
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    }

    // The child writes the failed step and its errno here when chdir or exec
    // fails; a successful exec closes the pipe (FD_CLOEXEC), so the parent
    // reads nothing.
    enum ChildStep : int { kStepChdir = 1, kStepExec = 2 };
    [[noreturn]] void ChildFail(int reportFd, int step) {
        const int report[2] = {step, errno};
        ssize_t ignored = ::write(reportFd, report, sizeof(report));
        (void)ignored;
        ::_exit(127);
    }
} // namespace

bool ClaudeCliProcess::Start(const std::vector<std::string>& argv,
                             const std::string& workingDirectory,
                             const std::string& standardInput,
                             LineCallback onLine,
                             ExitCallback onExit,
                             std::string& outError) {
    if (argv.empty()) { outError = "No program to run."; return false; }
    if (running_.load()) { outError = "Claude is still answering."; return false; }
    if (reader_.joinable()) reader_.join();   // the previous run has finished

    // Writing to a child that has already exited raises SIGPIPE, whose
    // default action ends this program; the write's EPIPE is enough.
    ::signal(SIGPIPE, SIG_IGN);

    int in[2] = {-1, -1}, out[2] = {-1, -1}, err[2] = {-1, -1}, report[2] = {-1, -1};
    if (!MakePipe(in) || !MakePipe(out) || !MakePipe(err) || !MakePipe(report)) {
        outError = std::string("Could not create pipes: ") + std::strerror(errno);
        for (int* p : {in, out, err, report}) { CloseFd(p[0]); CloseFd(p[1]); }
        return false;
    }

    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        outError = std::string("Could not start a process: ") + std::strerror(errno);
        for (int* p : {in, out, err, report}) { CloseFd(p[0]); CloseFd(p[1]); }
        return false;
    }
    if (pid == 0) {
        // Its own process group, so Stop() reaches the processes the CLI
        // starts (its tools) as well as the CLI itself.
        ::setpgid(0, 0);
        ::dup2(in[0], STDIN_FILENO);
        ::dup2(out[1], STDOUT_FILENO);
        ::dup2(err[1], STDERR_FILENO);
        if (!workingDirectory.empty() && ::chdir(workingDirectory.c_str()) != 0)
            ChildFail(report[1], kStepChdir);
        ::execvp(args[0], args.data());
        ChildFail(report[1], kStepExec);
    }

    CloseFd(in[0]); CloseFd(out[1]); CloseFd(err[1]); CloseFd(report[1]);

    int childReport[2] = {0, 0};
    ssize_t got;
    do { got = ::read(report[0], childReport, sizeof(childReport)); } while (got < 0 && errno == EINTR);
    CloseFd(report[0]);
    if (got == static_cast<ssize_t>(sizeof(childReport))) {
        int status = 0;
        ::waitpid(pid, &status, 0);
        CloseFd(in[1]); CloseFd(out[0]); CloseFd(err[0]);
        const int childErrno = childReport[1];
        if (childReport[0] == kStepChdir)
            outError = "Cannot work in the folder '" + workingDirectory + "': " +
                       std::strerror(childErrno);
        else if (childErrno == ENOENT)
            outError = "'" + argv[0] + "' was not found. Install Claude Code and run "
                       "'claude' once in a terminal to sign in, or set its path in UltraClaude.";
        else
            outError = "Could not run '" + argv[0] + "': " + std::strerror(childErrno);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(childMutex_);
        pid_ = pid;
    }
    stdinFd_ = in[1];
    stdoutFd_ = out[0];
    stderrFd_ = err[0];
    SetNonBlocking(stdinFd_);
    SetNonBlocking(stdoutFd_);
    SetNonBlocking(stderrFd_);

    running_ = true;
    reader_ = std::thread(&ClaudeCliProcess::Pump, this, standardInput,
                          std::move(onLine), std::move(onExit));
    return true;
}

void ClaudeCliProcess::Pump(std::string standardInput, LineCallback onLine, ExitCallback onExit) {
    size_t written = 0;
    if (standardInput.empty()) CloseFd(stdinFd_);
    std::string lineBuffer;
    std::string standardError;
    char chunk[16384];

    while (stdoutFd_ >= 0 || stderrFd_ >= 0) {
        pollfd fds[3];
        int count = 0;
        int outIndex = -1, errIndex = -1, inIndex = -1;
        if (stdoutFd_ >= 0) { outIndex = count; fds[count++] = {stdoutFd_, POLLIN, 0}; }
        if (stderrFd_ >= 0) { errIndex = count; fds[count++] = {stderrFd_, POLLIN, 0}; }
        if (stdinFd_ >= 0)  { inIndex = count;  fds[count++] = {stdinFd_, POLLOUT, 0}; }

        if (::poll(fds, static_cast<nfds_t>(count), -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (inIndex >= 0 && fds[inIndex].revents) {
            if (fds[inIndex].revents & (POLLERR | POLLHUP)) {
                CloseFd(stdinFd_);
            } else {
                const ssize_t n = ::write(stdinFd_, standardInput.data() + written,
                                          standardInput.size() - written);
                if (n > 0) written += static_cast<size_t>(n);
                else if (n < 0 && errno != EAGAIN && errno != EINTR) CloseFd(stdinFd_);
                if (written >= standardInput.size()) CloseFd(stdinFd_);
            }
        }
        if (outIndex >= 0 && fds[outIndex].revents) {
            const ssize_t n = ::read(stdoutFd_, chunk, sizeof(chunk));
            if (n > 0) {
                lineBuffer.append(chunk, static_cast<size_t>(n));
                EmitLines(lineBuffer, onLine);
            } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                CloseFd(stdoutFd_);
            }
        }
        if (errIndex >= 0 && fds[errIndex].revents) {
            const ssize_t n = ::read(stderrFd_, chunk, sizeof(chunk));
            if (n > 0) AppendBounded(standardError, chunk, static_cast<size_t>(n));
            else if (n == 0 || (errno != EAGAIN && errno != EINTR)) CloseFd(stderrFd_);
        }
    }
    CloseFd(stdinFd_);
    CloseFd(stdoutFd_);
    CloseFd(stderrFd_);
    if (!lineBuffer.empty() && onLine) onLine(lineBuffer);

    int status = 0;
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(childMutex_);
        pid = pid_;
    }
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    {
        // Cleared under the lock, so Stop() never signals a reused pid.
        std::lock_guard<std::mutex> lock(childMutex_);
        pid_ = -1;
    }
    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    running_ = false;
    if (onExit) onExit(exitCode, standardError);
}

void ClaudeCliProcess::Stop() {
    std::lock_guard<std::mutex> lock(childMutex_);
    if (pid_ > 0) {
        ::kill(-pid_, SIGTERM);
        ::kill(pid_, SIGTERM);
    }
}

#else
// ============================================================== Windows

namespace {
    // Quotes one argument the way CommandLineToArgvW and the C runtime read
    // it back: backslashes are literal except before a quote.
    std::wstring QuoteArgument(const std::wstring& arg) {
        if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
        std::wstring out = L"\"";
        for (size_t i = 0;; ++i) {
            size_t backslashes = 0;
            while (i < arg.size() && arg[i] == L'\\') { ++i; ++backslashes; }
            if (i == arg.size()) { out.append(backslashes * 2, L'\\'); break; }
            if (arg[i] == L'"') { out.append(backslashes * 2 + 1, L'\\'); out += L'"'; }
            else { out.append(backslashes, L'\\'); out += arg[i]; }
        }
        out += L'"';
        return out;
    }

    bool EndsWithInsensitive(const std::wstring& s, const wchar_t* suffix) {
        const size_t n = wcslen(suffix);
        return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
    }

    // Finds a bare program name on PATH the way a console would: claude.exe
    // from the native installer, claude.cmd from npm.
    std::wstring ResolveProgram(const std::wstring& name) {
        if (name.find_first_of(L"\\/") != std::wstring::npos) return name;
        for (const wchar_t* ext : {L".exe", L".cmd", L".bat"}) {
            wchar_t found[MAX_PATH * 2];
            const DWORD n = SearchPathW(nullptr, name.c_str(), ext, MAX_PATH * 2, found, nullptr);
            if (n > 0 && n < MAX_PATH * 2) return std::wstring(found, n);
        }
        return name;
    }

    void CloseHandleSafe(void*& h) {
        if (h) { CloseHandle(static_cast<HANDLE>(h)); h = nullptr; }
    }

    std::string LastErrorText() {
        const DWORD code = GetLastError();
        wchar_t* buffer = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                       reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
        std::string text = buffer ? UltraCanvas::WideToUtf8(buffer) : "error " + std::to_string(code);
        if (buffer) LocalFree(buffer);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        return text;
    }
} // namespace

bool ClaudeCliProcess::Start(const std::vector<std::string>& argv,
                             const std::string& workingDirectory,
                             const std::string& standardInput,
                             LineCallback onLine,
                             ExitCallback onExit,
                             std::string& outError) {
    if (argv.empty()) { outError = "No program to run."; return false; }
    if (running_.load()) { outError = "Claude is still answering."; return false; }
    if (reader_.joinable()) reader_.join();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr,
           errRead = nullptr, errWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0) || !CreatePipe(&outRead, &outWrite, &sa, 0) ||
        !CreatePipe(&errRead, &errWrite, &sa, 0)) {
        outError = "Could not create pipes: " + LastErrorText();
        for (HANDLE h : {inRead, inWrite, outRead, outWrite, errRead, errWrite}) if (h) CloseHandle(h);
        return false;
    }
    // Our ends stay in this process.
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    const std::wstring program = ResolveProgram(UltraCanvas::Utf8ToWide(argv[0]));
    std::wstring commandLine = QuoteArgument(program);
    for (size_t i = 1; i < argv.size(); ++i)
        commandLine += L" " + QuoteArgument(UltraCanvas::Utf8ToWide(argv[i]));
    std::wstring application = program;
    if (EndsWithInsensitive(program, L".cmd") || EndsWithInsensitive(program, L".bat")) {
        // A batch file runs only inside cmd.exe. Every argument here is one
        // UltraClaude chose (flags, a model name, a session id); the user's
        // text goes through standard input and never reaches this line.
        wchar_t comspec[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
        application = (n > 0 && n < MAX_PATH) ? std::wstring(comspec, n) : L"C:\\Windows\\System32\\cmd.exe";
        commandLine = L"cmd.exe /d /s /c \"" + commandLine + L"\"";
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;
    PROCESS_INFORMATION pi{};
    const std::wstring directory = UltraCanvas::Utf8ToWide(workingDirectory);
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    const BOOL started = CreateProcessW(
            application.c_str(), mutableCommandLine.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
            nullptr, directory.empty() ? nullptr : directory.c_str(), &si, &pi);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (!started) {
        const bool notFound = GetLastError() == ERROR_FILE_NOT_FOUND;
        outError = notFound
            ? "'" + argv[0] + "' was not found. Install Claude Code and run 'claude' once "
              "in a terminal to sign in, or set its path in UltraClaude."
            : "Could not run '" + argv[0] + "': " + LastErrorText();
        CloseHandle(inWrite); CloseHandle(outRead); CloseHandle(errRead);
        return false;
    }

    // A job, so Stop() ends the processes the CLI starts as well as the CLI.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    {
        std::lock_guard<std::mutex> lock(childMutex_);
        process_ = pi.hProcess;
        job_ = job;
    }
    stdinWrite_ = inWrite;
    stdoutRead_ = outRead;
    stderrRead_ = errRead;

    running_ = true;
    reader_ = std::thread(&ClaudeCliProcess::Pump, this, standardInput,
                          std::move(onLine), std::move(onExit));
    return true;
}

void ClaudeCliProcess::Pump(std::string standardInput, LineCallback onLine, ExitCallback onExit) {
    // Standard input and standard error on threads of their own, so a child
    // that writes before it has read everything cannot deadlock us.
    std::thread writer([this, input = std::move(standardInput)]() {
        size_t written = 0;
        while (written < input.size()) {
            DWORD n = 0;
            const DWORD want = static_cast<DWORD>(std::min<size_t>(input.size() - written, 65536));
            if (!WriteFile(static_cast<HANDLE>(stdinWrite_), input.data() + written, want, &n, nullptr) || n == 0)
                break;
            written += n;
        }
        CloseHandleSafe(stdinWrite_);
    });
    std::string standardError;
    std::thread errorReader([this, &standardError]() {
        char chunk[4096];
        DWORD n = 0;
        while (ReadFile(static_cast<HANDLE>(stderrRead_), chunk, sizeof(chunk), &n, nullptr) && n > 0)
            AppendBounded(standardError, chunk, n);
    });

    std::string lineBuffer;
    char chunk[16384];
    DWORD n = 0;
    while (ReadFile(static_cast<HANDLE>(stdoutRead_), chunk, sizeof(chunk), &n, nullptr) && n > 0) {
        lineBuffer.append(chunk, n);
        EmitLines(lineBuffer, onLine);
    }
    if (!lineBuffer.empty() && onLine) onLine(lineBuffer);

    errorReader.join();
    writer.join();
    CloseHandleSafe(stdoutRead_);
    CloseHandleSafe(stderrRead_);

    HANDLE process;
    {
        std::lock_guard<std::mutex> lock(childMutex_);
        process = static_cast<HANDLE>(process_);
    }
    WaitForSingleObject(process, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process, &code);
    {
        std::lock_guard<std::mutex> lock(childMutex_);
        CloseHandleSafe(process_);
        CloseHandleSafe(job_);
    }
    running_ = false;
    if (onExit) onExit(static_cast<int>(code), standardError);
}

void ClaudeCliProcess::Stop() {
    std::lock_guard<std::mutex> lock(childMutex_);
    if (job_) TerminateJobObject(static_cast<HANDLE>(job_), 1);
    else if (process_) TerminateProcess(static_cast<HANDLE>(process_), 1);
}

#endif

} // namespace UltraClaude
