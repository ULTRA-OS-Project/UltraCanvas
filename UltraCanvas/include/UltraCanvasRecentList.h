// UltraCanvas/include/UltraCanvasRecentList.h
// A list of what was opened last - site folders, bookkeeping files, projects
// - kept as one entry per line in a file of the application's settings
// folder, the newest first, with a cap. An application remembers an entry
// when it opens the thing, forgets one when the thing is gone, and reads the
// list for its start window. The entries are the application's strings: a
// path it normalised, a name, an id - this file compares them as text.
//
// Header-only, like UltraCanvasDataFolder.h, so a start window that links
// nothing but the UI uses it, and a test runs it on a temp folder. The file
// is UTF-8, as every path in an UltraCanvas application is
// (UltraCanvasPathUtf8.h); a missing or unreadable file is an empty list.
//
// doc-check: example
//   // UltraPress: the site folders the editor opened, the last one first.
//   const std::string file = UltraCanvas::PathToUtf8(UltraCanvas::UltraCanvasSettingsFolder() / "UltraPress" / "recent");
//   UltraCanvas::RememberRecent(file, "/home/erika/sites/shop");          // now first, once
//   const std::vector<std::string> recent = UltraCanvas::LoadRecentList(file);
//   UltraCanvas::ForgetRecent(file, "/home/erika/sites/old");            // gone since
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasPathUtf8.h"   // PathFromUtf8
#include "UltraCanvasSettingsFolder.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace UltraCanvas {

    // The entries of `file`, first first, each once; empty when the file is
    // missing or cannot be read, or `file` is empty.
    inline std::vector<std::string> LoadRecentList(const std::string& file) {
        std::vector<std::string> entries;
        if (file.empty()) return entries;
        std::ifstream in(PathFromUtf8(file));
        std::string line;
        while (in && std::getline(in, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && std::find(entries.begin(), entries.end(), line) == entries.end())
                entries.push_back(line);
        }
        return entries;
    }

    // Writes `entries` as `file`, its folder made first; false when the file
    // cannot be written.
    inline bool SaveRecentList(const std::string& file, const std::vector<std::string>& entries) {
        if (file.empty()) return false;
        const std::filesystem::path path = PathFromUtf8(file);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::trunc);
        if (!out) return false;
        for (const std::string& entry : entries) out << entry << '\n';
        return static_cast<bool>(out);
    }

    // Puts `entry` first (once) and keeps the `keep` newest; false when the
    // file cannot be written or `entry` is empty.
    inline bool RememberRecent(const std::string& file, const std::string& entry, std::size_t keep = 8) {
        if (file.empty() || entry.empty()) return false;
        std::vector<std::string> entries = LoadRecentList(file);
        entries.erase(std::remove(entries.begin(), entries.end(), entry), entries.end());
        entries.insert(entries.begin(), entry);
        if (entries.size() > keep) entries.resize(keep);
        return SaveRecentList(file, entries);
    }

    // Drops `entry`; false when the file cannot be written.
    inline bool ForgetRecent(const std::string& file, const std::string& entry) {
        if (file.empty()) return false;
        std::vector<std::string> entries = LoadRecentList(file);
        entries.erase(std::remove(entries.begin(), entries.end(), entry), entries.end());
        return SaveRecentList(file, entries);
    }

} // namespace UltraCanvas
