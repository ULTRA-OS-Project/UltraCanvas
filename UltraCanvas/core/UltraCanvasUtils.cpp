// core/UltraCanvasUtils.h
// Utils
// Version: 1.0.0
// Last Modified: 2025-09-14
// Author: UltraCanvas Framework

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <windowsx.h>  // GET_X_LPARAM, GET_Y_LPARAM macros
#include <shellapi.h>  // ShellExecuteA
#include "UltraCanvasDebug.h"
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if !defined(_WIN32) && !defined(_WIN64)
#include <cerrno>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "UltraCanvasUtils.h"
#include <sstream>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <string>

// ULTRACANVAS_VERSION is defined by UltraCanvas/CMakeLists.txt from the first
// line of Docs/UltraCanvas/CHANGELOG.md, the same line the packaging scripts
// use for the artefact file names — so what the demo app's info window shows
// always matches the build it came from. The fallback only applies to builds
// made outside CMake.
#ifndef ULTRACANVAS_VERSION
#define ULTRACANVAS_VERSION "0.0.0-dev"
#endif

namespace UltraCanvas {
    const char* versionString = ULTRACANVAS_VERSION;

#if defined(_WIN32) || defined(_WIN64)
    std::wstring Utf8ToWide(const std::string& utf8) {
        if (utf8.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                       static_cast<int>(utf8.size()), nullptr, 0);
        if (size <= 0) return L"";
        std::wstring result(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                            static_cast<int>(utf8.size()), &result[0], size);
        return result;
    }

    std::string WideToUtf8(const std::wstring& wide) {
        if (wide.empty()) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                       static_cast<int>(wide.size()),
                                       nullptr, 0, nullptr, nullptr);
        if (size <= 0) return "";
        std::string result(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                            static_cast<int>(wide.size()),
                            &result[0], size, nullptr, nullptr);
        return result;
    }
#endif

    std::filesystem::path PathFromUtf8(const std::string& utf8) {
#if defined(_WIN32) || defined(_WIN64)
        return std::filesystem::path(Utf8ToWide(utf8));
#else
        return std::filesystem::path(utf8);
#endif
    }

    std::string PathToUtf8(const std::filesystem::path& p) {
#if defined(_WIN32) || defined(_WIN64)
        return WideToUtf8(p.native());
#else
        return p.string();
#endif
    }

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

        std::ifstream file(PathFromUtf8(filePath));
        if (!file.is_open()) {
            debugOutput << "Failed to open file: " << filePath << std::endl;
            return "// Error: Could not load file: " + filePath;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        return buffer.str();
    }

// ===== BASE64 DECODING HELPER =====
    static const std::string base64Chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::vector<uint8_t> Base64Decode(const std::string& input) {

        std::vector<uint8_t> result;

        if (input.empty()) {
            return result;
        }

        // Build decode table
        std::vector<int> decodeTable(256, -1);
        for (size_t i = 0; i < base64Chars.size(); ++i) {
            decodeTable[static_cast<unsigned char>(base64Chars[i])] = static_cast<int>(i);
        }

        // Calculate output size (approximate)
        size_t inputLen = input.length();
        size_t padding = 0;
        if (inputLen >= 2) {
            if (input[inputLen - 1] == '=') padding++;
            if (input[inputLen - 2] == '=') padding++;
        }
        size_t outputLen = (inputLen / 4) * 3 - padding;
        result.reserve(outputLen);

        uint32_t buffer = 0;
        int bitsCollected = 0;

        for (char c : input) {
            if (c == '=') break; // End of data
            if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue; // Skip whitespace

            int value = decodeTable[static_cast<unsigned char>(c)];
            if (value < 0) continue; // Invalid character, skip

            buffer = (buffer << 6) | static_cast<uint32_t>(value);
            bitsCollected += 6;

            if (bitsCollected >= 8) {
                bitsCollected -= 8;
                result.push_back(static_cast<uint8_t>((buffer >> bitsCollected) & 0xFF));
            }
        }

        return result;
    }

    std::string Base64Encode(const std::vector<uint8_t>& in, bool wrap) {
        std::string out;
        std::size_t lineWidth = 0;
        for (std::size_t i = 0; i < in.size(); i += 3) {
            const std::size_t left = in.size() - i;
            const uint32_t t = (static_cast<uint32_t>(in[i]) << 16) |
                               (left > 1 ? static_cast<uint32_t>(in[i+1]) << 8 : 0) |
                               (left > 2 ? static_cast<uint32_t>(in[i+2])      : 0);
            out.push_back(base64Chars[(t >> 18) & 0x3f]);
            out.push_back(base64Chars[(t >> 12) & 0x3f]);
            out.push_back(left > 1 ? base64Chars[(t >> 6) & 0x3f] : '=');
            out.push_back(left > 2 ? base64Chars[ t       & 0x3f] : '=');
            lineWidth += 4;
            if (wrap && lineWidth >= 76) { out.append("\r\n"); lineWidth = 0; }
        }
        if (wrap && lineWidth > 0) out.append("\r\n");
        return out;
    }


    std::string FormatFileSize(size_t bytes) {
        std::ostringstream oss;
        if (bytes >= 1024 * 1024 * 1024) {
            oss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
        } else if (bytes >= 1024 * 1024) {
            oss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0)) << " MB";
        } else if (bytes >= 1024) {
            oss << std::fixed << std::setprecision(2) << (bytes / 1024.0) << " KB";
        } else {
            oss << bytes << " bytes";
        }
        return oss.str();
    }

    std::string GetExecutableDir() {
        std::string path;
#if defined(__linux__) || defined(__unix__)
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            path = buf;
        }
