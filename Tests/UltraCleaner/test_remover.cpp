// Tests/UltraCleaner/test_remover.cpp
// The remover: what it deletes, what it refuses, and that simulate mode
// really is a simulation.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_support.h"

#include "UltraCleanerRemover.h"
#include "UltraCleanerPaths.h"
#include "UltraCleanerScanner.h"

#include <cstdlib>
#include <filesystem>
#include <utility>

using namespace UltraCleaner;
using ultracleaner_test::TempTree;

namespace {

CleanRule ContentsRule(const std::string& root) {
    CleanRule rule;
    rule.id = "test.contents";
    rule.category = CleanCategory::ApplicationCaches;
    rule.title = "Test cache";
    rule.roots = { root };
    rule.mode = RuleMode::ClearDirectoryContents;
    rule.platforms = PlatformMask::All;
    return rule;
}

ScanReport ScanOf(const std::string& root) {
    Scanner scanner;
    return scanner.Scan({ ContentsRule(root) });
}

} // namespace

TEST(SimulateModeRemovesNothing) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/a.bin", 100);
    tree.File("cache/sub/b.bin", 50);

    ScanReport report = ScanOf(cache);
    Remover remover;
    const RemovalReport result = remover.Remove(report);   // Simulate by default

    REQUIRE(result.simulated);
    REQUIRE_EQ(result.removedItems, static_cast<size_t>(2));
    REQUIRE_EQ(result.freedBytes, static_cast<uint64_t>(150));
    REQUIRE(tree.Exists("cache/a.bin"));
    REQUIRE(tree.Exists("cache/sub/b.bin"));
}

TEST(PermanentDeleteRemovesTheSelectedItemsOnly) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/gone.bin", 20);
    tree.File("cache/kept.bin", 20);

    ScanReport report = ScanOf(cache);
    REQUIRE_EQ(report.items.size(), static_cast<size_t>(2));
    for (auto& item : report.items) {
        item.selected = item.path.find("gone.bin") != std::string::npos;
    }

    RemovalOptions options;
    options.mode = RemovalMode::DeletePermanently;

    Remover remover;
    const RemovalReport result = remover.Remove(report, options);

    REQUIRE(result.simulated == false);
    REQUIRE_EQ(result.removedItems, static_cast<size_t>(1));
    REQUIRE(tree.Exists("cache/gone.bin") == false);
    REQUIRE(tree.Exists("cache/kept.bin"));
    REQUIRE(result.failures.empty());
}

TEST(PermanentDeleteRemovesDirectoriesWhole) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/sub/one.bin", 10);
    tree.File("cache/sub/two/three.bin", 10);

    ScanReport report = ScanOf(cache);
    RemovalOptions options;
    options.mode = RemovalMode::DeletePermanently;

    Remover remover;
    remover.Remove(report, options);

    REQUIRE(tree.Exists("cache/sub") == false);
    REQUIRE(tree.Exists("cache"));            // the root itself survives
}

TEST(RemoverRefusesAnItemPathThatLeftTheAllowedRoots) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/a.bin", 10);
    const std::string precious = tree.File("precious/keep.txt", 10);

    ScanReport report = ScanOf(cache);
    // Simulates a report whose items were tampered with between the scan and
    // the clean: the guard is rebuilt from allowedRoots, not from the items.
    report.items[0].path = precious;

    RemovalOptions options;
    options.mode = RemovalMode::DeletePermanently;

    Remover remover;
    const RemovalReport result = remover.Remove(report, options);

    REQUIRE_EQ(result.refusedByGuard, static_cast<size_t>(1));
    REQUIRE_EQ(result.removedItems, static_cast<size_t>(0));
    REQUIRE(tree.Exists("precious/keep.txt"));
}

TEST(RemoverCountsItemsThatVanishedBeforeTheClean) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/a.bin", 10);

    ScanReport report = ScanOf(cache);
    std::error_code ec;
    std::filesystem::remove(report.items[0].path, ec);

    RemovalOptions options;
    options.mode = RemovalMode::DeletePermanently;

    Remover remover;
    const RemovalReport result = remover.Remove(report, options);
    REQUIRE_EQ(result.skippedMissing, static_cast<size_t>(1));
    REQUIRE(result.failures.empty());
}

TEST(RemoverReportsProgressAndStopsWhenCancelled) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    for (int i = 0; i < 10; ++i) {
        tree.File("cache/f" + std::to_string(i) + ".bin", 4);
    }

    ScanReport report = ScanOf(cache);
    Remover remover;
    size_t calls = 0;
    const RemovalReport result =
        remover.Remove(report, {}, [&](size_t, size_t, const std::string&) {
            ++calls;
            remover.RequestCancel();
        });
    REQUIRE_EQ(calls, static_cast<size_t>(1));
    REQUIRE(result.cancelled);
}

