// Apps/UltraCleaner/engine/UltraCleanerRules.h
// The declarative table of what UltraCleaner considers junk. Every location
// the scanner looks at comes from a CleanRule — there is no code path that
// removes a path a rule did not name — so reviewing this one table is
// enough to know what the application can touch on each operating system.
//
// A rule names its roots with the tokens from UltraCleanerPaths.h, so a
// single row can cover all three platforms where the layout only differs in
// the base directory. Rules whose platform mask excludes the running system,
// or whose tokens do not resolve, are simply not scanned.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCleanerTypes.h"

#include <string>
#include <vector>

namespace UltraCleaner {

// ===== PLATFORM MASK =====
namespace PlatformMask {
    constexpr unsigned Linux   = 1u << 0;
    constexpr unsigned MacOS   = 1u << 1;
    constexpr unsigned Windows = 1u << 2;
    constexpr unsigned Posix   = Linux | MacOS;
    constexpr unsigned All     = Linux | MacOS | Windows;
}

unsigned PlatformBit(CleanerPlatform platform);

// ===== RULE MODES =====
enum class RuleMode {
    // Everything directly inside the root is removable; the root itself stays.
    // One item is reported per top-level entry, sized recursively.
    ClearDirectoryContents,
    // Files matching `namePatterns`, found by walking the root down to
    // `maxDepth` levels. Directories are never reported by this mode.
    RemoveMatchingFiles,
    // Directories directly inside the root whose name matches `namePatterns`.
    RemoveMatchingDirectories,
    // Symbolic links under the root whose target no longer exists.
    RemoveDanglingSymlinks,
    // The Windows recycle bin, which is shell-managed rather than a plain
    // directory: queried and emptied through the shell API. Reported as one
    // item, and ignored on the other platforms.
    WindowsRecycleBin
};

// ===== A RULE =====
struct CleanRule {
    std::string id;                          // stable, used in reports and tests
    CleanCategory category = CleanCategory::TemporaryFiles;
    std::string title;                       // human-readable, shown per item
    std::vector<std::string> roots;          // tokenized; may contain '*' segments
    RuleMode mode = RuleMode::ClearDirectoryContents;
    std::vector<std::string> namePatterns;   // globs, for the Matching* modes
    std::vector<std::string> excludeNames;   // globs never removed inside a root
    int minAgeDays = 0;                      // skip anything modified more recently
    int maxDepth = 1;                        // walk limit for RemoveMatchingFiles
    // False for anything whose removal costs the user something they may not
    // expect — a permanent trash empty, a long re-download, a build that now
    // takes an hour. Such categories start unticked in the UI.
    bool safeByDefault = true;
    unsigned platforms = PlatformMask::All;
};

// The complete table, every platform.
const std::vector<CleanRule>& BuiltinRules();

// The subset that applies to `platform` (defaults to the running one).
std::vector<CleanRule> RulesForPlatform(CleanerPlatform platform);
std::vector<CleanRule> RulesForCurrentPlatform();

// Lookup by id; nullptr when unknown.
const CleanRule* FindRule(const std::string& id);

} // namespace UltraCleaner
