// OS/Linux/UltraCanvasLinuxVolumeMonitor.cpp
// Mount-table backend for UltraCanvasVolumeMonitor: the kernel itself says
// when something is mounted or unmounted, so a stick appears the moment it is
// plugged in and an idle machine costs nothing.
//
// /proc/self/mountinfo is pollable: poll() reports POLLPRI (and POLLERR) on it
// whenever the mount table of this namespace changes. The event has to be
// consumed by re-reading the file from the start, or the next poll() returns
// at once and the loop spins - which is the whole subtlety of this file.
//
// One thread waits on that plus a self-pipe, so Stop() wakes it immediately
// instead of leaving a thread parked in poll() until the next mount.
// Version: 1.0.0
// Last Modified: 2026-09-01
// Author: UltraCanvas Framework

#include "UltraCanvasVolumeMonitor.h"

#include <atomic>
#include <cerrno>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace UltraCanvas {

    namespace {

        // Both files describe the same mount table. mountinfo is the modern
        // one and the one that reports every change; mounts is the fallback on
        // a kernel or container that does not expose it.
        constexpr const char* kMountInfoPaths[] = {
            "/proc/self/mountinfo",
            "/proc/self/mounts",
        };

        // A mount point in /proc/self/mounts has its spaces, tabs, newlines
        // and backslashes written as three-digit octal escapes, so a volume
        // named "USB STICK" arrives as "/media/bob/USB\\040STICK" and would
        // never match the path a directory listing gives.
        std::string Unescape(const std::string& field) {
            std::string out;
            out.reserve(field.size());
            for (size_t i = 0; i < field.size(); ++i) {
                if (field[i] != '\\' || i + 3 >= field.size()) {
                    out.push_back(field[i]);
                    continue;
                }
                const char a = field[i + 1], b = field[i + 2], c = field[i + 3];
                if (a < '0' || a > '7' || b < '0' || b > '7' || c < '0' || c > '7') {
                    out.push_back(field[i]);
                    continue;
                }
                out.push_back(static_cast<char>((a - '0') * 64 + (b - '0') * 8 + (c - '0')));
                i += 3;
            }
            return out;
        }

        class MountInfoVolumeBackend final : public IVolumeMonitorBackend {
        public:
            ~MountInfoVolumeBackend() override { Stop(); }

            bool Start(std::function<void()> onChanged) override {
                Stop();
                if (!onChanged) return false;

                for (const char* path : kMountInfoPaths) {
                    mountFd = ::open(path, O_RDONLY | O_CLOEXEC);
                    if (mountFd >= 0) break;
                }
                if (mountFd < 0) return false;   // no /proc: the caller polls

                // Self-pipe: Stop() writes a byte to break the poll() below.
                if (::pipe2(wakePipe, O_NONBLOCK | O_CLOEXEC) != 0) {
                    CloseFd(mountFd);
                    return false;
                }

                // Read the table once before waiting: poll() only reports a
                // change against what has already been read, so an unread file
                // makes the first poll() return immediately, forever.
                DrainMountTable();

                callback = std::move(onChanged);
                stopping.store(false);
                worker = std::thread([this]() { Run(); });
                return true;
            }

            void Stop() override {
                stopping.store(true);
                if (wakePipe[1] >= 0) {
                    const char byte = 1;
                    ssize_t ignored = ::write(wakePipe[1], &byte, 1);
                    (void)ignored;
                }
                if (worker.joinable()) worker.join();
                CloseFd(mountFd);
                CloseFd(wakePipe[0]);
                CloseFd(wakePipe[1]);
                callback = nullptr;
                stopping.store(false);
            }

        private:
            static void CloseFd(int& fd) {
                if (fd >= 0) ::close(fd);
                fd = -1;
            }

            // Re-read the whole file from the start. This is what acknowledges
            // the event: until the contents have been consumed the kernel
            // keeps reporting the same change.
            void DrainMountTable() {
                if (mountFd < 0) return;
                if (::lseek(mountFd, 0, SEEK_SET) == off_t(-1)) return;
                char buffer[4096];
                for (;;) {
                    const ssize_t got = ::read(mountFd, buffer, sizeof(buffer));
                    if (got > 0) continue;
                    if (got < 0 && errno == EINTR) continue;
                    break;                       // end of file, or unreadable
                }
            }

            void Run() {
                while (!stopping.load()) {
                    struct pollfd fds[2];
                    // POLLPRI is how a mount-table change is reported; POLLERR
                    // arrives with it and needs no separate handling.
                    fds[0].fd = mountFd;
                    fds[0].events = POLLPRI | POLLERR;
                    fds[0].revents = 0;
                    fds[1].fd = wakePipe[0];
                    fds[1].events = POLLIN;
                    fds[1].revents = 0;

                    const int ready = ::poll(fds, 2, -1);
                    if (ready < 0) {
                        if (errno == EINTR) continue;
                        return;                  // unusable; the caller polls
                    }
                    if (stopping.load()) return;
                    if (fds[1].revents & POLLIN) return;   // Stop() woke us

                    if (!(fds[0].revents & (POLLPRI | POLLERR))) continue;

                    // Consume the event before reporting it: a callback that
                    // re-lists the volumes must see the table this change
                    // produced, and an unconsumed event spins the loop.
                    DrainMountTable();
                    if (!stopping.load() && callback) callback();
                }
            }

            int mountFd = -1;
            int wakePipe[2] = {-1, -1};
            std::atomic<bool> stopping{false};
            std::thread worker;
            std::function<void()> callback;
        };

    } // namespace

    std::unique_ptr<IVolumeMonitorBackend> CreateNativeVolumeMonitorBackend() {
        return std::make_unique<MountInfoVolumeBackend>();
    }

    std::set<std::string> ListPlatformMountPoints() {
        // /proc/self/mounts, second field. Read whole rather than parsed line
        // by line through a stream: the kernel generates it on the fly, and a
        // partial read of a table that changes mid-read is the one way to get
        // a torn line out of it.
        std::set<std::string> points;
        std::string text;
        const int fd = ::open("/proc/self/mounts", O_RDONLY | O_CLOEXEC);
        if (fd < 0) return points;
        char buffer[8192];
        for (;;) {
            const ssize_t got = ::read(fd, buffer, sizeof(buffer));
            if (got > 0) { text.append(buffer, static_cast<size_t>(got)); continue; }
            if (got < 0 && errno == EINTR) continue;
            break;
        }
        ::close(fd);

        size_t lineStart = 0;
        while (lineStart < text.size()) {
            size_t lineEnd = text.find('\n', lineStart);
            if (lineEnd == std::string::npos) lineEnd = text.size();
            const std::string line = text.substr(lineStart, lineEnd - lineStart);
            lineStart = lineEnd + 1;

            // "<source> <mount point> <type> <options> <dump> <pass>". Only
            // the second field is wanted, and it is the only one that can
            // contain an escape.
            const size_t first = line.find(' ');
            if (first == std::string::npos) continue;
            const size_t second = line.find(' ', first + 1);
            if (second == std::string::npos) continue;
            std::string point = Unescape(line.substr(first + 1, second - first - 1));
            if (!point.empty()) points.insert(std::move(point));
        }
        return points;
    }

} // namespace UltraCanvas
