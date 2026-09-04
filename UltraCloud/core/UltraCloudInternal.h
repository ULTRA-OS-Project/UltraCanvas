// UltraCloud/core/UltraCloudInternal.h
// Small helpers shared by the module's own sources (not public API): JSON in
// and out, RFC 3339 dates, query-string building.
// Version: 0.3.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <DataFormats/UltraCanvasJSON.h>
#include <UltraNet/UltraNetUrl.h>

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace UltraCloud {
namespace internal {

using UltraCanvas::JSONValue;

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
    auto size = std::filesystem::file_size(path, ec);
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