#if !defined(_WIN32) && !defined(_WIN64) && !defined(__APPLE__)
TEST(XdgTrashMoveWritesATrashInfoRecord) {
    // The trash lives in the real user data directory, so the test cleans up
    // after itself rather than leaving a stray entry behind.
    const std::string trash = TrashDir();
    if (trash.empty()) return;

    TempTree tree;
    const std::string victim = tree.File("cache/trash-me.bin", 12);

    std::string error;
    if (!MoveToPlatformTrash(victim, error)) return;   // no writable trash here

    const std::filesystem::path trashed =
        std::filesystem::path(trash) / "files" / "trash-me.bin";
    const std::filesystem::path info =
        std::filesystem::path(trash) / "info" / "trash-me.bin.trashinfo";

    std::error_code ec;
    const bool movedAway = !std::filesystem::exists(victim, ec);
    const bool inTrash = std::filesystem::exists(trashed, ec);
    const bool hasInfo = std::filesystem::exists(info, ec);

    std::filesystem::remove(trashed, ec);
    std::filesystem::remove(info, ec);

    REQUIRE(movedAway);
    REQUIRE(inTrash);
    REQUIRE(hasInfo);
}
#endif

// ===== Emptying the trash =====
// "Move to trash" applied to something that already lives in the trash used
// to move it beside itself: the file was renamed, a second .trashinfo was
// written, the trash grew, and the report claimed the space back. These two
// pin the behaviour down on both sides.
#if !defined(_WIN32) && !defined(_WIN64)
namespace {

// Points HomeDir()/UserDataDir() — and so TrashDir() — inside a temp tree for
// the duration of one test, then puts the environment back.
class ScopedHome {
public:
    explicit ScopedHome(const std::string& home)
        : oldHome_(Capture("HOME")), oldXdg_(Capture("XDG_DATA_HOME")) {
        setenv("HOME", home.c_str(), 1);
        unsetenv("XDG_DATA_HOME");        // else it wins over HOME on Linux
    }
    ~ScopedHome() {
        Restore("HOME", oldHome_);
        Restore("XDG_DATA_HOME", oldXdg_);
    }
    ScopedHome(const ScopedHome&) = delete;
    ScopedHome& operator=(const ScopedHome&) = delete;

private:
    static std::pair<std::string, bool> Capture(const char* name) {
        const char* value = std::getenv(name);
        return { value ? value : "", value != nullptr };
    }
    static void Restore(const char* name, const std::pair<std::string, bool>& saved) {
        if (saved.second) setenv(name, saved.first.c_str(), 1);
        else              unsetenv(name);
    }
    std::pair<std::string, bool> oldHome_;
    std::pair<std::string, bool> oldXdg_;
};

// A rule that clears whatever directory it is pointed at, standing in for the
// real trash rule without depending on the platform rule table.
CleanRule TrashContentsRule(const std::string& root) {
    CleanRule rule = ContentsRule(root);
    rule.id = "test.trash";
    rule.category = CleanCategory::TrashBin;
    rule.title = "Trash";
    rule.roots = { root };
    return rule;
}

} // namespace

TEST(MoveToTrashLeavesWhatIsAlreadyInTheTrashAlone) {
    TempTree tree;
    ScopedHome home(tree.Path());

    const std::string files = tree.Dir(".local/share/Trash/files");
    tree.File(".local/share/Trash/files/already-deleted.bin", 40);

    Scanner scanner;
    ScanReport report = scanner.Scan({ TrashContentsRule(files) });
    REQUIRE_EQ(report.items.size(), static_cast<size_t>(1));
    for (auto& item : report.items) item.selected = true;

    RemovalOptions options;
    options.mode = RemovalMode::MoveToTrash;

    Remover remover;
    const RemovalReport result = remover.Remove(report, options);

    // Nothing moved, nothing claimed, and no second copy left behind.
    REQUIRE_EQ(result.removedItems, static_cast<size_t>(0));
    REQUIRE_EQ(result.freedBytes, static_cast<uint64_t>(0));
    REQUIRE_EQ(result.skippedAlreadyInTrash, static_cast<size_t>(1));
    REQUIRE(result.failures.empty());
    REQUIRE(tree.Exists(".local/share/Trash/files/already-deleted.bin"));

    size_t entries = 0;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(files, ec);
         it != std::filesystem::directory_iterator(); ++it) {
        ++entries;
    }
    REQUIRE_EQ(entries, static_cast<size_t>(1));
}

TEST(PermanentDeleteIsWhatActuallyEmptiesTheTrash) {
    TempTree tree;
    ScopedHome home(tree.Path());

    const std::string files = tree.Dir(".local/share/Trash/files");
    tree.File(".local/share/Trash/files/already-deleted.bin", 40);

    Scanner scanner;
    ScanReport report = scanner.Scan({ TrashContentsRule(files) });
    for (auto& item : report.items) item.selected = true;

    RemovalOptions options;
    options.mode = RemovalMode::DeletePermanently;

    Remover remover;
    const RemovalReport result = remover.Remove(report, options);

    REQUIRE_EQ(result.removedItems, static_cast<size_t>(1));
    REQUIRE_EQ(result.freedBytes, static_cast<uint64_t>(40));
    REQUIRE_EQ(result.skippedAlreadyInTrash, static_cast<size_t>(0));
    REQUIRE(!tree.Exists(".local/share/Trash/files/already-deleted.bin"));
}
#endif
