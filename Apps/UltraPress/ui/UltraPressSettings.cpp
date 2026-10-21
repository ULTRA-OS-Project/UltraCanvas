// Apps/UltraPress/ui/UltraPressSettings.cpp
// See UltraPressSettings.h.
// Version: 0.8.5 - over the framework's UltraCanvasAppSettings
// Version: 0.8.4
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressSettings.h"

#include "UltraCanvasAppSettings.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasSettingsFolder.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace UltraCanvas;

namespace UltraPress {

namespace {

const char* kSettingsFile = "settings.json";
const char* kOldLookFile = "look";   // 0.8.0 to 0.8.2
const size_t kKeep = 8;

// One spelling per folder, so "./site" and "site/" are the same entry:
// lexically_normal keeps a trailing separator, so it is dropped here too.
std::string Normal(const std::string& dir) {
    std::string normal = PathToUtf8(PathFromUtf8(dir).lexically_normal());
    while (normal.size() > 1 && (normal.back() == '/' || normal.back() == '\\')) normal.pop_back();
    return normal;
}

UltraCanvasAppSettings FileIn(const std::string& dir) {
    return UltraCanvasAppSettings::AtFile(PathToUtf8(PathFromUtf8(dir) / kSettingsFile));
}

} // namespace

std::string UltraPressSettings::Directory() {
    const std::filesystem::path folder = UltraCanvasSettingsFolder();
    if (folder.empty()) return "";
    return PathToUtf8(folder / "UltraPress");
}

UltraPressSettings UltraPressSettings::Load(const std::string& dir) {
    UltraPressSettings s;
    if (dir.empty()) return s;
    UltraCanvasAppSettings file = FileIn(dir);
    if (!file.Load()) {
        // The look file of 0.8, until the settings are first written.
        std::ifstream look(PathFromUtf8(dir) / kOldLookFile);
        std::string word;
        if (look && (look >> word)) s.dark = word == "dark";
        return s;
    }
    s.dark = file.GetString("look", "light") == "dark";
    s.recentSites = file.GetList("recent");
    s.windowWidth = file.GetInt("window.width", 0);
    s.windowHeight = file.GetInt("window.height", 0);
    return s;
}

bool UltraPressSettings::Save(const std::string& dir) const {
    if (dir.empty()) return false;
    UltraCanvasAppSettings file = FileIn(dir);
    file.Set("look", dark ? "dark" : "light");
    file.Set("recent", recentSites);
    file.Set("window.width", windowWidth);
    file.Set("window.height", windowHeight);
    if (!file.Save()) return false;
    // The 0.8 file is in here now.
    std::error_code ec;
    std::filesystem::remove(PathFromUtf8(dir) / kOldLookFile, ec);
    return true;
}

void UltraPressSettings::RememberSite(const std::string& siteDir) {
    if (siteDir.empty()) return;
    const std::string site = Normal(siteDir);
    recentSites.erase(std::remove(recentSites.begin(), recentSites.end(), site), recentSites.end());
    recentSites.insert(recentSites.begin(), site);
    if (recentSites.size() > kKeep) recentSites.resize(kKeep);
}

void UltraPressSettings::ForgetSite(const std::string& siteDir) {
    const std::string site = Normal(siteDir);
    recentSites.erase(std::remove(recentSites.begin(), recentSites.end(), site), recentSites.end());
}

} // namespace UltraPress
