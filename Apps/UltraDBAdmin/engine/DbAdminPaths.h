// Apps/UltraDBAdmin/engine/DbAdminPaths.h
// Where UltraDBAdmin keeps its own files: the connection list, the vault that
// holds their passwords, the SQL history. One directory per user, readable by
// that user alone.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>

namespace UltraDBAdmin {

// The data directory (created if missing): %LOCALAPPDATA%\UltraDBAdmin,
// ~/Library/Application Support/UltraDBAdmin, or $XDG_DATA_HOME/UltraDBAdmin
// (~/.local/share/UltraDBAdmin). UTF-8. Empty when no home can be found.
std::string DataDirectory();

} // namespace UltraDBAdmin
