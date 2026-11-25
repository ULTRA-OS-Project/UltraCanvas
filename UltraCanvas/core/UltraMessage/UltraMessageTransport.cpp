// UltraCanvas/core/UltraMessage/UltraMessageTransport.cpp
// Local stream transport: Unix domain sockets (Linux, macOS, BSD) and Windows
// named pipes behind one Connection / Listener pair. Every blocking wait also
// watches a private wake-up object so Close() from another thread returns it.
// Version: 0.1.4 - the bus is hosted and joined only in a folder that is this user's alone
//                  (BusFolderIsPrivate): in /tmp, without XDG_RUNTIME_DIR, another account
//                  could make the folder first and have its broker met
// Version: 0.1.3 - a bus path longer than a Unix socket's address holds is reached through
//                  its folder on Linux (UltraCanvasUnixSocketAddress.h): a long
//                  XDG_RUNTIME_DIR left every application without a bus
// Version: 0.1.2 - Windows listener: a cancelled ConnectNamedPipe is waited for
//                  before its stack OVERLAPPED goes out of scope
// Version: 0.1.1 - ProcessIsTerminating
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraMessageTransport.h"
#ifndef _WIN32
#  include "UltraCanvasUnixSocketAddress.h"
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <poll.h>
#  include <sys/file.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/un.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <sys/ucred.h>
#  endif
#endif

