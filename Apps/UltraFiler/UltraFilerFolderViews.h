// Apps/UltraFiler/UltraFilerFolderViews.h
// Per-folder display state: how each folder was last looked at - view type,
// sort field and sort direction. Entering a folder restores what it had, so a
// picture folder can stay on large thumbnails sorted by date while a source
// folder stays on details sorted by name, without either being reset by the
// other. Stored next to the other UltraFiler lists as folderviews.txt
// (one line per folder), most recently used first and capped, so the file
// cannot grow without bound.
// Version: 1.0.0
// Last Modified: 2026-08-23
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasFilerWidget.h"   // FilerViewType, FilerSortField
#include "UltraFilerSettings.h"       // GetConfigDirectory()

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace UltraCanvas {

// One folder's remembered display state.
struct FilerFolderView {
    std::string    path;
    FilerViewType  view      = FilerViewType::ThumbnailsMedium;
    FilerSortField sort      = FilerSortField::Name;
    bool           ascending = true;
    std::time_t    usedAt    = 0;
};

class UltraFilerFolderViews {
public:
    // Folders remembered at once. Beyond this the least recently entered one
    // is dropped - a folder nobody has opened in months is not worth a line.
    static constexpr size_t kMaxEntries = 400;

    // ===== QUERIES =====

    const FilerFolderView* Find(const std::string& path) const {
        auto it = std::find_if(entries.begin(), entries.end(),
                [&path](const FilerFolderView& e) { return e.path == path; });
        return it == entries.end() ? nullptr : &*it;
    }

    size_t Count() const { return entries.size(); }

    // ===== MUTATORS =====

    // Record how `path` is being looked at now and persist it. A folder whose
    // state is unchanged is still moved to the front (it was just entered),
    // but nothing is written - the file only changes when the state does.
    void Remember(const std::string& path, FilerViewType view,
                  FilerSortField sort, bool ascending) {
        if (path.empty()) return;
        auto it = std::find_if(entries.begin(), entries.end(),
                [&path](const FilerFolderView& e) { return e.path == path; });
        const bool same = it != entries.end() && it->view == view &&
                          it->sort == sort && it->ascending == ascending;
        FilerFolderView entry;
        entry.path = path;
        entry.view = view;
        entry.sort = sort;
        entry.ascending = ascending;
        entry.usedAt = std::time(nullptr);
        if (it != entries.end()) entries.erase(it);
        entries.insert(entries.begin(), std::move(entry));
        if (entries.size() > kMaxEntries) entries.resize(kMaxEntries);
        if (!same) Save();
    }

    void ClearAll() {
        entries.clear();
        Save();
    }

    // ===== PERSISTENCE =====

    static std::string GetFolderViewsPath() {
        return UltraFilerSettings::GetConfigDirectory() + "/folderviews.txt";
    }

    bool Load() {
        entries.clear();
        std::ifstream file(GetFolderViewsPath());
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            // path <TAB> view <TAB> sort <TAB> asc|desc <TAB> used at
            std::vector<std::string> parts;
            size_t start = 0;
            for (;;) {
                size_t tab = line.find('\t', start);
                if (tab == std::string::npos) {
                    parts.push_back(line.substr(start));
                    break;
                }
                parts.push_back(line.substr(start, tab - start));
                start = tab + 1;
            }
            if (parts.size() < 4 || parts[0].empty()) continue;
            FilerFolderView entry;
            entry.path = parts[0];
            if (!ParseView(parts[1], entry.view)) continue;
            if (!ParseSort(parts[2], entry.sort)) continue;
            entry.ascending = (parts[3] != "desc");
            if (parts.size() > 4) {
                try { entry.usedAt = static_cast<std::time_t>(std::stoll(parts[4])); }
                catch (...) { entry.usedAt = 0; }
            }
            entries.push_back(std::move(entry));
            if (entries.size() >= kMaxEntries) break;
        }
        return true;
    }

    bool Save() const {
        std::error_code ec;
        std::filesystem::create_directories(
                UltraFilerSettings::GetConfigDirectory(), ec);
        if (ec) return false;

        std::ofstream file(GetFolderViewsPath());
        if (!file.is_open()) return false;

        file << "# UltraFiler folder display state\n";
        file << "# path<TAB>view<TAB>sort<TAB>asc|desc<TAB>last entered (epoch seconds)\n";
        for (const FilerFolderView& e : entries) {
            file << e.path << '\t' << ViewName(e.view) << '\t'
                 << SortName(e.sort) << '\t' << (e.ascending ? "asc" : "desc")
                 << '\t' << static_cast<long long>(e.usedAt) << '\n';
        }
        return true;
    }

private:
    // Names rather than numbers: the file stays readable, and reordering the
    // enums cannot silently turn every stored folder into a different view.
    static const char* ViewName(FilerViewType v) {
        switch (v) {
            case FilerViewType::Details:             return "details";
            case FilerViewType::List:                return "list";
            case FilerViewType::ThumbnailsSmall:     return "icons-small";
            case FilerViewType::ThumbnailsMedium:    return "icons-medium";
            case FilerViewType::ThumbnailsBig:       return "icons-big";
            case FilerViewType::ThumbnailsMaximized: return "icons-max";
            case FilerViewType::BarSize:             return "bars";
            case FilerViewType::TreeMap:             return "treemap";
            case FilerViewType::GourceTree:          return "gource";
            case FilerViewType::View3D:              return "view3d";
        }
        return "icons-medium";
    }

    static bool ParseView(const std::string& name, FilerViewType& out) {
        static const struct { const char* name; FilerViewType value; } kViews[] = {
            {"details",      FilerViewType::Details},
            {"list",         FilerViewType::List},
            {"icons-small",  FilerViewType::ThumbnailsSmall},
            {"icons-medium", FilerViewType::ThumbnailsMedium},
            {"icons-big",    FilerViewType::ThumbnailsBig},
            {"icons-max",    FilerViewType::ThumbnailsMaximized},
            {"bars",         FilerViewType::BarSize},
            {"treemap",      FilerViewType::TreeMap},
            {"gource",       FilerViewType::GourceTree},
            {"view3d",       FilerViewType::View3D},
        };
        for (const auto& v : kViews)
            if (name == v.name) { out = v.value; return true; }
        return false;
    }

    static const char* SortName(FilerSortField f) {
        switch (f) {
            case FilerSortField::Name:         return "name";
            case FilerSortField::Size:         return "size";
            case FilerSortField::Type:         return "type";
            case FilerSortField::ModifiedDate: return "modified";
            case FilerSortField::CreatedDate:  return "created";
        }
        return "name";
    }

    static bool ParseSort(const std::string& name, FilerSortField& out) {
        static const struct { const char* name; FilerSortField value; } kSorts[] = {
            {"name",     FilerSortField::Name},
            {"size",     FilerSortField::Size},
            {"type",     FilerSortField::Type},
            {"modified", FilerSortField::ModifiedDate},
            {"created",  FilerSortField::CreatedDate},
        };
        for (const auto& s : kSorts)
            if (name == s.name) { out = s.value; return true; }
        return false;
    }

    std::vector<FilerFolderView> entries;   // most recently entered first
};

} // namespace UltraCanvas
