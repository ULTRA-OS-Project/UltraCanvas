// Apps/UltraPress/ui/UltraPressSettings.h
// The program's own settings, in one file: <settings folder>/UltraPress/
// settings.json - the look (light or dark), the site folders opened last
// (the last first, eight at most) and the editor window's size. The place
// for the next preference too. UltraPress 0.8 kept the look and the list
// in two files beside it (`look`, `recent`); a folder that has those and
// no settings.json is read from them once, and they go when the settings
// are first written.
// Version: 0.8.4
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace UltraPress {

struct UltraPressSettings {
    bool dark = false;
    std::vector<std::string> recentSites;   // last opened first
    int windowWidth = 0;                    // 0: the program's default
    int windowHeight = 0;

    // <settings folder>/UltraPress; empty without a home.
    static std::string Directory();
    // The settings in `dir`, the defaults where the file says nothing or
    // there is none (the 0.8 files adopted then).
    static UltraPressSettings Load(const std::string& dir);
    // Writes settings.json in `dir` (made first); false when it cannot be.
    bool Save(const std::string& dir) const;

    // Puts `siteDir` first in the recent list (once, its spelling
    // normalised), keeps the eight newest; drops it.
    void RememberSite(const std::string& siteDir);
    void ForgetSite(const std::string& siteDir);
};

} // namespace UltraPress
