// include/UltraCanvasFolderWatcher.h
// Watches ONE directory and reports when its content changes: an entry
// created, deleted, renamed, or written to by anything - this process or
// another program. Not recursive: it answers "did the folder I am showing
// change", which is what a file display needs.
//
//   UltraCanvasFolderWatcher watcher;
//   if (!watcher.Watch(folder, [] { /* background thread! */ })) {
//       // no native backend here - fall back to polling
//   }
//
// The callback runs on the watcher's own thread, so it must do nothing but
// hand the news over (set an atomic, post an event). One save can produce
// several callbacks - the receiver coalesces.
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
        virtual bool Start(const std::string& path,
                           std::function<void()> onChanged) = 0;
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

        UltraCanvasFolderWatcher();
        ~UltraCanvasFolderWatcher();

        UltraCanvasFolderWatcher(const UltraCanvasFolderWatcher&) = delete;
        UltraCanvasFolderWatcher& operator=(const UltraCanvasFolderWatcher&) = delete;

        // Watch `path`, replacing whatever was watched before. False when this
        // build has no native backend, or the folder cannot be watched - the
        // caller falls back to polling. An empty path just stops.
        bool Watch(const std::string& path, ChangedCallback onChanged);

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
