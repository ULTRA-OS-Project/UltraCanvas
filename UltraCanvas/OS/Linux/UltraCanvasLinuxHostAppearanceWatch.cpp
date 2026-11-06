// OS/Linux/UltraCanvasLinuxHostAppearanceWatch.cpp
// The config folders the desktops write their look to, watched
// (UltraCanvasLinuxHostAppearanceWatch.h).
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#include "UltraCanvasLinuxHostAppearanceWatch.h"
#include "UltraCanvasLinuxDesktop.h"
#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <system_error>

namespace UltraCanvas {

int LinuxHostAppearanceWatch::Start(std::function<void()> onChanged) {
    Stop();
    if (!onChanged || !UltraCanvasFolderWatcher::NativeBackendAvailable()) return 0;
    const std::filesystem::path home = LinuxDesktop::ConfigHome();
    if (home.empty()) return 0;
    // ~/.config itself for kdeglobals; dconf/ for GSettings' database; the
    // GTK folders for settings.ini. A watch is not recursive, so each is its own.
    const std::filesystem::path folders[] = {home, home / "dconf", home / "gtk-4.0", home / "gtk-3.0"};
    int started = 0;
    for (const std::filesystem::path& folder : folders) {
        std::error_code ec;
        if (!std::filesystem::is_directory(folder, ec)) continue;
        auto watcher = std::make_unique<UltraCanvasFolderWatcher>();
        if (watcher->Watch(PathToUtf8(folder), onChanged)) {
            watchers.push_back(std::move(watcher));
            ++started;
        }
    }
    return started;
}

void LinuxHostAppearanceWatch::Stop() {
    for (auto& watcher : watchers) {
        if (watcher) watcher->Stop();
    }
    watchers.clear();
}

}  // namespace UltraCanvas
