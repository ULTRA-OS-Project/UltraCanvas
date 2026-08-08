// Apps/UltraFiler/UltraFilerHistory.h
// Recently used files, folders and applications of UltraFiler — the data
// behind the toolbar's clock button (the History view with its Files /
// Folders / Apps tabs). Three most-recently-used lists, each capped, each
// entry remembering when it was last used and how often it was used.
// Persisted next to the settings (UltraFilerSettings::GetConfigDirectory())
// as a tab separated text file, so paths keep their '=' and spaces.
// Version: 1.0.0
// Last Modified: 2026-08-08
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

// ===== WHAT KIND OF THING WAS USED =====
// One most-recently-used list per kind; the values double as indexes into
// UltraFilerHistory's lists, so keep them contiguous and starting at 0.
enum class FilerHistoryKind {
    File = 0,     // a document that was opened
    Folder,       // a folder that was worked in (not merely browsed)
    App           // an application / installer that was launched
};
constexpr size_t kFilerHistoryKindCount = 3;

// ===== ONE REMEMBERED PATH =====
struct FilerHistoryItem {
    std::string path;
    std::time_t lastUsed = 0;    // epoch seconds of the most recent use
    uint32_t    useCount = 0;    // how often it was used
};

class UltraFilerHistory {
public:
    // Longest list kept per kind; the oldest entries drop off the end.
    static constexpr size_t kMaxItemsPerKind = 300;

    // ===== RECORDING =====
    // Moves `path` to the front of its list (adding it when new) and saves.
    void Record(FilerHistoryKind kind, const std::string& path) {
        if (path.empty()) return;
        std::vector<FilerHistoryItem>& list = ListOf(kind);

        FilerHistoryItem item;
        item.path = path;
        auto it = std::find_if(list.begin(), list.end(),
                [&path](const FilerHistoryItem& i) { return i.path == path; });
        if (it != list.end()) {
            item.useCount = it->useCount;
            list.erase(it);
        }
        item.useCount += 1;
        item.lastUsed = std::time(nullptr);
        list.insert(list.begin(), std::move(item));
        if (list.size() > kMaxItemsPerKind) list.resize(kMaxItemsPerKind);
        Save();
    }

    // ===== READING =====
    // The remembered paths, most recently used first. Entries that have
    // meanwhile disappeared from disk are forgotten here rather than shown as
    // dead tiles, so the lists heal themselves as the filesystem changes.
    std::vector<std::string> Paths(FilerHistoryKind kind) {
        std::vector<FilerHistoryItem>& list = ListOf(kind);
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

    const std::vector<FilerHistoryItem>& Items(FilerHistoryKind kind) const {
        return lists[IndexOf(kind)];
    }

    // ===== CLEARING =====
    void Clear(FilerHistoryKind kind) {
        ListOf(kind).clear();
        Save();
    }

    void ClearAll() {
        for (std::vector<FilerHistoryItem>& list : lists) list.clear();
        Save();
    }

    bool IsEmpty() const {
        for (const std::vector<FilerHistoryItem>& list : lists)
            if (!list.empty()) return false;
        return true;
    }

    // ===== PERSISTENCE =====

    static std::string GetHistoryPath() {
        return UltraFilerSettings::GetConfigDirectory() + "/history.txt";
    }

    bool Load() {
        for (std::vector<FilerHistoryItem>& list : lists) list.clear();

        std::ifstream file(GetHistoryPath());
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;

            // kind \t lastUsed \t useCount \t path   (the path may contain
            // anything but a tab, so it is taken as the rest of the line).
            const size_t t1 = line.find('\t');
            if (t1 == std::string::npos) continue;
            const size_t t2 = line.find('\t', t1 + 1);
            if (t2 == std::string::npos) continue;
            const size_t t3 = line.find('\t', t2 + 1);
            if (t3 == std::string::npos) continue;

            FilerHistoryKind kind;
            if (!ParseKind(line.substr(0, t1), kind)) continue;

            FilerHistoryItem item;
            item.lastUsed = static_cast<std::time_t>(
                    std::strtoll(line.substr(t1 + 1, t2 - t1 - 1).c_str(), nullptr, 10));
            item.useCount = static_cast<uint32_t>(
                    std::strtoul(line.substr(t2 + 1, t3 - t2 - 1).c_str(), nullptr, 10));
            item.path = line.substr(t3 + 1);
            if (item.path.empty()) continue;

            std::vector<FilerHistoryItem>& list = ListOf(kind);
            if (list.size() < kMaxItemsPerKind) list.push_back(std::move(item));
        }

        // The file is written newest first, but a hand-edited one need not be.
        for (std::vector<FilerHistoryItem>& list : lists) {
            std::stable_sort(list.begin(), list.end(),
                    [](const FilerHistoryItem& a, const FilerHistoryItem& b) {
                        return a.lastUsed > b.lastUsed;
                    });
        }
        return true;
    }

    bool Save() const {
        std::error_code ec;
        std::filesystem::create_directories(
                UltraFilerSettings::GetConfigDirectory(), ec);
        if (ec) return false;

        std::ofstream file(GetHistoryPath());
        if (!file.is_open()) return false;

        file << "# UltraFiler History\n";
        file << "# kind<TAB>last used (epoch seconds)<TAB>use count<TAB>path\n";
        for (size_t k = 0; k < kFilerHistoryKindCount; ++k) {
            for (const FilerHistoryItem& item : lists[k]) {
                file << KindName(static_cast<FilerHistoryKind>(k)) << '\t'
                     << static_cast<long long>(item.lastUsed) << '\t'
                     << item.useCount << '\t' << item.path << '\n';
            }
        }
        return true;
    }

private:
    static size_t IndexOf(FilerHistoryKind kind) {
        const size_t index = static_cast<size_t>(kind);
        return index < kFilerHistoryKindCount ? index : 0;
    }

    std::vector<FilerHistoryItem>& ListOf(FilerHistoryKind kind) {
        return lists[IndexOf(kind)];
    }

    static const char* KindName(FilerHistoryKind kind) {
        switch (kind) {
            case FilerHistoryKind::File:   return "file";
            case FilerHistoryKind::Folder: return "folder";
            case FilerHistoryKind::App:    return "app";
        }
        return "file";
    }

    static bool ParseKind(const std::string& name, FilerHistoryKind& out) {
        if (name == "file")   { out = FilerHistoryKind::File;   return true; }
        if (name == "folder") { out = FilerHistoryKind::Folder; return true; }
        if (name == "app")    { out = FilerHistoryKind::App;    return true; }
        return false;
    }

    std::vector<FilerHistoryItem> lists[kFilerHistoryKindCount];
};

} // namespace UltraCanvas
