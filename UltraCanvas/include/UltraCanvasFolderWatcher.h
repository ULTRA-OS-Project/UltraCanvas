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
// Backends: Linux/BSD (inotify), Windows (ReadDirectoryChangesW) and macOS
// (FSEvents). Where none exists - Android, WebAssembly - Watch() returns false
// and NativeBackendAvailable() is false, so the caller keeps polling; adding a
// backend later needs no change above this header.
//
// Every backend is listening by the time Watch() returns: a change made after
// it is reported, so a caller may start the watch and then list the folder.
//
// A watch holds the folder open on Windows, and Windows will not delete a
// folder anything holds: on FAT and exFAT it stays listed as "delete pending"
// until the handle goes, and a folder ABOVE it cannot be renamed or moved at
// all. So a program about to delete, rename or move a folder holds watches
// off it for as long as that takes:
//
//   UltraCanvasFolderWatchHold hold({folder});
//   // ... delete, rename or move `folder` ...
//
// The hold lets go of every watch in the process on that folder or below it -
// a second file display showing a subfolder of the one being deleted, say -
// and refuses new ones there until it ends. Each released watch reports
// itself failed, as if its volume had gone, so its owner falls back to
// polling the same path and hears when it is gone.
// Version: 1.2.0 - UltraCanvasFolderWatchHold: the process's watches let go of a
//                  folder about to be deleted, renamed or moved
// Version: 1.1.0 - macOS backend (FSEvents); a change right after Watch() is never missed
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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
        // build has no native backend, the folder cannot be watched, or an
        // UltraCanvasFolderWatchHold keeps watches off it - the caller falls
        // back to polling. An empty path just stops.
        //
        // `onFailed` is optional and fires at most once, when a watch that had
        // started stops working (the volume was unmounted, the handle went
        // bad) or was released by an UltraCanvasFolderWatchHold. It runs on
        // the watcher's thread - or, for a release, on the hold's thread -
        // under the same rules as `onChanged`: hand the news over, never call
        // back into the watcher. It never fires because of Stop() or the
        // destructor, so it is safe for it to capture the caller.
        bool Watch(const std::string& path, ChangedCallback onChanged,
                   FailedCallback onFailed = nullptr);

        // Stop watching. The callback never runs after this returns.
        void Stop();

        // Still listening. False after Stop(), and after a release, which -
        // unlike Stop() - keeps WatchedPath() for the owner to fall back to.
        bool IsWatching() const { return watching.load(); }
        const std::string& WatchedPath() const { return watchedPath; }

        // Whether this build has a native backend at all. False means every
        // Watch() will fail, so a caller can skip trying and poll straight away.
        static bool NativeBackendAvailable();

    private:
        friend class UltraCanvasFolderWatchHold;
        void StopLocked();
        // Stops the watch as if it had died: the handle goes, WatchedPath()
        // stays, `onFailed` runs. The caller holds `mutex`.
        void ReleaseLocked();

        // Guards backend, watchedPath and failedCallback against a release
        // from another thread; the owner's own calls take it too.
        mutable std::mutex mutex;
        std::unique_ptr<IFolderWatchBackend> backend;
        std::string watchedPath;
        // What the backend calls when the watch dies, kept so a release can
        // call it too; it lets itself run once per watch.
        FailedCallback failedCallback;
        std::atomic<bool> watching{false};
    };

    // ===== KEEPING WATCHES OFF A FOLDER =====
    // For as long as it lives, no watch in this process holds any of
    // `folders` or anything below them (UTF-8 paths; compared without regard
    // to case on Windows). Taking it releases every watch already there -
    // each stops as if its watch had died: its `onFailed` runs, on this
    // thread, and its WatchedPath() stays, so its owner polls that path
    // instead - and Watch() on a path there answers false until it ends.
    //
    // Take one before deleting, renaming or moving a folder: on Windows a
    // watch holds its folder open, which keeps a deleted folder listed
    // ("delete pending") on FAT and exFAT, and refuses the rename of any
    // folder above it. Holds may overlap; each lifts only its own folders.
    // Safe from any thread.
    class UltraCanvasFolderWatchHold {
    public:
        explicit UltraCanvasFolderWatchHold(std::vector<std::string> folders);
        ~UltraCanvasFolderWatchHold();

        UltraCanvasFolderWatchHold(const UltraCanvasFolderWatchHold&) = delete;
        UltraCanvasFolderWatchHold& operator=(const UltraCanvasFolderWatchHold&) = delete;

        // How many running watches taking the hold released.
        size_t Released() const { return released; }

    private:
        std::vector<std::string> folders;
        size_t released = 0;
    };

} // namespace UltraCanvas
