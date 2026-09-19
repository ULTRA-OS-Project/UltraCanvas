// Apps/UltraNetMonitor/ui/UltraNetMonitorPaths.h
// Where the application keeps its activity store when the user does not
// say: the platform's per-user data directory, never a cache directory (a
// cache may be swept; a record of activity may not).
// Version: 0.3.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraNetMonitor {

// %LOCALAPPDATA%\UltraNetMonitor\activity.db on Windows,
// ~/Library/Application Support/UltraNetMonitor/activity.db on macOS,
// $XDG_DATA_HOME/UltraNetMonitor/activity.db (else ~/.local/share/...)
// elsewhere. The directory is created on demand. Empty when the platform
// offers nowhere writable.
std::string DefaultStorePath();

} // namespace UltraNetMonitor
