// Apps/UltraPassword/core/VaultLocation.cpp
// See VaultLocation.h.
// Version: 0.1.1 - the rule and the move come from the framework's UltraCanvasDataFolder.h
// Version: 0.1.0 - the per-platform application data folder
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "VaultLocation.h"

#include "UltraCanvasDataFolder.h"   // DataFolderRootFor / AdoptLegacyDataFolder
#include "UltraCanvasPathUtf8.h"     // PathFromUtf8 / PathToUtf8 / GetEnvUtf8

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
using UltraCanvas::GetEnvUtf8;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraPassword {

DataDirPlatform CurrentDataDirPlatform() {
    return UltraCanvas::CurrentDataFolderPlatform();
}

std::string ResolveDataDir(DataDirPlatform platform, const std::string& xdgDataHome,
                           const std::string& appData, const std::string& home) {
    const fs::path root = UltraCanvas::DataFolderRootFor(platform, xdgDataHome, appData, home);
    if (root.empty()) return std::string();
    return PathToUtf8(root / kDataFolderName);
}

std::vector<std::string> LegacyDataDirs(DataDirPlatform platform, const std::string& home) {
    return UltraCanvas::LegacyUnixDataFolders(platform, home, kDataFolderName);
}

std::string AdoptLegacyDataDir(const std::string& dataDir,
                               const std::vector<std::string>& candidates,
                               std::string& error) {
    return UltraCanvas::AdoptLegacyDataFolder(dataDir, candidates, {kVaultFileName}, error);
}

std::string VaultPathIn(const std::string& dataDir) {
    return PathToUtf8(PathFromUtf8(dataDir) / kVaultFileName);
}

std::string DefaultVaultPath(std::string* movedFrom, std::string* error) {
    if (movedFrom) movedFrom->clear();
    if (error) error->clear();
    const std::string home = GetEnvUtf8("HOME");
    std::string dir = ResolveDataDir(CurrentDataDirPlatform(), GetEnvUtf8("XDG_DATA_HOME"),
                                     GetEnvUtf8("APPDATA"), home);
    std::error_code ec;
    if (dir.empty()) dir = PathToUtf8(fs::current_path(ec) / kDataFolderName);

    std::string moveError;
    const std::string from =
        AdoptLegacyDataDir(dir, LegacyDataDirs(CurrentDataDirPlatform(), home), moveError);
    if (movedFrom) *movedFrom = from;
    if (error) *error = moveError;

    fs::create_directories(PathFromUtf8(dir), ec);   // best effort; Create reports real errors
    return VaultPathIn(dir);
}

} // namespace UltraPassword
