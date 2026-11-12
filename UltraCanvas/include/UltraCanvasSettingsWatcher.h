// include/UltraCanvasSettingsWatcher.h
// The shared settings files as live settings: one UltraCanvasFolderWatcher
// on the UltraCanvas settings folder, a handler per file name, run on the
// UI thread once per burst of changes and only when that file's modification
// time or existence changed since the last look. Where the build has no
// native watch (macOS, a platform without a backend) the owner polls
// instead: Poll() from a timer every few seconds - the stat of three files.
//
// The application base owns one (UltraCanvasApplication.h): Appearance.conf
// re-reads the host appearance and the theme follows, as a change of the
// desktop does. A handler runs on the UI thread, so it may do anything.
//
//   UltraCanvasSettingsWatcher watcher;
//   watcher.Watch("Appearance.conf", [&] { app.RefreshHostAppearance(); });
//   const bool native = watcher.Start(UltraCanvasSettingsFolder(), [&](std::function<void()> f) { app.PostToUIThread(std::move(f)); });
//   // not native: a timer calls watcher.Poll() every five seconds
//
// Version: 1.0.1 - a check posted to the UI thread does nothing once the watcher is gone
// Version: 1.0.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasFolderWatcher.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasSettingsWatcher {
    public:
        using Handler = std::function<void()>;
        // Runs a task on the UI thread.
        using Poster = std::function<void(std::function<void()>)>;

        UltraCanvasSettingsWatcher() = default;
        ~UltraCanvasSettingsWatcher() { Stop(); *alive = false; }
        UltraCanvasSettingsWatcher(const UltraCanvasSettingsWatcher&) = delete;
        UltraCanvasSettingsWatcher& operator=(const UltraCanvasSettingsWatcher&) = delete;

        // A file of the folder and what to do when it changed. Before or
        // after Start; a second Watch of the same name replaces the handler.
        void Watch(const std::string& fileName, Handler handler);

        // Remembers the files as they are now and starts the native watch on
        // `folder`; true when one runs, false when the build has none or the
        // folder cannot be watched (the owner then polls). `post` runs a
        // task on the UI thread; the handlers run through it.
        bool Start(const std::filesystem::path& folder, Poster post);
        void Stop();
        bool IsNative() const { return native; }
        const std::filesystem::path& Folder() const { return folder; }

        // Looks at the files now, on the caller's thread, and runs the
        // handler of each that changed. The native watch calls it on the UI
        // thread once per burst; a polling owner calls it from its timer.
        void Poll();

    private:
        struct Entry {
            std::string fileName;
            Handler handler;
            bool exists = false;
            std::filesystem::file_time_type modified{};
            std::uintmax_t size = 0;
        };
        void Remember(Entry& entry) const;
        void ScheduleCheck();

        std::filesystem::path folder;
        Poster post;
        std::vector<Entry> entries;
        std::unique_ptr<UltraCanvasFolderWatcher> watcher;
        bool native = false;
        std::atomic<bool> checkPending{false};
        // A check posted to the UI thread may run after the watcher is gone
        // (the application resets its watcher at shutdown): the task holds
        // this and does nothing then. Set and read on the UI thread.
        std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    };

} // namespace UltraCanvas
