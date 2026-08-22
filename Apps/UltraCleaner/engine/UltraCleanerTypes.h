// Apps/UltraCleaner/engine/UltraCleanerTypes.h
// Shared vocabulary of the UltraCleaner engine: the platforms it knows, the
// categories junk is grouped under, the item a scan reports and the totals
// the UI shows. Everything here is headless — no UltraCanvas dependency —
// so the engine can be driven from the GUI, from the command line or from a
// test binary.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCleaner {

// ===== PLATFORM =====
enum class CleanerPlatform {
    Linux,
    MacOS,
    Windows,
    Unknown
};

// The platform this binary was compiled for.
CleanerPlatform CurrentPlatform();
std::string PlatformName(CleanerPlatform platform);

// ===== CATEGORIES =====
// The grouping shown in the UI. A rule belongs to exactly one category, and
// the user selects whole categories rather than individual rules.
enum class CleanCategory {
    TemporaryFiles,     // OS and per-user temp directories
    ApplicationCaches,  // ~/.cache, ~/Library/Caches, %LOCALAPPDATA%\…\Cache
    BrowserCaches,      // Chromium/Firefox/Edge/Safari on-disk caches
    Logs,               // application and system logs owned by the user
    CrashReports,       // crash dumps, core files, diagnostic reports
    Thumbnails,         // thumbnail databases and preview caches
    TrashBin,           // the desktop trash / recycle bin
    PackageCaches,      // npm/pip/gradle/cargo/composer download caches
    DeveloperJunk,      // DerivedData, build leftovers, simulator caches
    InstallerLeftovers, // downloaded installers and update payloads
    BrokenLinks,        // dangling symlinks and zero-byte leftovers
    CategoryCount       // sentinel — never a real category
};

const char* CategoryKey(CleanCategory category);
std::string CategoryTitle(CleanCategory category);
std::string CategoryDescription(CleanCategory category);

// Categories in the order the UI lists them (excludes CategoryCount).
const std::vector<CleanCategory>& AllCategories();

// ===== SCAN RESULTS =====
// One removable thing: a file, or a directory removed whole. `sizeBytes` is
// the file's size, or the recursive size of a directory.
struct CleanItem {
    std::string path;
    std::string ruleId;                     // the rule that proposed it
    CleanCategory category = CleanCategory::TemporaryFiles;
    uint64_t sizeBytes = 0;
    int64_t modifiedAt = 0;                 // seconds since the epoch
    bool isDirectory = false;
    bool safeByDefault = true;              // pre-ticked in the UI
    bool selected = true;
};

// Per-category totals, derived from the item list.
struct CategorySummary {
    CleanCategory category = CleanCategory::TemporaryFiles;
    uint64_t totalBytes = 0;
    size_t itemCount = 0;
    bool safeByDefault = true;              // false if any rule in it is not
};

// What a scan found. `skippedProtected` counts candidates the safety guard
// refused — a non-zero value means a rule tried to reach outside its root and
// is worth investigating, not something the user needs to act on.
struct ScanReport {
    std::vector<CleanItem> items;
    // Every rule root that resolved on this machine. The remover rebuilds
    // its guard from these, so a report is self-contained: nothing can be
    // removed that the scan that produced it was not allowed to look at.
    std::vector<std::string> allowedRoots;
    std::vector<CategorySummary> categories;   // only non-empty categories
    uint64_t totalBytes = 0;
    size_t totalItems = 0;
    size_t skippedProtected = 0;
    size_t unreadablePaths = 0;               // permission denied etc.
    bool cancelled = false;

    const CategorySummary* Find(CleanCategory category) const;
};

// Recompute `categories`, `totalBytes` and `totalItems` from `items`.
void SummarizeReport(ScanReport& report);

// ===== FORMATTING =====
// "1.4 GB" / "512 KB" / "0 bytes" — decimal units, one decimal place above
// a kilobyte. Used by the GUI and the CLI alike so both agree.
std::string FormatByteSize(uint64_t bytes);

} // namespace UltraCleaner
