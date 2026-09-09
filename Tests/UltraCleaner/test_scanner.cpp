// Tests/UltraCleaner/test_scanner.cpp
// The scanner, driven with synthetic rules over a temporary tree so the
// tests never depend on what happens to be in the running user's caches.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "test_support.h"

#include "UltraCleanerScanner.h"

#include <filesystem>

using namespace UltraCleaner;
using ultracleaner_test::TempTree;

namespace {

CleanRule ContentsRule(const std::string& root, int minAgeDays = 0) {
    CleanRule rule;
    rule.id = "test.contents";
    rule.category = CleanCategory::ApplicationCaches;
    rule.title = "Test cache";
    rule.roots = { root };
    rule.mode = RuleMode::ClearDirectoryContents;
    rule.minAgeDays = minAgeDays;
    rule.platforms = PlatformMask::All;
    return rule;
}

// Backdates a path so the age filter has something to bite on.
void Backdate(const std::string& path, int days) {
    std::error_code ec;
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(path, now - std::chrono::hours(24 * days), ec);
}

} // namespace

TEST(ScannerReportsTopLevelEntriesWithRecursiveSizes) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/a.bin", 100);
    tree.File("cache/sub/b.bin", 250);
    tree.File("cache/sub/c.bin", 150);

    Scanner scanner;
    const ScanReport report = scanner.Scan({ ContentsRule(cache) });

    REQUIRE_EQ(report.totalItems, static_cast<size_t>(2));   // a.bin and sub/
    REQUIRE_EQ(report.totalBytes, static_cast<uint64_t>(500));
    REQUIRE_EQ(report.categories.size(), static_cast<size_t>(1));
    REQUIRE_EQ(report.categories[0].itemCount, static_cast<size_t>(2));
}

TEST(ScannerNeverProposesTheRootItself) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/a.bin", 10);

    Scanner scanner;
    const ScanReport report = scanner.Scan({ ContentsRule(cache) });
    for (const auto& item : report.items) {
        REQUIRE(item.path != cache);
    }
}

TEST(ScannerHonoursTheAgeFilter) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    const std::string fresh = tree.File("cache/fresh.bin", 10);
    const std::string stale = tree.File("cache/stale.bin", 10);
    Backdate(stale, 30);

    Scanner scanner;
    const ScanReport report = scanner.Scan({ ContentsRule(cache, /*minAgeDays=*/7) });

    REQUIRE_EQ(report.totalItems, static_cast<size_t>(1));
    REQUIRE(report.items[0].path == stale);
    (void)fresh;
}

TEST(ScannerHonoursExcludeNames) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/keep.bin", 10);
    tree.File("cache/drop.bin", 10);

    CleanRule rule = ContentsRule(cache);
    rule.excludeNames = { "keep.*" };

    Scanner scanner;
    const ScanReport report = scanner.Scan({ rule });
    REQUIRE_EQ(report.totalItems, static_cast<size_t>(1));
    REQUIRE(report.items[0].path.find("drop.bin") != std::string::npos);
}

TEST(ScannerMatchesFilesByPatternWithinDepth) {
    TempTree tree;
    const std::string root = tree.Dir("logs");
    tree.File("logs/one.log", 10);
    tree.File("logs/nested/two.log", 10);
    tree.File("logs/nested/deeper/three.log", 10);
    tree.File("logs/notes.txt", 10);

    CleanRule rule;
    rule.id = "test.logs";
    rule.category = CleanCategory::Logs;
    rule.title = "Test logs";
    rule.roots = { root };
    rule.mode = RuleMode::RemoveMatchingFiles;
    rule.namePatterns = { "*.log" };
    rule.maxDepth = 2;
    rule.platforms = PlatformMask::All;

    Scanner scanner;
    const ScanReport report = scanner.Scan({ rule });

    // one.log (depth 0) and nested/two.log (depth 1); three.log is deeper,
    // and notes.txt does not match.
    REQUIRE_EQ(report.totalItems, static_cast<size_t>(2));
    for (const auto& item : report.items) {
        REQUIRE(item.isDirectory == false);
        REQUIRE(item.path.find("three.log") == std::string::npos);
        REQUIRE(item.path.find("notes.txt") == std::string::npos);
    }
}

