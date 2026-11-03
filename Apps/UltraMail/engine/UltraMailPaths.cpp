// Apps/UltraMail/engine/UltraMailPaths.cpp
// See UltraMailPaths.h.
// Version: 0.2.0 - SecureDataDir
// Version: 0.1.1 - the rule and the move come from the framework's UltraCanvasDataFolder.h
// Version: 0.1.0 - the per-platform application data folder
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailPaths.h"

#include "UltraCanvasDataFolder.h"   // DataFolderRootFor / AdoptLegacyDataFolder
#include "UltraCanvasPathUtf8.h"     // PathToUtf8 / GetEnvUtf8

#include <filesystem>
#include <system_error>

using UltraCanvas::PathFromUtf8;
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

bool SecureDataDir(const std::string& dataDir, std::string& error) {
    namespace fs = std::filesystem;
    error.clear();
    const fs::path directory = PathFromUtf8(dataDir);
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec || !fs::is_directory(directory, ec)) {
        error = "cannot create " + dataDir + (ec ? ": " + ec.message() : std::string());
        return false;
    }
#if !defined(_WIN32)
    fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace, ec);
    if (ec) {
        error = "cannot make " + dataDir + " private: " + ec.message();
        return false;
    }
    // One level down is enough: nobody else can pass through the folder, and
    // a copy of one of these files or folders taken elsewhere keeps its mode.
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entryError;
        const fs::file_status status = it->symlink_status(entryError);
        if (entryError || fs::is_symlink(status)) continue;
        const fs::perms wanted = fs::is_directory(status) ? fs::perms::owner_all
                                                          : (fs::perms::owner_read | fs::perms::owner_write);
        if ((status.permissions() & fs::perms::all) != wanted)
            fs::permissions(it->path(), wanted, fs::perm_options::replace, entryError);
    }
#endif
    return true;
}

} // namespace UltraMail
