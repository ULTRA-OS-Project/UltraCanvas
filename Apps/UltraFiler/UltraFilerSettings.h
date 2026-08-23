// Apps/UltraFiler/UltraFilerSettings.h
// Persistent application settings for UltraFiler. Follows the UltraTexter
// config pattern: a simple key=value file in the platform config directory
// (~/.config/UltraFiler/config.ini on Linux, %APPDATA%\UltraFiler\config.ini
// on Windows, ~/Library/Application Support/UltraFiler/config.ini on macOS).
// Settings are applied live by the settings dialog and saved on every change.
// Version: 1.2.0
// Last Modified: 2026-08-22
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace UltraCanvas {

class UltraFilerSettings {
public:
    // ===== DEFAULTS =====
    // The tree colours ship as named constants so the settings dialog can
    // offer "Restore defaults" without repeating the literals.
    static inline const Color kDefaultTreeDriveBackgroundColor{226, 236, 248, 255};
    static inline const Color kDefaultTreeSelectedFolderColor{0, 120, 215, 255};

    // ===== THE SETTINGS =====
    // Media viewer: backdrop behind transparent images — the checkered
    // pattern used by image editors, or a preset solid colour (default white).
    bool  previewCheckeredBackground = false;
    Color previewTransparentColor    = Color(255, 255, 255, 255);

    // Display > Treeview: the row background of the drive entries in the
    // folder tree (the drive roots on Windows, "File System" and the mounted
    // volumes elsewhere), so the drives stand out from the folders below
    // them, and the highlight of the selected folder.
    Color treeDriveBackgroundColor = kDefaultTreeDriveBackgroundColor;
    Color treeSelectedFolderColor  = kDefaultTreeSelectedFolderColor;

    // Extras > Open prompt: the command line program the "Open prompt" menu
    // entry starts. Empty means "whatever this OS provides" - the platform
    // default is detected at run time (see UltraFilerPrompt).
    std::string promptApplication;

    // ===== PERSISTENCE =====

    static std::string GetConfigDirectory() {
#if defined(_WIN32) || defined(_WIN64)
        const char* appdata = std::getenv("APPDATA");
        return appdata ? std::string(appdata) + "\\UltraFiler" : std::string("UltraFiler");
#elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        return home ? std::string(home) + "/Library/Application Support/UltraFiler"
                    : std::string("UltraFiler");
#else
        const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
        if (xdgConfig) return std::string(xdgConfig) + "/UltraFiler";
        const char* home = std::getenv("HOME");
        return home ? std::string(home) + "/.config/UltraFiler" : std::string("UltraFiler");
#endif
    }

    static std::string GetConfigPath() { return GetConfigDirectory() + "/config.ini"; }

    bool Load() {
        std::ifstream file(GetConfigPath());
        if (!file.is_open()) return false;

        std::map<std::string, std::string> kv;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            kv[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
        }

        auto it = kv.find("preview.transparent.checkered");
        if (it != kv.end())
            previewCheckeredBackground =
                    (it->second == "true" || it->second == "1" || it->second == "yes");
        it = kv.find("preview.transparent.color");
        if (it != kv.end()) ParseColor(it->second, previewTransparentColor);
        it = kv.find("tree.drive.background.color");
        if (it != kv.end()) ParseColor(it->second, treeDriveBackgroundColor);
        it = kv.find("tree.selected.folder.color");
        if (it != kv.end()) ParseColor(it->second, treeSelectedFolderColor);
        it = kv.find("extras.prompt.application");
        if (it != kv.end()) promptApplication = it->second;
        return true;
    }

    bool Save() const {
        std::error_code ec;
        std::filesystem::create_directories(GetConfigDirectory(), ec);
        if (ec) return false;

        std::ofstream file(GetConfigPath());
        if (!file.is_open()) return false;

        file << "# UltraFiler Configuration\n\n";
        file << "preview.transparent.checkered = "
             << (previewCheckeredBackground ? "true" : "false") << "\n";
        file << "preview.transparent.color = "
             << FormatColor(previewTransparentColor) << "\n";
        file << "tree.drive.background.color = "
             << FormatColor(treeDriveBackgroundColor) << "\n";
        file << "tree.selected.folder.color = "
             << FormatColor(treeSelectedFolderColor) << "\n";
        file << "extras.prompt.application = " << promptApplication << "\n";
        return true;
    }

private:
    static std::string Trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t");
        return s.substr(start, end - start + 1);
    }

    // "#RRGGBB" — the backdrop colour is always opaque.
    static std::string FormatColor(const Color& c) {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", c.r, c.g, c.b);
        return std::string(buf);
    }

    static void ParseColor(const std::string& text, Color& out) {
        std::string hex = Trim(text);
        if (hex.size() == 7 && hex[0] == '#') hex.erase(0, 1);
        if (hex.size() != 6) return;
        char* end = nullptr;
        unsigned long v = std::strtoul(hex.c_str(), &end, 16);
        if (!end || *end != '\0') return;
        out = Color(static_cast<uint8_t>((v >> 16) & 0xFF),
                    static_cast<uint8_t>((v >> 8) & 0xFF),
                    static_cast<uint8_t>(v & 0xFF), 255);
    }
};

} // namespace UltraCanvas