TEST(ScannerFindsDanglingSymlinksOnly) {
    TempTree tree;
    const std::string root = tree.Dir("links");
    const std::string target = tree.File("links/real.txt", 5);

    std::error_code ec;
    std::filesystem::create_symlink(target, root + "/good", ec);
    std::filesystem::create_symlink(root + "/gone.txt", root + "/dangling", ec);
    if (ec) return;   // no symlink support on this filesystem

    CleanRule rule;
    rule.id = "test.links";
    rule.category = CleanCategory::BrokenLinks;
    rule.title = "Test links";
    rule.roots = { root };
    rule.mode = RuleMode::RemoveDanglingSymlinks;
    rule.maxDepth = 2;
    rule.platforms = PlatformMask::All;

    Scanner scanner;
    const ScanReport report = scanner.Scan({ rule });
    REQUIRE_EQ(report.totalItems, static_cast<size_t>(1));
    REQUIRE(report.items[0].path.find("dangling") != std::string::npos);
}

TEST(ScannerFiltersByCategory) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    const std::string logs  = tree.Dir("logs");
    tree.File("cache/a.bin", 10);
    tree.File("logs/b.bin", 10);

    CleanRule cacheRule = ContentsRule(cache);
    CleanRule logRule = ContentsRule(logs);
    logRule.id = "test.logdir";
    logRule.category = CleanCategory::Logs;

    ScanOptions options;
    options.categories = { CleanCategory::Logs };

    Scanner scanner;
    const ScanReport report = scanner.Scan({ cacheRule, logRule }, options);
    REQUIRE_EQ(report.totalItems, static_cast<size_t>(1));
    REQUIRE_EQ(static_cast<int>(report.items[0].category),
               static_cast<int>(CleanCategory::Logs));
}

TEST(ScannerCanLeaveOutRulesThatAreNotSafeByDefault) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/a.bin", 10);

    CleanRule rule = ContentsRule(cache);
    rule.safeByDefault = false;

    ScanOptions options;
    options.includeUnsafeRules = false;

    Scanner scanner;
    REQUIRE_EQ(scanner.Scan({ rule }, options).totalItems, static_cast<size_t>(0));

    // Included by default, but unticked, so a careless "Clean" cannot take it.
    const ScanReport included = scanner.Scan({ rule });
    REQUIRE_EQ(included.totalItems, static_cast<size_t>(1));
    REQUIRE(included.items[0].selected == false);
    REQUIRE(included.items[0].safeByDefault == false);
}

TEST(ScannerRecordsTheRootsItWasAllowedToSee) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/a.bin", 10);

    Scanner scanner;
    const ScanReport report = scanner.Scan({ ContentsRule(cache) });
    REQUIRE_EQ(report.allowedRoots.size(), static_cast<size_t>(1));
}

TEST(ScannerToleratesRootsThatDoNotExist) {
    Scanner scanner;
    const ScanReport report =
        scanner.Scan({ ContentsRule("/no/such/directory/anywhere") });
    REQUIRE_EQ(report.totalItems, static_cast<size_t>(0));
    // A missing root is normal — most rules name software that is not here.
    REQUIRE_EQ(report.unreadablePaths, static_cast<size_t>(0));
}

TEST(ScannerReportsProgress) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    tree.File("cache/a.bin", 64);

    size_t calls = 0;
    ScanProgress last;
    Scanner scanner;
    scanner.Scan({ ContentsRule(cache) }, {}, [&](const ScanProgress& progress) {
        ++calls;
        last = progress;
    });
    REQUIRE(calls > 0);
    REQUIRE_EQ(last.rulesTotal, static_cast<size_t>(1));
    REQUIRE_EQ(last.rulesCompleted, static_cast<size_t>(1));
    REQUIRE_EQ(last.bytesFound, static_cast<uint64_t>(64));
}

TEST(ScannerStopsWhenCancelled) {
    TempTree tree;
    const std::string cache = tree.Dir("cache");
    for (int i = 0; i < 20; ++i) {
        tree.File("cache/file" + std::to_string(i) + ".bin", 8);
    }

    Scanner scanner;
    const ScanReport report =
        scanner.Scan({ ContentsRule(cache) }, {},
                     [&](const ScanProgress&) { scanner.RequestCancel(); });
    REQUIRE(report.cancelled);
}
