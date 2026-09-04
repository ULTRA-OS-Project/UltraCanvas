// UltraCloud/core/UltraCloudInternal.h
// Small helpers shared by the module's own sources (not public API): JSON in
// and out, RFC 3339 dates, query-string building.
// Version: 0.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <DataFormats/UltraCanvasJSON.h>
#include <UltraNet/UltraNetUrl.h>

#include <cstdint>
#include <ctime>
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
