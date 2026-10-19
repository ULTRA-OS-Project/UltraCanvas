// Apps/UltraSocial/engine/UltraSocialPaths.cpp
// Version: 0.1.1 - the rule and the move come from the framework's UltraCanvasDataFolder.h
// Version: 0.1.0 - the per-platform application data folder
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialPaths.h"

#include "UltraCanvasDataFolder.h"   // DataFolderRootFor / AdoptLegacyDataFolder
#include "UltraCanvasPathUtf8.h"     // PathFromUtf8 / PathToUtf8 / GetEnvUtf8

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraSocial {

namespace {

constexpr const char* kFolderName = "UltraSocial";

// SQLite's files beside a database: a rollback journal left by a crash, or
// the write-ahead log and its index.
constexpr const char* kJournalSuffixes[] = {"-journal", "-wal", "-shm"};

} // namespace

std::string DatabasePath(const std::string& dataDir) {
    return PathToUtf8(PathFromUtf8(dataDir) / kDatabaseFileName);
}

DataDirPlatform CurrentDataDirPlatform() {
    return UltraCanvas::CurrentDataFolderPlatform();
}

std::string ResolveDataDir(DataDirPlatform platform, const std::string& xdgDataHome,
                           const std::string& appData, const std::string& home) {
    const fs::path root = UltraCanvas::DataFolderRootFor(platform, xdgDataHome, appData, home);
    if (root.empty()) return std::string();
    return PathToUtf8(root / kFolderName);
}

std::string DefaultDataDir() {
    using UltraCanvas::GetEnvUtf8;
    return ResolveDataDir(CurrentDataDirPlatform(), GetEnvUtf8("XDG_DATA_HOME"),
                          GetEnvUtf8("APPDATA"), GetEnvUtf8("HOME"));
}

std::vector<std::string> LegacyDataDirs(DataDirPlatform platform, const std::string& home,
                                        const std::string& currentDir,
                                        const std::string& executableDir) {
    // ~/.local/share/UltraSocial on macOS and Windows; nothing on Linux.
    std::vector<std::string> dirs = UltraCanvas::LegacyUnixDataFolders(platform, home, kFolderName);
    if (platform == DataDirPlatform::Windows) {
        // Without HOME (the usual case) 0.1.x wrote to "./UltraSocial": the
        // working directory, which an Explorer start makes the executable's.
        if (!currentDir.empty())
            dirs.push_back(PathToUtf8(PathFromUtf8(currentDir) / kFolderName));
        if (!executableDir.empty())
            dirs.push_back(PathToUtf8(PathFromUtf8(executableDir) / kFolderName));
    }
    return dirs;
}

std::string AdoptLegacyDataDir(const std::string& dataDir,
                               const std::vector<std::string>& candidates,
                               std::string& error) {
    // Under either name: a folder 0.1.x wrote has social.db.
    return UltraCanvas::AdoptLegacyDataFolder(dataDir, candidates,
                                              {kDatabaseFileName, kLegacyDatabaseFileName}, error);
}

bool RenameLegacyDatabase(const std::string& dataDir, std::string& error) {
    error.clear();
    const fs::path directory = PathFromUtf8(dataDir);
    const fs::path legacy = directory / kLegacyDatabaseFileName;
    const fs::path current = directory / kDatabaseFileName;
    std::error_code ec;
    if (!fs::is_regular_file(legacy, ec) || fs::exists(current, ec)) return false;

    // The journal files first: renamed without them, the database would lose
    // commits still in its write-ahead log, or the rollback a transaction cut
    // short by a crash needs.
    for (const char* suffix : kJournalSuffixes) {
        const fs::path from = directory / PathFromUtf8(std::string(kLegacyDatabaseFileName) + suffix);
        if (!fs::exists(from, ec)) continue;
        fs::rename(from, directory / PathFromUtf8(std::string(kDatabaseFileName) + suffix), ec);
        if (ec) {
            error = "could not rename " + PathToUtf8(from) + ": " + ec.message();
            return false;
        }
    }
    fs::rename(legacy, current, ec);
    if (ec) {
        error = "could not rename " + PathToUtf8(legacy) + " to " + kDatabaseFileName +
                ": " + ec.message();
        return false;
    }
    return true;
}

bool PrepareDataDir(const std::string& dataDir, std::string& error) {
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
#endif
    return true;
}

} // namespace UltraSocial
