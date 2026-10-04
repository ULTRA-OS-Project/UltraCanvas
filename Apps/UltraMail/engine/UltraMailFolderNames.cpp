// Apps/UltraMail/engine/UltraMailFolderNames.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailFolderNames.h"

#include <UltraNet/UltraNetMime.h>   // UltraNet_ImapUtf7Decode

#include <cctype>
#include <map>

namespace UltraMail {

namespace {

bool StartsWithNoCase(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

bool IsInbox(const std::string& name) {
    return name.size() == 5 && StartsWithNoCase(name, "INBOX");
}

// A level as it reads: decoded, a migrated "INBOX^" in front left out.
std::string LevelName(std::string level) {
    if (level.size() > 6 && StartsWithNoCase(level, "INBOX^")) level = level.substr(6);
    return UltraNet_ImapUtf7Decode(level);
}

} // namespace

std::string AccountFolderDelimiter(const std::vector<Folder>& accountFolders) {
    std::map<std::string, int> listed;
    for (const auto& f : accountFolders)
        if (!f.delimiter.empty()) ++listed[f.delimiter];
    if (!listed.empty()) {
        auto best = listed.begin();
        for (auto it = listed.begin(); it != listed.end(); ++it)
            if (it->second > best->second) best = it;
        return best->first;
    }
    bool dotted = false;
    for (const auto& f : accountFolders) {
        if (f.name.find('/') != std::string::npos) return "/";
        if (f.name.size() > 6 && StartsWithNoCase(f.name, "INBOX.")) dotted = true;
    }
    return dotted ? "." : "/";
}

std::string FolderDelimiter(const Folder& folder, const std::vector<Folder>& accountFolders) {
    return folder.delimiter.empty() ? AccountFolderDelimiter(accountFolders) : folder.delimiter;
}

std::vector<std::string> FolderLevels(const std::string& name, const std::string& delimiter) {
    std::vector<std::string> levels;
    if (delimiter.empty()) { levels.push_back(name); return levels; }
    std::size_t start = 0;
    for (;;) {
        const std::size_t cut = name.find(delimiter, start);
        levels.push_back(name.substr(start, cut == std::string::npos ? std::string::npos
                                                                      : cut - start));
        if (cut == std::string::npos) break;
        start = cut + delimiter.size();
    }
    return levels;
}

std::string FolderDisplayName(const std::string& name, const std::string& delimiter) {
    if (IsInbox(name)) return "Inbox";
    const std::vector<std::string> levels = FolderLevels(name, delimiter);
    return LevelName(levels.empty() ? name : levels.back());
}

std::string FolderDisplayPath(const std::string& name, const std::string& delimiter) {
    if (IsInbox(name)) return "Inbox";
    std::vector<std::string> levels = FolderLevels(name, delimiter);
    if (levels.size() > 1 && IsInbox(levels.front())) levels.erase(levels.begin());
    std::string path;
    for (const auto& level : levels) {
        if (!path.empty()) path += " / ";
        path += LevelName(level);
    }
    return path;
}

} // namespace UltraMail
