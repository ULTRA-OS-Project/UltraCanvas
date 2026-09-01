// Apps/UltraFiler/UltraFilerSettings.h
// Persistent application settings for UltraFiler. Follows the UltraTexter
// config pattern: a simple key=value file in the platform config directory
// (~/.config/UltraFiler/config.ini on Linux, %APPDATA%\UltraFiler\config.ini
// on Windows, ~/Library/Application Support/UltraFiler/config.ini on macOS).
// Settings are applied live by the settings dialog and saved on every change.
// Version: 1.4.0
// Last Modified: 2026-08-25
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

    // Display > PDF Inventory: the range the thumbnail width slider offers and
    // the width the preview ships with. A thumbnail below ~32 px shows nothing
    // recognisable, and above ~120 px the inventory starts crowding the page
    // out of a preview pane.
    static constexpr int kMinPdfThumbnailWidth     = 32;
    static constexpr int kMaxPdfThumbnailWidth     = 120;
    static constexpr int kDefaultPdfThumbnailWidth = 56;
    // The same slider in relative mode: percent of the preview's own width.
    static constexpr int kMinPdfThumbnailPercent     = 5;
    static constexpr int kMaxPdfThumbnailPercent    = 40;
    static constexpr int kDefaultPdfThumbnailPercent = 25;

    // ===== THE SETTINGS =====
    // Media viewer: backdrop behind transparent images — the checkered
    // pattern used by image editors, or a preset solid colour (default white).
    bool  previewCheckeredBackground = false;
    Color previewTransparentColor    = Color(255, 255, 255, 255);

    // Display > PDF Inventory: how wide the page thumbnails in the preview's
    // PDF page inventory are - a fixed pixel width (what a preview pane wants:
    // the same strip whatever the pane's size) or a share of the preview's
    // width, so the inventory grows with the window.
    bool pdfThumbnailAbsoluteWidth = true;
    int  pdfThumbnailWidth         = kDefaultPdfThumbnailWidth;    // pixels
    int  pdfThumbnailWidthPercent  = kDefaultPdfThumbnailPercent;  // % of width

    // Display > Treeview: the row background of the drive entries in the
    // folder tree (the drive roots on Windows, "File System" and the mounted
    // volumes elsewhere), so the drives stand out from the folders below
    // them, and the highlight of the selected folder.
    Color treeDriveBackgroundColor = kDefaultTreeDriveBackgroundColor;
    Color treeSelectedFolderColor  = kDefaultTreeSelectedFolderColor;

    // Display > Home folder: what the Home folder shows - in the folder tree
    // and in the file display alike. "Predefined only" lists the main user
    // folders (Desktop, Documents, Downloads, Music, Pictures, Videos) and
    // nothing else; "all" lists every subfolder. A Windows profile carries a
    // dozen system folders ("3D Objects", "Saved Games", the sync clients),
    // so the curated view ships as the default there; a Linux or macOS home
    // folder is the user's own, so those default to showing everything.
#if defined(_WIN32) || defined(_WIN64)
    bool homeShowPredefinedOnly = true;
#else
    bool homeShowPredefinedOnly = false;
#endif

    // Handling > Drag & Drop: what dropping dragged files onto a folder of the
    // file display does without a modifier - move them (the default) or copy
    // them. Ctrl at the drop always copies and Shift always moves, whichever
    // way this is set.
    bool dropOnFolderCopies = false;

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
        it = kv.find("display.pdf.inventory.mode");
        if (it != kv.end()) pdfThumbnailAbsoluteWidth = (it->second != "relative");
        it = kv.find("display.pdf.inventory.width");
        if (it != kv.end())
            ParseInt(it->second, pdfThumbnailWidth,
                     kMinPdfThumbnailWidth, kMaxPdfThumbnailWidth);
        it = kv.find("display.pdf.inventory.percent");
        if (it != kv.end())
            ParseInt(it->second, pdfThumbnailWidthPercent,
                     kMinPdfThumbnailPercent, kMaxPdfThumbnailPercent);
        it = kv.find("tree.drive.background.color");
        if (it != kv.end()) ParseColor(it->second, treeDriveBackgroundColor);
        it = kv.find("tree.selected.folder.color");
        if (it != kv.end()) ParseColor(it->second, treeSelectedFolderColor);
        it = kv.find("display.home.content");
        if (it != kv.end()) homeShowPredefinedOnly = (it->second == "predefined");
        it = kv.find("handling.dragdrop.drop.on.folder");
        if (it != kv.end()) dropOnFolderCopies = (it->second == "copy");
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
        file << "display.pdf.inventory.mode = "
             << (pdfThumbnailAbsoluteWidth ? "absolute" : "relative") << "\n";
        file << "display.pdf.inventory.width = " << pdfThumbnailWidth << "\n";
        file << "display.pdf.inventory.percent = " << pdfThumbnailWidthPercent
             << "\n";
        file << "tree.drive.background.color = "
             << FormatColor(treeDriveBackgroundColor) << "\n";
        file << "tree.selected.folder.color = "
             << FormatColor(treeSelectedFolderColor) << "\n";
        file << "display.home.content = "
             << (homeShowPredefinedOnly ? "predefined" : "all") << "\n";
        file << "handling.dragdrop.drop.on.folder = "
             << (dropOnFolderCopies ? "copy" : "move") << "\n";
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

    // A whole number, kept inside [minValue, maxValue]; anything unparseable
    // leaves the setting at the value it had.
    static void ParseInt(const std::string& text, int& out,
                         int minValue, int maxValue) {
        const std::string trimmed = Trim(text);
        if (trimmed.empty()) return;
        char* end = nullptr;
        const long v = std::strtol(trimmed.c_str(), &end, 10);
        if (!end || *end != '\0') return;
        out = static_cast<int>(v < minValue ? minValue
                                            : (v > maxValue ? maxValue : v));
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
