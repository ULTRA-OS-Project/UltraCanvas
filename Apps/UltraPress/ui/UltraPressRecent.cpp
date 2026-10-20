// Apps/UltraPress/ui/UltraPressRecent.cpp
// See UltraPressRecent.h.
// Version: 0.8.3 - over the framework's UltraCanvasRecentList.h
// Version: 0.8.2
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressRecent.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasRecentList.h"

#include <filesystem>

using namespace UltraCanvas;

namespace UltraPress {

namespace {

const char* kRecentFile = "recent";

std::string RecentFile(const std::string& settingsDir) {
    return settingsDir.empty() ? std::string() : PathToUtf8(PathFromUtf8(settingsDir) / kRecentFile);
}

// One spelling per folder, so "./site" and "site/" are the same entry:
// lexically_normal keeps a trailing separator, so it is dropped here too.
std::string Normal(const std::string& dir) {
    std::string normal = PathToUtf8(PathFromUtf8(dir).lexically_normal());
    while (normal.size() > 1 && (normal.back() == '/' || normal.back() == '\\')) normal.pop_back();
    return normal;
}

} // namespace

std::vector<std::string> LoadRecentSites(const std::string& settingsDir) {
    return LoadRecentList(RecentFile(settingsDir));
}

bool RememberSite(const std::string& settingsDir, const std::string& siteDir) {
    if (siteDir.empty()) return false;
    return RememberRecent(RecentFile(settingsDir), Normal(siteDir));
}

bool ForgetSite(const std::string& settingsDir, const std::string& siteDir) {
    return ForgetRecent(RecentFile(settingsDir), Normal(siteDir));
}

} // namespace UltraPress
