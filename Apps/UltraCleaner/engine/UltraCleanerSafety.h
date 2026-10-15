// Apps/UltraCleaner/engine/UltraCleanerSafety.h
// The guard every candidate path passes before it may be scanned or removed.
// UltraCleaner deletes files, so the interesting question is not what it can
// find but what it can be talked into destroying; this file is the answer,
// and it is deliberately the shortest one in the engine.
//
// A path is removable only when all of the following hold:
//   * it is absolute, and at least two levels below the filesystem root;
//   * it lies strictly inside a root the current rule set resolved — a root
//     itself is never removable, only its contents;
//   * neither it nor its symlink-resolved form is a protected location, and
//     it is not an ancestor of one (so ~/Downloads/old.dmg may go while
//     ~/Downloads and ~ may not);
//   * following the symlinks in its parent chain still lands inside an
//     allowed root, so a link planted in a cache cannot redirect a delete
//     into the user's documents;
//   * it is a regular file, a directory or a symlink — never a socket, a
//     fifo or a device node.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <string>
#include <vector>

namespace UltraCleaner {

// ===== VERDICTS =====
enum class SafetyVerdict {
    Allowed,
    NotAbsolute,        // relative or empty path
    TooShallow,         // "/tmp", "C:/" and friends
    OutsideAllowedRoot, // no rule root covers it
    ProtectedLocation,  // is, or contains, something the user cannot lose
    SymlinkEscape,      // its parent chain resolves out of every root
    SpecialFile         // socket / fifo / device
};

struct SafetyCheck {
    SafetyVerdict verdict = SafetyVerdict::Allowed;
    std::string reason;                       // human-readable, for reports
    bool Allowed() const { return verdict == SafetyVerdict::Allowed; }
};

// ===== PROTECTED LOCATIONS =====
// Directories that are never removed, and never removed *through* — a
// candidate that contains one of these is refused as well. Resolved for the
// running user at first use.
const std::vector<std::string>& ProtectedPaths();

// True when `path` is a protected location or an ancestor of one.
bool IsProtectedPath(const std::string& path);

// ===== THE GUARD =====
// Holds the roots the current scan resolved from the rule table. Nothing
// outside them is ever touched, so a rule that fails to resolve simply makes
// its own paths unreachable rather than widening anything.
class PathGuard {
public:
    // Registers a root. Non-existent roots are kept (a rule may name a
    // directory that is absent on this machine). An existing one is kept
    // twice: as given and with its symlinks resolved, and a path inside
    // either counts as inside. The scanner walks the root as given, so its
    // candidates carry the given spelling: on macOS $TMPDIR is
    // /var/folders/..., and /var is a link to /private/var. Holding only the
    // resolved form refused every one of them as outside the roots, and the
    // temp-file rules found nothing there. The parent-chain check in Check()
    // still compares against the resolved form only.
    void AllowRoot(const std::string& root);
    void Reset();

    // The roots as given (normalized): what a remover rebuilds its guard
    // from, resolving them again on its own machine and moment.
    const std::vector<std::string>& AllowedRoots() const { return roots_; }

    // The full check described at the top of this file.
    SafetyCheck Check(const std::string& path) const;

    // Whether `path` lies strictly inside any allowed root (no symlink,
    // protection or file-type checks). Exposed for the scanner's walk.
    bool IsInsideAllowedRoot(const std::string& path) const;

private:
    std::vector<std::string> roots_;          // as given, normalized
    std::vector<std::string> resolvedRoots_;  // same order, symlinks resolved
};

} // namespace UltraCleaner
