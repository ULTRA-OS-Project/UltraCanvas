// Apps/UltraFiler/UltraFilerRemotePath.h
// The path scheme of a remote drive - "ultracloud://<accountId><remote path>" -
// and the timestamps a remote listing carries.
//
// A remote drive is an UltraCloud account - an FTP / SFTP server, a Nextcloud,
// a WebDAV share - carried in UltraFiler as a place you can browse. Its paths
// have to travel through everything that already moves folder paths around
// (the folder display, the tree, the breadcrumb, the history), so they are
// spelled as one string rather than a pair, with a scheme no local path can
// collide with.
//
// Kept apart from the rest, dependency-free and header-only, for the reason
// UltraFilerVolumeSpace.h is: the parsing is worth testing on its own, and a
// test of it should not need the framework, let alone a network module.
//
//   ultracloud://ftp-files-example-org/           the drive's root
//   ultracloud://ftp-files-example-org/Docs       a folder or a file in it
//
// The account id is an UltraCloud slug and never contains '/', which is what
// makes the split unambiguous.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#pragma once

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

namespace UltraCanvas {

// The scheme every remote-drive path starts with.
inline const char* RemoteFilerScheme() { return "ultracloud://"; }

// Is this one of our remote-drive paths? Cheap enough to ask per entry, and
// deliberately strict: the scheme alone is not a path, so it answers false.
inline bool IsRemoteFilerPath(const std::string& path) {
    const std::string scheme = RemoteFilerScheme();
    return path.size() > scheme.size() &&
           path.compare(0, scheme.size(), scheme) == 0;
}

// Builds the path of `remotePath` on `accountId`. `remotePath` is normalised
// to a leading '/' and no trailing one, so the root is "…/" and everything
// else has no trailing separator - the same shape a local path has, and what
// keeps two spellings of one folder from looking like two folders to the
// history and the prefetch cache.
inline std::string MakeRemoteFilerPath(const std::string& accountId,
                                       const std::string& remotePath) {
    if (accountId.empty()) return std::string();
    std::string p = remotePath;
    while (!p.empty() && p.back() == '/') p.pop_back();
    if (p.empty()) return RemoteFilerScheme() + accountId + "/";
    if (p.front() != '/') p.insert(p.begin(), '/');
    return RemoteFilerScheme() + accountId + p;
}

// Splits a remote path into its account id and the path within that account
// ("/" at the root). False - and both outputs untouched - when `path` is not
// a remote path or carries no account id.
inline bool SplitRemoteFilerPath(const std::string& path,
                                 std::string& accountId,
                                 std::string& remotePath) {
    if (!IsRemoteFilerPath(path)) return false;
    const std::string rest = path.substr(std::string(RemoteFilerScheme()).size());
    const std::string::size_type slash = rest.find('/');
    const std::string id = slash == std::string::npos ? rest : rest.substr(0, slash);
    if (id.empty()) return false;
    accountId = id;
    if (slash == std::string::npos) {
        remotePath = "/";
    } else {
        std::string p = rest.substr(slash);
        while (p.size() > 1 && p.back() == '/') p.pop_back();
        remotePath = p.empty() ? "/" : p;
    }
    return true;
}

// The account id alone, "" when `path` is not a remote path.
inline std::string RemoteFilerAccountId(const std::string& path) {
    std::string id, remote;
    return SplitRemoteFilerPath(path, id, remote) ? id : std::string();
}

// True when `path` is a drive's own root rather than something inside it -
// the point at which "up" leaves the drive instead of climbing it.
inline bool IsRemoteFilerRoot(const std::string& path) {
    std::string id, remote;
    return SplitRemoteFilerPath(path, id, remote) && remote == "/";
}

// The folder containing `path`. Empty at a drive root (and for anything that
// is not a remote path): there is no remote parent above it, and the caller
// decides where "up" goes from there.
inline std::string RemoteFilerParent(const std::string& path) {
    std::string id, remote;
    if (!SplitRemoteFilerPath(path, id, remote)) return std::string();
    if (remote == "/") return std::string();
    const std::string::size_type slash = remote.find_last_of('/');
    if (slash == std::string::npos) return std::string();
    return MakeRemoteFilerPath(id, slash == 0 ? "/" : remote.substr(0, slash));
}

// The last segment of `path` - what an entry is called. The drive root has no
// name of its own, so that answers "" and the caller uses the drive's label.
inline std::string RemoteFilerName(const std::string& path) {
    std::string id, remote;
    if (!SplitRemoteFilerPath(path, id, remote) || remote == "/")
        return std::string();
    const std::string::size_type slash = remote.find_last_of('/');
    return slash == std::string::npos ? remote : remote.substr(slash + 1);
}

// Appends one child name to a remote folder path. The name is used as given -
// a listing's names are already the server's - so a caller must not pass a
// path here.
inline std::string RemoteFilerChild(const std::string& folderPath,
                                    const std::string& name) {
    std::string id, remote;
    if (!SplitRemoteFilerPath(folderPath, id, remote) || name.empty())
        return std::string();
    return MakeRemoteFilerPath(id, remote == "/" ? "/" + name
                                                 : remote + "/" + name);
}

// ===== THE TIME A REMOTE LISTING REPORTS =====
// UltraCloud hands a modification time on as the string the provider sent,
// because no two of them agree on a format; the file display needs a
// time_t. The two that actually turn up:
//
//   "20260917100000"                  FTP, from MLSD's modify= fact (UTC)
//   "Wed, 03 Sep 2026 10:00:00 GMT"   WebDAV, RFC 1123
//
// Anything else - including a server that reported nothing - answers 0, which
// the display already shows as a blank date rather than as 1970.
//
// Both forms are UTC, so both are read as UTC: timegm() where there is one,
// and the same arithmetic by hand on Windows, where there is not. Parsing
// them as local time would move every remote file by the viewer's offset.
//
// Lives beside the path scheme, and not in the drives implementation, for the
// same reason: it is worth testing without the framework or a network module.
inline std::time_t RemoteFilerTimeFromUtcParts(int year, int month, int day,
                                               int hour, int minute, int second) {
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 60) {
        return 0;
    }
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon  = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = minute;
    tm.tm_sec  = second;
    tm.tm_isdst = 0;
#if defined(_WIN32) || defined(_WIN64)
    const std::time_t t = _mkgmtime(&tm);
#else
    const std::time_t t = timegm(&tm);
#endif
    return t < 0 ? 0 : t;
}

