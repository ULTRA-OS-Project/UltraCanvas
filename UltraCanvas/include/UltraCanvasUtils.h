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

    std::string ToLowerCase(const std::string &str);
    bool StartsWith(const std::string& str, const std::string& prefix);
    std::string Trim(const std::string& str);
    std::vector<std::string> Split(const std::string& str, char delimiter);
    Color ParseColor(const std::string& colorStr);
    std::string GetFileExtension(const std::string& filePath);
}