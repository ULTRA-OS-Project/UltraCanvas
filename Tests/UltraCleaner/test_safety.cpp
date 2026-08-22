// Tests/UltraCleaner/test_safety.cpp
// The guard is what stands between a rule table typo and someone's
// documents, so it gets the most tests in the suite.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_support.h"

#include "UltraCleanerPaths.h"
#include "UltraCleanerSafety.h"

#include <filesystem>

#if !defined(_WIN32) && !defined(_WIN64)
#include <sys/stat.h>
#include <sys/types.h>
#endif

using namespace UltraCleaner;
using ultracleaner_test::TempTree;

namespace {

PathGuard GuardFor(const std::string& root) {
    PathGuard guard;
    guard.AllowRoot(root);
    return guard;
}

} // namespace

TEST(GuardAllowsContentInsideItsRoot) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/blob.bin", 16);

    PathGuard guard = GuardFor(cache);
    REQUIRE(guard.Check(cache + "/blob.bin").Allowed());
    REQUIRE(guard.Check(cache + "/nested/deeper/blob.bin").Allowed());
}

TEST(GuardRefusesTheRootItself) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    PathGuard guard = GuardFor(cache);
    // Rules clear a root's contents; the root must survive the clean.
    REQUIRE_EQ(static_cast<int>(guard.Check(cache).verdict),
               static_cast<int>(SafetyVerdict::OutsideAllowedRoot));
}

TEST(GuardRefusesAnythingOutsideEveryRoot) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("documents/report.txt", 8);
    PathGuard guard = GuardFor(cache);
    REQUIRE_EQ(static_cast<int>(guard.Check(tree.Path() + "/documents/report.txt").verdict),
               static_cast<int>(SafetyVerdict::OutsideAllowedRoot));
}

TEST(GuardRefusesRelativePaths) {
    PathGuard guard = GuardFor("/tmp/whatever");
    REQUIRE_EQ(static_cast<int>(guard.Check("relative/path").verdict),
               static_cast<int>(SafetyVerdict::NotAbsolute));
    REQUIRE_EQ(static_cast<int>(guard.Check("").verdict),
               static_cast<int>(SafetyVerdict::NotAbsolute));
}

TEST(GuardRefusesShallowPaths) {
    // A guard whose root is the filesystem root would still not let a
    // top-level directory through.
    PathGuard guard = GuardFor("/");
    REQUIRE_EQ(static_cast<int>(guard.Check("/usr").verdict),
               static_cast<int>(SafetyVerdict::TooShallow));
}

TEST(GuardRefusesProtectedLocationsEvenInsideARoot) {
    // The worst case the guard exists for: a root wide enough to contain the
    // user's documents, and a candidate that is one of them.
    const std::string home = HomeDir();
    REQUIRE(!home.empty());

    PathGuard guard = GuardFor(home);
    REQUIRE_EQ(static_cast<int>(guard.Check(home + "/Documents").verdict),
               static_cast<int>(SafetyVerdict::ProtectedLocation));
    REQUIRE_EQ(static_cast<int>(guard.Check(home + "/.ssh").verdict),
               static_cast<int>(SafetyVerdict::ProtectedLocation));

    // And the home directory itself is refused whichever check fires first —
    // a shallow home ("/root") trips the depth rule before the name rule.
    PathGuard wider = GuardFor(std::filesystem::path(home).parent_path().string());
    REQUIRE(wider.Check(home).Allowed() == false);
}

TEST(GuardRefusesAncestorsOfProtectedLocations) {
    const std::string home = HomeDir();
    REQUIRE(!home.empty());
    // Removing ~/.local would take ~/.local/share with it.
    REQUIRE(IsProtectedPath(home + "/.local"));
    REQUIRE(IsProtectedPath(home + "/Documents"));
    REQUIRE(IsProtectedPath(home));
    REQUIRE(IsProtectedPath("/"));
    REQUIRE(IsProtectedPath("/usr"));
}

TEST(GuardAllowsFilesInsideAProtectedDirectory) {
    // ~/Downloads is protected as a directory, but the installer rule must
    // still be able to name a file inside it.
    const std::string downloads = DownloadsDir();
    REQUIRE(!downloads.empty());
    REQUIRE(IsProtectedPath(downloads));
    REQUIRE(IsProtectedPath(downloads + "/old-installer.dmg") == false);
}

TEST(GuardRefusesASymlinkThatEscapesTheRoot) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    const std::string outside = tree.Dir("precious");
    tree.File("precious/keep.txt", 32);

    std::error_code ec;
    std::filesystem::create_directory_symlink(outside, cache + "/escape", ec);
    if (ec) return;   // filesystem without symlinks (some CI Windows runners)

    PathGuard guard = GuardFor(cache);
    // The link itself is inside the root, so removing the link is fine…
    REQUIRE(guard.Check(cache + "/escape").Allowed());
    // …but a path *through* it lands outside, and must be refused.
    REQUIRE_EQ(static_cast<int>(guard.Check(cache + "/escape/keep.txt").verdict),
               static_cast<int>(SafetyVerdict::SymlinkEscape));
}

TEST(GuardRefusesSpecialFiles) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    std::error_code ec;
    std::filesystem::create_directories(cache, ec);

#if !defined(_WIN32) && !defined(_WIN64)
    const std::string fifo = cache + "/socketish";
    if (::mkfifo(fifo.c_str(), 0600) != 0) return;   // not supported here
    PathGuard guard = GuardFor(cache);
    REQUIRE_EQ(static_cast<int>(guard.Check(fifo).verdict),
               static_cast<int>(SafetyVerdict::SpecialFile));
#endif
}

TEST(GuardWithNoRootsAllowsNothing) {
    PathGuard guard;
    REQUIRE(guard.Check("/tmp/anything/at/all").Allowed() == false);
    REQUIRE(guard.AllowedRoots().empty());
}
