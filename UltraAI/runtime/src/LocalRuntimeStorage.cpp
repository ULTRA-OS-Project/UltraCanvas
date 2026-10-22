// UltraAI/runtime/src/LocalRuntimeStorage.cpp
// The runtime's folders and files, hashing (UltraCrypt), unpacking
// (VirtualFS), and the processes it starts and stops.
// Version: 0.1.0
// Last Modified: 2026-10-20
// Author: UltraAI Module

#include "LocalRuntimeInternal.h"

#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCrypt/UltraCryptCore.h"
#include "VirtualFS/VirtualFS.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>     // before windows.h, which would otherwise pull in winsock.h
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace UltraAI {
namespace local_detail {

namespace fs = std::filesystem;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

// ===== Storage =====

Paths Paths::For(const fs::path& root) {
    Paths p;
    p.root           = root;
    p.engines        = root / "engines";
    p.models         = root / "models";
    p.downloads      = root / "downloads";
    p.logs           = root / "logs";
    p.runtimeJson    = root / "runtime.json";
    p.runtimeLock    = root / "runtime.lock";
    p.runtimeKey     = root / "runtime.key";
    p.settingsJson   = root / "runtime-settings.json";
    p.galleryJson    = root / "gallery" / "index.json";
    p.catalogueJson  = root / "gallery" / "catalogue.json";
    p.userModelsJson = root / "user-models.json";
    return p;
}

fs::path Paths::DefaultRoot() {
    return UltraCanvas::UltraCanvasLocalDataFolderRoot() / "UltraAI";
}

bool Paths::EnsureFolders(std::string& outError) const {
    std::error_code ec;
    for (const fs::path* folder : {&root, &engines, &models, &downloads, &logs}) {
        fs::create_directories(*folder, ec);
        if (ec) {
            outError = "cannot create " + PathToUtf8(*folder) + ": " + ec.message();
            return false;
        }
    }
    fs::create_directories(galleryJson.parent_path(), ec);
    return true;
}

std::string ModelFolderName(const std::string& modelId) {
    std::string out;
    for (unsigned char c : modelId) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            out += static_cast<char>(c);
        } else {
            out += '_';
        }
    }
    if (out.empty() || out == "." || out == "..") out = "model";
    if (out.size() > 120) out.resize(120);
    return out;
}

bool ReadWholeFile(const fs::path& path, std::string& out) {
    std::FILE* f = UltraCanvas::OpenFileUtf8(PathToUtf8(path), "rb");
    if (!f) return false;
    out.clear();
    char buffer[65536];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, n);
    std::fclose(f);
    return true;
}

bool WriteFileAtomic(const fs::path& path, const std::string& content, std::string& outError) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.parent_path() /
        (PathToUtf8(path.filename()) + ".tmp-" + std::to_string(ThisProcessId()));
    std::FILE* f = UltraCanvas::OpenFileUtf8(PathToUtf8(tmp), "wb");
    if (!f) {
        outError = "cannot write " + PathToUtf8(tmp);
        return false;
    }
    const bool ok = std::fwrite(content.data(), 1, content.size(), f) == content.size();
    std::fclose(f);
    if (!ok) {
        fs::remove(tmp, ec);
        outError = "cannot write " + PathToUtf8(tmp);
        return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        // Windows refuses to rename over an open file; replace in two steps.
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) {
            fs::remove(tmp, ec);
            outError = "cannot replace " + PathToUtf8(path) + ": " + ec.message();
            return false;
        }
    }
    return true;
}

uint64_t FolderSizeBytes(const fs::path& folder) {
    uint64_t total = 0;
    std::error_code ec;
    if (!fs::exists(folder, ec)) return 0;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (it->is_regular_file(ec)) total += it->file_size(ec);
    }
    return total;
}

int64_t NowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// ===== Hashing and archives =====

