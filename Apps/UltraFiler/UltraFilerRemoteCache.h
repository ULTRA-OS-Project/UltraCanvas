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
//
// Beside it, a second small file names the FTP uploads that stopped part way
// (RemoteUnfinishedUpload), so the part they left on the server can be
// removed the next time the drive is used.
// Version: 1.3.0 - RemoteUnfinishedUpload and its file
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerRemotePath.h"

#include <chrono>
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

// How many subfolders of one opened folder are fetched ahead. Each is a
// listing - a CWD, a data connection and a LIST on the connection the drive
// already has open - so a folder of a thousand subfolders must not become a
// thousand requests the moment it is opened: the first ones, in the order the
// server listed them (folders first, by name), are what the user is most
// likely to open next.
inline constexpr std::size_t kRemotePrefetchPerFolder = 24;

// How long the drive worker keeps an FTP connection open with nothing to do.
// The listings of one burst - a folder, its subfolders fetched ahead, the
// next folder opened while they come in - share one login; a drive left
// alone is let go. Short, because closing sends QUIT and waits for the reply:
// a connection whose network went away since (a laptop moved to another
// network) does not answer, and libcurl before 8.10 waits up to two minutes,
// with the next request - or the window closing - waiting behind it.
inline constexpr std::chrono::seconds kRemoteConnectionIdleClose{15};

// The longest wait a cloud drive sits out when the service limits requests
// and asks to come back later (UltraCloud::RateLimitPolicy). The drive has one
// worker, so a wait holds every job queued behind it; the status line says
// what it is waiting for. A longer wait is reported as the failure instead.
inline constexpr long long kRemoteRateLimitMaxWaitSeconds = 60;

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

// ---- Preview copies --------------------------------------------------------
// A picture on a drive cannot be handed to the media viewer: it reads files,
// and an ultracloud:// path is not one. So a file the user selects for the
// preview pane is downloaded into a cache of its own first, and the copy is
// what is shown. The copies are kept - a second look at the same file, in
// this run or the next, costs no transfer - and named so that a file that has
// changed on the server is fetched again rather than shown from before.

// The largest file fetched only to be previewed. Selecting a file is not
// asking to download it, and a 2 GB TIFF on a slow server would hold the
// drive's queue for minutes; that one is opened by downloading it.
inline constexpr uint64_t kRemotePreviewMaxBytes = 64ull * 1024 * 1024;

// The cache folder of one preview copy: a 64-bit FNV-1a of the remote path,
// the size and the modification time the listing reported, in hex. Any of
// them changing - the file replaced on the server - gives a new folder, and
// the old one simply ages out. FNV rather than std::hash because the name
// has to be the same in the next run and on every platform.
inline std::string RemotePreviewCacheKey(const std::string& remotePath,
                                         uint64_t size, std::time_t modified) {
    uint64_t h = 14695981039346656037ull;
    auto mix = [&h](const std::string& s) {
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        h ^= 0xff; h *= 1099511628211ull;   // a separator no byte can fake
    };
    mix(remotePath);
    mix(std::to_string(size));
    mix(std::to_string(static_cast<long long>(modified)));
    static const char* hex = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[i] = hex[h & 0xf]; h >>= 4; }
    return out;
}

// The copy's own file name: the remote name, made safe for any local
// filesystem - the viewer reads the format from the extension and shows the
// name, so both are kept. A server may list names Windows cannot store
// (a ':' or a '?'), or a name that is really a path ("..").
inline std::string RemotePreviewLocalName(const std::string& remoteName) {
    std::string out;
    out.reserve(remoteName.size());
    for (unsigned char c : remoteName) {
        const bool bad = c < 0x20 || c == '/' || c == '\\' || c == ':' ||
                         c == '*' || c == '?' || c == '"' || c == '<' ||
                         c == '>' || c == '|';
        out.push_back(bad ? '_' : static_cast<char>(c));
    }
    // Windows drops trailing dots and blanks, which would change the name
    // under us; and "." / ".." are not names at all.
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty()) out = "preview";
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

// ---- Uploads that stopped part way -----------------------------------------
// An FTP or SFTP server writes an upload into the file as it arrives, and has
// no way to take it back: an upload stopped part way - the window closed
// under it, the connection broke - leaves the part already sent on the
// server, under the file's own name, looking like a file. UltraFiler keeps a
// note of each one and removes the part the next time the drive is used, as
// long as what is there is still recognisably that part.

// Whether an upload stopped part way on a drive of this provider leaves the
// part it sent on the server. FTP, FTPS and SFTP do (the "ftp" provider);
// WebDAV, Nextcloud and the cloud services keep an incomplete upload apart
// from the file and drop it.
inline bool RemoteProviderLeavesPartialUploads(const std::string& providerId) {
    return providerId == "ftp";
}

