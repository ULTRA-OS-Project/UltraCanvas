// include/NetworkMonitor/NetworkMonitorCsv.h
// The two helpers every CSV the module writes shares: RFC 4180 quoting of a
// field, and a UTC ISO-8601 timestamp. Internal to the module; the public
// exports are NetworkMonitor_Export*Csv in NetworkMonitor.h and
// NetworkMonitorStore.h.
//
// Version: 0.6.0
// Last Modified: 2026-09-23
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace UltraCanvas {
namespace NetworkMonitorCsv {

// A field, quoted when it holds a comma, a quote or a line break; a quote
// inside is doubled.
inline std::string Field(const std::string& text) {
    if (text.find_first_of(",\"\r\n") == std::string::npos) return text;
    std::string quoted = "\"";
    for (char c : text) { if (c == '"') quoted += '"'; quoted += c; }
    return quoted + "\"";
}

// "2026-09-23T08:51:21Z" for a Unix second.
inline std::string IsoUtc(int64_t seconds) {
    const std::time_t when = static_cast<std::time_t>(seconds);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &when);
#else
    gmtime_r(&when, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

} // namespace NetworkMonitorCsv
} // namespace UltraCanvas
