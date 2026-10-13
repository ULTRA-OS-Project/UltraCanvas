// Apps/UltraMail/engine/UltraMailPaths.cpp
// See UltraMailPaths.h.
// Version: 0.1.0 - the per-platform application data folder
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailPaths.h"

#include "UltraCanvasPathUtf8.h"   // PathFromUtf8 / PathToUtf8 / GetEnvUtf8

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace UltraMail {

namespace {

bool HoldsMailbox(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / kMailboxFileName, ec);
}

// The same folder, however it is spelt (a symlinked home, "./" components).
bool SameFolder(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::exists(a, ec) && fs::exists(b, ec) && fs::equivalent(a, b, ec)) return true;
    return a.lexically_normal() == b.lexically_normal();
}

} // namespace

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
    } else if (platform == DataDirPlatform::Windows && !appData.empty()) {
        root = PathFromUtf8(appData);
    } else if (!home.empty()) {
        // Windows without %APPDATA% keeps the folder it always had under HOME.
        root = platform == DataDirPlatform::MacOS
                   ? PathFromUtf8(home) / "Library" / "Application Support"
                   : PathFromUtf8(home) / ".local" / "share";
    }
    if (root.empty()) return std::string();
    return PathToUtf8(root / kDataFolderName);
}

std::string DefaultDataDir() {
    using UltraCanvas::GetEnvUtf8;
    return ResolveDataDir(CurrentDataDirPlatform(), GetEnvUtf8("XDG_DATA_HOME"),
                          GetEnvUtf8("APPDATA"), GetEnvUtf8("HOME"));
}

std::vector<std::string> LegacyDataDirs(DataDirPlatform platform, const std::string& home) {
    std::vector<std::string> dirs;
    if (platform != DataDirPlatform::MacOS || home.empty()) return dirs;
    dirs.push_back(PathToUtf8(PathFromUtf8(home) / ".local" / "share" / kDataFolderName));
    return dirs;
}

std::string AdoptLegacyDataDir(const std::string& dataDir,
                               const std::vector<std::string>& candidates,
                               std::string& error) {
    error.clear();
    if (dataDir.empty()) return std::string();
    const fs::path target = PathFromUtf8(dataDir);
    if (HoldsMailbox(target)) return std::string();   // already in place

    for (const auto& candidate : candidates) {
        const fs::path source = PathFromUtf8(candidate);
        if (candidate.empty() || SameFolder(source, target) || !HoldsMailbox(source))
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

} // namespace UltraMail
