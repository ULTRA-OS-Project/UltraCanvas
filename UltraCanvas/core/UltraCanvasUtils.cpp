// core/UltraCanvasUtils.h
// Utils
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#include "UltraCanvasUtils.h"
#include <sstream>
#include <string>

namespace UltraCanvas {
    std::string ToLowerCase(const std::string &str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    bool StartsWith(const std::string &str, const std::string &prefix) {
        return str.substr(0, prefix.length()) == prefix;
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
}