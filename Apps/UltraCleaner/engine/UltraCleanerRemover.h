// Apps/UltraCleaner/engine/UltraCleanerRemover.h
// Removal of the items a scan proposed. Three modes, in order of how much
// they can cost the user:
//
//   Simulate      — touch nothing, report what would go. The default.
//   MoveToTrash   — hand the path to the platform's trash so the user can
//                   undo (XDG trash on Linux, ~/.Trash on macOS, the shell's
//                   recycle bin on Windows).
//   DeletePermanently — unlink it.
//
// Every path is re-checked by a PathGuard rebuilt from the report's own
// allowed roots before anything happens to it. The scan already checked, but
// a scan and a clean are separated by however long the user spent reading
// the list, and the check is cheap.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCleanerTypes.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace UltraCleaner {

// ===== MODE =====
enum class RemovalMode {
    Simulate,
    MoveToTrash,
    DeletePermanently
};

struct RemovalOptions {
    RemovalMode mode = RemovalMode::Simulate;
    // Stop after this many failures; 0 = never stop. A wall of "permission
    // denied" usually means the user needs an elevated run, not 4000 lines.
    size_t failureLimit = 50;
};

// ===== RESULT =====
struct RemovalFailure {
    std::string path;
    std::string reason;
};

struct RemovalReport {
    size_t removedItems = 0;
    uint64_t freedBytes = 0;
    size_t refusedByGuard = 0;          // failed the safety check at clean time
    size_t skippedMissing = 0;          // vanished between scan and clean
    std::vector<RemovalFailure> failures;
    bool cancelled = false;
    bool simulated = false;
};

// ===== REMOVER =====
class Remover {
public:
    // (itemsDone, itemsTotal, currentPath)
    using ProgressCallback =
        std::function<void(size_t, size_t, const std::string&)>;

    // Removes the selected items of `report`. Items with `selected == false`
    // are left alone. Safe to run on a worker thread.
    RemovalReport Remove(const ScanReport& report,
                         const RemovalOptions& options = {},
                         ProgressCallback onProgress = nullptr);

    void RequestCancel() { cancelRequested_ = true; }
    void ResetCancel()   { cancelRequested_ = false; }
    bool IsCancelRequested() const { return cancelRequested_; }

private:
    std::atomic<bool> cancelRequested_{false};
};

// ===== TRASH =====
// Moves one path to the platform trash. Returns false with `error` set when
// the platform has no trash, or the move failed. Exposed separately because
// it is useful on its own and because the tests exercise it directly.
bool MoveToPlatformTrash(const std::string& path, std::string& error);

// Empties the Windows recycle bin; a no-op returning false elsewhere.
bool EmptyPlatformRecycleBin(std::string& error);

} // namespace UltraCleaner
