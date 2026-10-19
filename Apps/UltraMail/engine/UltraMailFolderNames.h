// Apps/UltraMail/engine/UltraMailFolderNames.h
// How a folder's IMAP name reads: its levels split by the separator the server
// lists ("INBOX.Drafts" on Courier-style servers, "Work/Projects" elsewhere),
// the last level as its name ("Drafts", not "INBOX.Drafts"), decoded from
// IMAP's modified UTF-7. The folder tree, the list's title, the status line and
// the "Move to folder" menu all read names through here, so they agree.
// Version: 0.2.0 - NewFolderName / CanDeleteFolder (Add folder, Delete folder)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraMailTypes.h"

#include <string>
#include <vector>

namespace UltraMail {

// The separator of an account's folders when a folder has none stored (it was
// stored before separators were): one the server listed for another of them,
// else "." when the names are Courier-style ("INBOX.Drafts", no "/" anywhere),
// else "/".
std::string AccountFolderDelimiter(const std::vector<Folder>& accountFolders);

// The separator `folder` is split by: its own, else AccountFolderDelimiter.
std::string FolderDelimiter(const Folder& folder, const std::vector<Folder>& accountFolders);

// The levels of a folder name ("INBOX.Work.2026" by "." -> INBOX, Work, 2026),
// still in modified UTF-7; one level when there is no separator.
std::vector<std::string> FolderLevels(const std::string& name, const std::string& delimiter);

// The name a folder shows: "Inbox" for INBOX, else its last level, decoded,
// with an "INBOX^" in front left out - how a folder came across from a server
// with another separator ("INBOX.INBOX^Sent" reads "Sent").
std::string FolderDisplayName(const std::string& name, const std::string& delimiter);

// The folder's place as a person reads it: its levels below the inbox,
// decoded, joined with " / " ("INBOX.Projects.2026" -> "Projects / 2026");
// "Inbox" for INBOX.
std::string FolderDisplayPath(const std::string& name, const std::string& delimiter);

// The full name, in its wire form (modified UTF-7), of a folder a person
// named `typed` (UTF-8) below `parent` - a folder's full name, or empty (or
// INBOX) for the account's top level. On an account whose folders all sit
// below the inbox (Courier-style "INBOX.Drafts"), a top-level folder goes
// there too. Empty, with `error` saying why, when the name cannot be used:
// nothing typed, the separator, a wildcard (* or %) or a control character
// in it, or a folder of that name already there.
std::string NewFolderName(const std::string& typed, const std::string& parent,
                          const std::vector<Folder>& accountFolders, std::string& error);

// Whether `folder` may be deleted from the folder tree: not the inbox, not a
// folder with a role (Sent, Drafts, Trash, Junk, Archive - the app files mail
// there), and none below it (most servers refuse those, the rest would leave
// the subfolders orphaned). `error`, when given, says why not.
bool CanDeleteFolder(const Folder& folder, const std::vector<Folder>& accountFolders,
                     std::string* error = nullptr);

} // namespace UltraMail
