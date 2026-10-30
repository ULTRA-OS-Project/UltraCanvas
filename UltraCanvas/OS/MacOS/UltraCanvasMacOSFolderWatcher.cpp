// OS/MacOS/UltraCanvasMacOSFolderWatcher.cpp
// FSEvents backend for UltraCanvasFolderWatcher: watches one directory (not
// its subtree) and reports every change to its entries.
//
// FSEvents is recursive and reports directories, not files: an event names the
// folder in which something changed. Events naming a folder below the watched
// one are dropped, which gives the same "this folder only" answer inotify and
// ReadDirectoryChangesW give. Anything FSEvents could not keep track of (it
// coalesced, or dropped events) is reported, because a rescan too many costs
// one listing and one too few leaves the display wrong.
//
// The stream delivers on a private serial dispatch queue, which is the
// backend's thread. Stop() stops and invalidates the stream and then drains the
// queue, so no callback is running or still to come once it returns.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasFolderWatcher.h"

#include <atomic>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <sys/stat.h>

namespace UltraCanvas {

    namespace {

        // Long enough to fold the steps of one save into a single batch, short
        // enough that the user does not see the delay. NoDefer delivers the
        // first event of a quiet spell at once and only batches what follows.
        constexpr CFTimeInterval kLatencySeconds = 0.05;

        // Events that mean "something changed here, and FSEvents cannot say
        // exactly what": reported whatever path they carry.
        constexpr FSEventStreamEventFlags kRescanFlags =
                kFSEventStreamEventFlagMustScanSubDirs |
                kFSEventStreamEventFlagUserDropped |
                kFSEventStreamEventFlagKernelDropped |
                kFSEventStreamEventFlagEventIdsWrapped;

        // FSEvents ends a directory path with '/', realpath() does not.
        std::string WithoutTrailingSlash(const char* path) {
            std::string out = path ? path : "";
            while (out.size() > 1 && out.back() == '/') out.pop_back();
            return out;
        }

