// OS/Linux/UltraCanvasLinuxHostAppearanceWatch.h
// INTERNAL: hears the desktop's look change on Linux through the files the
// desktops write it to - ~/.config/dconf/user (GSettings, and the portal on
// GNOME), ~/.config/kdeglobals (KDE), gtk-4.0/ and gtk-3.0/settings.ini - with
// one UltraCanvasFolderWatcher per folder, and calls back from the watchers'
// threads: the receiver posts to the UI thread
// (ScheduleHostAppearanceRefresh, which coalesces the several writes one
// change makes). Shared by the X11 and the Wayland backends; the X11 backend
// adds XSETTINGS' PropertyNotify on its own. Not a public header.
// Version: 1.0.0
// Last Modified: 2026-11-06
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasFolderWatcher.h"

#include <functional>
#include <memory>
#include <vector>

namespace UltraCanvas {

    class LinuxHostAppearanceWatch {
    public:
        LinuxHostAppearanceWatch() = default;
        ~LinuxHostAppearanceWatch() { Stop(); }
        LinuxHostAppearanceWatch(const LinuxHostAppearanceWatch&) = delete;
        LinuxHostAppearanceWatch& operator=(const LinuxHostAppearanceWatch&) = delete;

        // Watches the folders that exist; `onChanged` runs on a watcher's
        // thread on any change in them. How many folders are watched.
        int Start(std::function<void()> onChanged);
        void Stop();

    private:
        std::vector<std::unique_ptr<UltraCanvasFolderWatcher>> watchers;
    };

} // namespace UltraCanvas