namespace UltraMessage {
namespace Internal {

namespace {
constexpr size_t kReadChunk = 64 * 1024;
}

// ===========================================================================
// POSIX: Unix domain sockets
// ===========================================================================
#ifndef _WIN32

namespace {

std::string Errno(const char* what) {
    return std::string(what) + ": " + std::strerror(errno);
}

bool MakeWakePipe(int& readEnd, int& writeEnd) {
    int fds[2];
    if (::pipe(fds) != 0) return false;
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[0], F_SETFL, O_NONBLOCK);
    ::fcntl(fds[1], F_SETFL, O_NONBLOCK);
    readEnd = fds[0];
    writeEnd = fds[1];
    return true;
}

void SignalWake(int writeEnd) {
    if (writeEnd < 0) return;
    const char byte = 1;
    ssize_t rc;
    do { rc = ::write(writeEnd, &byte, 1); } while (rc < 0 && errno == EINTR);
}

void CloseFd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

// Waits for `fd` to become readable or the wake pipe to fire. Returns 1 for
// readable, 0 for woken (closing), -1 on error.
int WaitReadable(int fd, int wakeFd, int timeoutMs) {
    pollfd fds[2];
    fds[0].fd = fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = wakeFd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;
    for (;;) {
        const int rc = ::poll(fds, 2, timeoutMs);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (rc == 0) return 0;
        if (fds[1].revents) return 0;
        if (fds[0].revents) return 1;
    }
}

// The address of the bus socket. A socket's address holds a path of 107
// bytes on Linux (103 on macOS and the BSDs), and the bus lies under the
// user's runtime folder, which can be longer than that - a session with a
// runtime folder of its own, a sandbox, a test's private one. No application
// then reached the bus: a second start of a single-instance application
// could not ask the running copy forward, and said "already running"
// instead. UnixSocketAddress reaches such a socket through its folder on
// Linux; the bus stays where its path says.
bool ResolveBusAddress(const std::string& busPath, UltraCanvas::UnixSocketAddress& address, std::string& error,
                       bool* folderMissing = nullptr) {
    if (address.Resolve(busPath, error, folderMissing)) return true;
    if (error.rfind("path too long", 0) == 0) error = "bus " + error;
    return false;
}

// The bus is one user's: whoever holds its folder decides which broker an
// application meets there, and so reads what it posts and answers what it
// asks. The socket is owner-only, but that is no help when the folder is
// not: without XDG_RUNTIME_DIR the bus lies in /tmp/ultramessage-<uid>, a
// name anyone can work out, in a folder everyone may write to. Another
// account that made that folder first - or the one inside it - had its own
// broker met by this user's applications, or took the folder away under
// them. So before the bus is hosted or joined, its folder must be a real
// folder (no link) of this user's that nobody else can write to, and the
// folder above it this user's or the system's - a folder everyone may write
// to counts as the system's only with the sticky bit, where an entry is
// its owner's to rename. Such a folder cannot be swapped between this check
// and the bind() or connect() after it.
//
// A bus folder not there yet is judged by the folder above it alone: the
// broker checks again once it has made it, and a client finds no socket.
// Root is let through: it
// joins a user's bus on purpose (a tool started with sudo), and no check
// here protects root from the user it serves.
//
// `modeToo` off leaves out who may write to the bus folder itself: the broker
// asks that way before it makes the folder owner-only, so that a folder of
// this user's with a looser mode is tightened rather than refused - and a
// link or another user's folder is refused before anything in it is touched.
bool BusFolderIsPrivate(const std::string& busPath, std::string& error, bool modeToo = true) {
    const uid_t me = ::geteuid();
    if (me == 0) return true;
    const std::string folder = ParentDirectory(busPath);
    if (folder.empty()) return true;
    const std::string refused = "the bus folder " + folder + " is not this user's alone (";
    struct stat own{};
    if (::lstat(folder.c_str(), &own) == 0) {
        if (!S_ISDIR(own.st_mode)) {
            error = refused + "it is not a folder of its own, but a link or a file)";
            return false;
        }
        if (own.st_uid != me) {
            error = refused + "it belongs to another user)";
            return false;
        }
        if (modeToo && (own.st_mode & (S_IWGRP | S_IWOTH))) {
            error = refused + "others may write to it)";
            return false;
        }
    }
    // The folder above, also while the bus folder is still to be made: the
    // broker does not make it inside a folder that fails here.
    const std::string above = ParentDirectory(folder);
    struct stat up{};
    if (above.empty() || above == folder || ::lstat(above.c_str(), &up) != 0) return true;
    if (up.st_uid != me && up.st_uid != 0) {
        error = refused + "the folder above it, " + above + ", belongs to another user)";
        return false;
    }
    if (S_ISDIR(up.st_mode) && (up.st_mode & S_IWOTH) && !(up.st_mode & S_ISVTX)) {
        error = refused + "everyone may write to the folder above it, " + above + ")";
        return false;
    }
    return true;
}

} // namespace

Connection::~Connection() {
    Close();
    CloseFd(fd_);
    CloseFd(wakeRead_);
    CloseFd(wakeWrite_);
}

void Connection::Close() {
    bool expected = false;
    if (!closed_.compare_exchange_strong(expected, true)) return;
    if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR);
    SignalWake(wakeWrite_);
    // The reader thread owns the descriptor until it has left ReceiveFrame;
    // the fd itself is closed in the destructor, which runs after the last
    // shared_ptr (the reader's included) is gone.
}

void Connection::QueryPeer() {
    peerPid_ = 0;
#if defined(__linux__)
    ucred credentials{};
    socklen_t length = sizeof(credentials);
    if (::getsockopt(fd_, SOL_SOCKET, SO_PEERCRED, &credentials, &length) == 0)
        peerPid_ = static_cast<int>(credentials.pid);
#elif defined(__APPLE__)
    pid_t pid = 0;
    socklen_t length = sizeof(pid);
    if (::getsockopt(fd_, SOL_LOCAL, LOCAL_PEERPID, &pid, &length) == 0)
        peerPid_ = static_cast<int>(pid);
#endif
}

bool Connection::WriteAll(const char* data, size_t length, std::string& error) {
    size_t sent = 0;
    while (sent < length) {
        if (closed_.load()) {
            error = "connection closed";
            return false;
        }
#ifdef MSG_NOSIGNAL
        const ssize_t rc = ::send(fd_, data + sent, length - sent, MSG_NOSIGNAL);
#else
        const ssize_t rc = ::send(fd_, data + sent, length - sent, 0);
#endif
        if (rc < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd pfd{fd_, POLLOUT, 0};
                ::poll(&pfd, 1, 1000);
                continue;
            }
            error = Errno("send");
            return false;
        }
        sent += static_cast<size_t>(rc);
    }
    return true;
}