// The month in an RFC 1123 date ("Sep"), 0 when it is not one of the twelve.
inline int RemoteFilerMonthFromName(const std::string& name) {
    static const char* kMonths[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    for (int i = 0; i < 12; ++i) {
        if (name == kMonths[i]) return i + 1;
    }
    return 0;
}

inline std::time_t ParseRemoteFilerTime(const std::string& text) {
    if (text.empty()) return 0;

    // MLSD: fourteen digits, YYYYMMDDHHMMSS. Some servers append a fractional
    // part ("20260917100000.123"), which is ignored.
    if (text.size() >= 14) {
        bool digits = true;
        for (std::size_t i = 0; i < 14; ++i) {
            if (text[i] < '0' || text[i] > '9') { digits = false; break; }
        }
        if (digits && (text.size() == 14 || text[14] == '.')) {
            const auto num = [&text](std::size_t at, std::size_t len) {
                return std::atoi(text.substr(at, len).c_str());
            };
            return RemoteFilerTimeFromUtcParts(num(0, 4), num(4, 2), num(6, 2),
                                               num(8, 2), num(10, 2), num(12, 2));
        }
    }

    // RFC 1123: "Wed, 03 Sep 2026 10:00:00 GMT". The weekday is not needed;
    // what follows it is day, month name, year, then the clock.
    const std::string::size_type comma = text.find(", ");
    const std::string rest = comma == std::string::npos ? text
                                                        : text.substr(comma + 2);
    int day = 0, year = 0, hour = 0, minute = 0, second = 0;
    char month[16] = {0};
    if (std::sscanf(rest.c_str(), "%d %15s %d %d:%d:%d",
                    &day, month, &year, &hour, &minute, &second) == 6) {
        const int m = RemoteFilerMonthFromName(month);
        if (m) return RemoteFilerTimeFromUtcParts(year, m, day, hour, minute, second);
    }
    return 0;
}

} // namespace UltraCanvas
