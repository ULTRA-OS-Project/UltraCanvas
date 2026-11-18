// Plugins/UltraIM/signal/SignalDaemon.cpp
// signal-cli found, started as the provider's child process and stopped again
// Version: 0.2.0 - the proxy setting as Java options (DaemonProxyFor, ChooseDaemonProxy,
//                  JavaToolOptionsWith, JvmTookOptions); Start sets variables in the child's environment
// Version: 0.1.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "SignalDaemon.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetSocket.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwctype>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// The process environment; macOS' <unistd.h> does not declare it.
extern "C" char** environ;
#endif

namespace UltraIMSignal {

namespace fs = std::filesystem;

namespace {

bool IsProgram(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
#if defined(_WIN32)
    return true;
#else
    return ::access(p.c_str(), X_OK) == 0;
#endif
}

// A host as a program's options can carry it: a name or an address, nothing
// that would end the option or start another.
bool IsHostText(const std::string& host) {
    if (host.empty()) return false;
    for (const unsigned char c : host) {
        if (!std::isalnum(c) && c != '.' && c != '-' && c != '_' && c != ':') return false;
    }
    return true;
}

} // namespace

// ============================================================================
// THE PROXY
// ============================================================================

DaemonProxy DaemonProxyFor(const UltraNetProxyConfig& setting, const UltraNetProxyConfig& resolved) {
    // Java is not to ask the operating system a second time: what the
    // setting came to is what is passed.
    const std::string own = "-Djava.net.useSystemProxies=false";
    DaemonProxy out;
    const bool named = !resolved.host.empty() &&
                       (resolved.type == UltraNetProxyType::Http || resolved.type == UltraNetProxyType::Https ||
                        resolved.type == UltraNetProxyType::Socks4 || resolved.type == UltraNetProxyType::Socks5);
    if (!named) {
        // The operating system's settings naming none: the environment as it
        // is. Anything else is the person saying "directly".
        if (setting.type == UltraNetProxyType::System) return out;
        out.kind = DaemonProxy::Kind::Direct;
        out.jvmOptions = own + " -Dhttps.proxyHost= -Dhttp.proxyHost= -DsocksProxyHost=";
        return out;
    }
    out.text = UltraNet_DescribeProxy(resolved);
    const bool http = resolved.type == UltraNetProxyType::Http;
    const bool socks = resolved.type == UltraNetProxyType::Socks5;
    std::string why;
    if (!http && !socks) why = "it takes an HTTP or a SOCKS 5 proxy";
    else if (!resolved.credentials.username.empty()) why = "it takes no proxy that asks for a user name and password";
    else if (!IsHostText(resolved.host) || resolved.port <= 0 || resolved.port > 65535)
        why = "that is not an address it can be given";
    if (!why.empty()) {
        out.kind = DaemonProxy::Kind::Unusable;
        out.text = "Signal cannot connect through the proxy " + out.text + ": signal-cli is what connects, and " + why +
                   ". Choose another in the proxy settings.";
        return out;
    }
    const std::string port = std::to_string(resolved.port);
    out.kind = DaemonProxy::Kind::Through;
    // The other kind is emptied: Java asks https.proxyHost before
    // socksProxyHost, and the environment's own options may name either.
    if (http) {
        out.jvmOptions = own + " -Dhttps.proxyHost=" + resolved.host + " -Dhttps.proxyPort=" + port +
                         " -Dhttp.proxyHost=" + resolved.host + " -Dhttp.proxyPort=" + port + " -DsocksProxyHost=";
    } else {
        out.jvmOptions = own + " -DsocksProxyHost=" + resolved.host + " -DsocksProxyPort=" + port +
                         " -DsocksProxyVersion=5 -Dhttps.proxyHost= -Dhttp.proxyHost=";
    }
    return out;
}

DaemonProxy ChooseDaemonProxy() {
    // Signal's servers are names under signal.org, on port 443: its chat
    // service stands for them in the exceptions and in a PAC script.
    return DaemonProxyFor(UltraNet_GetGlobalProxy(), UltraNet_ResolveProxy("https://chat.signal.org:443/"));
}

std::string JavaToolOptionsWith(const std::string& options) {
    const std::string inherited = UltraCanvas::GetEnvUtf8("JAVA_TOOL_OPTIONS");
    if (inherited.empty() || options.empty()) return inherited + options;
    // Ours last: of two options for one property Java keeps the later.
    return inherited + " " + options;
}

bool JvmTookOptions(const std::string& logPath, uint64_t from) {
    std::ifstream log(UltraCanvas::PathFromUtf8(logPath), std::ios::binary);
    if (!log) return false;
    log.seekg(static_cast<std::streamoff>(from));
    if (!log) return false;
    // The line is the runtime's first; the options it repeats can be long.
    std::string text(256 * 1024, '\0');
    log.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(std::max<std::streamsize>(0, log.gcount())));
    return text.find("Picked up JAVA_TOOL_OPTIONS") != std::string::npos;
}

