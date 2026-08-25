// OS/Linux/UltraCanvasLinuxFolderWatcher.cpp
// inotify backend for UltraCanvasFolderWatcher: watches one directory (not its
// subtree) and reports every change to its entries. Also used on the BSDs when
// they build with inotify compatibility; systems without it fall back to the
// core file's null backend and the caller polls.
//
// The read loop runs on its own thread and waits on two descriptors at once -
// the inotify queue and a self-pipe - so Stop() wakes it immediately instead
// of leaving it blocked until the next filesystem event.
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework

#include "UltraCanvasFolderWatcher.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace UltraCanvas {

    namespace {

        // Everything that changes what a file display shows: entries appearing
        // and disappearing (CREATE / DELETE / MOVED_FROM / MOVED_TO), their
        // content and size (CLOSE_WRITE, plus MODIFY so a long write shows
        // progress), and their metadata (ATTRIB - a chmod changes the shown
        // attributes). DELETE_SELF / MOVE_SELF report the watched folder itself
        // going away, which the rescan then discovers.
        constexpr uint32_t kWatchMask =
                IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO |
                IN_CLOSE_WRITE | IN_MODIFY | IN_ATTRIB |
                IN_DELETE_SELF | IN_MOVE_SELF;

        class InotifyFolderWatchBackend final : public IFolderWatchBackend {
        public:
            ~InotifyFolderWatchBackend() override { Stop(); }

            bool Start(const std::string& path,
                       std::function<void()> onChanged) override {
                Stop();

                inotifyFd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
                if (inotifyFd < 0) return false;

                watchDescriptor = inotify_add_watch(inotifyFd, path.c_str(), kWatchMask);
                if (watchDescriptor < 0) {   // gone, unreadable, or out of watches
                    CloseFd(inotifyFd);
                    return false;
                }
                // Self-pipe: Stop() writes a byte to break the poll() below.
                if (pipe2(wakePipe, O_NONBLOCK | O_CLOEXEC) != 0) {
                    CloseFd(inotifyFd);
                    return false;
                }

                callback = std::move(onChanged);
                stopping.store(false);
                worker = std::thread([this]() { Run(); });
                return true;
            }

            void Stop() override {
                if (worker.joinable()) {
                    stopping.store(true);
                    if (wakePipe[1] >= 0) {
                        const char byte = 1;
                        // A full pipe already carries the wake-up.
                        ssize_t ignored = ::write(wakePipe[1], &byte, 1);
                        (void)ignored;
                    }
                    worker.join();
                }
                CloseFd(wakePipe[0]);
                CloseFd(wakePipe[1]);
                CloseFd(inotifyFd);
                watchDescriptor = -1;
                callback = nullptr;
            }

        private:
            static void CloseFd(int& fd) {
                if (fd >= 0) ::close(fd);
                fd = -1;
            }

            void Run() {
                // Sized for the worst case one read can return: inotify refuses
                // to deliver an event that would not fit in the buffer.
                std::vector<char> buffer(64 * 1024);
                while (!stopping.load()) {
                    struct pollfd fds[2];
                    fds[0].fd = inotifyFd;
                    fds[0].events = POLLIN;
                    fds[0].revents = 0;
                    fds[1].fd = wakePipe[0];
                    fds[1].events = POLLIN;
                    fds[1].revents = 0;

                    const int ready = ::poll(fds, 2, -1);
                    if (ready < 0) {
                        if (errno == EINTR) continue;
                        return;              // the queue is unusable; caller polls
                    }
                    if (stopping.load()) return;
                    if (fds[1].revents & POLLIN) return;   // Stop() woke us

                    if (!(fds[0].revents & POLLIN)) continue;

                    // Drain the queue and report ONCE for the batch: a single
                    // save is several events, and the receiver would coalesce
                    // them anyway.
                    bool sawEvent = false;
                    for (;;) {
                        const ssize_t got = ::read(inotifyFd, buffer.data(), buffer.size());
                        if (got > 0) { sawEvent = true; continue; }
                        if (got < 0 && errno == EINTR) continue;
                        break;               // EAGAIN: queue drained
                    }
                    if (sawEvent && !stopping.load() && callback) callback();
                }
            }

            int inotifyFd = -1;
            int watchDescriptor = -1;
            int wakePipe[2] = {-1, -1};
            std::atomic<bool> stopping{false};
            std::thread worker;
            std::function<void()> callback;
        };

    } // namespace

    std::unique_ptr<IFolderWatchBackend> CreateNativeFolderWatchBackend() {
        return std::make_unique<InotifyFolderWatchBackend>();
    }

} // namespace UltraCanvas
