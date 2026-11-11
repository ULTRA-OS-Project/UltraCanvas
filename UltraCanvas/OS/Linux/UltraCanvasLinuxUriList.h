// OS/Linux/UltraCanvasLinuxUriList.h
// file:// URIs and the text/uri-list format (RFC 2483), as the freedesktop
// clipboards and drags carry file lists: percent-encoded, one per CRLF line,
// "#" lines comments. Shared by the X11 and Wayland clipboard backends.
// Version: 1.0.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {
namespace LinuxUriList {

    // "/home/me/a b.txt" -> "file:///home/me/a%20b.txt". The path is UTF-8.
    std::string EncodeFileUri(const std::string& path);
    // The reverse; also takes "file:/path", "file://localhost/path" and a
    // bare absolute path. "" for anything else (an http URI, say).
    std::string DecodeFileUri(const std::string& uri);
    // Every file path on a text/uri-list, in order; other URIs left out.
    std::vector<std::string> ParsePaths(const std::string& uriList);

} // namespace LinuxUriList
} // namespace UltraCanvas
