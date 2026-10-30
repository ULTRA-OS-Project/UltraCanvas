// include/UltraCanvasRunsCodeWhenOpened.h
// FileAssociations::RunsCodeWhenOpened: whether opening a file of this name
// would run code - a program, a script, an installer, a shortcut, a disk image
// (Windows opens an .exe by running it and mounts an .iso; a .desktop file
// starts what it names). Judged by the name alone - for a file that arrived
// from elsewhere (a mail attachment, a download) there is no execute
// permission to look at. A mail reader or a preview offers such a file to be
// saved, never opened.
//
// Header-only and C++17, with no link dependency, so a headless module asks
// the same question as the UI: UltraMail's threat scanner (in its engine,
// which does not link the UltraCanvas library) names such an attachment with
// it, and UltraMail and the media viewer refuse to open one.
// UltraCanvasFileAssociations.h includes it.
// Version: 1.0.0 - moved out of UltraCanvasFileAssociations.cpp, header-only
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

namespace UltraCanvas {
namespace FileAssociations {

// Programs, scripts, installers, shortcuts, disk images and the like on
// Windows, macOS and Linux: what the system runs, mounts or follows when it
// opens them. Case-insensitive; a folder part of the name is ignored.
inline bool RunsCodeWhenOpened(const std::string& fileName) {
    static const std::unordered_set<std::string> kRunnable = {
        // Windows
        "exe", "com", "bat", "cmd", "msi", "msp", "mst", "scr", "pif", "cpl", "hta", "jar",
        "js", "jse", "vbs", "vbe", "wsf", "wsh", "ws", "ps1", "psm1", "psd1", "lnk", "url", "scf",
        "reg", "inf", "dll", "sys", "msc", "application", "appref-ms", "gadget", "chm", "iso", "img",
        "vhd", "vhdx", "appx", "msix", "appxbundle", "msixbundle", "settingcontent-ms",
        // macOS
        "app", "command", "tool", "pkg", "mpkg", "dmg", "terminal", "workflow", "scpt",
        // Linux and other POSIX
        "sh", "bash", "csh", "ksh", "zsh", "fish", "desktop", "appimage", "run", "bin", "deb",
        "rpm", "snap", "flatpakref", "apk",
        // script languages an association may run
        "py", "pyw", "pl", "rb", "php"
    };
    std::string name = fileName;
    const size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name.erase(0, slash + 1);
    // "invoice.pdf.exe " and "setup.exe." end where the system says they do.
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return kRunnable.count(ext) > 0;
}

} // namespace FileAssociations
} // namespace UltraCanvas