std::string FindSignalCli(const std::string& pluginDirectory) {
    const std::string fromEnvironment = UltraCanvas::GetEnvUtf8("ULTRAIM_SIGNAL_CLI");
    if (!fromEnvironment.empty()) {
        return IsProgram(UltraCanvas::PathFromUtf8(fromEnvironment)) ? fromEnvironment : std::string();
    }
#if defined(_WIN32)
    const char* const names[] = {"signal-cli.bat", "signal-cli.cmd", "signal-cli.exe"};
    const char separator = ';';
#else
    const char* const names[] = {"signal-cli"};
    const char separator = ':';
#endif
    if (!pluginDirectory.empty()) {
        const fs::path bin = UltraCanvas::PathFromUtf8(pluginDirectory) / "signal-cli" / "bin";
        for (const char* name : names) {
            if (IsProgram(bin / name)) return UltraCanvas::PathToUtf8(bin / name);
        }
    }
    const std::string path = UltraCanvas::GetEnvUtf8("PATH");
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find(separator, start);
        if (end == std::string::npos) end = path.size();
        const std::string dir = path.substr(start, end - start);
        if (!dir.empty()) {
            for (const char* name : names) {
                const fs::path candidate = UltraCanvas::PathFromUtf8(dir) / name;
                if (IsProgram(candidate)) return UltraCanvas::PathToUtf8(candidate);
            }
        }
        start = end + 1;
    }
    return std::string();
}

int FreeLoopbackPort() {
    UltraNetSocketOptions options;
    options.bindAddress = "127.0.0.1";
    const UltraNetHandle listener = UltraNet_TcpListen(0, options);
    if (listener == UltraNetInvalidHandle) return 0;
    UltraNetEndpoint local;
    const UltraNetResult r = UltraNet_SocketLocalEndpoint(listener, local);
    UltraNet_SocketClose(listener);
    return r.success ? local.port : 0;
}

SignalDaemon::~SignalDaemon() {
    Stop();
}

#if defined(_WIN32)

namespace {

std::wstring Wide(const std::string& utf8) {
    return UltraCanvas::PathFromUtf8(utf8).wstring();   // path-string-ok: a wide string, UTF-16
}

// Standard argv quoting for CreateProcessW's command line.
void AppendQuoted(std::wstring& line, const std::wstring& arg) {
    if (!line.empty()) line += L' ';
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
        line += arg;
        return;
    }
    line += L'"';
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') line.append(backslashes * 2 + 1, L'\\');
        else line.append(backslashes, L'\\');
        backslashes = 0;
        line += c;
    }
    line.append(backslashes * 2, L'\\');
    line += L'"';
}

// UTF-8 as UTF-16, exactly (Wide goes by way of a path).
std::wstring WideText(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), n);
    return wide;
}

