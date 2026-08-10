// Apps/UltraFiler/UltraFilerFavorites.h
// Pinned favorites of UltraFiler — the data behind the toolbar's heart button
// (the Favorites view with its Files / Folders / Apps tabs) and behind the
// folder tree's "Pinned" section. Unlike the history, nothing enters these
// lists on its own: every entry was pinned deliberately (menu bar "Pin") and
// stays until it is unpinned or vanishes from disk. Entries keep the order
// they were pinned in. Persisted next to the settings
// (UltraFilerSettings::GetConfigDirectory()) as a tab separated text file,
// so paths keep their '=' and spaces.
// Version: 1.0.0
// Last Modified: 2026-08-10
// Author: UltraCanvas Framework
#pragma once

#include "UltraFilerSettings.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== WHAT KIND OF PIN =====
// File / Folder / App fill the three tabs of the Favorites view; Tree holds
// the folders shown under the folder tree's "Pinned" node. The values double
// as indexes into UltraFilerFavorites' lists, so keep them contiguous and
// starting at 0.
enum class FilerFavoriteKind {
    File = 0,     // a pinned document
    Folder,       // a pinned folder (Favorites view's Folders tab)
    App,          // a pinned application / installer
    Tree          // a folder pinned into the folder tree's "Pinned" section
};
constexpr size_t kFilerFavoriteKindCount = 4;

// ===== ONE PINNED PATH =====
struct FilerFavoriteItem {
    std::string path;
    std::time_t pinnedAt = 0;    // epoch seconds of when it was pinned
};

class UltraFilerFavorites {
public:
    // Longest list kept per kind; further pins are refused rather than
    // silently dropping the oldest — a favorite is not a history entry.
    static constexpr size_t kMaxItemsPerKind = 300;

    // ===== PINNING =====
    // Appends `path` to its list (favorites keep pin order) and saves.
    // Returns false when the path is already pinned, empty, or the list is
    // full — nothing changes then.
    bool Pin(FilerFavoriteKind kind, const std::string& path) {
        if (path.empty()) return false;
        std::vector<FilerFavoriteItem>& list = ListOf(kind);
        if (list.size() >= kMaxItemsPerKind) return false;
        if (Contains(list, path)) return false;

        FilerFavoriteItem item;
        item.path = path;
        item.pinnedAt = std::time(nullptr);
        list.push_back(std::move(item));
        Save();
        return true;
    }

    // Removes `path` from its list and saves. Returns false when it was not
    // pinned.
    bool Unpin(FilerFavoriteKind kind, const std::string& path) {
        std::vector<FilerFavoriteItem>& list = ListOf(kind);
        auto it = std::find_if(list.begin(), list.end(),
                [&path](const FilerFavoriteItem& i) { return i.path == path; });
        if (it == list.end()) return false;
        list.erase(it);
        Save();
        return true;
    }

    bool IsPinned(FilerFavoriteKind kind, const std::string& path) const {
        return Contains(lists[IndexOf(kind)], path);
    }

    // ===== READING =====
    // The pinned paths in pin order. Entries that have meanwhile disappeared
    // from disk are forgotten here rather than shown as dead tiles, so the
    // lists heal themselves as the filesystem changes.
    std::vector<std::string> Paths(FilerFavoriteKind kind) {
        std::vector<FilerFavoriteItem>& list = ListOf(kind);
        std::vector<std::string> paths;
        paths.reserve(list.size());
        const size_t before = list.size();
        std::error_code ec;
        for (size_t i = 0; i < list.size();) {
            if (!std::filesystem::exists(list[i].path, ec) || ec) {
                ec.clear();
                list.erase(list.begin() + i);
                continue;
            }
            paths.push_back(list[i].path);
            ++i;
        }
        if (list.size() != before) Save();
        return paths;
    }

    const std::vector<FilerFavoriteItem>& Items(FilerFavoriteKind kind) const {
        return lists[IndexOf(kind)];
    }

    // ===== CLEARING =====
    void Clear(FilerFavoriteKind kind) {
        ListOf(kind).clear();
        Save();
    }

    void ClearAll() {
        for (std::vector<FilerFavoriteItem>& list : lists) list.clear();
        Save();
    }

    bool IsEmpty() const {
        for (const std::vector<FilerFavoriteItem>& list : lists)
            if (!list.empty()) return false;
        return true;
    }

    // ===== PERSISTENCE =====

    static std::string GetFavoritesPath() {
        return UltraFilerSettings::GetConfigDirectory() + "/favorites.txt";
    }

    bool Load() {
        for (std::vector<FilerFavoriteItem>& list : lists) list.clear();

        std::ifstream file(GetFavoritesPath());
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;

            // kind \t pinnedAt \t path   (the path may contain anything but a
            // tab, so it is taken as the rest of the line).
            const size_t t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            const size_t t2 = line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;

            FilerFavoriteKind kind;
            if (!ParseKind(line.substr(0, t1), kind)) continue;

            FilerFavoriteItem item;
            item.pinnedAt = static_cast<std::time_t>(
                    std::strtoll(line.substr(t1 + 1, t2 - t1 - 1).c_str(), nullptr, 10));
            item.path = line.substr(t2 + 1);
            if (item.path.empty()) continue;

            std::vector<FilerFavoriteItem>& list = ListOf(kind);
            if (list.size() < kMaxItemsPerKind && !Contains(list, item.path))
                list.push_back(std::move(item));
        }
        return true;
    }

    bool Save() const {
        std::error_code ec;
        std::filesystem::create_directories(
                UltraFilerSettings::GetConfigDirectory(), ec);
        if (ec) return false;

        std::ofstream file(GetFavoritesPath());
        if (!file.is_open()) return false;

        file << "# UltraFiler Favorites\n";
        file << "# kind<TAB>pinned at (epoch seconds)<TAB>path\n";
        for (size_t k = 0; k < kFilerFavoriteKindCount; ++k) {
            for (const FilerFavoriteItem& item : lists[k]) {
                file << KindName(static_cast<FilerFavoriteKind>(k)) << '\t'
                     << static_cast<long long>(item.pinnedAt) << '\t'
                     << item.path << '\n';
            }
        }
        return true;
    }

private:
    static size_t IndexOf(FilerFavoriteKind kind) {
        const size_t index = static_cast<size_t>(kind);
        return index < kFilerFavoriteKindCount ? index : 0;
    }

    std::vector<FilerFavoriteItem>& ListOf(FilerFavoriteKind kind) {
        return lists[IndexOf(kind)];
    }

    static bool Contains(const std::vector<FilerFavoriteItem>& list,
                         const std::string& path) {
        return std::find_if(list.begin(), list.end(),
                [&path](const FilerFavoriteItem& i) { return i.path == path; })
               != list.end();
    }

    static const char* KindName(FilerFavoriteKind kind) {
        switch (kind) {
            case FilerFavoriteKind::File:   return "file";
            case FilerFavoriteKind::Folder: return "folder";
            case FilerFavoriteKind::App:    return "app";
            case FilerFavoriteKind::Tree:   return "tree";
        }
        return "file";
    }

    static bool ParseKind(const std::string& name, FilerFavoriteKind& out) {
        if (name == "file")   { out = FilerFavoriteKind::File;   return true; }
        if (name == "folder") { out = FilerFavoriteKind::Folder; return true; }
        if (name == "app")    { out = FilerFavoriteKind::App;    return true; }
        if (name == "tree")   { out = FilerFavoriteKind::Tree;   return true; }
        return false;
    }

    std::vector<FilerFavoriteItem> lists[kFilerFavoriteKindCount];
};

} // namespace UltraCanvas
