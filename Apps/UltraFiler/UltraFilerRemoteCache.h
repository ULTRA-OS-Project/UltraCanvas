// Apps/UltraFiler/UltraFilerRemoteCache.h
// The parts of the remote drives' listing cache that need no server and no
// UI: which drives prefetch their subfolders, which subfolders of a listing
// are worth fetching ahead, and the file the listings are kept in between
// runs so a drive opens on what it showed last time.
//
// Kept apart from UltraFilerRemoteDrives for the same reason the path scheme
// is: header-only and free of UltraCloud and of the filer widget, so
// Tests/FilerRemoteCacheTest.cpp can check it on any build.
//
// The file format is line based and versioned:
//
//     UltraFilerRemoteCache 1
//     L <tab> ultracloud://<account>/<folder>
//     E <tab> d|f <tab> size <tab> modified <tab> name <tab> path
//     ...
//
// Every field is escaped (backslash, tab, CR, LF), because an FTP server will
// happily list a file whose name holds a tab, and a name must never be able
// to start a record of its own. Anything that does not parse drops the
// listing it belongs to, not the file: a cache is an optimisation, and a bad
// line in it is a reason to ask the server again, never a reason to fail.
// Version: 1.0.0
// Last Modified: 2026-09-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerRemotePath.h"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <unordered_set>
#include <vector>

