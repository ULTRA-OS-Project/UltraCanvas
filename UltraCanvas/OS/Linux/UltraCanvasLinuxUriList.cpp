// OS/Linux/UltraCanvasLinuxUriList.cpp
// file:// URIs and text/uri-list, shared by the X11 and Wayland clipboards
// and the drag and drop. See the header.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxUriList.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace UltraCanvas {
namespace LinuxUriList {

    std::string EncodeFileUri(const std::string& path) {
        static const char* hex = "0123456789ABCDEF";
        std::string uri = "file://";
        for (unsigned char c : path) {
            bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                              (c >= '0' && c <= '9') ||
                              c == '-' || c == '_' || c == '.' || c == '~' ||
                              c == '/';
            if (unreserved) {
                uri += static_cast<char>(c);
            } else {
                uri += '%';
                uri += hex[c >> 4];
                uri += hex[c & 0x0F];
            }
        }
        return uri;
    }

    std::string DecodeFileUri(const std::string& uri) {
        std::string path;
        if (uri.compare(0, 7, "file://") == 0) {
            path = uri.substr(7);
        } else if (uri.compare(0, 5, "file:") == 0) {
            path = uri.substr(5);
        } else if (!uri.empty() && uri[0] == '/') {
            path = uri;   // bare path (some apps put plain paths on the list)
        } else {
            return "";
        }

        // "file://localhost/path" → strip the host part.
        if (!path.empty() && path[0] != '/') {
            size_t slash = path.find('/');
            if (slash == std::string::npos) return "";
            path = path.substr(slash);
        }

        // Percent-decode (%20 → space, UTF-8 bytes, ...).
        std::string decoded;
        decoded.reserve(path.size());
        for (size_t i = 0; i < path.size(); ++i) {
            if (path[i] == '%' && i + 2 < path.size() &&
                std::isxdigit(static_cast<unsigned char>(path[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(path[i + 2]))) {
                char hexPair[3] = { path[i + 1], path[i + 2], '\0' };
                decoded += static_cast<char>(std::strtol(hexPair, nullptr, 16));
                i += 2;
            } else {
                decoded += path[i];
            }
        }
        return decoded;
    }

    std::vector<std::string> ParsePaths(const std::string& uriList) {
        std::vector<std::string> paths;
        std::istringstream stream(uriList);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::string path = DecodeFileUri(line);
            if (!path.empty()) paths.push_back(path);
        }
        return paths;
    }

} // namespace LinuxUriList
} // namespace UltraCanvas
