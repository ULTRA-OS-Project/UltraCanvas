// Tests/UltraCleaner/test_paths.cpp
// Token expansion, glob matching and the path comparisons the guard relies on.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_support.h"

#include "UltraCleanerPaths.h"
#include "UltraCleanerTypes.h"

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

TEST(WildcardExpansionYieldsPlainPathUnchanged) {
    auto matches = ExpandWildcardDirectories("/does/not/exist");
    REQUIRE_EQ(matches.size(), static_cast<size_t>(1));
    REQUIRE(matches[0] == "/does/not/exist");
}