bool Sha256File(const fs::path& path, std::string& outHex, std::string& outError) {
    std::vector<uint8_t> digest;
    const UltraCryptResult r = UltraCrypt_HashFile(UltraCryptHashAlgorithm::SHA256,
                                                   PathToUtf8(path), digest);
    if (!r.success) {
        outError = r.message.empty() ? std::string("SHA-256 is not available in this build")
                                     : r.message;
        return false;
    }
    outHex = UltraCrypt_ToHex(digest);
    std::transform(outHex.begin(), outHex.end(), outHex.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return true;
}

Error UnpackArchive(const fs::path& archive, const fs::path& dest) {
    Error error;
    std::error_code ec;
    fs::create_directories(dest, ec);
    // The manager registers its providers (libarchive) on Initialize, which
    // an application that never opened an archive has not called.
    VirtualFS::VirtualFSManager& vfs = VirtualFS::VirtualFSManager::Instance();
    if (!vfs.IsInitialized()) vfs.Initialize();
    VirtualFS::VirtualFSExtractOptions options = VirtualFS::VirtualFSExtractOptions::Overwrite();
    options.preservePermissions = true;
    std::string detail;
    const VirtualFS::VirtualFSResult r = VirtualFS::VirtualFS_ExtractAll(PathToUtf8(archive), PathToUtf8(dest),
                                                   options, nullptr, &detail);
    if (r != VirtualFS::VirtualFSResult::Ok) {
        error.code    = ErrorCode::UnsupportedFormat;
        error.message = "cannot unpack " + PathToUtf8(archive.filename()) + ": " +
                        (detail.empty() ? VirtualFS::VirtualFSResultToString(r) : detail);
    }
    return error;
}

std::string FindExecutableIn(const fs::path& folder, const std::string& executable) {
    std::error_code ec;
#if defined(_WIN32)
    const std::string wanted = executable + ".exe";
#else
    const std::string wanted = executable;
#endif
    if (fs::is_regular_file(folder, ec)) {
        const std::string name = PathToUtf8(folder.filename());
        return (name == wanted || name == executable) ? PathToUtf8(folder) : std::string();
    }
    if (!fs::is_directory(folder, ec)) return {};
    // Breadth first, so a server at the top wins over one in a deeper
    // examples folder.
    std::deque<fs::path> queue{folder};
    while (!queue.empty()) {
        const fs::path dir = queue.front();
        queue.pop_front();
        std::vector<fs::path> subdirs;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (it->is_directory(ec)) {
                subdirs.push_back(it->path());
            } else if (it->is_regular_file(ec) && PathToUtf8(it->path().filename()) == wanted) {
                return PathToUtf8(it->path());
            }
        }
        std::sort(subdirs.begin(), subdirs.end());
        for (const fs::path& s : subdirs) queue.push_back(s);
    }
    return {};
}

void MarkExecutables(const fs::path& folder) {
#if !defined(_WIN32)
    std::error_code ec;
    for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = PathToUtf8(it->path().filename());
        const bool library = name.find(".so") != std::string::npos ||
                             name.find(".dylib") != std::string::npos;
        const bool program = name.find('.') == std::string::npos;
        if (!library && !program) continue;
        fs::permissions(it->path(),
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
    }
#else
    (void)folder;
#endif
}

// ===== Processes =====

uint64_t ThisProcessId() {
#if defined(_WIN32)
    return GetCurrentProcessId();
#else
    return static_cast<uint64_t>(getpid());
#endif
}

#if defined(_WIN32)

namespace {

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

// One argument as CreateProcessW's command line wants it: quoted, with
// backslashes before quotes doubled (the C runtime's rule).
std::wstring QuoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += c;
        } else {
            out.append(backslashes, L'\\');
            out += c;
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out += L'"';
    return out;
}

} // namespace

