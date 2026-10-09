// Tests/UltraCleaner/test_paths.cpp
// Token expansion, glob matching and the path comparisons the guard relies on.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_support.h"

#include "UltraCleanerPaths.h"
#include "UltraCleanerTypes.h"

#include <filesystem>
#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"

using namespace UltraCleaner;
using ultracleaner_test::TempTree;

TEST(HomeDirectoryResolves) {
    REQUIRE(!HomeDir().empty());
    // Tokens are normalized to forward slashes with no trailing separator.
    REQUIRE(HomeDir().back() != '/');
}

TEST(ExpandTokensSubstitutesKnownTokens) {
    const std::string expanded = ExpandTokens("{HOME}/.cache/example");
    REQUIRE(expanded == HomeDir() + "/.cache/example");
}

TEST(ExpandTokensRejectsUnknownTokens) {
    REQUIRE(ExpandTokens("{NOT_A_TOKEN}/x").empty());
}

TEST(ExpandTokensRejectsUnresolvableTokens) {
    // {LOCALAPPDATA} exists only on Windows; everywhere else the whole
    // pattern must come back empty so the rule is skipped rather than
    // silently pointed at "/Microsoft/Windows".
    const std::string expanded = ExpandTokens("{LOCALAPPDATA}/Microsoft/Windows");
    if (CurrentPlatform() == CleanerPlatform::Windows) {
        REQUIRE(!expanded.empty());
    } else {
        REQUIRE(expanded.empty());
    }
}

TEST(GlobMatchHandlesStarsAndQuestionMarks) {
    REQUIRE(GlobMatch("thumbcache_256.db", "thumbcache_*.db"));
    REQUIRE(GlobMatch("core.1234", "core.[0-9]*"));
    REQUIRE(GlobMatch("core.py", "core.[0-9]*") == false);
    REQUIRE(GlobMatch("app.log.3", "*.log.[0-9]"));
    REQUIRE(GlobMatch("app.log.x", "*.log.[0-9]") == false);
    REQUIRE(GlobMatch("b", "[!a]"));
    REQUIRE(GlobMatch("a", "[!a]") == false);
    REQUIRE(GlobMatch("core", "core"));
    REQUIRE(GlobMatch("a.log", "*.log"));
    REQUIRE(GlobMatch("a.log.1", "*.log") == false);
    REQUIRE(GlobMatch("abc", "a?c"));
    REQUIRE(GlobMatch("ac", "a?c") == false);
    REQUIRE(GlobMatch("anything", "*"));
    REQUIRE(GlobMatch("systemd-private-abc", "systemd-private-*"));
}

TEST(GlobMatchDoesNotBacktrackForever) {
    // A pattern that would blow up a naive recursive matcher.
    REQUIRE(GlobMatch(std::string(64, 'a') + "b", "*a*a*a*a*a*a*a*b"));
    REQUIRE(GlobMatch(std::string(64, 'a'), "*a*a*a*a*a*a*a*b") == false);
}

TEST(IsPathInsideRespectsBoundaries) {
    REQUIRE(IsPathInside("/home/user/x", "/home/user"));
    REQUIRE(IsPathInside("/home/user", "/home/user"));
    REQUIRE(IsPathInside("/home/user2", "/home/user") == false);
    REQUIRE(IsPathInside("/home", "/home/user") == false);
    REQUIRE(IsPathInside("/anything", "/"));
}

TEST(WildcardExpansionListsMatchingDirectories) {
    TempTree tree;
    tree.Dir("profiles/Default/Cache");
    tree.Dir("profiles/Profile 1/Cache");
    tree.Dir("profiles/Profile 2/NotCache");
    tree.File("profiles/loose.txt", 4);

    auto matches = ExpandWildcardDirectories(tree.Path() + "/profiles/*/Cache");
    REQUIRE_EQ(matches.size(), static_cast<size_t>(2));
}

// A folder named outside the Windows code page - a Thai user name, an emoji -
// is listed and named like any other. The parent of a wildcard used to reach
// the filesystem as narrow text, which Windows reads in the ANSI code page,
// so nothing under such a folder was found there. The folders are made with
// PathFromUtf8 rather than TempTree's helpers, which take narrow text too.
TEST(WildcardExpansionListsFoldersNamedOutsideTheCodePage) {
    TempTree tree;
    const std::string parent = tree.Path() + "/ผู้ใช้";
    std::error_code ec;
    std::filesystem::create_directories(
        UltraCanvas::PathFromUtf8(parent + "/โปรไฟล์ 😀/Cache"), ec);
    REQUIRE(!ec);

    auto matches = ExpandWildcardDirectories(parent + "/*/Cache");
    REQUIRE_EQ(matches.size(), static_cast<size_t>(1));
    REQUIRE(matches[0] == parent + "/โปรไฟล์ 😀/Cache");
}

TEST(WildcardExpansionYieldsPlainPathUnchanged) {
    auto matches = ExpandWildcardDirectories("/does/not/exist");
    REQUIRE_EQ(matches.size(), static_cast<size_t>(1));
    REQUIRE(matches[0] == "/does/not/exist");
}

// ===== System refusals =====
// macOS 27 refuses other developers' app containers with EPERM and no
// prompt. Only that error marks a location the user can open up with Full
// Disk Access; the file's own permissions (EACCES) and absence are not it.

TEST(OnlyOperationNotPermittedIsASystemRefusal) {
    REQUIRE(IsSystemRefusal(std::make_error_code(std::errc::operation_not_permitted)));
    REQUIRE(!IsSystemRefusal(std::make_error_code(std::errc::permission_denied)));
    REQUIRE(!IsSystemRefusal(std::make_error_code(std::errc::no_such_file_or_directory)));
    REQUIRE(!IsSystemRefusal(std::error_code()));
}

TEST(AnOrdinaryOrMissingDirectoryIsNotRefused) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    REQUIRE(!IsRefusedBySystem(cache));
    REQUIRE(!IsRefusedBySystem(tree.Path() + "/no-such-folder"));
}

TEST(WildcardExpansionCountsNoRefusalOverAnOrdinaryTree) {
    TempTree tree;
    tree.Dir("Containers/com.example.one/Data/Library/Caches");
    tree.Dir("Containers/com.example.two/Data");          // no Caches inside

    size_t refused = 0;
    auto matches = ExpandWildcardDirectories(
        tree.Path() + "/Containers/*/Data/Library/Caches", &refused);
    REQUIRE_EQ(matches.size(), static_cast<size_t>(1));
    // A container without the folder is not a refusal: it simply has none.
    REQUIRE_EQ(refused, static_cast<size_t>(0));
}
