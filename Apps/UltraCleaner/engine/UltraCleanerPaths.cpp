// Apps/UltraCleaner/engine/UltraCleanerPaths.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerPaths.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

namespace UltraCleaner {
namespace {

std::string EnvOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return (value && *value) ? std::string(value) : std::string();
}

// Trailing separators make "inside" comparisons and joins awkward; strip them
// (but never turn "/" or "C:/" into "").
std::string StripTrailingSeparator(std::string path) {
    while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) {
        if (path.size() == 3 && path[1] == ':') break;   // "C:/"
        path.pop_back();
    }
    return path;
}

std::string ToForwardSlashes(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return StripTrailingSeparator(std::move(path));
}

bool IsDirectory(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec) && !ec;
}

} // namespace

std::string HomeDir() {
#if defined(_WIN32) || defined(_WIN64)
    std::string profile = EnvOrEmpty("USERPROFILE");
    if (profile.empty()) {
        const std::string drive = EnvOrEmpty("HOMEDRIVE");
        const std::string path  = EnvOrEmpty("HOMEPATH");
        if (!drive.empty() && !path.empty()) profile = drive + path;
    }
    return ToForwardSlashes(profile);
#else
    return ToForwardSlashes(EnvOrEmpty("HOME"));
#endif
}

std::string UserTempDir() {
#if defined(_WIN32) || defined(_WIN64)
    std::string temp = EnvOrEmpty("TEMP");
    if (temp.empty()) temp = EnvOrEmpty("TMP");
    return ToForwardSlashes(temp);
#else
    const std::string tmpdir = EnvOrEmpty("TMPDIR");
    if (!tmpdir.empty()) return ToForwardSlashes(tmpdir);
    // macOS gives every session a private TMPDIR under /var/folders; without
    // it in the environment there is nothing user-specific to clean, so fall
    // back to the shared temp directory.
    return "/tmp";
#endif
}

std::string SystemTempDir() {
#if defined(_WIN32) || defined(_WIN64)
    const std::string root = EnvOrEmpty("SystemRoot");
    if (root.empty()) return {};
    return ToForwardSlashes(root + "/Temp");
#elif defined(__APPLE__)
    return "/private/var/tmp";
#else
    return "/var/tmp";
#endif
}

std::string UserCacheDir() {
#if defined(_WIN32) || defined(_WIN64)
    return LocalAppDataDir();
#elif defined(__APPLE__)
    const std::string home = HomeDir();
    return home.empty() ? std::string() : home + "/Library/Caches";
#else
    const std::string xdg = EnvOrEmpty("XDG_CACHE_HOME");
    if (!xdg.empty()) return ToForwardSlashes(xdg);
    const std::string home = HomeDir();
    return home.empty() ? std::string() : home + "/.cache";
#endif
}

std::string UserDataDir() {
#if defined(_WIN32) || defined(_WIN64)
    return RoamingAppDataDir();
#elif defined(__APPLE__)
    const std::string home = HomeDir();
    return home.empty() ? std::string() : home + "/Library/Application Support";
#else
    const std::string xdg = EnvOrEmpty("XDG_DATA_HOME");
    if (!xdg.empty()) return ToForwardSlashes(xdg);
    const std::string home = HomeDir();
    return home.empty() ? std::string() : home + "/.local/share";
#endif
}

std::string UserStateDir() {
#if defined(_WIN32) || defined(_WIN64)
    return LocalAppDataDir();
#elif defined(__APPLE__)
    const std::string home = HomeDir();
    return home.empty() ? std::string() : home + "/Library/Logs";
#else
    const std::string xdg = EnvOrEmpty("XDG_STATE_HOME");
    if (!xdg.empty()) return ToForwardSlashes(xdg);
    const std::string home = HomeDir();
    return home.empty() ? std::string() : home + "/.local/state";
#endif
}

std::string LocalAppDataDir() {
#if defined(_WIN32) || defined(_WIN64)
    std::string local = EnvOrEmpty("LOCALAPPDATA");
    if (local.empty()) {
        const std::string home = HomeDir();
        if (!home.empty()) local = home + "/AppData/Local";
    }
    return ToForwardSlashes(local);
#else
    return {};
#endif
}

