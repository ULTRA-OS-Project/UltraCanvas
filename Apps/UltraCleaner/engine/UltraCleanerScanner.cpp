// Apps/UltraCleaner/engine/UltraCleanerScanner.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerScanner.h"

#include "UltraCleanerPaths.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <shellapi.h>
#endif

namespace UltraCleaner {
namespace {

namespace fs = std::filesystem;

constexpr fs::directory_options kWalkOptions =
    fs::directory_options::skip_permission_denied;

bool MatchesAny(const std::string& name,
                const std::vector<std::string>& patterns) {
    for (const auto& pattern : patterns) {
        if (GlobMatch(name, pattern)) return true;
    }
    return false;
}

// A rule root that simply does not exist here is not a problem — most rules
// name software the user has not installed. Only a root that exists and
// still cannot be listed is worth counting.
bool NoteUnreadableRoot(const std::string& root, ScanReport& report) {
    std::error_code ec;
    if (fs::exists(fs::path(root), ec) && !ec) ++report.unreadablePaths;
    return false;
}

int64_t NowSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

uint64_t DirectorySize(const std::string& path, uint64_t fileLimit) {
    std::error_code ec;
    uint64_t total = 0;
    uint64_t seen = 0;

    fs::recursive_directory_iterator iterator(fs::path(path), kWalkOptions, ec);
    if (ec) return 0;
    const fs::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (fileLimit != 0 && ++seen > fileLimit) break;
        std::error_code entryEc;
        // A symlink's target may be huge and elsewhere; the link itself costs
        // next to nothing and is all that gets removed.
        if (iterator->is_symlink(entryEc) || entryEc) { continue; }
        if (!iterator->is_regular_file(entryEc) || entryEc) continue;
        const uintmax_t size = iterator->file_size(entryEc);
        if (entryEc) continue;
        total += static_cast<uint64_t>(size);
    }
    return total;
}

int64_t LastWriteSeconds(const std::string& path) {
    std::error_code ec;
    const fs::file_time_type written = fs::last_write_time(fs::path(path), ec);
    if (ec) return 0;

    // file_clock and system_clock tick at the same rate but need not share an
    // epoch (libstdc++ puts file_clock's in 2174), and std::chrono::clock_cast
    // is not in every standard library this builds against. The offset is
    // fixed for the life of the process, so measure it once.
    using namespace std::chrono;
    static const int64_t epochOffset = []() {
        const auto fileNow = duration_cast<seconds>(
            fs::file_time_type::clock::now().time_since_epoch()).count();
        const auto systemNow = duration_cast<seconds>(
            system_clock::now().time_since_epoch()).count();
        return static_cast<int64_t>(systemNow) - static_cast<int64_t>(fileNow);
    }();

    return static_cast<int64_t>(
               duration_cast<seconds>(written.time_since_epoch()).count()) +
           epochOffset;
}

std::vector<std::string> ResolveRuleRoots(const CleanRule& rule) {
    std::vector<std::string> resolved;
    for (const auto& pattern : rule.roots) {
        const std::string expanded = ExpandTokens(pattern);
        if (expanded.empty()) continue;              // token unknown here
        for (auto& path : ExpandWildcardDirectories(expanded)) {
            if (std::find(resolved.begin(), resolved.end(), path) == resolved.end()) {
                resolved.push_back(std::move(path));
            }
        }
    }
    return resolved;
}

void Scanner::Report(const std::string& path) {
    if (!onProgress_) return;
    progress_.currentPath = path;
    onProgress_(progress_);
}

ScanReport Scanner::Scan(const std::vector<CleanRule>& rules,
                         const ScanOptions& options,
                         ProgressCallback onProgress) {
    cancelRequested_ = false;
    options_ = options;
    onProgress_ = std::move(onProgress);
    progress_ = ScanProgress{};

    // Which rules actually run: category filter first, then the "unsafe"
    // switch. Doing this up front makes the progress count truthful.
    std::vector<const CleanRule*> selected;
    for (const auto& rule : rules) {
        if (!options_.includeUnsafeRules && !rule.safeByDefault) continue;
        if (!options_.categories.empty() &&
            std::find(options_.categories.begin(), options_.categories.end(),
                      rule.category) == options_.categories.end()) {
            continue;
        }
        selected.push_back(&rule);
    }
    progress_.rulesTotal = selected.size();

    // One guard for the whole scan, holding every root the selected rules
    // resolved. The report carries the same list so the remover can rebuild
    // it without trusting the item paths.
    ScanReport report;
    PathGuard guard;
    std::vector<std::vector<std::string>> rootsPerRule;
    rootsPerRule.reserve(selected.size());
    for (const auto* rule : selected) {
        auto roots = ResolveRuleRoots(*rule);
        for (const auto& root : roots) guard.AllowRoot(root);
        rootsPerRule.push_back(std::move(roots));
    }
    report.allowedRoots = guard.AllowedRoots();

    for (size_t index = 0; index < selected.size(); ++index) {
        if (cancelRequested_) {
            report.cancelled = true;
            break;
        }
        const CleanRule& rule = *selected[index];
        progress_.currentRuleTitle = rule.title;
        Report(rule.title);

        if (rule.mode == RuleMode::WindowsRecycleBin) {
            ScanRecycleBin(rule, report);
        } else {
            for (const auto& root : rootsPerRule[index]) {
                if (cancelRequested_) break;
                switch (rule.mode) {
                    case RuleMode::ClearDirectoryContents:
                        ScanClearContents(rule, root, guard, report);
                        break;
                    case RuleMode::RemoveMatchingFiles:
                        ScanMatchingFiles(rule, root, guard, report);
                        break;
                    case RuleMode::RemoveMatchingDirectories:
                        ScanMatchingDirectories(rule, root, guard, report);
                        break;
                    case RuleMode::RemoveDanglingSymlinks:
                        ScanDanglingSymlinks(rule, root, guard, report);
                        break;
                    case RuleMode::WindowsRecycleBin:
                        break;   // handled above
                }
            }
        }
        progress_.rulesCompleted = index + 1;
        Report(rule.title);
    }
    if (cancelRequested_) report.cancelled = true;

    SummarizeReport(report);
    return report;
}

void Scanner::ScanClearContents(const CleanRule& rule, const std::string& root,
                                const PathGuard& guard, ScanReport& report) {
    std::error_code ec;
    fs::directory_iterator iterator(fs::path(root), kWalkOptions, ec);
    if (ec) {
        NoteUnreadableRoot(root, report);
        return;
    }
    const int64_t now = NowSeconds();
    const int64_t minAge = static_cast<int64_t>(rule.minAgeDays) * 86400;

    const fs::directory_iterator end;
    for (; iterator != end; iterator.increment(ec)) {
        if (ec) { ec.clear(); ++report.unreadablePaths; continue; }
        if (cancelRequested_) return;

        const std::string path = NormalizeForCompare(iterator->path().string());
        const std::string name = iterator->path().filename().string();
        if (MatchesAny(name, rule.excludeNames)) continue;

        const SafetyCheck check = guard.Check(iterator->path().string());
        if (!check.Allowed()) { ++report.skippedProtected; continue; }

        std::error_code entryEc;
        const bool isSymlink = iterator->is_symlink(entryEc);
        const bool isDirectory = !isSymlink && iterator->is_directory(entryEc);

        const int64_t modified = LastWriteSeconds(iterator->path().string());
        if (minAge > 0 && modified > 0 && (now - modified) < minAge) continue;

        CleanItem item;
        item.path = iterator->path().string();
        item.ruleId = rule.id;
        item.category = rule.category;
        item.isDirectory = isDirectory;
        item.modifiedAt = modified;
        item.safeByDefault = rule.safeByDefault;
        item.selected = rule.safeByDefault;
        if (isDirectory) {
            item.sizeBytes = DirectorySize(item.path, options_.sizeWalkFileLimit);
        } else if (!isSymlink) {
            const uintmax_t size = iterator->file_size(entryEc);
            item.sizeBytes = entryEc ? 0 : static_cast<uint64_t>(size);
        }

        progress_.bytesFound += item.sizeBytes;
        ++progress_.itemsFound;
        report.items.push_back(std::move(item));
        Report(path);
    }
}

void Scanner::ScanMatchingFiles(const CleanRule& rule, const std::string& root,
                                const PathGuard& guard, ScanReport& report) {
    std::error_code ec;
    fs::recursive_directory_iterator iterator(fs::path(root), kWalkOptions, ec);
    if (ec) {
        NoteUnreadableRoot(root, report);
        return;
    }
    const int64_t now = NowSeconds();
    const int64_t minAge = static_cast<int64_t>(rule.minAgeDays) * 86400;

    const fs::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(ec)) {
        if (ec) { ec.clear(); ++report.unreadablePaths; continue; }
        if (cancelRequested_) return;

        const std::string name = iterator->path().filename().string();
        std::error_code entryEc;

        // Depth 0 is the root's own children, so a maxDepth of N means N
        // levels of directories below the root.
        if (rule.maxDepth >= 0 && iterator.depth() >= rule.maxDepth - 1) {
            iterator.disable_recursion_pending();
        }
        if (iterator->is_directory(entryEc) && !entryEc) {
            // An excluded directory is not descended into at all — that is
            // what makes the .DS_Store rule skip ~/Library cheaply.
            if (MatchesAny(name, rule.excludeNames)) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        if (iterator->is_symlink(entryEc)) continue;
        if (!iterator->is_regular_file(entryEc) || entryEc) continue;
        if (!MatchesAny(name, rule.namePatterns)) continue;

        const SafetyCheck check = guard.Check(iterator->path().string());
        if (!check.Allowed()) { ++report.skippedProtected; continue; }

        const int64_t modified = LastWriteSeconds(iterator->path().string());
        if (minAge > 0 && modified > 0 && (now - modified) < minAge) continue;

        CleanItem item;
        item.path = iterator->path().string();
        item.ruleId = rule.id;
        item.category = rule.category;
        item.isDirectory = false;
        item.modifiedAt = modified;
        item.safeByDefault = rule.safeByDefault;
        item.selected = rule.safeByDefault;
        const uintmax_t size = iterator->file_size(entryEc);
        item.sizeBytes = entryEc ? 0 : static_cast<uint64_t>(size);

        progress_.bytesFound += item.sizeBytes;
        ++progress_.itemsFound;
        report.items.push_back(std::move(item));
        Report(item.path);
    }
}

void Scanner::ScanMatchingDirectories(const CleanRule& rule,
                                      const std::string& root,
                                      const PathGuard& guard,
                                      ScanReport& report) {
    std::error_code ec;
    fs::directory_iterator iterator(fs::path(root), kWalkOptions, ec);
    if (ec) {
        NoteUnreadableRoot(root, report);
        return;
    }
    const int64_t now = NowSeconds();
    const int64_t minAge = static_cast<int64_t>(rule.minAgeDays) * 86400;

    const fs::directory_iterator end;
    for (; iterator != end; iterator.increment(ec)) {
        if (ec) { ec.clear(); ++report.unreadablePaths; continue; }
        if (cancelRequested_) return;

        std::error_code entryEc;
        if (iterator->is_symlink(entryEc)) continue;
        if (!iterator->is_directory(entryEc) || entryEc) continue;

        const std::string name = iterator->path().filename().string();
        if (MatchesAny(name, rule.excludeNames)) continue;
        if (!MatchesAny(name, rule.namePatterns)) continue;

        const SafetyCheck check = guard.Check(iterator->path().string());
        if (!check.Allowed()) { ++report.skippedProtected; continue; }

        const int64_t modified = LastWriteSeconds(iterator->path().string());
        if (minAge > 0 && modified > 0 && (now - modified) < minAge) continue;

        CleanItem item;
        item.path = iterator->path().string();
        item.ruleId = rule.id;
        item.category = rule.category;
        item.isDirectory = true;
        item.modifiedAt = modified;
        item.safeByDefault = rule.safeByDefault;
        item.selected = rule.safeByDefault;
        item.sizeBytes = DirectorySize(item.path, options_.sizeWalkFileLimit);

        progress_.bytesFound += item.sizeBytes;
        ++progress_.itemsFound;
        report.items.push_back(std::move(item));
        Report(item.path);
    }
}

void Scanner::ScanDanglingSymlinks(const CleanRule& rule,
                                   const std::string& root,
                                   const PathGuard& guard,
                                   ScanReport& report) {
    std::error_code ec;
    fs::recursive_directory_iterator iterator(fs::path(root), kWalkOptions, ec);
    if (ec) {
        NoteUnreadableRoot(root, report);
        return;
    }
    const fs::recursive_directory_iterator end;
    for (; iterator != end; iterator.increment(ec)) {
        if (ec) { ec.clear(); ++report.unreadablePaths; continue; }
        if (cancelRequested_) return;

        if (rule.maxDepth >= 0 && iterator.depth() >= rule.maxDepth - 1) {
            iterator.disable_recursion_pending();
        }
        std::error_code entryEc;
        if (!iterator->is_symlink(entryEc) || entryEc) continue;

        // A dangling link is one whose target does not exist. exists()
        // follows the link, which is exactly the question being asked.
        std::error_code targetEc;
        if (fs::exists(iterator->path(), targetEc) && !targetEc) continue;

        const SafetyCheck check = guard.Check(iterator->path().string());
        if (!check.Allowed()) { ++report.skippedProtected; continue; }

        CleanItem item;
        item.path = iterator->path().string();
        item.ruleId = rule.id;
        item.category = rule.category;
        item.isDirectory = false;
        item.modifiedAt = LastWriteSeconds(item.path);
        item.safeByDefault = rule.safeByDefault;
        item.selected = rule.safeByDefault;
        item.sizeBytes = 0;

        ++progress_.itemsFound;
        report.items.push_back(std::move(item));
        Report(item.path);
    }
}

void Scanner::ScanRecycleBin(const CleanRule& rule, ScanReport& report) {
#if defined(_WIN32) || defined(_WIN64)
    SHQUERYRBINFO info{};
    info.cbSize = sizeof(info);
    // A null root queries every drive's recycle bin at once.
    if (FAILED(SHQueryRecycleBinW(nullptr, &info))) return;
    if (info.i64NumItems == 0) return;

    CleanItem item;
    item.path = "Recycle Bin";
    item.ruleId = rule.id;
    item.category = rule.category;
    item.isDirectory = true;
    item.modifiedAt = NowSeconds();
    item.safeByDefault = rule.safeByDefault;
    item.selected = rule.safeByDefault;
    item.sizeBytes = static_cast<uint64_t>(info.i64Size);

    progress_.bytesFound += item.sizeBytes;
    ++progress_.itemsFound;
    report.items.push_back(std::move(item));
    Report(item.path);
#else
    (void)rule;
    (void)report;
#endif
}

} // namespace UltraCleaner
