// Apps/UltraPress/ui/UltraPressRecent.h
// The sites opened last, for the start window to offer: the framework's
// recent list (UltraCanvasRecentList.h) in <settings folder>/UltraPress/
// recent, one folder per line, the last opened first, at most eight. A
// folder is remembered when the editor opens it and forgotten when the start
// window finds no site in it any more; its spelling is normalised here, so
// "site/" and "./site" are one entry.
// Version: 0.8.3 - over the framework's UltraCanvasRecentList.h
// Version: 0.8.2
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace UltraPress {

// The folders, last opened first; empty when nothing was opened yet or the
// folder cannot be read.
std::vector<std::string> LoadRecentSites(const std::string& settingsDir);
// Puts `siteDir` first (once), keeps the eight newest; false when the folder
// cannot be written.
bool RememberSite(const std::string& settingsDir, const std::string& siteDir);
// Drops `siteDir`; false when the folder cannot be written.
bool ForgetSite(const std::string& settingsDir, const std::string& siteDir);

} // namespace UltraPress