bool Connection::ReadSome(uint8_t* buffer, size_t capacity, size_t& received, std::string& error) {
    received = 0;
    for (;;) {
        if (closed_.load()) {
            error = "connection closed";
            return false;
        }
        const int ready = WaitReadable(fd_, wakeRead_, -1);
        if (ready <= 0) {
            error = ready < 0 ? Errno("poll") : "connection closed";
            return false;
        }
        const ssize_t rc = ::recv(fd_, buffer, capacity, 0);
        if (rc < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            error = Errno("recv");
            return false;
        }
        if (rc == 0) {
            error = "peer closed the connection";
            return false;
        }
        received = static_cast<size_t>(rc);
        return true;
    }
}

Listener::~Listener() {
    Close();
    CloseFd(fd_);
    CloseFd(wakeRead_);
    CloseFd(wakeWrite_);
    if (lockFd_ >= 0) {
        ::flock(lockFd_, LOCK_UN);
        ::close(lockFd_);
        lockFd_ = -1;
    }
    if (!path_.empty()) ::unlink(path_.c_str());
}

void Listener::Close() {
    bool expected = false;
    if (!closed_.compare_exchange_strong(expected, true)) return;
    SignalWake(wakeWrite_);
}

std::unique_ptr<Listener> Listener::Create(const std::string& busPath, std::string& error,
                                           bool& alreadyInUse) {
    alreadyInUse = false;
    // Twice: a folder already there is looked at before its mode is set, and
    // the one made or tightened here before the lock file is written into it.
    if (!BusFolderIsPrivate(busPath, error, /*modeToo=*/false)) return nullptr;
    if (!EnsureParentDirectory(busPath)) {
        error = "cannot create the bus directory for " + busPath;
        return nullptr;
    }
    if (!BusFolderIsPrivate(busPath, error)) return nullptr;
    std::unique_ptr<Listener> listener(new Listener());
    listener->path_ = busPath;

    // The lock file decides who may take the socket: only its holder may
    // unlink a stale socket and bind a fresh one, so two processes electing
    // at once never both win.
    const std::string lockPath = busPath + ".lock";
    listener->lockFd_ = ::open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (listener->lockFd_ < 0) {
        error = Errno("open lock file");
        return nullptr;
    }
    if (::flock(listener->lockFd_, LOCK_EX | LOCK_NB) != 0) {
        alreadyInUse = true;
        error = "another broker holds the bus lock";
        ::close(listener->lockFd_);
        listener->lockFd_ = -1;
        listener->path_.clear();
        return nullptr;
    }

    UltraCanvas::UnixSocketAddress address;
    if (!ResolveBusAddress(busPath, address, error)) {
        listener->path_.clear();
        return nullptr;
    }
    // We hold the lock, so whatever socket file exists is stale.
    ::unlink(busPath.c_str());

    listener->fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener->fd_ < 0) {
        error = Errno("socket");
        listener->path_.clear();
        return nullptr;
    }
    ::fcntl(listener->fd_, F_SETFD, FD_CLOEXEC);
    const mode_t previous = ::umask(0077);
    const int bound = ::bind(listener->fd_, address.Get(), address.Size());
    ::umask(previous);
    if (bound != 0) {
        error = Errno("bind");
        listener->path_.clear();
        return nullptr;
    }
    ::chmod(busPath.c_str(), 0600);
    if (::listen(listener->fd_, 64) != 0) {
        error = Errno("listen");
        return nullptr;
    }
    if (!MakeWakePipe(listener->wakeRead_, listener->wakeWrite_)) {
        error = Errno("pipe");
        return nullptr;
    }
    return listener;
}