std::string RoamingAppDataDir() {
#if defined(_WIN32) || defined(_WIN64)
    std::string roaming = EnvOrEmpty("APPDATA");
    if (roaming.empty()) {
        const std::string home = HomeDir();
        if (!home.empty()) roaming = home + "/AppData/Roaming";
    }
    return ToForwardSlashes(roaming);
#else
    return {};
#endif
}

std::string DownloadsDir() {
    const std::string home = HomeDir();
    if (home.empty()) return {};
#if !defined(_WIN32) && !defined(_WIN64) && !defined(__APPLE__)
    // XDG user dirs can move Downloads; honour the environment override when
    // the session exports it.
    const std::string xdg = EnvOrEmpty("XDG_DOWNLOAD_DIR");
    if (!xdg.empty()) return ToForwardSlashes(xdg);
#endif
    return home + "/Downloads";
}

std::string WindowsDir() {
#if defined(_WIN32) || defined(_WIN64)
    return ToForwardSlashes(EnvOrEmpty("SystemRoot"));
#else
    return {};
#endif
}

std::string TrashDir() {
#if defined(_WIN32) || defined(_WIN64)
    // The recycle bin is not a plain directory — it is reached through the
    // shell, so there is no path to hand out here.
    return {};
#elif defined(__APPLE__)
    const std::string home = HomeDir();
    return home.empty() ? std::string() : home + "/.Trash";
#else
    const std::string data = UserDataDir();
    return data.empty() ? std::string() : data + "/Trash";
#endif
}

std::string ExpandTokens(const std::string& pattern) {
    std::string out;
    out.reserve(pattern.size() + 32);

    size_t index = 0;
    while (index < pattern.size()) {
        if (pattern[index] != '{') {
            out += pattern[index++];
            continue;
        }
        const size_t close = pattern.find('}', index);
        if (close == std::string::npos) {          // stray '{' — literal
            out += pattern.substr(index);
            break;
        }
        const std::string token = pattern.substr(index + 1, close - index - 1);
        std::string value;
        if      (token == "HOME")          value = HomeDir();
        else if (token == "TEMP")          value = UserTempDir();
        else if (token == "SYSTEMTEMP")    value = SystemTempDir();
        else if (token == "CACHE")         value = UserCacheDir();
        else if (token == "DATA")          value = UserDataDir();
        else if (token == "STATE")         value = UserStateDir();
        else if (token == "LOCALAPPDATA")  value = LocalAppDataDir();
        else if (token == "APPDATA")       value = RoamingAppDataDir();
        else if (token == "DOWNLOADS")     value = DownloadsDir();
        else if (token == "TRASH")         value = TrashDir();
        else if (token == "WINDIR")        value = WindowsDir();
        else return {};                            // unknown token

        if (value.empty()) return {};              // unresolved on this system
        out += value;
        index = close + 1;
    }
    return ToForwardSlashes(out);
}

namespace {

// Matches the single pattern element at `pi` — a literal, '?', or a
// '[...]' character class with ranges and '!'/'^' negation — against `ch`,
// and reports where the element ends. '*' is handled by the caller.
bool MatchGlobElement(const std::string& pattern, size_t pi, char ch,
                      size_t& nextPi) {
    if (pattern[pi] == '?') {
        nextPi = pi + 1;
        return true;
    }
    if (pattern[pi] != '[') {
        nextPi = pi + 1;
        return pattern[pi] == ch;
    }

    size_t i = pi + 1;
    bool negate = false;
    if (i < pattern.size() && (pattern[i] == '!' || pattern[i] == '^')) {
        negate = true;
        ++i;
    }
    bool matched = false;
    bool first = true;                     // a leading ']' is a literal
    while (i < pattern.size() && (pattern[i] != ']' || first)) {
        first = false;
        if (i + 2 < pattern.size() && pattern[i + 1] == '-' &&
            pattern[i + 2] != ']') {
            if (ch >= pattern[i] && ch <= pattern[i + 2]) matched = true;
            i += 3;
        } else {
            if (ch == pattern[i]) matched = true;
            ++i;
        }
    }
    if (i >= pattern.size()) {             // unterminated '[' — a literal
        nextPi = pi + 1;
        return ch == '[';
    }
    nextPi = i + 1;
    return matched != negate;
}

} // namespace

