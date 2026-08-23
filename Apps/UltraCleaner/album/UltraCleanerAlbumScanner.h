// Apps/UltraCleaner/album/UltraCleanerAlbumScanner.h
// Finds the pictures in a photo album that are the same, or nearly the same,
// and proposes which one of each set to keep.
//
// Two questions, deliberately kept apart because they carry different
// certainty:
//
//   Duplicates  Byte-identical files, and files whose decoded pixels are
//               identical. These are facts. Removing one loses nothing, so
//               they may be pre-selected.
//   Similar     Shots that merely look alike — a burst, a rescale, an edit.
//               These are judgements. They are never pre-selected, and how
//               freely they group is the caller's choice (SimilarityLevel).
//
// The scan is read-only. Removal goes through UltraCleaner's existing
// Remover, so an album clean-up inherits the same PathGuard, the same trash
// support and the same simulate-by-default posture as the rest of the app.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCleanerImageDescriptor.h"
#include "UltraCleanerTypes.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace UltraCleaner {

// ===== WHY A SET WAS GROUPED =====
enum class GroupReason {
    IdenticalFiles,   // same bytes
    IdenticalPixels,  // different files, same decoded image
    Similar           // alike to within the chosen level
};

const char* GroupReasonName(GroupReason reason);

// A set of pictures the scan believes belong together. `keeperIndex` indexes
// into `members` and marks the one worth keeping — sharpest first, then
// largest, then biggest file. It is a suggestion; the caller may move it.
struct PictureGroup {
    GroupReason reason = GroupReason::Similar;
    std::vector<ImageDescriptor> members;
    size_t keeperIndex = 0;

    // Bytes that would come back if everything except the keeper went.
    uint64_t RecoverableBytes() const;
    const ImageDescriptor& Keeper() const { return members[keeperIndex]; }
};

// ===== OPTIONS =====
struct AlbumScanOptions {
    SimilarityLevel level = SimilarityLevel::Moments;
    bool recursive = true;
    // Only consider pictures taken within this many seconds of each other as
    // "similar". 0 disables the gate. It applies only where BOTH pictures
    // carry a usable capture time — many albums have none, and a missing
    // timestamp must never silently exclude a real match.
    int64_t maxSecondsApart = 0;
    // Pictures below this size are ignored: thumbnails and icons match
    // everything and are rarely worth deleting.
    int minimumEdgePixels = 64;
};

struct AlbumScanProgress {
    size_t filesSeen = 0;
    size_t filesDescribed = 0;
    std::string currentPath;
};

struct AlbumScanReport {
    std::vector<PictureGroup> groups;
    size_t picturesScanned = 0;
    size_t photographs = 0;
    size_t screenshots = 0;
    size_t unreadable = 0;
    bool cancelled = false;

    size_t GroupedPictures() const;
    uint64_t RecoverableBytes() const;
};

// ===== SCANNER =====
class AlbumScanner {
public:
    using ProgressCallback = std::function<void(const AlbumScanProgress&)>;

    // Walks `folder` and groups what it finds. Safe to run on a worker
    // thread; one AlbumScanner runs one scan at a time.
    AlbumScanReport Scan(const std::string& folder,
                         const AlbumScanOptions& options = {},
                         ProgressCallback onProgress = nullptr);

    // Re-groups an already-scanned set at a different level. Describing the
    // pictures is the expensive part and does not depend on the level, so
    // changing it costs only the comparisons — the UI can offer this as an
    // instant control rather than a rescan.
    static AlbumScanReport Regroup(const std::vector<ImageDescriptor>& pictures,
                                   const AlbumScanOptions& options);

    // Everything the last Scan() described, in the order it found it. Feed
    // this back into Regroup().
    const std::vector<ImageDescriptor>& Described() const { return described_; }

    void RequestCancel() { cancelRequested_ = true; }
    void ResetCancel()   { cancelRequested_ = false; }
    bool IsCancelRequested() const { return cancelRequested_; }

private:
    std::atomic<bool> cancelRequested_{false};
    std::vector<ImageDescriptor> described_;
};

// Turns an album report into the ScanReport the existing Remover consumes,
// selecting every non-keeper of the groups named in `groupsToClean`.
// Exact-duplicate groups may be pre-selected; similar groups never are, so
// the caller decides explicitly which groups to act on.
ScanReport ToRemovalReport(const AlbumScanReport& album,
                           const std::vector<size_t>& groupsToClean);

} // namespace UltraCleaner
