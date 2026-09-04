// Apps/UltraFiler/UltraFilerFolderIcons.h
// User-defined folder icons of UltraFiler — the data behind the filer context
// menus' "Extras > Set folder icon". A folder can be given any image the
// framework can read (SVG, PNG, JPEG, WebP, QOI, …); the picture is converted
// once to a QOI file in the application's config directory and it is that copy
// the file display and the folder tree draw, so the icon survives the original
// being moved, renamed or deleted, and decoding it costs no format plugin.
// Persisted next to the settings (UltraFilerSettings::GetConfigDirectory()) as
// a tab separated text file, so paths keep their '=' and spaces.
//
// The well-known user folders (Desktop, Documents, Downloads, Music, Pictures,
// Videos) carry icons of their own without anything being stored here — those
// live in `media/icons` and are resolved by UltraFilerWindow. This class only
// holds what the user set, which is also why it wins over them.
// Version: 1.0.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerSettings.h"

#include "UltraCanvasImage.h"   // SaveImageFileAsQoi

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>

namespace UltraCanvas {

class UltraFilerFolderIcons {
public:
    // Edge length the chosen picture is converted at. Big enough for the
    // "Maximized thumbnails" view (which draws folder icons at up to ~200 px)
    // and for a HiDPI display of it, small enough that a converted icon stays
    // a file of a few tens of kilobytes.
    static constexpr int kIconEdge = 256;

    // ===== READING =====
    // The icon the user set for `folderPath`, or "" when they set none. Two
    // spellings of the same folder answer the same (see IdentityKey).
    std::string IconFor(const std::string& folderPath) const {
        if (folderPath.empty()) return {};
        auto it = icons.find(IdentityKey(folderPath));
        return it == icons.end() ? std::string() : it->second;
    }

    bool HasIcon(const std::string& folderPath) const {
        return !IconFor(folderPath).empty();
    }

    // ===== SETTING =====
    // Converts `imagePath` to a QOI icon of this folder and saves the mapping.
    // Returns false with `error` filled when the picture cannot be read or the
    // icon cannot be written; nothing changes then. A previous icon of the
    // same folder is replaced and its file deleted.
    bool SetFromImage(const std::string& folderPath, const std::string& imagePath,
                      std::string& error) {
        if (folderPath.empty() || imagePath.empty()) {
            error = "No folder or image given.";
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(GetIconsDirectory(), ec);
        if (ec) {
            error = "Cannot create " + GetIconsDirectory() + ": " + ec.message();
            return false;
        }

        const std::string key = IdentityKey(folderPath);
        const std::string destination = UnusedIconPath(key);
        // The conversion is the step that can fail on the user's choice (an
        // image the build has no loader for, a file that is not an image at
        // all), so it happens before anything is recorded.
        const std::string why = SaveImageFileAsQoi(imagePath, destination, kIconEdge);
        if (!why.empty()) {
            error = why;
            std::filesystem::remove(destination, ec);   // no half-written icon
            return false;
        }

        auto existing = icons.find(key);
        if (existing != icons.end()) {
            RemoveIconFile(existing->second);
            existing->second = destination;
        } else {
            icons.emplace(key, destination);
        }
        Save();
        return true;
    }

    // Drops the icon of `folderPath` and deletes the converted file. Returns
    // false when the folder had no icon of its own.
    bool Clear(const std::string& folderPath) {
        auto it = icons.find(IdentityKey(folderPath));
        if (it == icons.end()) return false;
        RemoveIconFile(it->second);
        icons.erase(it);
        Save();
        return true;
    }

    // ===== PERSISTENCE =====

    static std::string GetIconsDirectory() {
        return UltraFilerSettings::GetConfigDirectory() + "/foldericons";
    }

    static std::string GetIconsPath() {
        return UltraFilerSettings::GetConfigDirectory() + "/foldericons.txt";
    }

    bool Load() {
        icons.clear();

        std::ifstream file(GetIconsPath());
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;

            // icon file \t folder path   (the folder path may contain anything
            // but a tab, so it is taken as the rest of the line).
            const size_t tab = line.find('\t');
            if (tab == std::string::npos) continue;
            const std::string icon = line.substr(0, tab);
            const std::string folder = line.substr(tab + 1);
            if (icon.empty() || folder.empty()) continue;
            // An icon file that is gone (a config directory copied without it,
            // a user tidying up) would draw nothing at all, which is worse
            // than the default folder look: forget the mapping instead. The
            // folder itself is NOT checked - one on an unplugged drive keeps
            // its icon for when it comes back.
            std::error_code ec;
            if (!std::filesystem::exists(icon, ec) || ec) continue;
            icons[IdentityKey(folder)] = icon;
        }
        return true;
    }

    bool Save() const {
        std::error_code ec;
        std::filesystem::create_directories(
                UltraFilerSettings::GetConfigDirectory(), ec);
        if (ec) return false;

        std::ofstream file(GetIconsPath());
        if (!file.is_open()) return false;

        file << "# UltraFiler folder icons\n";
        // The folder is written as its identity key (see IdentityKey), which
        // is the path normalised - and lower-cased on Windows, where a path
        // is not case sensitive.
        file << "# icon file<TAB>folder\n";
        for (const auto& entry : icons)
            file << entry.second << '\t' << entry.first << '\n';
        return true;
    }

    // ===== FOLDER IDENTITY =====
    // Makes two spellings of the same folder compare equal: normalised, no
    // trailing separator, case-folded on Windows. UltraFilerWindow compares
    // folders through this too, so what the tree, the file display and this
    // store call "the same folder" cannot drift apart.
    static std::string IdentityKey(const std::string& path) {
        std::string key = std::filesystem::path(path).lexically_normal().string();
        while (key.size() > 1 && (key.back() == '/' || key.back() == '\\'))
            key.pop_back();
#if defined(_WIN32) || defined(_WIN64)
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
#endif
        return key;
    }

private:
    // A file name of our own for `key`'s icon: the hash of the folder,
    // stepped past any name another folder already uses (a hash collision, or
    // an icon left behind by a folder whose entry was dropped).
    static std::string UnusedIconPath(const std::string& key) {
        const std::string base = GetIconsDirectory() + "/" +
                ToHex(std::hash<std::string>{}(key));
        std::error_code ec;
        for (int suffix = 0; suffix < 1000; ++suffix) {
            std::string candidate = suffix == 0
                    ? base + ".qoi"
                    : base + "-" + std::to_string(suffix) + ".qoi";
            if (!std::filesystem::exists(candidate, ec) || ec) return candidate;
        }
        return base + ".qoi";
    }

    static std::string ToHex(size_t value) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%016llx",
                      static_cast<unsigned long long>(value));
        return buffer;
    }

    // Deletes a converted icon — but only one of ours, so a path that somehow
    // points at the user's own picture is left alone.
    static void RemoveIconFile(const std::string& iconPath) {
        const std::string dir = GetIconsDirectory();
        if (iconPath.size() <= dir.size() ||
            iconPath.compare(0, dir.size(), dir) != 0)
            return;
        std::error_code ec;
        std::filesystem::remove(iconPath, ec);
    }

    // folder identity key -> converted icon file
    std::map<std::string, std::string> icons;
};

} // namespace UltraCanvas