struct RemoteUnfinishedUpload {
    std::string path;          // the ultracloud:// path of the file on the server
    std::string server;        // the drive's address when it was left
    uint64_t bytesSent = 0;    // what had gone out when it stopped
    uint64_t fileSize = 0;     // the whole local file
    int64_t stoppedAt = 0;     // when it stopped, seconds since the epoch
};

// How long a note is kept for a drive that is not used again. Past that, the
// file at that path is as likely to be something else by now.
inline constexpr int64_t kRemoteUnfinishedUploadMaxAgeSeconds = 30ll * 24 * 3600;

// What libcurl may have sent after its last progress report when a
// connection broke: one round of its upload buffer, with room to spare. The
// part on the server can be that much longer than bytesSent says.
inline constexpr uint64_t kRemoteUnfinishedUploadSlackBytes = 1024 * 1024;

// Whether a file of `sizeOnServer` bytes, found where the upload went, is
// the part it left: shorter than the whole file, and no longer than what had
// been sent. A file of any other size was put there since - finished by
// another program, or another file of that name - and is not touched.
inline bool IsLeftByUnfinishedUpload(const RemoteUnfinishedUpload& upload,
                                     uint64_t sizeOnServer) {
    return sizeOnServer < upload.fileSize &&
           sizeOnServer <= upload.bytesSent + kRemoteUnfinishedUploadSlackBytes;
}

// Adds `upload` to `list`, in place of an earlier note for the same path:
// one file has one part on the server, the latest upload's.
inline void NoteRemoteUnfinishedUpload(std::vector<RemoteUnfinishedUpload>& list,
                                       const RemoteUnfinishedUpload& upload) {
    for (RemoteUnfinishedUpload& u : list) {
        if (u.path != upload.path) continue;
        u = upload;
        return;
    }
    list.push_back(upload);
}

// The file, line based like the listings':
//
//     UltraFilerUnfinishedUploads 1
//     U <tab> sent <tab> size <tab> stopped-at <tab> server <tab> path
//
inline constexpr const char* kRemoteUnfinishedUploadsHeader = "UltraFilerUnfinishedUploads 1";

inline std::string SerializeUnfinishedUploads(
        const std::vector<RemoteUnfinishedUpload>& uploads) {
    std::string out = kRemoteUnfinishedUploadsHeader;
    out.push_back('\n');
    for (const RemoteUnfinishedUpload& u : uploads) {
        if (!IsRemoteFilerPath(u.path)) continue;
        out += "U\t" + std::to_string(u.bytesSent);
        out += "\t" + std::to_string(u.fileSize);
        out += "\t" + std::to_string(u.stoppedAt > 0 ? u.stoppedAt : 0);
        out += "\t" + EscapeRemoteCacheField(u.server);
        out += "\t" + EscapeRemoteCacheField(u.path);
        out += "\n";
    }
    return out;
}

// Reads what SerializeUnfinishedUploads wrote. False only when the text is
// not this file; a damaged line is dropped, and so is a note older than
// kRemoteUnfinishedUploadMaxAgeSeconds at `now`. A note that cannot be read
// costs a part left on a server, which is where things stood before.
inline bool ParseUnfinishedUploads(const std::string& text, int64_t now,
                                   std::vector<RemoteUnfinishedUpload>& out) {
    out.clear();
    std::size_t pos = 0;
    bool first = true;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) {
            if (line != kRemoteUnfinishedUploadsHeader) return false;
            first = false;
            continue;
        }
        const std::vector<std::string> f = SplitRemoteCacheLine(line);
        RemoteUnfinishedUpload u;
        uint64_t stoppedAt = 0;
        if (f.size() != 6 || f[0] != "U" ||
            !ParseRemoteCacheNumber(f[1], u.bytesSent) ||
            !ParseRemoteCacheNumber(f[2], u.fileSize) ||
            !ParseRemoteCacheNumber(f[3], stoppedAt) ||
            stoppedAt > static_cast<uint64_t>(INT64_MAX) ||
            !UnescapeRemoteCacheField(f[4], u.server) ||
            !UnescapeRemoteCacheField(f[5], u.path) ||
            !IsRemoteFilerPath(u.path) || RemoteFilerName(u.path).empty())
            continue;
        u.stoppedAt = static_cast<int64_t>(stoppedAt);
        if (now - u.stoppedAt > kRemoteUnfinishedUploadMaxAgeSeconds) continue;
        NoteRemoteUnfinishedUpload(out, u);
    }
    return !first;
}

} // namespace UltraCanvas
