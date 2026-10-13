// Apps/UltraMail/engine/UltraMailPaths.cpp
// See UltraMailPaths.h.
// Version: 0.1.1 - the rule and the move come from the framework's UltraCanvasDataFolder.h
// Version: 0.1.0 - the per-platform application data folder
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailPaths.h"

#include "UltraCanvasDataFolder.h"   // DataFolderRootFor / AdoptLegacyDataFolder
#include "UltraCanvasPathUtf8.h"     // PathToUtf8 / GetEnvUtf8

using UltraCanvas::PathToUtf8;

namespace UltraMail {

DataDirPlatform CurrentDataDirPlatform() {
    return UltraCanvas::CurrentDataFolderPlatform();
}

std::string ResolveDataDir(DataDirPlatform platform, const std::string& xdgDataHome,
                           const std::string& appData, const std::string& home) {
    const std::filesystem::path root =
        UltraCanvas::DataFolderRootFor(platform, xdgDataHome, appData, home);
    if (root.empty()) return std::string();
    return PathToUtf8(root / kDataFolderName);
}

std::string DefaultDataDir() {
    using UltraCanvas::GetEnvUtf8;
    return ResolveDataDir(CurrentDataDirPlatform(), GetEnvUtf8("XDG_DATA_HOME"),
                          GetEnvUtf8("APPDATA"), GetEnvUtf8("HOME"));
}

std::vector<std::string> LegacyDataDirs(DataDirPlatform platform, const std::string& home) {
    return UltraCanvas::LegacyUnixDataFolders(platform, home, kDataFolderName);
}

std::string AdoptLegacyDataDir(const std::string& dataDir,
                               const std::vector<std::string>& candidates,
                               std::string& error) {
    return UltraCanvas::AdoptLegacyDataFolder(dataDir, candidates, {kMailboxFileName}, error);
}

} // namespace UltraMail
