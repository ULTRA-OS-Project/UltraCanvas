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
                       std::function<void()> onChanged,
                       std::function<void()> onFailed) override {
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
                failedCallback = std::move(onFailed);
                failureReported = false;
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
                failedCallback = nullptr;
                failureReported = false;
            }

        private:
            static void CloseFd(int& fd) {
                if (fd >= 0) ::close(fd);
                fd = -1;
            }

            // Tell the caller the watch is over, exactly once, and never
            // because of Stop() - a failure it asked for is a failure it can
            // act on, a Stop() it performed is not news.
            void ReportFailure() {
                if (failureReported || stopping.load() || !failedCallback) return;
                failureReported = true;
                failedCallback();
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
                        ReportFailure();     // the queue is unusable
                        return;
                    }
                    if (stopping.load()) return;
                    if (fds[1].revents & POLLIN) return;   // Stop() woke us

                    if (!(fds[0].revents & POLLIN)) continue;

                    // Drain the queue and report ONCE for the batch: a single
                    // save is several events, and the receiver would coalesce
                    // them anyway.
                    bool sawEvent = false;
                    bool watchGone = false;
                    for (;;) {
                        const ssize_t got = ::read(inotifyFd, buffer.data(), buffer.size());
                        if (got > 0) {
                            sawEvent = true;
                            if (WatchEnded(buffer.data(), static_cast<size_t>(got)))
                                watchGone = true;
                            continue;
                        }
                        if (got < 0 && errno == EINTR) continue;
                        break;               // EAGAIN: queue drained
                    }
                    if (sawEvent && !stopping.load() && callback) callback();
                    if (watchGone) {
                        // The descriptor is gone - the folder was deleted, or
                        // the volume it lives on was unmounted. Waiting on it
                        // again would park this thread on a queue that can
                        // never produce another event.
                        ReportFailure();
                        return;
                    }
                }
            }

            // Does this batch say the watch itself has ended? IN_IGNORED is
            // the kernel retiring the descriptor (after a delete, a move of
            // the folder, or an unmount) and IN_UNMOUNT precedes it when the
            // filesystem goes away. Both arrive whether or not they were
            // asked for. IN_Q_OVERFLOW deliberately does not count: events
            // were lost, but the watch is alive and the rescan covers it.
            static bool WatchEnded(const char* data, size_t length) {
                size_t offset = 0;
                while (offset + sizeof(struct inotify_event) <= length) {
                    const auto* event =
                            reinterpret_cast<const struct inotify_event*>(data + offset);
                    if (event->mask & (IN_IGNORED | IN_UNMOUNT)) return true;
                    offset += sizeof(struct inotify_event) + event->len;
                }
                return false;
            }

            int inotifyFd = -1;
            int watchDescriptor = -1;
            int wakePipe[2] = {-1, -1};
            std::atomic<bool> stopping{false};
            std::thread worker;
            std::function<void()> callback;
            std::function<void()> failedCallback;
            bool failureReported = false;   // worker thread only
        };

    } // namespace

    std::unique_ptr<IFolderWatchBackend> CreateNativeFolderWatchBackend() {
        return std::make_unique<InotifyFolderWatchBackend>();
    }

} // namespace UltraCanvas
