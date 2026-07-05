// Apps/UltraMail/engine/UltraMailTypes.cpp
// FolderRole <-> string mapping.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraMailTypes.h"

namespace UltraMail {

std::string ToString(FolderRole role) {
    switch (role) {
        case FolderRole::Inbox:   return "inbox";
        case FolderRole::Sent:    return "sent";
        case FolderRole::Drafts:  return "drafts";
        case FolderRole::Junk:    return "junk";
        case FolderRole::Trash:   return "trash";
        case FolderRole::Archive: return "archive";
        case FolderRole::Normal:
        default:                  return "normal";
    }
}

FolderRole FolderRoleFromString(const std::string& s) {
    if (s == "inbox")   return FolderRole::Inbox;
    if (s == "sent")    return FolderRole::Sent;
    if (s == "drafts")  return FolderRole::Drafts;
    if (s == "junk")    return FolderRole::Junk;
    if (s == "trash")   return FolderRole::Trash;
    if (s == "archive") return FolderRole::Archive;
    return FolderRole::Normal;
}

} // namespace UltraMail