ConnectionPtr Listener::Accept() {
    for (;;) {
        if (closed_.load()) return nullptr;
        const int ready = WaitReadable(fd_, wakeRead_, -1);
        if (ready <= 0) return nullptr;
        const int client = ::accept(fd_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED)
                continue;
            return nullptr;
        }
        ::fcntl(client, F_SETFD, FD_CLOEXEC);
        ConnectionPtr connection(new Connection());
        connection->fd_ = client;
        if (!MakeWakePipe(connection->wakeRead_, connection->wakeWrite_)) {
            // `connection` owns the descriptor from the assignment above, so
            // ~Connection closes it. Closing it here as well would shut down
            // and close a number a concurrent thread may already have
            // reopened.
            return nullptr;
        }
        connection->QueryPeer();
        return connection;
    }
}

ConnectionPtr ConnectToBus(const std::string& busPath, int timeoutMs, std::string& error) {
    if (!BusFolderIsPrivate(busPath, error)) return nullptr;
    UltraCanvas::UnixSocketAddress address;
    bool folderMissing = false;
    if (!ResolveBusAddress(busPath, address, error, &folderMissing)) {
        // As ENOENT from connect() below: nothing has made the bus yet.
        if (folderMissing) error = "no broker listens on " + busPath;
        return nullptr;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            error = Errno("socket");
            return nullptr;
        }
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        int rc;
        do { rc = ::connect(fd, address.Get(), address.Size()); }
        while (rc != 0 && errno == EINTR);
        if (rc == 0) {
            ConnectionPtr connection(new Connection());
            connection->fd_ = fd;
            if (!MakeWakePipe(connection->wakeRead_, connection->wakeWrite_)) {
                error = Errno("pipe");
                return nullptr;
            }
            connection->QueryPeer();
            return connection;
        }
        const int err = errno;
        ::close(fd);
        if (err == ENOENT || err == ECONNREFUSED) {
            error = "no broker listens on " + busPath;
            return nullptr;
        }
        if (err == EAGAIN && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        error = Errno("connect");
        return nullptr;
    }
}

// ===========================================================================
// Windows: named pipes
// ===========================================================================
#else

namespace {

std::string LastError(const char* what) {
    const DWORD code = ::GetLastError();
    return std::string(what) + ": error " + std::to_string(code);
}

DWORD kPipeBuffer = 64 * 1024;

// Completes an overlapped operation or aborts it when `stopEvent` fires.
bool CompleteOverlapped(HANDLE pipe, OVERLAPPED& overlapped, HANDLE stopEvent,
                        DWORD& transferred, std::string& error) {
    HANDLE handles[2] = {overlapped.hEvent, stopEvent};
    const DWORD waited = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    if (waited == WAIT_OBJECT_0 + 1) {
        ::CancelIoEx(pipe, &overlapped);
        ::GetOverlappedResult(pipe, &overlapped, &transferred, TRUE);
        error = "connection closed";
        return false;
    }
    if (!::GetOverlappedResult(pipe, &overlapped, &transferred, TRUE)) {
        error = LastError("overlapped I/O");
        return false;
    }
    return true;
}

} // namespace

Connection::~Connection() {
    Close();
    if (pipe_) {
        ::CloseHandle(static_cast<HANDLE>(pipe_));
        pipe_ = nullptr;
    }
    if (stopEvent_) {
        ::CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }
}

void Connection::Close() {
    bool expected = false;
    if (!closed_.compare_exchange_strong(expected, true)) return;
    if (stopEvent_) ::SetEvent(static_cast<HANDLE>(stopEvent_));
    if (pipe_) ::CancelIoEx(static_cast<HANDLE>(pipe_), nullptr);
}

void Connection::QueryPeer() {
    peerPid_ = 0;
    ULONG pid = 0;
    if (pipe_ && ::GetNamedPipeClientProcessId(static_cast<HANDLE>(pipe_), &pid))
        peerPid_ = static_cast<int>(pid);
    if (peerPid_ == CurrentProcessId() || peerPid_ == 0) {
        // On the client side the "client" pid is our own; ask for the server.
        ULONG serverPid = 0;
        if (pipe_ && ::GetNamedPipeServerProcessId(static_cast<HANDLE>(pipe_), &serverPid) &&
            serverPid != static_cast<ULONG>(CurrentProcessId()))
            peerPid_ = static_cast<int>(serverPid);
    }
}