// The name of an environment entry ("NAME=value"), upper case as Windows
// compares names; "=C:=C:\\" entries have theirs after the first character.
std::wstring EntryName(const std::wstring& entry) {
    const size_t eq = entry.find(L'=', !entry.empty() && entry[0] == L'=' ? 1 : 0);
    std::wstring name = entry.substr(0, eq);
    for (wchar_t& c : name) c = static_cast<wchar_t>(towupper(c));
    return name;
}

// This process's environment block with `environment` set in it, sorted by
// name as Windows keeps it. Empty when there is nothing to set: the child
// then inherits ours.
std::wstring EnvironmentBlockWith(const std::vector<std::pair<std::string, std::string>>& environment) {
    std::wstring block;
    if (environment.empty()) return block;
    std::vector<std::wstring> entries;
    for (const auto& variable : environment) entries.push_back(WideText(variable.first) + L"=" + WideText(variable.second));
    const size_t ours = entries.size();
    if (wchar_t* current = GetEnvironmentStringsW()) {
        for (const wchar_t* e = current; *e; e += wcslen(e) + 1) {
            const std::wstring entry(e);
            const std::wstring name = EntryName(entry);
            bool replaced = false;
            for (size_t i = 0; i < ours; ++i) replaced = replaced || EntryName(entries[i]) == name;
            if (!replaced) entries.push_back(entry);
        }
        FreeEnvironmentStringsW(current);
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const std::wstring& a, const std::wstring& b) { return EntryName(a) < EntryName(b); });
    for (const std::wstring& entry : entries) {
        block += entry;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

} // namespace

bool SignalDaemon::Start(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
                         std::string& error, const std::vector<std::pair<std::string, std::string>>& environment) {
    Stop();
    std::lock_guard<std::mutex> lock(mutex_);
    std::wstring environmentBlock = EnvironmentBlockWith(environment);
    std::wstring line;
    std::wstring ext = UltraCanvas::PathFromUtf8(program).extension().wstring();   // path-string-ok: wide
    for (wchar_t& c : ext) c = static_cast<wchar_t>(towlower(c));
    if (ext == L".bat" || ext == L".cmd") {
        // A batch file runs in cmd.exe.
        AppendQuoted(line, L"cmd.exe");
        line += L" /d /s /c \"";
        std::wstring inner;
        AppendQuoted(inner, Wide(program));
        for (const std::string& a : args) AppendQuoted(inner, Wide(a));
        line += inner + L"\"";
    } else {
        AppendQuoted(line, Wide(program));
        for (const std::string& a : args) AppendQuoted(line, Wide(a));
    }
    SECURITY_ATTRIBUTES inherit = {};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(Wide(logPath).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        // Closing the job (the provider gone, even by a crash) ends signal-cli too.
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    if (log != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nullptr;
        si.hStdOutput = log;
        si.hStdError = log;
    }
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');
    const BOOL started = CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, log != INVALID_HANDLE_VALUE,
                                        CREATE_NO_WINDOW | CREATE_SUSPENDED |
                                            (environmentBlock.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT),
                                        environmentBlock.empty() ? nullptr : static_cast<LPVOID>(environmentBlock.data()),
                                        nullptr, &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!started) {
        if (job) CloseHandle(job);
        error = "Could not start " + program;
        return false;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    job_ = job;
    pid_ = pi.dwProcessId;
    return true;
}

void SignalDaemon::Stop(int graceMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!process_) return;
    // signal-cli has no console to be asked on: the job ends the tree.
    (void)graceMs;
    if (job_) TerminateJobObject(static_cast<HANDLE>(job_), 0);
    else TerminateProcess(static_cast<HANDLE>(process_), 0);
    WaitForSingleObject(static_cast<HANDLE>(process_), 10000);
    CloseHandle(static_cast<HANDLE>(process_));
    if (job_) CloseHandle(static_cast<HANDLE>(job_));
    process_ = nullptr;
    job_ = nullptr;
    pid_ = 0;
}

