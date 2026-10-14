// Apps/UltraMail/engine/UltraMailFolderNames.cpp
// Version: 0.2.0 - NewFolderName / CanDeleteFolder
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailFolderNames.h"

#include <UltraNet/UltraNetMime.h>   // UltraNet_ImapUtf7Decode / Encode

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

bool SameNameNoCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && StartsWithNoCase(a, b);
}

std::string Trimmed(const std::string& s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return std::string();
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

// Whether every folder of the account but the inbox sits below it - a server
// whose personal namespace is "INBOX." (Courier, some Dovecot setups), where a
// folder outside it cannot be made.
bool AllBelowInbox(const std::vector<Folder>& accountFolders, const std::string& delimiter) {
    bool any = false;
    for (const auto& f : accountFolders) {
        if (IsInbox(f.name)) continue;
        if (!StartsWithNoCase(f.name, "INBOX" + delimiter)) return false;
        any = true;
    }
    return any;
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

std::string NewFolderName(const std::string& typed, const std::string& parent,
                          const std::vector<Folder>& accountFolders, std::string& error) {
    error.clear();
    const std::string name = Trimmed(typed);
    if (name.empty()) { error = "Type a name for the folder."; return std::string(); }

    const Folder* parentFolder = nullptr;
    if (!parent.empty() && !IsInbox(parent))
        for (const auto& f : accountFolders)
            if (f.name == parent) { parentFolder = &f; break; }
    const std::string delimiter = parentFolder ? FolderDelimiter(*parentFolder, accountFolders)
                                               : AccountFolderDelimiter(accountFolders);
    if (!delimiter.empty() && name.find(delimiter) != std::string::npos) {
        error = "A folder name cannot contain \"" + delimiter + "\" on this server.";
        return std::string();
    }
    for (unsigned char c : name) {
        if (c == '*' || c == '%') {
            error = "A folder name cannot contain * or %.";
            return std::string();
        }
        if (c < 0x20 || c == 0x7F) {
            error = "A folder name cannot contain control characters.";
            return std::string();
        }
    }

    std::string prefix;
    if (parentFolder) prefix = parentFolder->name + delimiter;
    else if (AllBelowInbox(accountFolders, delimiter)) prefix = "INBOX" + delimiter;
    // Compared without case: many servers keep names that way, and two
    // folders told apart only by it read as one.
    const std::string full = prefix + UltraNet_ImapUtf7Encode(name);
    bool taken = IsInbox(full);
    for (const auto& f : accountFolders) taken = taken || SameNameNoCase(f.name, full);
    if (taken) {
        error = "There is already a folder named \"" + name + "\" here.";
        return std::string();
    }
    return full;
}

bool CanDeleteFolder(const Folder& folder, const std::vector<Folder>& accountFolders,
                     std::string* error) {
    auto refuse = [&](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (folder.name.empty() || IsInbox(folder.name))
        return refuse("The inbox cannot be deleted.");
    if (folder.role != FolderRole::Normal)
        return refuse("This folder has a role (Sent, Drafts, Trash, Junk or Archive) "
                      "and cannot be deleted here.");
    const std::string below = folder.name + FolderDelimiter(folder, accountFolders);
    for (const auto& f : accountFolders)
        if (f.name.size() > below.size() && f.name.compare(0, below.size(), below) == 0)
            return refuse("Delete the folders inside it first.");
    if (error) error->clear();
    return true;
}

} // namespace UltraMail