namespace UltraCanvas {

// One entry of a cached listing: what the display needs to draw it again,
// and nothing that depends on the drive's current capabilities (read-only is
// asked of the drive when the listing is loaded, not remembered).
struct RemoteCachedEntry {
    std::string name;
    std::string path;          // the full ultracloud:// path
    bool isDirectory = false;
    uint64_t size = 0;
    std::time_t modifiedTime = 0;
};

// One folder's listing, keyed by its ultracloud:// path.
struct RemoteCachedListing {
    std::string folderPath;
    std::vector<RemoteCachedEntry> entries;
};

// How many subfolders of one opened folder are fetched ahead. Every FTP
// listing is a login of its own, so a folder of a thousand subfolders must
// not become a thousand connections the moment it is opened: the first ones,
// in the order the server listed them (folders first, by name), are what the
// user is most likely to open next.
inline constexpr std::size_t kRemotePrefetchPerFolder = 24;

// How many listings are kept on disk. Enough for the folders of a working
// session on a few servers; a cache that grows without bound slows every
// start-up to save a round trip on folders nobody opens.
inline constexpr std::size_t kRemoteCacheMaxListings = 512;

inline constexpr const char* kRemoteCacheHeader = "UltraFilerRemoteCache 1";

// Whether a drive of this provider fetches the subfolders of an opened folder
// ahead of being asked, and keeps its listings between runs. FTP only: it is
// a server the user runs or rents, with no request quota, and each listing is
// slow enough (a login per request) to be worth hiding. The cloud providers
// meter API calls, and prefetching on them would spend the user's quota on
// folders they may never open.
inline bool RemoteProviderPrefetches(const std::string& providerId) {
    return providerId == "ftp";
}

// The subfolders of a freshly fetched listing that are worth fetching ahead:
// folders only, not hidden ones (the display does not show them unless asked,
// so they are unlikely to be opened next), not ones already cached or queued
// (`known`), at most `limit` of them, in listing order.
inline std::vector<std::string> SelectRemotePrefetchTargets(
        const std::vector<RemoteCachedEntry>& entries,
        const std::unordered_set<std::string>& known,
        std::size_t limit = kRemotePrefetchPerFolder) {
    std::vector<std::string> out;
    for (const RemoteCachedEntry& e : entries) {
        if (out.size() >= limit) break;
        if (!e.isDirectory || e.path.empty()) continue;
        if (IsHiddenRemoteFilerName(e.name)) continue;
        if (known.count(e.path)) continue;
        out.push_back(e.path);
    }
    return out;
}

// ---- The file -------------------------------------------------------------

inline std::string EscapeRemoteCacheField(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

// False for a dangling backslash or an escape this format never writes.
inline bool UnescapeRemoteCacheField(const std::string& s, std::string& out) {
    out.clear();
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\') { out.push_back(s[i]); continue; }
        if (++i >= s.size()) return false;
        switch (s[i]) {
            case '\\': out.push_back('\\'); break;
            case 't':  out.push_back('\t'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            default:   return false;
        }
    }
    return true;
}

// Splits one line at its tabs. The fields are escaped, so a tab here is
// always a separator.
inline std::vector<std::string> SplitRemoteCacheLine(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

// Digits only, and no overflow: these are sizes and times this file wrote,
// so anything else is damage. Parsed by hand rather than with std::stoull,
// which throws, skips leading blanks and accepts a sign.
inline bool ParseRemoteCacheNumber(const std::string& s, uint64_t& out) {
    if (s.empty() || s.size() > 20) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (v > (UINT64_MAX - digit) / 10) return false;
        v = v * 10 + digit;
    }
    out = v;
    return true;
}

inline std::string SerializeRemoteListings(
        const std::vector<RemoteCachedListing>& listings) {
    std::string out = kRemoteCacheHeader;
    out.push_back('\n');
    for (const RemoteCachedListing& l : listings) {
        if (!IsRemoteFilerPath(l.folderPath)) continue;
        out += "L\t" + EscapeRemoteCacheField(l.folderPath) + "\n";
        for (const RemoteCachedEntry& e : l.entries) {
            const uint64_t modified = e.modifiedTime > 0
                    ? static_cast<uint64_t>(e.modifiedTime) : 0;
            out += "E\t";
            out += e.isDirectory ? "d" : "f";
            out += "\t" + std::to_string(e.size);
            out += "\t" + std::to_string(modified);
            out += "\t" + EscapeRemoteCacheField(e.name);
            out += "\t" + EscapeRemoteCacheField(e.path);
            out += "\n";
        }
    }
    return out;
}

// Reads what SerializeRemoteListings wrote. False only when the text is not
// this file at all (another version, another file); a damaged listing inside
// an otherwise good file is dropped and the rest are kept.
inline bool ParseRemoteListings(const std::string& text,
                                std::vector<RemoteCachedListing>& out) {
    out.clear();
    std::size_t pos = 0;
    auto nextLine = [&](std::string& line) {
        if (pos >= text.size()) return false;
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = end + 1;
        return true;
    };

    std::string line;
    if (!nextLine(line) || line != kRemoteCacheHeader) return false;

    RemoteCachedListing current;
    bool haveCurrent = false;
    bool currentBroken = false;
    auto flush = [&]() {
        if (haveCurrent && !currentBroken) out.push_back(std::move(current));
        current = RemoteCachedListing{};
        haveCurrent = false;
        currentBroken = false;
    };

    while (nextLine(line)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = SplitRemoteCacheLine(line);
        if (f[0] == "L") {
            flush();
            std::string folder;
            if (f.size() != 2 || !UnescapeRemoteCacheField(f[1], folder) ||
                !IsRemoteFilerPath(folder)) {
                // Its entries have nowhere to go; skip them with it.
                haveCurrent = true;
                currentBroken = true;
                continue;
            }
            current.folderPath = folder;
            haveCurrent = true;
        } else if (f[0] == "E") {
            if (!haveCurrent) continue;   // an entry before any listing
            if (currentBroken) continue;
            RemoteCachedEntry e;
            uint64_t size = 0, modified = 0;
            if (f.size() != 6 || (f[1] != "d" && f[1] != "f") ||
                !ParseRemoteCacheNumber(f[2], size) ||
                !ParseRemoteCacheNumber(f[3], modified) ||
                !UnescapeRemoteCacheField(f[4], e.name) ||
                !UnescapeRemoteCacheField(f[5], e.path) ||
                e.name.empty() || !IsRemoteFilerPath(e.path)) {
                currentBroken = true;
                continue;
            }
            e.isDirectory = f[1] == "d";
            e.size = e.isDirectory ? 0 : size;
            e.modifiedTime = static_cast<std::time_t>(modified);
            current.entries.push_back(std::move(e));
        } else {
            // A record kind this version does not know: the listing it sits
            // in can no longer be trusted to be complete.
            if (haveCurrent) currentBroken = true;
        }
    }
    flush();
    return true;
}

} // namespace UltraCanvas
