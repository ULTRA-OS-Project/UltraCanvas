// Apps/UltraViewer/UltraViewerSettings.h
// UltraViewer's persistent settings: a key = value file in the shared
// UltraCanvas settings folder (UltraCanvasSettingsFolder()/UltraViewer/
// config.ini - ~/.config/UltraCanvas/UltraViewer/config.ini on Linux). The
// settings dialog (the gear in the viewer's info bar) applies a change at
// once and saves it right away.
// Version: 1.0.0 - how long the viewer may be left alone before it forgets
//                  the passwords Unlock was given
// Last Modified: 2026-10-19
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasSettingsFolder.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>

namespace UltraCanvas {

class UltraViewerSettings {
public:
    // ===== PROTECTED FILES =====
    // How long the viewer may go without a key or mouse input before it
    // forgets the passwords Unlock was given and shows a file it unlocked
    // locked again (UltraCanvasMediaViewer::SetForgetPasswordsAfterIdle).
    // 0: never for idleness - the screen locking still forgets them.
    static constexpr int kMaxForgetPasswordsMinutes     = 120;
    static constexpr int kDefaultForgetPasswordsMinutes = 10;
    int forgetPasswordsAfterMinutes = kDefaultForgetPasswordsMinutes;

    // ===== THE FILE =====
    // Empty when the environment names no home to keep settings in.
    static std::filesystem::path ConfigPath() {
        const std::filesystem::path base = UltraCanvasSettingsFolder();
        return base.empty() ? base : base / "UltraViewer" / "config.ini";
    }

    // False when there is no file yet; the defaults then stand.
    bool Load() { return LoadFrom(ConfigPath()); }
    bool Save() const { return SaveTo(ConfigPath()); }

    bool LoadFrom(const std::filesystem::path& path) {
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
        auto it = kv.find("passwords.forget.minutes");
        if (it != kv.end()) ParseInt(it->second, forgetPasswordsAfterMinutes, 0, kMaxForgetPasswordsMinutes);
        return true;
    }

    bool SaveTo(const std::filesystem::path& path) const {
        if (path.empty()) return false;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return false;
        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << "# UltraViewer configuration\n\n";
        file << "passwords.forget.minutes = " << forgetPasswordsAfterMinutes << "\n";
        return file.good();
    }

private:
    static std::string Trim(const std::string& s) {
        const size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return {};
        const size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }
    // A whole number in the file: one that is not a number leaves `out` as
    // it was; one outside [lo, hi] is brought into it.
    static void ParseInt(const std::string& s, int& out, int lo, int hi) {
        if (s.empty()) return;
        char* end = nullptr;
        const long v = std::strtol(s.c_str(), &end, 10);   // locale-ok: a whole number
        if (end == s.c_str()) return;
        out = static_cast<int>(std::clamp<long>(v, lo, hi));
    }
};

} // namespace UltraCanvas
