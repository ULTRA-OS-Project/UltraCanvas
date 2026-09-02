// include/UltraCanvasFolderWatcher.h
// Watches ONE directory and reports when its content changes: an entry
// created, deleted, renamed, or written to by anything - this process or
// another program. Not recursive: it answers "did the folder I am showing
// change", which is what a file display needs.
//
//   UltraCanvasFolderWatcher watcher;
//   if (!watcher.Watch(folder, [] { /* background thread! */ },
//                      [] { /* the watch died - start polling */ })) {
//       // no native backend here - fall back to polling
//   }
//
// The callback runs on the watcher's own thread, so it must do nothing but
// hand the news over (set an atomic, post an event). One save can produce
// several callbacks - the receiver coalesces.
//
// A watch can also die after it started, and silently: the volume it is on is
// unmounted, the share drops, the handle is invalidated. The optional second
// callback is how the caller hears about that - without it a file display
// simply stops noticing changes and looks frozen until the user navigates
// away, which is exactly what happened when a USB stick was pulled while its
// folder was open.
//
// Backends: Linux/BSD (inotify) and Windows (ReadDirectoryChangesW). Where
// none exists - macOS, Android, WebAssembly - Watch() returns false and
// NativeBackendAvailable() is false, so the caller keeps polling; adding a
// backend later needs no change above this header.
// Version: 1.0.0
// Last Modified: 2026-08-25
// Author: UltraCanvas Framework
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace UltraCanvas {

    // ===== BACKEND INTERFACE (implemented per platform under OS/<Platform>/) =====
    // Not part of the public surface: callers use UltraCanvasFolderWatcher.
    class IFolderWatchBackend {
    public:
        virtual ~IFolderWatchBackend() = default;
        // Begin watching `path`. `onChanged` is invoked from the backend's own
        // thread for every batch of changes it sees. False when the folder
        // cannot be watched (gone, no permission, out of watch descriptors).
        //
        // `onFailed` (may be empty) is invoked at most once, from the same
        // thread, when the watch stops working on its own - never as a result
        // of Stop(). After it the backend reports nothing further.
        virtual bool Start(const std::string& path,
                           std::function<void()> onChanged,
                           std::function<void()> onFailed) = 0;
        // Stop and join. Safe to call twice, and safe if Start() failed. The
        // callback never runs after this returns.
        virtual void Stop() = 0;
    };

    // Provided by the platform backend where one exists; the core file
    // supplies a null-returning definition on every other platform.
    std::unique_ptr<IFolderWatchBackend> CreateNativeFolderWatchBackend();

    // ===== THE WATCHER =====
    class UltraCanvasFolderWatcher {
    public:
        using ChangedCallback = std::function<void()>;
        using FailedCallback = std::function<void()>;

        UltraCanvasFolderWatcher();
        ~UltraCanvasFolderWatcher();

        UltraCanvasFolderWatcher(const UltraCanvasFolderWatcher&) = delete;
        UltraCanvasFolderWatcher& operator=(const UltraCanvasFolderWatcher&) = delete;

        // Watch `path`, replacing whatever was watched before. False when this
        // build has no native backend, or the folder cannot be watched - the
        // caller falls back to polling. An empty path just stops.
        //
        // `onFailed` is optional and fires at most once, when a watch that had
        // started stops working (the volume was unmounted, the handle went
        // bad). It runs on the watcher's thread under the same rules as
        // `onChanged`, and never fires because of Stop() or the destructor, so
        // it is safe for it to capture the caller.
        bool Watch(const std::string& path, ChangedCallback onChanged,
                   FailedCallback onFailed = nullptr);

        // Stop watching. The callback never runs after this returns.
        void Stop();

        bool IsWatching() const { return watching; }
        const std::string& WatchedPath() const { return watchedPath; }

        // Whether this build has a native backend at all. False means every
        // Watch() will fail, so a caller can skip trying and poll straight away.
        static bool NativeBackendAvailable();

    private:
        std::unique_ptr<IFolderWatchBackend> backend;
        std::string watchedPath;
        bool watching = false;
    };

} // namespace UltraCanvas
