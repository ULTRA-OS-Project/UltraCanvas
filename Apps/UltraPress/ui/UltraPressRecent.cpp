// Apps/UltraPress/ui/UltraPressRecent.cpp
// See UltraPressRecent.h.
// Version: 0.8.2
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressRecent.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace UltraCanvas;

namespace UltraPress {

namespace {

const char* kRecentFile = "recent";
const size_t kKeep = 8;

// One spelling per folder, so "./site" and "site/" are the same entry:
// lexically_normal keeps a trailing separator, so it is dropped here too.
std::string Normal(const std::string& dir) {
    std::string normal = PathToUtf8(PathFromUtf8(dir).lexically_normal());
    while (normal.size() > 1 && (normal.back() == '/' || normal.back() == '\\')) normal.pop_back();
    return normal;
}

bool Save(const std::string& settingsDir, const std::vector<std::string>& dirs) {
    if (settingsDir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(PathFromUtf8(settingsDir), ec);
    std::ofstream out(PathFromUtf8(settingsDir) / kRecentFile, std::ios::trunc);
    if (!out) return false;
    for (const std::string& dir : dirs) out << dir << '\n';
    return static_cast<bool>(out);
}

} // namespace

std::vector<std::string> LoadRecentSites(const std::string& settingsDir) {
    std::vector<std::string> dirs;
    if (settingsDir.empty()) return dirs;
    std::ifstream in(PathFromUtf8(settingsDir) / kRecentFile);
    std::string line;
    while (in && std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty() && std::find(dirs.begin(), dirs.end(), line) == dirs.end()) dirs.push_back(line);
    }
    return dirs;
}

bool RememberSite(const std::string& settingsDir, const std::string& siteDir) {
    if (settingsDir.empty() || siteDir.empty()) return false;
    const std::string dir = Normal(siteDir);
    std::vector<std::string> dirs = LoadRecentSites(settingsDir);
    dirs.erase(std::remove(dirs.begin(), dirs.end(), dir), dirs.end());
    dirs.insert(dirs.begin(), dir);
    if (dirs.size() > kKeep) dirs.resize(kKeep);
    return Save(settingsDir, dirs);
}

bool ForgetSite(const std::string& settingsDir, const std::string& siteDir) {
    if (settingsDir.empty()) return false;
    const std::string dir = Normal(siteDir);
    std::vector<std::string> dirs = LoadRecentSites(settingsDir);
    dirs.erase(std::remove(dirs.begin(), dirs.end(), dir), dirs.end());
    return Save(settingsDir, dirs);
}

} // namespace UltraPress
