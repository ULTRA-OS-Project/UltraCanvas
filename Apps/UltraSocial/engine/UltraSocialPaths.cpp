// Apps/UltraSocial/engine/UltraSocialPaths.cpp
// Version: 0.1.0 - the per-platform application data folder
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSocialPaths.h"

#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8 / GetEnvUtf8

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

// Under either name: a folder 0.1.x wrote has social.db.
bool HoldsDatabase(const fs::path& directory) {
    std::error_code ec;
    return fs::is_regular_file(directory / kDatabaseFileName, ec) ||
           fs::is_regular_file(directory / kLegacyDatabaseFileName, ec);
}

// The same folder, however it is spelled ("a/b", "a/./b", a symlink).
bool SameFolder(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::equivalent(a, b, ec)) return true;
    return a.lexically_normal() == b.lexically_normal();
}

} // namespace

std::string DatabasePath(const std::string& dataDir) {
    return PathToUtf8(PathFromUtf8(dataDir) / kDatabaseFileName);
}

DataDirPlatform CurrentDataDirPlatform() {
#if defined(_WIN32)
    return DataDirPlatform::Windows;
#elif defined(__APPLE__)
    return DataDirPlatform::MacOS;
#else
    return DataDirPlatform::Unix;
#endif
}

std::string ResolveDataDir(DataDirPlatform platform, const std::string& xdgDataHome,
                           const std::string& appData, const std::string& home) {
    fs::path root;
    if (!xdgDataHome.empty()) {
        root = PathFromUtf8(xdgDataHome);
    } else if (platform == DataDirPlatform::Windows) {
        if (!appData.empty()) root = PathFromUtf8(appData);
        else if (!home.empty()) root = PathFromUtf8(home) / "AppData" / "Roaming";
    } else if (!home.empty()) {
        root = platform == DataDirPlatform::MacOS
                   ? PathFromUtf8(home) / "Library" / "Application Support"
                   : PathFromUtf8(home) / ".local" / "share";
    }
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
    std::vector<std::string> dirs;
    if (platform == DataDirPlatform::Unix) return dirs;   // the folder did not move
    if (!home.empty())
        dirs.push_back(PathToUtf8(PathFromUtf8(home) / ".local" / "share" / kFolderName));
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
    error.clear();
    if (dataDir.empty()) return std::string();
    const fs::path target = PathFromUtf8(dataDir);
    if (HoldsDatabase(target)) return std::string();   // already in place

    for (const auto& candidate : candidates) {
        const fs::path source = PathFromUtf8(candidate);
        if (candidate.empty() || SameFolder(source, target) || !HoldsDatabase(source))
            continue;

        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        // An empty folder in the way (made by hand, or by an installer) is
        // replaced; anything else in it is kept and the data copied beside.
        if (fs::is_directory(target, ec) && fs::is_empty(target, ec)) fs::remove(target, ec);

        ec.clear();
        if (!fs::exists(target, ec)) {
            fs::rename(source, target, ec);
            if (!ec) return candidate;
        }

        // Another drive, or the target already has files: copy, then remove.
        ec.clear();
        fs::create_directories(target, ec);
        fs::copy(source, target,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "could not move " + candidate + " to " + dataDir + ": " + ec.message();
            return std::string();
        }
        fs::remove_all(source, ec);   // the copy is complete; a leftover is harmless
        return candidate;
    }
    return std::string();
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
