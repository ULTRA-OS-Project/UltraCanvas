// include/UltraCanvasUtils.h
// Utils
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <cctype>


namespace UltraCanvas {
    extern const char* versionString;
    std::string ToLowerCase(const std::string &str);
    bool StartsWith(const std::string& str, const std::string& prefix);
    std::string Trim(const std::string& str);
    std::vector<std::string> Split(const std::string& str, char delimiter);
    Color ParseColor(const std::string& colorStr);
    std::string GetFileExtension(const std::string& filePath);
    std::string LoadFile(const std::string& filePath);

    inline std::string LTrimWhitespace(std::string s) {
        std::string result = s;
        result.erase(result.begin(), std::find_if(s.begin(), result.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        return result;
    }

// Trim from the end (in place)
    inline std::string RTrimWhitespace(std::string s) {
        std::string result = s;
        result.erase(std::find_if(result.rbegin(), result.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), result.end());
        return result;
    }

    inline std::string TrimWhitespace(std::string s) {
        return LTrimWhitespace(RTrimWhitespace(s));
    }
}