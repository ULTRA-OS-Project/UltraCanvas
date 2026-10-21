// UltraCloud/core/UltraCloudInternal.h
// Small helpers shared by the module's own sources (not public API): JSON in
// and out, RFC 3339 dates, query-string building, and what an upload tells
// CloudService about itself (UploadReport).
// Version: 0.4.0 - UploadReport
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <DataFormats/UltraCanvasJSON.h>
#include <UltraNet/UltraNetUrl.h>

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

namespace UltraCloud {
namespace internal {

using UltraCanvas::JSONValue;

// What a provider whose uploads land as they arrive (partialUploadsRemain)
// reports about the upload it is running, to the CloudService call that set
// it on the thread (SetThreadUploadReport). The service needs to know when
// the server has created the file at its name - from then on what is there
// is the upload's, and is removed if it does not finish - and whether the
// server refused to create it, which leaves the name as it was.
struct UploadReport {
    // Going straight to the file's name: an SFTP server says nothing when it
    // creates the file, so the provider looks in the folder first, and a
    // name that is free there counts as created by this upload.
    bool checkFirst = false;
    // The provider can tell when the server creates the file (FTP: the reply
    // to STOR); `opened` false then means nothing was created at all.
    bool watched = false;
    bool opened = false;      // the server created (or emptied) the file
    bool refused = false;     // the server refused to create it; nothing written
    // Called once, on the uploading thread, when `opened` becomes true.
    std::function<void()> onOpened;
};
inline thread_local UploadReport* t_uploadReport = nullptr;
// Sets the calling thread's report and answers the one it replaces.
inline UploadReport* SetThreadUploadReport(UploadReport* report) {
    UploadReport* previous = t_uploadReport;
    t_uploadReport = report;
    return previous;
}
inline UploadReport* ThreadUploadReport() { return t_uploadReport; }

inline JSONValue ParseJson(const std::string& text) {
    UltraCanvas::JSONParseResult pr;
    return UltraCanvas::JSON::Parse(text, &pr);
}

inline std::string ToJson(const JSONValue& value) {
    return UltraCanvas::JSON::Serialize(value);
}

inline std::vector<uint8_t> Bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

// "2026-09-04T10:00:00Z" for an epoch second (UTC); empty for <= 0.
inline std::string Rfc3339(int64_t epoch) {
    if (epoch <= 0) return "";
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// "?a=1&b=x%20y" from key/value pairs (values percent-encoded).
inline std::string Query(const std::vector<std::pair<std::string, std::string>>& params) {
    std::string q;
    for (const auto& [k, v] : params) {
        q += q.empty() ? "?" : "&";
        q += k + "=" + UltraNet_UrlEncode(v);
    }
    return q;
}

// ---- Chunked file reading (large uploads never load the whole file) ----------
// Size of a local file, -1 when it cannot be read.
inline int64_t FileSize(const std::string& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(UltraCanvas::PathFromUtf8(path), ec);
    return ec ? -1 : static_cast<int64_t>(size);
}
// Bytes [offset, offset + size) of the file into `out` (short at the end).
inline bool ReadChunk(std::ifstream& is, int64_t offset, int64_t size, std::vector<uint8_t>& out) {
    out.assign(static_cast<std::size_t>(size), 0);
    is.clear();
    is.seekg(offset, std::ios::beg);
    is.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    const auto got = is.gcount();
    if (got < 0) return false;
    out.resize(static_cast<std::size_t>(got));
    return true;
}
// "bytes s-e/total" for a chunk.
inline std::string ContentRange(int64_t offset, int64_t length, int64_t total) {
    return "bytes " + std::to_string(offset) + "-" + std::to_string(offset + length - 1) + "/"
         + std::to_string(total);
}

// The last path segment ("report.pdf" of "/Docs/report.pdf") and its parent.
inline std::string Leaf(const std::string& path) {
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
inline std::string Parent(const std::string& path) {
    auto slash = path.find_last_of('/');
    return (slash == std::string::npos || slash == 0) ? "/" : path.substr(0, slash);
}

} // namespace internal
} // namespace UltraCloud
