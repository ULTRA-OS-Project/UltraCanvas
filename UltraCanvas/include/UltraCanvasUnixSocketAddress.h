// include/UltraCanvasUnixSocketAddress.h
// The address of a Unix domain socket at a path of any length.
//
// A socket's address (sockaddr_un) holds a path of 107 bytes on Linux and
// 103 on macOS and the BSDs, and the sockets a program makes for itself lie
// under folders it does not choose the length of: the user's runtime folder,
// a virtual machine's own. A longer path could not be bound or connected at
// all. On Linux such a socket is reached through its folder: the folder is
// opened, which takes a path of any length, and the socket named as
// /proc/self/fd/<n>/<file>, which the kernel resolves to the same file. The
// socket stays where its path says; only the way to name it is shorter.
// Elsewhere a longer path is refused, with the reason.
//
//   UltraCanvas::UnixSocketAddress address;
//   std::string error;
//   if (!address.Resolve(path, error)) return Fail(error);
//   bind(fd, address.Get(), address.Size());     // or connect()
//
// The address keeps the folder open, so it lives until bind() or connect()
// has returned. Header-only and with nothing of the framework in it, for the
// modules that do not link the core (UltraMessage, UltraWin).
//
// A program that has ANOTHER program make the socket (QEMU, virtiofsd) cannot
// hand it a /proc/self/fd path of its own: there, start the other program in
// the socket's folder and give it the file's name alone - Fits() says when
// that is needed.
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#ifndef _WIN32

#include <cerrno>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace UltraCanvas {

    class UnixSocketAddress {
    public:
        UnixSocketAddress() = default;
        ~UnixSocketAddress() { CloseFolder(); }
        UnixSocketAddress(const UnixSocketAddress&) = delete;
        UnixSocketAddress& operator=(const UnixSocketAddress&) = delete;

        // True when `path` can be an address as it is, on every system.
        static bool Fits(const std::string& path) {
            return path.size() < sizeof(static_cast<sockaddr_un*>(nullptr)->sun_path);
        }

        // False with `error`. `folderMissing` is set when the path was too
        // long and its folder is not there - to a client that waits for a
        // server, "not yet" rather than "never".
        bool Resolve(const std::string& path, std::string& error, bool* folderMissing = nullptr) {
            if (folderMissing) *folderMissing = false;
            CloseFolder();
            std::memset(&addr_, 0, sizeof(addr_));
            addr_.sun_family = AF_UNIX;
            if (Fits(path)) {
                std::memcpy(addr_.sun_path, path.c_str(), path.size() + 1);
                return true;
            }
#if defined(__linux__)
            const size_t slash = path.rfind('/');
            if (slash != std::string::npos && slash > 0 && slash + 1 < path.size()) {
                folder_ = ::open(path.substr(0, slash).c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC);
                if (folder_ < 0) {
                    const int code = errno;
                    if (folderMissing) *folderMissing = code == ENOENT || code == ENOTDIR;
                    error = std::string("cannot open the folder of the socket ") + path + ": " + std::strerror(code);
                    return false;
                }
                // "/<file>" comes along with its slash.
                const std::string through = "/proc/self/fd/" + std::to_string(folder_) + path.substr(slash);
                if (Fits(through)) {
                    if (::access("/proc/self/fd", X_OK) != 0) {
                        error = "path too long for a Unix socket, and no /proc to reach it through its folder: " +
                                path;
                        return false;
                    }
                    std::memcpy(addr_.sun_path, through.c_str(), through.size() + 1);
                    return true;
                }
            }
#endif
            error = "path too long for a Unix socket: " + path;
            return false;
        }

        const sockaddr* Get() const { return reinterpret_cast<const sockaddr*>(&addr_); }
        socklen_t Size() const { return static_cast<socklen_t>(sizeof(addr_)); }

    private:
        void CloseFolder() {
            if (folder_ >= 0) ::close(folder_);
            folder_ = -1;
        }

        sockaddr_un addr_{};
        int folder_ = -1;   // the socket's folder, while its path is too long to name
    };

} // namespace UltraCanvas

#endif // !_WIN32