#elif defined(_WIN32) || defined(_WIN64)
        // Must use the wide API: GetModuleFileNameA converts through the
        // legacy ANSI code page, turning any character it can't represent
        // (e.g. Thai folder names) into literal '?', which then poisons every
        // asset path derived from the executable directory.
        wchar_t buf[4096];
        DWORD len = GetModuleFileNameW(nullptr, buf,
                                       DWORD(sizeof(buf) / sizeof(buf[0]) - 1));
        if (len > 0 && len < sizeof(buf) / sizeof(buf[0]) - 1) {
            path = WideToUtf8(std::wstring(buf, len));
        }
#elif defined(__APPLE__)
        char buf[PATH_MAX];
        uint32_t size = sizeof(buf);
        if (_NSGetExecutablePath(buf, &size) == 0) {
            char resolved[PATH_MAX];
            if (realpath(buf, resolved)) {
                path = resolved;
            } else {
                path = buf;
            }
        }
#endif
        debugOutput << "GetExecutableDir exec=" << path << std::endl;
        if (!path.empty()) {
            auto pos = path.find_last_of("/\\");
            if (pos != std::string::npos) {
                return path.substr(0, pos);
            }
        }
        return ".";
    }

    void OpenURL(const std::string& url) {
#if defined(_WIN32) || defined(_WIN64)
        ShellExecuteW(nullptr, L"open", Utf8ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
        system(("open \"" + url + "\"").c_str());
#else
        system(("xdg-open \"" + url + "\"").c_str());
#endif
    }

    bool LaunchDetachedProcess(const std::vector<std::string>& argv,
                               const std::string& workingDirectory,
                               std::string& outError) {
        outError.clear();
        if (argv.empty() || argv[0].empty()) {
            outError = "No application to launch.";
            return false;
        }
#if defined(_WIN32) || defined(_WIN64)
        // Standard argv quoting: quote arguments containing spaces / tabs /
        // quotes, backslash-escape embedded quotes (and the backslashes
        // directly before them).
        std::wstring cmdLine;
        for (const std::string& a : argv) {
            if (!cmdLine.empty()) cmdLine += L' ';
            const std::wstring w = Utf8ToWide(a);
            if (!w.empty() && w.find_first_of(L" \t\"") == std::wstring::npos) {
                cmdLine += w;
                continue;
            }
            cmdLine += L'"';
            size_t backslashes = 0;
            for (wchar_t c : w) {
                if (c == L'\\') { ++backslashes; continue; }
                if (c == L'"') cmdLine.append(backslashes * 2 + 1, L'\\');
                else cmdLine.append(backslashes, L'\\');
                backslashes = 0;
                cmdLine += c;
            }
            cmdLine.append(backslashes * 2, L'\\');
            cmdLine += L'"';
        }
        const std::wstring dirW = Utf8ToWide(workingDirectory);
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
        mutableCmd.push_back(L'\0');
        if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                            CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS,
                            nullptr, dirW.empty() ? nullptr : dirW.c_str(),
                            &si, &pi)) {
            outError = "Could not start \"" + argv[0] + "\".";
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
#else
        const pid_t pid = ::fork();
        if (pid < 0) {
            outError = std::string("Could not start \"") + argv[0] + "\": " +
                       std::strerror(errno);
            return false;
        }
        if (pid == 0) {
            // Grandchild execs in its own session; the intermediate child
            // exits immediately so the parent can reap it and the launched
            // program is never our zombie.
            if (::fork() == 0) {
                ::setsid();
                if (!workingDirectory.empty()) {
                    if (::chdir(workingDirectory.c_str()) != 0) {
                        // Keep the inherited directory - starting the program
                        // in the wrong folder still beats not starting it.
                    }
                }
                std::vector<char*> args;
                args.reserve(argv.size() + 1);
                for (const std::string& a : argv)
                    args.push_back(const_cast<char*>(a.c_str()));
                args.push_back(nullptr);
                ::execvp(args[0], args.data());
                ::_exit(127);
            }
            ::_exit(0);
        }
        int status = 0;
        ::waitpid(pid, &status, 0);
        return true;
#endif
    }

    std::string NormalizePath(const std::string& in) {
        std::string result;
#if defined(_WIN32) || defined(_WIN64)
        result = in;
        std::replace(result.begin(), result.end(), '/', '\\');
#else
        char real[PATH_MAX];
        realpath(in.c_str(), real);
        result = real;
#endif
        if (!in.empty() && (in.back() == '/' || in.back() == '\\')) {
#if defined(_WIN32) || defined(_WIN64)
            result.push_back('\\');
#else
            result.push_back('/');
#endif
        }
        return result;
    }

    std::string Trim(const std::string& s, const std::string& strippedChars) {
        const size_t a = s.find_first_not_of(strippedChars);
        if (a == std::string::npos) return {};
        const size_t b = s.find_last_not_of(strippedChars);
        return s.substr(a, b - a + 1);
    }
}