bool Connection::WriteAll(const char* data, size_t length, std::string& error) {
    HANDLE pipe = static_cast<HANDLE>(pipe_);
    size_t sent = 0;
    while (sent < length) {
        if (closed_.load()) {
            error = "connection closed";
            return false;
        }
        OVERLAPPED overlapped{};
        overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(length - sent, kPipeBuffer));
        if (!::WriteFile(pipe, data + sent, chunk, &written, &overlapped) &&
            ::GetLastError() != ERROR_IO_PENDING) {
            error = LastError("WriteFile");
            ::CloseHandle(overlapped.hEvent);
            return false;
        }
        const bool ok = CompleteOverlapped(pipe, overlapped, static_cast<HANDLE>(stopEvent_),
                                           written, error);
        ::CloseHandle(overlapped.hEvent);
        if (!ok) return false;
        sent += written;
    }
    return true;
}

bool Connection::ReadSome(uint8_t* buffer, size_t capacity, size_t& received, std::string& error) {
    received = 0;
    if (closed_.load()) {
        error = "connection closed";
        return false;
    }
    HANDLE pipe = static_cast<HANDLE>(pipe_);
    OVERLAPPED overlapped{};
    overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD read = 0;
    if (!::ReadFile(pipe, buffer, static_cast<DWORD>(capacity), &read, &overlapped)) {
        const DWORD code = ::GetLastError();
        if (code != ERROR_IO_PENDING) {
            ::CloseHandle(overlapped.hEvent);
            error = code == ERROR_BROKEN_PIPE ? "peer closed the connection" : LastError("ReadFile");
            return false;
        }
    }
    const bool ok = CompleteOverlapped(pipe, overlapped, static_cast<HANDLE>(stopEvent_), read, error);
    ::CloseHandle(overlapped.hEvent);
    if (!ok) return false;
    if (read == 0) {
        error = "peer closed the connection";
        return false;
    }
    received = read;
    return true;
}

Listener::~Listener() {
    Close();
    if (pendingPipe_) {
        ::CloseHandle(static_cast<HANDLE>(pendingPipe_));
        pendingPipe_ = nullptr;
    }
    if (stopEvent_) {
        ::CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }
}

void Listener::Close() {
    bool expected = false;
    if (!closed_.compare_exchange_strong(expected, true)) return;
    if (stopEvent_) ::SetEvent(static_cast<HANDLE>(stopEvent_));
}

bool Listener::CreateInstance(bool first, std::string& error, bool& alreadyInUse) {
    DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
    if (first) openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
    HANDLE pipe = ::CreateNamedPipeA(path_.c_str(), openMode,
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                         PIPE_REJECT_REMOTE_CLIENTS,
                                     PIPE_UNLIMITED_INSTANCES, kPipeBuffer, kPipeBuffer, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        const DWORD code = ::GetLastError();
        if (first && (code == ERROR_ACCESS_DENIED || code == ERROR_PIPE_BUSY)) {
            alreadyInUse = true;
            error = "another broker owns the pipe";
        } else {
            error = LastError("CreateNamedPipe");
        }
        return false;
    }
    pendingPipe_ = pipe;
    return true;
}

std::unique_ptr<Listener> Listener::Create(const std::string& busPath, std::string& error,
                                           bool& alreadyInUse) {
    alreadyInUse = false;
    std::unique_ptr<Listener> listener(new Listener());
    listener->path_ = busPath;
    listener->stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!listener->CreateInstance(true, error, alreadyInUse)) {
        listener->path_.clear();
        return nullptr;
    }
    return listener;
}

