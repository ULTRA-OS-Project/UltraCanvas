// Apps/UltraCleaner/ui/UltraCleanerAlbumView.h
// The photo-album half of UltraCleaner's window: pick a folder, choose how
// freely to group, and review what the scan proposes as thumbnails.
//
// Groups are the unit of review here, not files. Each group shows every
// picture in it with the suggested keeper marked, and a single checkbox for
// "clean this group" — because deciding one by one across a thousand photos
// is not a thing anyone finishes. Nothing is ticked for you: a burst is a
// judgement, and the app does not make it on the user's behalf.
//
// Changing the level re-groups without re-reading the disk. Describing the
// pictures is the expensive part and does not depend on the level, so the
// control is instant rather than a rescan.
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCleanerAlbumScanner.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCleaner {

class AlbumView {
public:
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> Build(float width,
                                                             float height);

    // Replaces the group list. Pass an empty report to clear it.
    void SetReport(const AlbumScanReport& report);

    void SetFolder(const std::string& folder);
    std::string Folder() const { return folder_; }

    SimilarityLevel Level() const { return level_; }

    // Indexes of the groups whose checkbox is ticked.
    std::vector<size_t> SelectedGroups() const;

    void SetStatus(const std::string& text);
    void SetBusy(bool busy);

    std::function<void()> onChooseFolder;
    std::function<void()> onScan;
    std::function<void()> onStop;
    std::function<void()> onClean;
    // The level changed — the host re-groups the pictures it already has.
    std::function<void(SimilarityLevel)> onLevelChanged;
    // A group's tick changed; the host refreshes its summary.
    std::function<void()> onSelectionChanged;

private:
    void RebuildGroups(const AlbumScanReport& report);

    std::shared_ptr<UltraCanvas::UltraCanvasContainer> root_;
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> groupList_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> folderLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> levelHint_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> levelDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> scanButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> stopButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> cleanButton_;

    std::vector<std::shared_ptr<UltraCanvas::UltraCanvasCheckbox>> groupChecks_;
    std::string folder_;
    SimilarityLevel level_ = SimilarityLevel::Moments;
};

} // namespace UltraCleaner