        // The default macOS volume is case-insensitive, and FSEvents spells a
        // path as the disk stores it, which need not be how it was asked for.
        // ASCII folding is enough here: a mismatch beyond it only means a
        // change is reported rather than dropped.
        bool SamePath(const std::string& a, const std::string& b) {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) !=
                    std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            }
            return true;
        }

        // Is `path` the folder `prefix` or somewhere inside it?
        bool IsAtOrBelow(const std::string& path, const std::string& prefix) {
            if (path.size() < prefix.size()) return false;
            if (!SamePath(path.substr(0, prefix.size()), prefix)) return false;
            return path.size() == prefix.size() || prefix == "/" ||
                   path[prefix.size()] == '/';
        }

        class FSEventsFolderWatchBackend final : public IFolderWatchBackend {
        public:
            ~FSEventsFolderWatchBackend() override { Stop(); }

            bool Start(const std::string& path,
                       std::function<void()> onChanged,
                       std::function<void()> onFailed) override {
                Stop();

                // FSEvents reports resolved paths - /private/var, not /var -
                // so the watched folder is compared in that form too.
                char resolved[PATH_MAX];
                if (!::realpath(path.c_str(), resolved)) return false;
                struct stat st{};
                if (::stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
                root = WithoutTrailingSlash(resolved);

                CFStringRef cfPath = CFStringCreateWithFileSystemRepresentation(
                        kCFAllocatorDefault, root.c_str());
                if (!cfPath) return false;
                const void* values[1] = {cfPath};
                CFArrayRef paths = CFArrayCreate(kCFAllocatorDefault, values, 1,
                                                 &kCFTypeArrayCallBacks);
                CFRelease(cfPath);
                if (!paths) return false;

                FSEventStreamContext context{};
                context.version = 0;
                context.info = this;

                // WatchRoot: the watched folder itself being deleted, moved
                // away or unmounted arrives as RootChanged - the end of the
                // watch, which the caller has to hear about.
                stream = FSEventStreamCreate(
                        kCFAllocatorDefault, &FSEventsFolderWatchBackend::OnEvents,
                        &context, paths, kFSEventStreamEventIdSinceNow,
                        kLatencySeconds,
                        kFSEventStreamCreateFlagNoDefer |
                        kFSEventStreamCreateFlagWatchRoot);
                CFRelease(paths);
                if (!stream) return false;

                queue = dispatch_queue_create("UltraCanvas.FolderWatcher",
                                              DISPATCH_QUEUE_SERIAL);
                if (!queue) {
                    ReleaseStream();
                    return false;
                }

                callback = std::move(onChanged);
                failedCallback = std::move(onFailed);
                failureReported = false;
                stopping.store(false);

                FSEventStreamSetDispatchQueue(stream, queue);
                // Registered with the service when this returns: whatever
                // changes from here on is delivered.
                if (!FSEventStreamStart(stream)) {
                    FSEventStreamInvalidate(stream);
                    ReleaseStream();
                    ReleaseQueue();
                    callback = nullptr;
                    failedCallback = nullptr;
                    return false;
                }
                return true;
            }

            void Stop() override {
                if (stream) {
                    stopping.store(true);
                    FSEventStreamStop(stream);
                    FSEventStreamInvalidate(stream);
                    ReleaseStream();
                }
                if (queue) {
                    // A delivery already running on the queue finishes before
                    // this empty task does: once it returns, nothing is left
                    // that could still call back.
                    dispatch_sync_f(queue, nullptr, [](void*) {});
                    ReleaseQueue();
                }
                callback = nullptr;
                failedCallback = nullptr;
                failureReported = false;
                root.clear();
            }

        private:
            void ReleaseStream() {
                if (!stream) return;
                FSEventStreamRelease(stream);
                stream = nullptr;
            }

            void ReleaseQueue() {
                if (!queue) return;
#if !(defined(__OBJC__) && __has_feature(objc_arc))
                dispatch_release(queue);    // under ARC the assignment does it
#endif
                queue = nullptr;
            }

            // Tell the caller the watch is over, exactly once, and never
            // because of Stop() - a failure it asked for is a failure it can
            // act on, a Stop() it performed is not news.
            void ReportFailure() {
                if (failureReported || stopping.load() || !failedCallback) return;
                failureReported = true;
                failedCallback();
            }

            static void OnEvents(ConstFSEventStreamRef /*stream*/, void* info,
                                 size_t count, void* eventPaths,
                                 const FSEventStreamEventFlags flags[],
                                 const FSEventStreamEventId /*ids*/[]) {
                static_cast<FSEventsFolderWatchBackend*>(info)->HandleEvents(
                        count, static_cast<char**>(eventPaths), flags);
            }

            // Runs on the queue. One report per batch, like the other
            // backends: a single save is several events.
            void HandleEvents(size_t count, char** paths,
                              const FSEventStreamEventFlags flags[]) {
                if (stopping.load() || failureReported) return;
                bool changed = false;
                bool watchGone = false;
                for (size_t i = 0; i < count; ++i) {
                    const std::string path = WithoutTrailingSlash(paths ? paths[i] : nullptr);
                    if (flags[i] & kFSEventStreamEventFlagRootChanged) {
                        // The folder was deleted, moved, or replaced.
                        changed = true;
                        watchGone = true;
                        continue;
                    }
                    if ((flags[i] & kFSEventStreamEventFlagUnmount) &&
                        IsAtOrBelow(root, path)) {
                        // The volume the folder is on went away.
                        changed = true;
                        watchGone = true;
                        continue;
                    }
                    if (flags[i] & kRescanFlags) {
                        changed = true;
                        continue;
                    }
                    // A change inside a subfolder is not a change to the
                    // listing of this one. Every other path - this folder, or
                    // one spelled in a way the comparison does not recognise -
                    // counts.
                    if (!SamePath(path, root) && IsAtOrBelow(path, root)) continue;
                    changed = true;
                }
                if (changed && !stopping.load() && callback) callback();
                if (watchGone) ReportFailure();
            }

            FSEventStreamRef stream = nullptr;
            dispatch_queue_t queue = nullptr;
            std::string root;                     // resolved, no trailing '/'
            std::atomic<bool> stopping{false};
            std::function<void()> callback;
            std::function<void()> failedCallback;
            bool failureReported = false;         // queue only
        };

    } // namespace

    std::unique_ptr<IFolderWatchBackend> CreateNativeFolderWatchBackend() {
        return std::make_unique<FSEventsFolderWatchBackend>();
    }

} // namespace UltraCanvas
