// Apps/UltraStore/core/Launcher.h
// The entry in the system's application menu that starts an installed
// program, per user:
//   Linux    <XDG_DATA_HOME or ~/.local/share>/applications/<id>.desktop
//   Windows  the Start menu's Programs folder, <name>.lnk
//   macOS    ~/Applications/<name>.app, a link to the bundle the program
//            file is in (a program outside a bundle gets none)
// The entry names the version folder itself, not `current` (which is a file
// where no symlink can be made), so it is written again after every update:
// WriteLauncher replaces what is there.
//
// Used for the store itself - the setup program writes it, the store
// refreshes it after it has updated itself. The same call serves any
// installed app when the menus are filled for all of them.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraStoreTypes.h"

#include <string>
#include <string_view>

namespace UltraStore {

struct LauncherEntry {
    std::string id;           // "org.ultraos.ultrastore": the entry's file name on Linux
    std::string name;         // "UltraStore": what the menu shows; the file name on Windows and macOS
    std::string comment;      // one line under the name, may be empty
    std::string executable;   // UTF-8, absolute: the program file
    std::string icon;         // UTF-8, absolute, may be empty (Linux: a PNG or SVG; Windows: an .ico or the program itself)
    std::string categories;   // Linux: "System;PackageManager;", may be empty
    std::string mimeTypes;    // Linux: "x-scheme-handler/ultrastore;" - the links the program opens, may be empty
    std::string windowClass;  // Linux: StartupWMClass, so the desktop ties the window to the entry; may be empty
    // The folder the entry is written into, for a test or a portable setup;
    // empty: the system's place above.
    std::string folderOverride;
};

// Where WriteLauncher puts the entry (UTF-8); empty when this system has
// no place for one, or the entry cannot have one (macOS, no bundle).
std::string LauncherPath(const LauncherEntry& entry);

// Writes or replaces the entry and returns its path. NotAvailable where
// there is nothing to write.
Result<std::string> WriteLauncher(const LauncherEntry& entry);

// Takes the entry away; fine when there is none.
Result<void> RemoveLauncher(const LauncherEntry& entry);

// The text of a Linux desktop entry for the program (every system: it is
// plain text, and the tests read it).
std::string DesktopEntryText(const LauncherEntry& entry);

} // namespace UltraStore
