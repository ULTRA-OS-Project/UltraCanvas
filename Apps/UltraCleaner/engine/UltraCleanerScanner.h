// Apps/UltraCleaner/engine/UltraCleanerScanner.h
// Turns the rule table into a ScanReport. The scan is read-only — it never
// removes anything — and is written to run on a worker thread: it reports
// progress through a callback and stops promptly when RequestCancel() is
// called, so the GUI stays responsive over a slow disk.
//
// Every candidate is checked by the PathGuard before it enters the report,
// so the report itself is already a safe work list; the remover checks again
// anyway, because the two run at different times.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCleanerRules.h"
#include "UltraCleanerSafety.h"
#include "UltraCleanerTypes.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace UltraCleaner {

// ===== OPTIONS =====
struct ScanOptions {
    // When non-empty, only these categories are scanned.
    std::vector<CleanCategory> categories;
    // Rules marked "not safe by default" are reported (unticked) by default;
    // set this false to leave them out of the scan altogether.
    bool includeUnsafeRules = true;
    // Directories whose recursive size exceeds this are still reported, but
    // sizing stops early to keep the scan bounded. 0 = no limit.
    uint64_t sizeWalkFileLimit = 200000;
};

// ===== PROGRESS =====
struct ScanProgress {
    std::string currentRuleTitle;
    std::string currentPath;
    size_t rulesCompleted = 0;
    size_t rulesTotal = 0;
    size_t itemsFound = 0;
    uint64_t bytesFound = 0;
};

// ===== SCANNER =====
class Scanner {
public:
    using ProgressCallback = std::function<void(const ScanProgress&)>;

    // Scans `rules` (already filtered to the running platform by the caller,
    // normally with RulesForCurrentPlatform()). Safe to call on any thread;
    // one Scanner runs one scan at a time.
    ScanReport Scan(const std::vector<CleanRule>& rules,
                    const ScanOptions& options = {},
                    ProgressCallback onProgress = nullptr);

    // Asks the running scan to stop. The report comes back with
    // `cancelled` set and whatever was found so far.
    void RequestCancel() { cancelRequested_ = true; }
    void ResetCancel()   { cancelRequested_ = false; }
    bool IsCancelRequested() const { return cancelRequested_; }

private:
    void ScanClearContents(const CleanRule& rule, const std::string& root,
                           const PathGuard& guard, ScanReport& report);
    void ScanMatchingFiles(const CleanRule& rule, const std::string& root,
                           const PathGuard& guard, ScanReport& report);
    void ScanMatchingDirectories(const CleanRule& rule, const std::string& root,
                                 const PathGuard& guard, ScanReport& report);
    void ScanDanglingSymlinks(const CleanRule& rule, const std::string& root,
                              const PathGuard& guard, ScanReport& report);
    void ScanRecycleBin(const CleanRule& rule, ScanReport& report);

    std::atomic<bool> cancelRequested_{false};
    ScanOptions options_;
    ProgressCallback onProgress_;
    ScanProgress progress_;

    void Report(const std::string& path);
};

// ===== HELPERS (shared with the remover and the tests) =====
// Recursive size of a directory in bytes, not following symlinks. Stops
// after `fileLimit` entries when that is non-zero.
uint64_t DirectorySize(const std::string& path, uint64_t fileLimit = 0);

// Seconds since the epoch of a path's last write, or 0 when unknown.
int64_t LastWriteSeconds(const std::string& path);

// Resolves a rule's roots for the running machine: token expansion, then
// wildcard expansion. Roots whose tokens do not resolve drop out.
std::vector<std::string> ResolveRuleRoots(const CleanRule& rule);

} // namespace UltraCleaner
