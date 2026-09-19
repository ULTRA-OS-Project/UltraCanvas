// core/NetworkMonitor/NetworkMonitorAddress.cpp
// Version: 0.2.0
// Last Modified: 2026-09-19
// Author: UltraCanvas Framework / ULTRA OS
#include "NetworkMonitor/NetworkMonitorAddress.h"

#include <cstdint>
#include <cstdio>

namespace UltraCanvas {
namespace NetworkMonitorAddress {

std::string FormatIPv4(const unsigned char* b) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return buffer;
}

std::string FormatIPv6(const unsigned char* b) {
    bool mapped = true;
    for (int i = 0; i < 10; ++i) if (b[i] != 0) { mapped = false; break; }
    if (mapped && b[10] == 0xFF && b[11] == 0xFF) return "::ffff:" + FormatIPv4(b + 12);

    uint16_t groups[8];
    for (int i = 0; i < 8; ++i) {
        groups[i] = static_cast<uint16_t>((b[2 * i] << 8) | b[2 * i + 1]);
    }
    int bestStart = -1, bestLength = 0;
    for (int i = 0; i < 8;) {
        if (groups[i] != 0) { ++i; continue; }
        int j = i;
        while (j < 8 && groups[j] == 0) ++j;
        if (j - i > bestLength) { bestStart = i; bestLength = j - i; }
        i = j;
    }
    if (bestLength < 2) bestStart = -1;

    std::string text;
    char buffer[8];
    for (int i = 0; i < 8;) {
        if (i == bestStart) {
            text += "::";
            i += bestLength;
            continue;
        }
        if (!text.empty() && text.back() != ':') text += ':';
        std::snprintf(buffer, sizeof buffer, "%x", groups[i]);
        text += buffer;
        ++i;
    }
    return text;
}

} // namespace NetworkMonitorAddress
} // namespace UltraCanvas
