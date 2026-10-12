// Apps/UltraPaint/UltraPaintSettings.h
// UltraPaint's persistent settings: a key = value file in the shared
// UltraCanvas settings folder (UltraCanvasSettingsFolder()/UltraPaint/
// config.ini - ~/.config/UltraCanvas/UltraPaint/config.ini on Linux). The
// settings window (UltraPaintSettingsDialog) applies a change to the running
// editor at once and saves it right away, so there is no OK / Cancel.
// Version: 1.1.0 - Tool palette: an adaptive or a static toolbar
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasPathUtf8.h"       // PathFromUtf8 / PathToUtf8
#include "UltraCanvasSettingsFolder.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>

namespace UltraCanvas {

class UltraPaintSettings {
public:
    // ===== DISPLAY > MOUSE POINTER MAGNIFICATION GLASS =====
    // The magnifying glass is a small view over the canvas that shows the
    // pixels around the pointer enlarged, with a crosshair on the pointer's
    // own pixel, while a tool that places things to the pixel (the rectangle
    // selection) is active. It keeps out of the pointer's way: when the
    // pointer comes near it, it moves to another corner of the canvas.
    static constexpr int kMinMagnifierSize     = 100;   // px, the view is square
    static constexpr int kMaxMagnifierSize     = 400;
    static constexpr int kDefaultMagnifierSize = 180;
    static constexpr int kMinMagnifierFactor     = 2;   // times the view's own zoom
    static constexpr int kMaxMagnifierFactor     = 4;
    static constexpr int kDefaultMagnifierFactor = 3;

    bool magnifierEnabled = true;
    int  magnifierFactor  = kDefaultMagnifierFactor;
    int  magnifierSize    = kDefaultMagnifierSize;

    void RestoreMagnifierDefaults() {
        magnifierEnabled = true;
        magnifierFactor  = kDefaultMagnifierFactor;
        magnifierSize    = kDefaultMagnifierSize;
    }

    // ===== TOOL PALETTE =====
    // Tools of a kind share one button of the palette - the selections, the
    // pens, the shapes - and a bar with all of them folds out while the
    // pointer rests on it. An adaptive toolbar shows on that button the tool
    // last picked from its group; a static one always shows the group's
    // main tool.
    bool paletteAdaptive = true;

    void RestorePaletteDefaults() {
        paletteAdaptive = true;
    }

    // ===== THE FILE =====
    // Empty when the environment names no home to keep settings in.
    static std::filesystem::path ConfigFolder() {
        const std::filesystem::path base = UltraCanvasSettingsFolder();
        return base.empty() ? base : base / "UltraPaint";
    }
    static std::filesystem::path ConfigPath() {
        const std::filesystem::path folder = ConfigFolder();
        return folder.empty() ? folder : folder / "config.ini";
    }

    // False when there is no file yet; the defaults then stand.
    bool Load() {
        const std::filesystem::path path = ConfigPath();
        if (path.empty()) return false;
        std::ifstream file(path);
        if (!file.is_open()) return false;

        std::map<std::string, std::string> kv;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            kv[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
        }

        auto it = kv.find("display.magnifier.enabled");
        if (it != kv.end()) magnifierEnabled = ParseBool(it->second, magnifierEnabled);
        it = kv.find("display.magnifier.factor");
        if (it != kv.end()) ParseInt(it->second, magnifierFactor, kMinMagnifierFactor, kMaxMagnifierFactor);
        it = kv.find("display.magnifier.size");
        if (it != kv.end()) ParseInt(it->second, magnifierSize, kMinMagnifierSize, kMaxMagnifierSize);
        it = kv.find("palette.toolbar");
        if (it != kv.end()) {
            if (it->second == "adaptive") paletteAdaptive = true;
            else if (it->second == "static") paletteAdaptive = false;
        }
        return true;
    }

    bool Save() const {
        const std::filesystem::path path = ConfigPath();
        if (path.empty()) return false;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << "# UltraPaint configuration\n\n";
        file << "display.magnifier.enabled = " << (magnifierEnabled ? "true" : "false") << "\n";
        file << "display.magnifier.factor = " << magnifierFactor << "\n";
        file << "display.magnifier.size = " << magnifierSize << "\n";
        file << "palette.toolbar = " << (paletteAdaptive ? "adaptive" : "static") << "\n";
        return file.good();
    }

private:
    static std::string Trim(const std::string& s) {
        const size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return {};
        const size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }
    static bool ParseBool(const std::string& s, bool fallback) {
        if (s == "true" || s == "1" || s == "yes" || s == "on") return true;
        if (s == "false" || s == "0" || s == "no" || s == "off") return false;
        return fallback;
    }
    // A whole number in the file: a value outside [lo, hi] or not a number
    // leaves `out` as it was.
    static void ParseInt(const std::string& s, int& out, int lo, int hi) {
        if (s.empty()) return;
        char* end = nullptr;
        const long v = std::strtol(s.c_str(), &end, 10);   // locale-ok: a whole number
        if (end == s.c_str()) return;
        out = static_cast<int>(std::clamp<long>(v, lo, hi));
    }
};

} // namespace UltraCanvas
