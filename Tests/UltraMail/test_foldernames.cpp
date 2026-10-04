// Tests/UltraMail/test_foldernames.cpp
// How folder names read: by the separator the server lists, so a Courier-style
// "INBOX.Drafts" is Drafts under the inbox and not a folder of that name.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "test_framework.h"

#include "UltraMailFolderNames.h"

#include <string>
#include <vector>

using namespace UltraMail;

namespace {
Folder F(const std::string& name, const std::string& delimiter = "") {
    Folder f; f.accountId = "a"; f.name = name; f.delimiter = delimiter; return f;
}
} // namespace

TEST(folder_names_split_by_the_servers_separator) {
    REQUIRE_EQ(FolderDisplayName("INBOX.Drafts", "."), std::string("Drafts"));
    REQUIRE_EQ(FolderDisplayName("INBOX.Job requests", "."), std::string("Job requests"));
    REQUIRE_EQ(FolderDisplayName("Work/Projects", "/"), std::string("Projects"));
    // A dot in a name is not a level where the separator is "/".
    REQUIRE_EQ(FolderDisplayName("Mr. Smith", "/"), std::string("Mr. Smith"));
    REQUIRE_EQ(FolderDisplayName("INBOX", "."), std::string("Inbox"));
    REQUIRE_EQ(FolderDisplayName("Inbox", "/"), std::string("Inbox"));
    // A folder carried over from a server with another separator.
    REQUIRE_EQ(FolderDisplayName("INBOX.INBOX^Sent", "."), std::string("Sent"));
    // Modified UTF-7 is decoded.
    REQUIRE_EQ(FolderDisplayName("INBOX.Entw&APw-rfe", "."), std::string("Entw\xC3\xBCrfe"));

    REQUIRE_EQ(FolderDisplayPath("INBOX.Projects.2026", "."), std::string("Projects / 2026"));
    REQUIRE_EQ(FolderDisplayPath("Work/Projects", "/"), std::string("Work / Projects"));
    REQUIRE_EQ(FolderDisplayPath("INBOX", "."), std::string("Inbox"));

    const std::vector<std::string> levels = FolderLevels("INBOX.Work.2026", ".");
    REQUIRE_EQ(levels.size(), (size_t)3);
    REQUIRE_EQ(levels[1], std::string("Work"));
    REQUIRE_EQ(FolderLevels("Plain", "").size(), (size_t)1);
}

TEST(folder_separator_from_the_server_else_from_the_names) {
    // Listed by the server: that one.
    std::vector<Folder> listed = { F("INBOX", "."), F("INBOX.Drafts", ".") };
    REQUIRE_EQ(FolderDelimiter(listed[1], listed), std::string("."));
    // Stored before separators were: one the server listed for another folder.
    std::vector<Folder> mixed = { F("INBOX", "/"), F("Old") };
    REQUIRE_EQ(FolderDelimiter(mixed[1], mixed), std::string("/"));
    // None listed yet: Courier-style names read with ".".
    std::vector<Folder> courier = { F("INBOX"), F("INBOX.Drafts"), F("INBOX.Trash") };
    REQUIRE_EQ(AccountFolderDelimiter(courier), std::string("."));
    // ... unless a "/" shows that is the separator.
    std::vector<Folder> slashed = { F("INBOX"), F("INBOX.old"), F("Work/Projects") };
    REQUIRE_EQ(AccountFolderDelimiter(slashed), std::string("/"));
    REQUIRE_EQ(AccountFolderDelimiter({}), std::string("/"));
}
