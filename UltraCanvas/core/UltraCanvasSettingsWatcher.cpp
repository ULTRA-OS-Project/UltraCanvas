// core/UltraCanvasSettingsWatcher.cpp
// The settings folder watched, the handlers run once per burst for the files
// that changed (UltraCanvasSettingsWatcher.h).
// Version: 1.0.1 - the file name joined through PathFromUtf8 (UTF-8 on Windows)
// Version: 1.0.0
// Last Modified: 2026-11-11
// Author: UltraCanvas Framework
#include "UltraCanvasSettingsWatcher.h"
#include "UltraCanvasPathUtf8.h"

#include <system_error>

namespace UltraCanvas {

    void UltraCanvasSettingsWatcher::Watch(const std::string& fileName, Handler handler) {
        for (Entry& entry : entries) {
            if (entry.fileName == fileName) {
                entry.handler = std::move(handler);
                return;
            }
        }
        Entry entry;
        entry.fileName = fileName;
        entry.handler = std::move(handler);
        if (!folder.empty()) Remember(entry);
        entries.push_back(std::move(entry));
    }

    void UltraCanvasSettingsWatcher::Remember(Entry& entry) const {
        std::error_code ec;
        const std::filesystem::path path = folder / PathFromUtf8(entry.fileName);
        entry.exists = std::filesystem::is_regular_file(path, ec) && !ec;
        if (entry.exists) {
            entry.modified = std::filesystem::last_write_time(path, ec);
            if (ec) entry.modified = {};
            entry.size = std::filesystem::file_size(path, ec);
            if (ec) entry.size = 0;
        } else {
            entry.modified = {};
            entry.size = 0;
        }
    }

    bool UltraCanvasSettingsWatcher::Start(const std::filesystem::path& where, Poster poster) {
        Stop();
        folder = where;
        post = std::move(poster);
        for (Entry& entry : entries) Remember(entry);
        native = false;
        if (folder.empty()) return false;
        if (!UltraCanvasFolderWatcher::NativeBackendAvailable()) return false;
        std::error_code ec;
        // The folder may not exist until a setting is first saved: made
        // here, empty, so the watch has something to hold.
        std::filesystem::create_directories(folder, ec);
        if (!std::filesystem::is_directory(folder, ec)) return false;
        watcher = std::make_unique<UltraCanvasFolderWatcher>();
        native = watcher->Watch(PathToUtf8(folder), [this]() { ScheduleCheck(); });
        if (!native) watcher.reset();
        return native;
    }

    void UltraCanvasSettingsWatcher::Stop() {
        if (watcher) watcher->Stop();
        watcher.reset();
        native = false;
    }

    void UltraCanvasSettingsWatcher::ScheduleCheck() {
        // On the watcher's thread: one look per burst, on the UI thread.
        if (!post) return;
        if (checkPending.exchange(true)) return;
        post([this]() {
            checkPending = false;
            Poll();
        });
    }

    void UltraCanvasSettingsWatcher::Poll() {
        if (folder.empty()) return;
        for (Entry& entry : entries) {
            const bool wasThere = entry.exists;
            const auto modified = entry.modified;
            const auto size = entry.size;
            Remember(entry);
            if (entry.exists != wasThere || entry.modified != modified || entry.size != size) {
                if (entry.handler) entry.handler();
            }
        }
    }

} // namespace UltraCanvas