ConnectionPtr Listener::Accept() {
    for (;;) {
        if (closed_.load()) return nullptr;
        if (!pendingPipe_) {
            std::string error;
            bool inUse = false;
            if (!CreateInstance(false, error, inUse)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
        }
        HANDLE pipe = static_cast<HANDLE>(pendingPipe_);
        OVERLAPPED overlapped{};
        overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        bool connected = false;
        if (::ConnectNamedPipe(pipe, &overlapped)) {
            connected = true;
        } else {
            const DWORD code = ::GetLastError();
            if (code == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (code == ERROR_IO_PENDING) {
                HANDLE handles[2] = {overlapped.hEvent, static_cast<HANDLE>(stopEvent_)};
                const DWORD waited = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                if (waited == WAIT_OBJECT_0) {
                    DWORD ignored = 0;
                    connected = ::GetOverlappedResult(pipe, &overlapped, &ignored, TRUE) != 0;
                } else {
                    // Wait for the cancel to land: until it has, the kernel
                    // still owns `overlapped`, and it lives in this frame.
                    ::CancelIoEx(pipe, &overlapped);
                    DWORD ignored = 0;
                    ::GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
                }
            }
        }
        ::CloseHandle(overlapped.hEvent);
        if (closed_.load()) return nullptr;
        if (!connected) {
            ::CloseHandle(pipe);
            pendingPipe_ = nullptr;
            continue;
        }
        pendingPipe_ = nullptr;
        ConnectionPtr connection(new Connection());
        connection->pipe_ = pipe;
        connection->stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        connection->QueryPeer();
        return connection;
    }
}

ConnectionPtr ConnectToBus(const std::string& busPath, int timeoutMs, std::string& error) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        HANDLE pipe = ::CreateFileA(busPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            ConnectionPtr connection(new Connection());
            connection->pipe_ = pipe;
            connection->stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            connection->QueryPeer();
            return connection;
        }
        const DWORD code = ::GetLastError();
        if (code == ERROR_FILE_NOT_FOUND) {
            error = "no broker listens on " + busPath;
            return nullptr;
        }
        if (code == ERROR_PIPE_BUSY && std::chrono::steady_clock::now() < deadline) {
            ::WaitNamedPipeA(busPath.c_str(), 200);
            continue;
        }
        error = LastError("CreateFile(pipe)");
        return nullptr;
    }
}

#endif

bool ProcessIsTerminating() {
#ifdef _WIN32
    using ShutdownInProgress = BOOLEAN(WINAPI*)();
    static const ShutdownInProgress query = [] {
        HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        return ntdll ? reinterpret_cast<ShutdownInProgress>(
                               reinterpret_cast<void (*)()>(::GetProcAddress(ntdll, "RtlDllShutdownInProgress")))
                     : nullptr;
    }();
    return query && query();
#else
    return false;
#endif
}

// ===========================================================================
// Shared: frames over the stream
// ===========================================================================

bool Connection::SendFrame(const JSONValue& frame, std::string& error) {
    return SendEncoded(EncodeFrame(frame), error);
}

bool Connection::SendEncoded(const std::string& bytes, std::string& error) {
    if (bytes.size() > UltraMsgMaxFrameBytes + 4) {
        error = "frame exceeds the size limit";
        return false;
    }
    std::lock_guard<std::mutex> lock(sendMutex_);
    return WriteAll(bytes.data(), bytes.size(), error);
}

bool Connection::ReceiveFrame(JSONValue& out, std::string& error) {
    // One thread reads a connection; frames decoded beyond the one it asked
    // for wait here for its next call.
    if (!pendingFrames_.empty()) {
        out = std::move(pendingFrames_.front());
        pendingFrames_.erase(pendingFrames_.begin());
        return true;
    }
    std::vector<uint8_t> buffer(kReadChunk);
    for (;;) {
        size_t received = 0;
        if (!ReadSome(buffer.data(), buffer.size(), received, error)) return false;
        std::vector<JSONValue> frames;
        if (!decoder_.Push(buffer.data(), received, frames, error)) return false;
        if (frames.empty()) continue;
        out = std::move(frames.front());
        for (size_t i = 1; i < frames.size(); ++i) pendingFrames_.push_back(std::move(frames[i]));
        return true;
    }
}

} // namespace Internal
} // namespace UltraMessage
