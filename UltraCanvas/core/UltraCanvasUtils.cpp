// core/UltraCanvasUtils.h
// Utils
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#include "UltraCanvasUtils.h"
#include <sstream>
#include <fstream>
#include <iostream>
#include <string>

namespace UltraCanvas {
    const char* versionString = "0.0.002";

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

    Color ParseColor(const std::string &colorStr) {
        if (colorStr.empty() || colorStr == "none") {
            return Colors::Transparent;
        }

        // Convert to lowercase for comparison
        std::string lower = colorStr;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // Extended SVG named colors map (includes all SVG 1.1 standard colors)
        static const std::unordered_map<std::string, Color> namedColors = {
                // Basic colors
                {"black", Color(0, 0, 0)},
                {"white", Color(255, 255, 255)},
                {"red", Color(255, 0, 0)},
                {"green", Color(0, 128, 0)},  // Note: SVG "green" is darker than pure green
                {"blue", Color(0, 0, 255)},
                {"yellow", Color(255, 255, 0)},
                {"cyan", Color(0, 255, 255)},
                {"magenta", Color(255, 0, 255)},
                {"gray", Color(128, 128, 128)},
                {"grey", Color(128, 128, 128)},  // Alternative spelling

                // Extended web colors
                {"aliceblue", Color(240, 248, 255)},
                {"antiquewhite", Color(250, 235, 215)},
                {"aqua", Color(0, 255, 255)},
                {"aquamarine", Color(127, 255, 212)},
                {"azure", Color(240, 255, 255)},
                {"beige", Color(245, 245, 220)},
                {"bisque", Color(255, 228, 196)},
                {"blanchedalmond", Color(255, 235, 205)},
                {"blueviolet", Color(138, 43, 226)},
                {"brown", Color(165, 42, 42)},
                {"burlywood", Color(222, 184, 135)},
                {"cadetblue", Color(95, 158, 160)},
                {"chartreuse", Color(127, 255, 0)},
                {"chocolate", Color(210, 105, 30)},
                {"coral", Color(255, 127, 80)},
                {"cornflowerblue", Color(100, 149, 237)},
                {"cornsilk", Color(255, 248, 220)},
                {"crimson", Color(220, 20, 60)},
                {"darkblue", Color(0, 0, 139)},
                {"darkcyan", Color(0, 139, 139)},
                {"darkgoldenrod", Color(184, 134, 11)},
                {"darkgray", Color(169, 169, 169)},
                {"darkgrey", Color(169, 169, 169)},
                {"darkgreen", Color(0, 100, 0)},
                {"darkkhaki", Color(189, 183, 107)},
                {"darkmagenta", Color(139, 0, 139)},
                {"darkolivegreen", Color(85, 107, 47)},
                {"darkorange", Color(255, 140, 0)},
                {"darkorchid", Color(153, 50, 204)},
                {"darkred", Color(139, 0, 0)},
                {"darksalmon", Color(233, 150, 122)},
                {"darkseagreen", Color(143, 188, 143)},
                {"darkslateblue", Color(72, 61, 139)},
                {"darkslategray", Color(47, 79, 79)},
                {"darkslategrey", Color(47, 79, 79)},
                {"darkturquoise", Color(0, 206, 209)},
                {"darkviolet", Color(148, 0, 211)},
                {"deeppink", Color(255, 20, 147)},
                {"deepskyblue", Color(0, 191, 255)},
                {"dimgray", Color(105, 105, 105)},
                {"dimgrey", Color(105, 105, 105)},
                {"dodgerblue", Color(30, 144, 255)},
                {"firebrick", Color(178, 34, 34)},
                {"floralwhite", Color(255, 250, 240)},
                {"forestgreen", Color(34, 139, 34)},
                {"fuchsia", Color(255, 0, 255)},
                {"gainsboro", Color(220, 220, 220)},
                {"ghostwhite", Color(248, 248, 255)},
                {"gold", Color(255, 215, 0)},
                {"goldenrod", Color(218, 165, 32)},
                {"greenyellow", Color(173, 255, 47)},
                {"honeydew", Color(240, 255, 240)},
                {"hotpink", Color(255, 105, 180)},
                {"indianred", Color(205, 92, 92)},
                {"indigo", Color(75, 0, 130)},
                {"ivory", Color(255, 255, 240)},
                {"khaki", Color(240, 230, 140)},
                {"lavender", Color(230, 230, 250)},
                {"lavenderblush", Color(255, 240, 245)},
                {"lawngreen", Color(124, 252, 0)},
                {"lemonchiffon", Color(255, 250, 205)},
                {"lightblue", Color(173, 216, 230)},
                {"lightcoral", Color(240, 128, 128)},
                {"lightcyan", Color(224, 255, 255)},
                {"lightgoldenrodyellow", Color(250, 250, 210)},
                {"lightgray", Color(211, 211, 211)},
                {"lightgrey", Color(211, 211, 211)},
                {"lightgreen", Color(144, 238, 144)},
                {"lightpink", Color(255, 182, 193)},
                {"lightsalmon", Color(255, 160, 122)},
                {"lightseagreen", Color(32, 178, 170)},
                {"lightskyblue", Color(135, 206, 250)},
                {"lightslategray", Color(119, 136, 153)},
                {"lightslategrey", Color(119, 136, 153)},
                {"lightsteelblue", Color(176, 196, 222)},
                {"lightyellow", Color(255, 255, 224)},
                {"lime", Color(0, 255, 0)},
                {"limegreen", Color(50, 205, 50)},
                {"linen", Color(250, 240, 230)},
                {"maroon", Color(128, 0, 0)},
                {"mediumaquamarine", Color(102, 205, 170)},
                {"mediumblue", Color(0, 0, 205)},
                {"mediumorchid", Color(186, 85, 211)},
                {"mediumpurple", Color(147, 112, 219)},
                {"mediumseagreen", Color(60, 179, 113)},
                {"mediumslateblue", Color(123, 104, 238)},
                {"mediumspringgreen", Color(0, 250, 154)},
                {"mediumturquoise", Color(72, 209, 204)},
                {"mediumvioletred", Color(199, 21, 133)},
                {"midnightblue", Color(25, 25, 112)},
                {"mintcream", Color(245, 255, 250)},
                {"mistyrose", Color(255, 228, 225)},
                {"moccasin", Color(255, 228, 181)},
                {"navajowhite", Color(255, 222, 173)},
                {"navy", Color(0, 0, 128)},
                {"oldlace", Color(253, 245, 230)},
                {"olive", Color(128, 128, 0)},
                {"olivedrab", Color(107, 142, 35)},
                {"orange", Color(255, 165, 0)},
                {"orangered", Color(255, 69, 0)},
                {"orchid", Color(218, 112, 214)},
                {"palegoldenrod", Color(238, 232, 170)},
                {"palegreen", Color(152, 251, 152)},
                {"paleturquoise", Color(175, 238, 238)},
                {"palevioletred", Color(219, 112, 147)},
                {"papayawhip", Color(255, 239, 213)},
                {"peachpuff", Color(255, 218, 185)},
                {"peru", Color(205, 133, 63)},
                {"pink", Color(255, 192, 203)},
                {"plum", Color(221, 160, 221)},
                {"powderblue", Color(176, 224, 230)},
                {"purple", Color(128, 0, 128)},
                {"rebeccapurple", Color(102, 51, 153)},
                {"rosybrown", Color(188, 143, 143)},
                {"royalblue", Color(65, 105, 225)},
                {"saddlebrown", Color(139, 69, 19)},
                {"salmon", Color(250, 128, 114)},
                {"sandybrown", Color(244, 164, 96)},
                {"seagreen", Color(46, 139, 87)},
                {"seashell", Color(255, 245, 238)},
                {"sienna", Color(160, 82, 45)},
                {"silver", Color(192, 192, 192)},
                {"skyblue", Color(135, 206, 235)},
                {"slateblue", Color(106, 90, 205)},
                {"slategray", Color(112, 128, 144)},
                {"slategrey", Color(112, 128, 144)},
                {"snow", Color(255, 250, 250)},
                {"springgreen", Color(0, 255, 127)},
                {"steelblue", Color(70, 130, 180)},
                {"tan", Color(210, 180, 140)},
                {"teal", Color(0, 128, 128)},
                {"thistle", Color(216, 191, 216)},
                {"tomato", Color(255, 99, 71)},
                {"turquoise", Color(64, 224, 208)},
                {"violet", Color(238, 130, 238)},
                {"wheat", Color(245, 222, 179)},
                {"whitesmoke", Color(245, 245, 245)},
                {"yellowgreen", Color(154, 205, 50)},
                {"transparent", Color(0, 0, 0, 0)}
        };

        auto it = namedColors.find(lower);
        if (it != namedColors.end()) {
            return it->second;
        }

        // Hex colors: #RGB, #RRGGBB, #RRGGBBAA
        if (colorStr[0] == '#') {
            std::string hex = colorStr.substr(1);

            // Remove any whitespace
            hex.erase(std::remove_if(hex.begin(), hex.end(), ::isspace), hex.end());

            if (hex.length() == 3) {
                // Expand shorthand: #RGB -> #RRGGBB
                hex = std::string(1, hex[0]) + hex[0] +
                      std::string(1, hex[1]) + hex[1] +
                      std::string(1, hex[2]) + hex[2];
            }

            if (hex.length() == 6) {
                try {
                    int r = std::stoi(hex.substr(0, 2), nullptr, 16);
                    int g = std::stoi(hex.substr(2, 2), nullptr, 16);
                    int b = std::stoi(hex.substr(4, 2), nullptr, 16);
                    return Color(r, g, b);
                } catch (...) {
                    return Colors::Black;
                }
            } else if (hex.length() == 8) {
                // #RRGGBBAA format
                try {
                    int r = std::stoi(hex.substr(0, 2), nullptr, 16);
                    int g = std::stoi(hex.substr(2, 2), nullptr, 16);
                    int b = std::stoi(hex.substr(4, 2), nullptr, 16);
                    int a = std::stoi(hex.substr(6, 2), nullptr, 16);
                    return Color(r, g, b, a);
                } catch (...) {
                    return Colors::Black;
                }
            }
        }

        // RGB/RGBA functions: rgb(255, 128, 0) or rgba(255, 128, 0, 0.5)
        if (lower.substr(0, 4) == "rgb(" || lower.substr(0, 5) == "rgba(") {
            size_t start = lower.find('(');
            size_t end = lower.find(')', start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string params = lower.substr(start + 1, end - start - 1);

                // Replace commas and percentages
                std::replace(params.begin(), params.end(), ',', ' ');
                std::replace(params.begin(), params.end(), '%', ' ');

                std::istringstream iss(params);
                float r, g, b, a = 1.0f;

                // Try to read values
                if (iss >> r >> g >> b) {
                    // Check if alpha value exists
                    iss >> a;

                    // Handle percentage values (if values > 1 and <= 100, assume direct RGB)
                    if (r <= 1.0f && g <= 1.0f && b <= 1.0f &&
                        (r > 0 || g > 0 || b > 0)) {
                        // Normalized values (0-1)
                        r *= 255;
                        g *= 255;
                        b *= 255;
                    }

                    // Clamp values
                    r = std::max(0.0f, std::min(255.0f, r));
                    g = std::max(0.0f, std::min(255.0f, g));
                    b = std::max(0.0f, std::min(255.0f, b));
                    a = std::max(0.0f, std::min(1.0f, a));

                    return Color(
                            static_cast<int>(r),
                            static_cast<int>(g),
                            static_cast<int>(b),
                            static_cast<int>(a * 255)
                    );
                }
            }
        }

        // HSL function: hsl(180, 50%, 50%)
        if (lower.substr(0, 4) == "hsl(" || lower.substr(0, 5) == "hsla(") {
            size_t start = lower.find('(');
            size_t end = lower.find(')', start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string params = lower.substr(start + 1, end - start - 1);

                // Replace commas and percentages
                std::replace(params.begin(), params.end(), ',', ' ');
                std::replace(params.begin(), params.end(), '%', ' ');

                std::istringstream iss(params);
                float h, s, l, a = 1.0f;

                if (iss >> h >> s >> l) {
                    iss >> a;  // Try to read alpha

                    // Convert percentage to 0-1
                    s = s / 100.0f;
                    l = l / 100.0f;

                    // HSL to RGB conversion
                    h = std::fmod(h, 360.0f);
                    if (h < 0) h += 360.0f;
                    h = h / 360.0f;

                    float r, g, b;
                    if (s == 0) {
                        r = g = b = l;  // Gray
                    } else {
                        auto hue2rgb = [](float p, float q, float t) {
                            if (t < 0) t += 1;
                            if (t > 1) t -= 1;
                            if (t < 1.0f/6.0f) return p + (q - p) * 6 * t;
                            if (t < 1.0f/2.0f) return q;
                            if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6;
                            return p;
                        };

                        float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
                        float p = 2 * l - q;
                        r = hue2rgb(p, q, h + 1.0f/3.0f);
                        g = hue2rgb(p, q, h);
                        b = hue2rgb(p, q, h - 1.0f/3.0f);
                    }

                    return Color(
                            static_cast<int>(r * 255),
                            static_cast<int>(g * 255),
                            static_cast<int>(b * 255),
                            static_cast<int>(a * 255)
                    );
                }
            }
        }

        // Default fallback
        return Colors::Black;
    }

    std::string GetFileExtension(const std::string& filePath) {
        size_t dotPos = filePath.find_last_of('.');
        if (dotPos == std::string::npos) return "";

        std::string ext = filePath.substr(dotPos + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext;
    }

    std::string LoadFile(const std::string& filePath) {
        if (filePath.empty()) return "";

        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filePath << std::endl;
            return "// Error: Could not load file: " + filePath;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        return buffer.str();
    }


}