bool StartEngineProcess(const ProcessStart& start, uint64_t& outPid, std::string& outError) {
    if (start.argv.empty()) { outError = "nothing to start"; return false; }
    std::wstring commandLine;
    for (size_t i = 0; i < start.argv.size(); ++i) {
        if (i) commandLine += L' ';
        commandLine += QuoteArg(Widen(start.argv[i]));
    }
    SECURITY_ATTRIBUTES sa {};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE log = INVALID_HANDLE_VALUE;
    if (!start.logPathUtf8.empty()) {
        log = CreateFileW(Widen(start.logPathUtf8).c_str(), FILE_APPEND_DATA,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    STARTUPINFOW si {};
    si.cb = sizeof(si);
    if (log != INVALID_HANDLE_VALUE) {
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdOutput = log;
        si.hStdError  = log;
        si.hStdInput  = INVALID_HANDLE_VALUE;
    }
    PROCESS_INFORMATION pi {};
    std::vector<wchar_t> buffer(commandLine.begin(), commandLine.end());
    buffer.push_back(L'\0');
    const std::wstring workingDir = Widen(start.workingDirUtf8);
    const BOOL ok = CreateProcessW(Widen(start.argv[0]).c_str(), buffer.data(), nullptr, nullptr,
                                   log != INVALID_HANDLE_VALUE,
                                   CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                                   nullptr, workingDir.empty() ? nullptr : workingDir.c_str(),
                                   &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) {
        outError = "CreateProcess failed with error " + std::to_string(GetLastError());
        return false;
    }
    outPid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool IsEngineProcessAlive(uint64_t pid) {
    if (!pid) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

void StopEngineProcess(uint64_t pid, int graceMs) {
    if (!pid) return;
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                           FALSE, static_cast<DWORD>(pid));
    if (!h) return;
    // A console-less server has no window to close; a control event only
    // reaches our own console group. Terminate after the grace period in
    // which a cooperative server might have gone on its own.
    if (WaitForSingleObject(h, static_cast<DWORD>(graceMs > 0 ? graceMs : 0)) != WAIT_OBJECT_0) {
        TerminateProcess(h, 1);   // win32-name-ok: the Win32 call itself
        WaitForSingleObject(h, 5000);
    }
    CloseHandle(h);
}

int FindFreeLoopbackPort() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { WSACleanup(); return 0; }
    sockaddr_in addr {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    int port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        int len = sizeof(addr);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == 0) port = ntohs(addr.sin_port);
    }
    closesocket(s);
    WSACleanup();
    return port;
}

#else

bool StartEngineProcess(const ProcessStart& start, uint64_t& outPid, std::string& outError) {
    if (start.argv.empty()) { outError = "nothing to start"; return false; }
    std::vector<std::string> argvStorage = start.argv;
    std::vector<char*> argv;
    for (std::string& a : argvStorage) argv.push_back(a.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (!start.logPathUtf8.empty()) {
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, start.logPathUtf8.c_str(),
                                         O_WRONLY | O_CREAT | O_APPEND, 0600);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    }
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (!start.workingDirUtf8.empty()) {
        // glibc 2.29 and macOS 10.15 have the _np form; the suite's
        // deployment targets are past both. Elsewhere (bionic, musl) the
        // engine runs from our working directory: its libraries are found
        // through its own folder ($ORIGIN), not the current one.
#if defined(__GLIBC__) || defined(__APPLE__)
        posix_spawn_file_actions_addchdir_np(&actions, start.workingDirUtf8.c_str());
#endif
    }
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    short flags = 0;
#ifdef POSIX_SPAWN_SETSID
    flags |= POSIX_SPAWN_SETSID;      // its own session: our end does not end it
#endif
    posix_spawnattr_setflags(&attr, flags);

    pid_t pid = 0;
    const int rc = posix_spawn(&pid, start.argv[0].c_str(), &actions, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        outError = std::string("cannot start ") + start.argv[0] + ": " + std::strerror(rc);
        return false;
    }
    outPid = static_cast<uint64_t>(pid);
    return true;
}

bool IsEngineProcessAlive(uint64_t pid) {
    if (!pid) return false;
    // A child of ours that has exited is reaped here; one started by
    // another process is asked with a null signal.
    int status = 0;
    const pid_t r = waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
    if (r == static_cast<pid_t>(pid)) return false;   // exited, now reaped
    if (r == 0) return true;                           // our child, running
    return kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

void StopEngineProcess(uint64_t pid, int graceMs) {
    if (!pid) return;
    kill(static_cast<pid_t>(pid), SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(graceMs);
    while (IsEngineProcessAlive(pid) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (IsEngineProcessAlive(pid)) {
        kill(static_cast<pid_t>(pid), SIGKILL);
        const auto hardDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (IsEngineProcessAlive(pid) && std::chrono::steady_clock::now() < hardDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

int FindFreeLoopbackPort() {
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 0;
    sockaddr_in addr {};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = 0;
    int port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        socklen_t len = sizeof(addr);
        if (getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == 0) port = ntohs(addr.sin_port);
    }
    close(s);
    return port;
}

#endif

std::string TailOfFile(const fs::path& path, int maxLines) {
    std::string content;
    if (!ReadWholeFile(path, content)) return {};
    std::deque<std::string> lines;
    std::stringstream in(content);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        lines.push_back(line);
        if (static_cast<int>(lines.size()) > maxLines) lines.pop_front();
    }
    std::string out;
    for (const std::string& l : lines) {
        if (!out.empty()) out += '\n';
        out += l;
    }
    return out;
}

// ===== The lock file =====

ScopedFileLock::ScopedFileLock(const fs::path& path, int timeoutMs) : path_(path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    const std::string mine = std::to_string(ThisProcessId());
    std::error_code ec;
    while (true) {
#if defined(_WIN32)
        const std::wstring wide = path.wstring();   // path-string-ok: a wide string for the W API
        HANDLE h = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, mine.data(), static_cast<DWORD>(mine.size()), &written, nullptr);
            CloseHandle(h);
            held_ = true;
            return;
        }
#else
        const int fd = open(PathToUtf8(path).c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            const ssize_t n = write(fd, mine.data(), mine.size());
            (void)n;
            close(fd);
            held_ = true;
            return;
        }
#endif
        // Someone holds it. A holder that is gone leaves a stale lock.
        std::string holder;
        if (ReadWholeFile(path, holder)) {
            const uint64_t pid = static_cast<uint64_t>(std::strtoull(holder.c_str(), nullptr, 10));
            if (!pid || !IsEngineProcessAlive(pid)) {
                fs::remove(path, ec);
                continue;
            }
        } else if (!fs::exists(path, ec)) {
            continue;
        }
        if (std::chrono::steady_clock::now() >= deadline) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

ScopedFileLock::~ScopedFileLock() {
    if (!held_) return;
    std::error_code ec;
    fs::remove(path_, ec);
}

std::string RandomKeyHex() {
    std::vector<uint8_t> bytes;
    if (!UltraCrypt_RandomBytes(bytes, 32).success || bytes.size() != 32) {
        std::random_device rd;
        bytes.resize(32);
        for (uint8_t& b : bytes) b = static_cast<uint8_t>(rd());
    }
    return UltraCrypt_ToHex(bytes);
}

} // namespace local_detail
} // namespace UltraAI
