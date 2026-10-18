// Apps/ArtCreator/ArtCreatorSettings.h
// ArtCreator's persistent settings: a key = value file in the shared
// UltraCanvas settings folder (UltraCanvasSettingsFolder()/ArtCreator/
// config.ini - ~/.config/UltraCanvas/ArtCreator/config.ini on Linux). It
// keeps the colour picker the way it was left: the design chosen in its list
// and whether its channels are open. The picker changes them, so they are
// saved as they change; there is no settings window.
// Version: 1.0.0
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasColorPicker.h"    // ColorPickerWheelStyle and its names
#include "UltraCanvasSettingsFolder.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>

namespace UltraCanvas {

class ArtCreatorSettings {
public:
    // ===== COLOUR PICKER =====
    // The design chosen in the colour picker's list (circle, slider + square,
    // rectangle, two sliders) and whether its channels - the HSV / HSL / RGB
    // tabs and sliders - are open (picker.design, picker.channels).
    ColorPickerWheelStyle pickerDesign = ColorPickerWheelStyle::Ring;
    bool pickerChannelsOpen = false;

    // ===== THE FILE =====
    // Empty when the environment names no home to keep settings in.
    static std::filesystem::path ConfigFolder() {
        const std::filesystem::path base = UltraCanvasSettingsFolder();
        return base.empty() ? base : base / "ArtCreator";
    }
    static std::filesystem::path ConfigPath() {
        const std::filesystem::path folder = ConfigFolder();
        return folder.empty() ? folder : folder / "config.ini";
    }

    // False when there is no file yet; the defaults then stand. A value the
    // file cannot mean leaves the setting as it was.
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

        auto it = kv.find("picker.design");
        if (it != kv.end()) ColorPickerWheelStyleFromName(it->second, pickerDesign);
        it = kv.find("picker.channels");
        if (it != kv.end()) {
            if (it->second == "open") pickerChannelsOpen = true;
            else if (it->second == "folded") pickerChannelsOpen = false;
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
        file << "# ArtCreator configuration\n\n";
        file << "picker.design = " << ColorPickerWheelStyleName(pickerDesign) << "\n";
        file << "picker.channels = " << (pickerChannelsOpen ? "open" : "folded") << "\n";
        return file.good();
    }

private:
    static std::string Trim(const std::string& s) {
        const size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return {};
        const size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }
};

} // namespace UltraCanvas
