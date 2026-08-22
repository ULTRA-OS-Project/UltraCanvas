// Apps/UltraCleaner/engine/UltraCleanerSafety.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerSafety.h"

#include "UltraCleanerPaths.h"

#include <algorithm>
#include <filesystem>

namespace UltraCleaner {
namespace {

namespace fs = std::filesystem;

// How many separators a path must have before it is deep enough to consider.
// "/tmp" (one) is a root, "/tmp/foo" (two) is content.
size_t SeparatorDepth(const std::string& path) {
    return static_cast<size_t>(std::count(path.begin(), path.end(), '/'));
}

bool LooksAbsolute(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/') return true;
    // "C:/..." — a Windows drive-qualified path.
    return path.size() >= 3 && path[1] == ':' && path[2] == '/';
}

void AddIfNotEmpty(std::vector<std::string>& out, const std::string& path) {
    if (!path.empty()) out.push_back(NormalizeForCompare(path));
}

std::vector<std::string> BuildProtectedPaths() {
    std::vector<std::string> paths;

    // Filesystem and OS roots.
    for (const char* root : {
             "/", "/bin", "/boot", "/dev", "/etc", "/home", "/lib", "/lib64",
             "/opt", "/proc", "/root", "/run", "/sbin", "/srv", "/sys", "/usr",
             "/var", "/Applications", "/Library", "/System", "/Users",
             "/Volumes", "/private", "/Network" }) {
        AddIfNotEmpty(paths, root);
    }
    for (const char* root : {
             "C:/", "C:/Windows", "C:/Program Files", "C:/Program Files (x86)",
             "C:/ProgramData", "C:/Users" }) {
        AddIfNotEmpty(paths, root);
    }

    // The user's own home and the directories inside it that hold work, not
    // junk. Each is protected as a directory; files *inside* them are still
    // reachable when a rule names them (the Downloads installer rule does).
    const std::string home = HomeDir();
    if (!home.empty()) {
        AddIfNotEmpty(paths, home);
        for (const char* leaf : {
                 "Desktop", "Documents", "Downloads", "Pictures", "Music",
                 "Movies", "Videos", "Public", "Templates", "Projects",
                 "OneDrive", "Dropbox", "Nextcloud", "iCloud Drive",
                 ".ssh", ".gnupg", ".pki", ".password-store", ".aws", ".kube",
                 ".config", ".local", ".local/share", ".var", ".mozilla",
                 ".thunderbird", "Library", "Library/Application Support",
                 "Library/Preferences", "Library/Keychains", "Library/Mail",
                 "Library/Messages", "Library/Photos", "Library/Containers",
                 "Library/Developer", "Library/Developer/Xcode",
                 "AppData", "AppData/Local", "AppData/Roaming",
                 ".npm", ".cargo", ".gradle", ".m2", ".nuget", "go", "go/pkg" }) {
            AddIfNotEmpty(paths, home + "/" + leaf);
        }
    }

    // The cache/data/state roots themselves: rules clear their contents, so
    // the roots must survive.
    AddIfNotEmpty(paths, UserCacheDir());
    AddIfNotEmpty(paths, UserDataDir());
    AddIfNotEmpty(paths, UserStateDir());
    AddIfNotEmpty(paths, UserTempDir());
    AddIfNotEmpty(paths, SystemTempDir());
    AddIfNotEmpty(paths, LocalAppDataDir());
    AddIfNotEmpty(paths, RoamingAppDataDir());
    AddIfNotEmpty(paths, DownloadsDir());
    AddIfNotEmpty(paths, WindowsDir());
    AddIfNotEmpty(paths, TrashDir());

    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

// Resolve symlinks as far as the filesystem allows. weakly_canonical copes
// with a path whose tail does not exist, which is the normal case when a
// scan result is re-checked after something else removed it.
std::string ResolveSymlinks(const std::string& path) {
    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(fs::path(path), ec);
    if (ec) return NormalizeForCompare(path);
    return NormalizeForCompare(resolved.string());
}

} // namespace

const std::vector<std::string>& ProtectedPaths() {
    static const std::vector<std::string> paths = BuildProtectedPaths();
    return paths;
}

bool IsProtectedPath(const std::string& path) {
    const std::string candidate = NormalizeForCompare(path);
    if (candidate.empty()) return true;
    for (const auto& protectedPath : ProtectedPaths()) {
        // Equal, or the candidate contains something protected: both mean
        // removing the candidate would take the protected location with it.
        if (candidate == protectedPath) return true;
        if (IsPathInside(protectedPath, candidate)) return true;
    }
    return false;
}

void PathGuard::AllowRoot(const std::string& root) {
    if (root.empty()) return;
    std::error_code ec;
    std::string normalized = NormalizeForCompare(root);
    if (fs::exists(fs::path(root), ec) && !ec) {
        normalized = ResolveSymlinks(root);
    }
    if (std::find(roots_.begin(), roots_.end(), normalized) == roots_.end()) {
        roots_.push_back(normalized);
    }
}

void PathGuard::Reset() {
    roots_.clear();
}

bool PathGuard::IsInsideAllowedRoot(const std::string& path) const {
    const std::string candidate = NormalizeForCompare(path);
    for (const auto& root : roots_) {
        // Strictly inside: a root is a container, never a target.
        if (candidate != root && IsPathInside(candidate, root)) return true;
    }
    return false;
}

SafetyCheck PathGuard::Check(const std::string& path) const {
    const std::string candidate = NormalizeForCompare(path);

    if (!LooksAbsolute(candidate)) {
        return { SafetyVerdict::NotAbsolute,
                 "not an absolute path: " + path };
    }
    if (SeparatorDepth(candidate) < 2) {
        return { SafetyVerdict::TooShallow,
                 "too close to the filesystem root: " + path };
    }
    if (!IsInsideAllowedRoot(candidate)) {
        return { SafetyVerdict::OutsideAllowedRoot,
                 "outside every location the rules allow: " + path };
    }
    if (IsProtectedPath(candidate)) {
        return { SafetyVerdict::ProtectedLocation,
                 "protected location: " + path };
    }

    // Follow the symlinks in the parent chain. The candidate itself may be a
    // link — removing a link removes the link — but its *parents* must not
    // lead out of the allowed roots, or a planted link would redirect the
    // delete somewhere the rules never named.
    std::error_code ec;
    const fs::path parent = fs::path(candidate).parent_path();
    if (!parent.empty()) {
        const std::string resolvedParent = ResolveSymlinks(parent.string());
        bool parentInside = false;
        for (const auto& root : roots_) {
            if (resolvedParent == root || IsPathInside(resolvedParent, root)) {
                parentInside = true;
                break;
            }
        }
        if (!parentInside) {
            return { SafetyVerdict::SymlinkEscape,
                     "a link in the path leads outside the allowed roots: " + path };
        }
        const std::string resolvedCandidate =
            resolvedParent + "/" + fs::path(candidate).filename().string();
        if (IsProtectedPath(resolvedCandidate)) {
            return { SafetyVerdict::ProtectedLocation,
                     "resolves to a protected location: " + path };
        }
    }

    // Sockets, fifos and device nodes are never junk, and unlinking one can
    // take a running session with it.
    const fs::file_status status = fs::symlink_status(fs::path(path), ec);
    if (!ec && fs::exists(status)) {
        const fs::file_type type = status.type();
        if (type != fs::file_type::regular &&
            type != fs::file_type::directory &&
            type != fs::file_type::symlink) {
            return { SafetyVerdict::SpecialFile,
                     "not a regular file or directory: " + path };
        }
    }

    return { SafetyVerdict::Allowed, {} };
}

} // namespace UltraCleaner