bool GlobMatch(const std::string& name, const std::string& pattern) {
    const std::string n = NormalizeForCompare(name);
    const std::string p = NormalizeForCompare(pattern);

    // Iterative backtracking matcher: it never recurses, so a pathological
    // pattern cannot blow the stack or take exponential time.
    size_t ni = 0, pi = 0;
    size_t starPi = std::string::npos, starNi = 0;
    while (ni < n.size()) {
        size_t nextPi = pi;
        if (pi < p.size() && p[pi] != '*' &&
            MatchGlobElement(p, pi, n[ni], nextPi)) {
            ++ni;
            pi = nextPi;
        } else if (pi < p.size() && p[pi] == '*') {
            starPi = pi++;
            starNi = ni;
        } else if (starPi != std::string::npos) {
            pi = starPi + 1;
            ni = ++starNi;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

std::vector<std::string> ExpandWildcardDirectories(const std::string& pattern) {
    std::vector<std::string> results;
    if (pattern.empty()) return results;

    const std::string normalized = ToForwardSlashes(pattern);
    if (normalized.find('*') == std::string::npos &&
        normalized.find('?') == std::string::npos) {
        results.push_back(normalized);
        return results;
    }

    // Walk segment by segment, carrying the set of directories that match
    // everything expanded so far. A rooted path starts from "/"; a Windows
    // path starts from its drive segment ("C:").
    std::string rest = normalized;
    std::vector<std::string> current;
    if (!rest.empty() && rest[0] == '/') {
        current.push_back("/");
        rest.erase(0, 1);
    } else {
        current.push_back("");
    }

    size_t start = 0;
    while (start <= rest.size()) {
        size_t slash = rest.find('/', start);
        if (slash == std::string::npos) slash = rest.size();
        const std::string segment = rest.substr(start, slash - start);
        start = slash + 1;
        if (segment.empty()) continue;

        std::vector<std::string> next;
        const bool wildcard = segment.find('*') != std::string::npos ||
                              segment.find('?') != std::string::npos;
        for (const auto& prefix : current) {
            if (!wildcard) {
                next.push_back(prefix.empty()      ? segment
                               : prefix == "/"     ? "/" + segment
                                                   : prefix + "/" + segment);
                continue;
            }
            if (prefix.empty()) continue;   // a relative pattern cannot start with '*'
            std::error_code ec;
            std::filesystem::directory_iterator iterator(
                prefix, std::filesystem::directory_options::skip_permission_denied, ec);
            if (ec) continue;
            for (const auto& entry : iterator) {
                std::error_code entryEc;
                if (!entry.is_directory(entryEc) || entryEc) continue;
                const std::string name = entry.path().filename().string();
                if (!GlobMatch(name, segment)) continue;
                next.push_back(ToForwardSlashes(entry.path().string()));
            }
        }
        current.swap(next);
        if (current.empty()) break;
    }

    for (auto& path : current) {
        if (!path.empty() && path != "/" && IsDirectory(path)) {
            results.push_back(path);
        }
    }
    return results;
}

std::string NormalizeForCompare(const std::string& path) {
    std::string out = ToForwardSlashes(path);
#if defined(_WIN32) || defined(_WIN64) || defined(__APPLE__)
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
    return out;
}

bool IsPathInside(const std::string& path, const std::string& ancestor) {
    if (ancestor.empty()) return false;
    const std::string p = NormalizeForCompare(path);
    const std::string a = NormalizeForCompare(ancestor);
    if (p == a) return true;
    if (p.size() <= a.size()) return false;
    if (p.compare(0, a.size(), a) != 0) return false;
    // "/home/user2" must not count as inside "/home/user".
    return a == "/" || p[a.size()] == '/';
}

} // namespace UltraCleaner
