// core/IODeviceManager/UltraCanvasIODeviceDnsSd.cpp
// Reading DNS-SD service names.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODeviceDnsSd.h"

#include <algorithm>
#include <cctype>

namespace UltraCanvas {

namespace {

std::string Lower(const std::string& text) {
    std::string out = text;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

size_t DnsSdServiceTypeStart(const std::string& name,
                             const std::vector<std::string>& serviceTypes) {
    const std::string lower = Lower(name);
    size_t best = std::string::npos;
    for (const std::string& type : serviceTypes) {
        const std::string marker = Lower(type);
        if (marker.empty()) continue;
        size_t at = lower.rfind(marker);
        while (at != std::string::npos) {
            const size_t after = at + marker.size();
            if (at > 0 && (after == lower.size() || lower[after] == '.')) break;
            at = at == 0 ? std::string::npos : lower.rfind(marker, at - 1);
        }
        if (at != std::string::npos && (best == std::string::npos || at > best)) best = at;
    }
    return best;
}

std::string DnsSdUnescape(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }
        if (i + 3 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isdigit(static_cast<unsigned char>(text[i + 2])) &&
            std::isdigit(static_cast<unsigned char>(text[i + 3]))) {
            const int value = (text[i + 1] - '0') * 100 + (text[i + 2] - '0') * 10 +
                              (text[i + 3] - '0');
            if (value <= 255) {
                out.push_back(static_cast<char>(value));
                i += 3;
                continue;
            }
        }
        out.push_back(text[i + 1]);
        ++i;
    }
    return out;
}

std::string DnsSdInstanceName(const std::string& serviceName,
                              const std::vector<std::string>& serviceTypes) {
    const size_t at = DnsSdServiceTypeStart(serviceName, serviceTypes);
    return DnsSdUnescape(at == std::string::npos ? serviceName : serviceName.substr(0, at));
}

}  // namespace UltraCanvas