bool SignalDaemon::IsRunning() {
    std::lock_guard<std::mutex> lock(mutex_);
    return process_ && WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_TIMEOUT;
}

int64_t SignalDaemon::ProcessId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pid_;
}

#else

bool SignalDaemon::Start(const std::string& program, const std::vector<std::string>& args, const std::string& logPath,
                         std::string& error, const std::vector<std::pair<std::string, std::string>>& environment) {
    Stop();
    std::lock_guard<std::mutex> lock(mutex_);
    // Everything the child needs is made before the fork: after it, only
    // async-signal-safe calls.
    std::vector<std::string> argv;
    argv.push_back(program);
    argv.insert(argv.end(), args.begin(), args.end());
    std::vector<char*> raw;
    for (std::string& a : argv) raw.push_back(a.data());
    raw.push_back(nullptr);
    // Its environment: ours, with `environment` set in it. Left empty when
    // there is nothing to set - the child then keeps ours as it is.
    std::vector<std::string> entries;
    std::vector<char*> envp;
    if (!environment.empty()) {
        for (char** e = environ; e && *e; ++e) {
            const std::string entry(*e);
            const std::string name = entry.substr(0, entry.find('='));
            bool replaced = false;
            for (const auto& variable : environment) replaced = replaced || variable.first == name;
            if (!replaced) entries.push_back(entry);
        }
        for (const auto& variable : environment) entries.push_back(variable.first + "=" + variable.second);
        for (std::string& entry : entries) envp.push_back(entry.data());
        envp.push_back(nullptr);
    }
    const int log = ::open(logPath.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    const int devNull = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    const pid_t pid = ::fork();
    if (pid < 0) {
        error = std::string("Could not start ") + program + ": " + std::strerror(errno);
        if (log >= 0) ::close(log);
        if (devNull >= 0) ::close(devNull);
        return false;
    }
    if (pid == 0) {
        // A process group of its own: signal-cli's script may start java
        // as a child of its shell, and Stop ends them together.
        ::setpgid(0, 0);
        if (devNull >= 0) ::dup2(devNull, 0);
        if (log >= 0) {
            ::dup2(log, 1);
            ::dup2(log, 2);
        }
        // The provider's handlers are not the daemon's.
        ::signal(SIGPIPE, SIG_DFL);
        ::signal(SIGTERM, SIG_DFL);
        ::signal(SIGINT, SIG_DFL);
        if (envp.empty()) ::execv(raw[0], raw.data());
        else ::execve(raw[0], raw.data(), envp.data());
        ::_exit(127);
    }
    ::setpgid(pid, pid);    // (also here, so Stop can never race the child's own call)
    if (log >= 0) ::close(log);
    if (devNull >= 0) ::close(devNull);
    pid_ = pid;
    return true;
}

void SignalDaemon::Stop(int graceMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pid_ <= 0) return;
    const pid_t pid = pid_;
    pid_ = -1;
    int status = 0;
    if (::waitpid(pid, &status, WNOHANG) == pid) return;    // already gone
    ::kill(-pid, SIGTERM);
    ::kill(pid, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceMs);
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid || (done < 0 && errno == ECHILD)) {
            ::kill(-pid, SIGKILL);  // whatever of the group is left (java under a shell)
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ::kill(-pid, SIGKILL);
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &status, 0);
}

bool SignalDaemon::IsRunning() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pid_ <= 0) return false;
    int status = 0;
    const pid_t done = ::waitpid(pid_, &status, WNOHANG);
    if (done == pid_ || (done < 0 && errno == ECHILD)) {
        pid_ = -1;
        return false;
    }
    return true;
}

int64_t SignalDaemon::ProcessId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pid_ > 0 ? pid_ : 0;
}

#endif

} // namespace UltraIMSignal
