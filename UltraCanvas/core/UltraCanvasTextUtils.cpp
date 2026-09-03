// core/UltraCanvasTextUtils.cpp
// Standalone text utilities. See the header for why this is a separate file.
//
// The string helpers, moved verbatim from UltraCanvasUtils.cpp. The Base64 /
// Base32 codecs that used to sit alongside them now live in
// UltraCanvasTextCodecs.cpp — see that file for why the split matters at link
// time. Both are declared by the same header.
//
// Version: 1.0.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCanvasTextUtils.h"

#include <sstream>

namespace UltraCanvas {

// ===========================================================================
// Strings
// ===========================================================================

std::string ToLowerCase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

bool StartsWith(const std::string& str, const std::string& prefix) {
    return str.substr(0, prefix.length()) == prefix;
}

std::string Trim(const std::string& s, const std::string& strippedChars) {
    const size_t a = s.find_first_not_of(strippedChars);
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(strippedChars);
    return s.substr(a, b - a + 1);
}

std::vector<std::string> Split(const std::string& str, char delimiter) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delimiter)) {
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

} // namespace UltraCanvas
