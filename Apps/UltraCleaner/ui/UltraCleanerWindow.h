// Apps/UltraCleaner/ui/UltraCleanerWindow.h
// UltraCleaner's main window: a toolbar (scan, stop, what-to-do-with-it,
// clean), the category panel on the left, and a detail list on the right
// naming exactly which paths are going. Nothing is painted by hand — the
// window is assembled from UltraCanvas elements, and the detail list is an
// UltraCanvasColumnsTreeView (Location / Clean / Size / Changed / Rule)
// rather than a drawn grid.
//
// Scanning and cleaning both block on the disk, so both run on a worker
// thread; results and progress come back through a queue the UI timer
// drains, which is the same shape UltraSocial uses for its network work.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCleanerAlbumView.h"
#include "UltraCleanerCategoryPanel.h"

#include "UltraCleanerAlbumScanner.h"
#include "UltraCleanerRemover.h"
#include "UltraCleanerScanner.h"
#include "UltraCleanerTypes.h"

#include "UltraCanvasButton.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasColumnsTreeView.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasWindow.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraCleaner {

class UltraCleanerWindow {
public:
    ~UltraCleanerWindow();

    // `albumFolder`, when given, opens the Photo albums tab on that folder
    // so the app can be started from a file manager or the command line.
    bool Initialize(const std::string& albumFolder = "");
    void Show();

private:
    // ===== CONSTRUCTION =====
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildRulePage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildAlbumPage();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildToolbar();
    std::shared_ptr<UltraCanvas::UltraCanvasContainer> BuildDetailPanel();

    // ===== ACTIONS =====
    void StartScan();
    void StopWork();
    void StartClean();
    void ConfirmAndClean(RemovalMode mode);

    // ===== VIEW UPDATES =====
    void RefreshDetailList();
    // Repaints one row after its tick changed, without rebuilding the list.
    void RefreshDetailRow(size_t itemIndex);
    void RefreshCategoryFilter();
    void RefreshSummary();
    void SetBusy(bool busy);
    void SetStatus(const std::string& text);

    // Applies a category-level tick to every item in that category.
    void ApplyCategorySelection(CleanCategory category, bool selected);
    // Flips one item, addressed by its index in report_.items.
    void ToggleItem(size_t itemIndex);
    // Toolbar shortcuts: back to the rules' own defaults, or nothing at all.
    void SelectSafeDefaults();
    void SelectNone();

    // Selected-item totals, for the summary line and the confirmation text.
    size_t SelectedItemCount() const;
    uint64_t SelectedBytes() const;

    // ===== ALBUM =====
    void ChooseAlbumFolder();
    void StartAlbumScan();
    void RegroupAlbum(SimilarityLevel level);
    void CleanAlbumSelection();
    void RefreshAlbumSummary();

    // ===== THREAD PLUMBING =====
    void RunOnUiThread(std::function<void()> action);
    void DrainUiQueue();

    std::shared_ptr<UltraCanvas::UltraCanvasWindow> window_;

    CategoryPanel categoryPanel_;
    std::shared_ptr<UltraCanvas::UltraCanvasColumnsTreeView> detailTree_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> categoryFilter_;
    std::shared_ptr<UltraCanvas::UltraCanvasDropdown> modeDropdown_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> scanButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> stopButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasButton> cleanButton_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> statusLabel_;
    std::shared_ptr<UltraCanvas::UltraCanvasLabel> summaryLabel_;

    Scanner scanner_;
    Remover remover_;
    ScanReport report_;

    AlbumView albumView_;
    AlbumScanner albumScanner_;
    AlbumScanReport albumReport_;
    std::vector<ImageDescriptor> albumPictures_;
    // The category the filter dropdown is on; CategoryCount means "all".
    CleanCategory filterCategory_ = CleanCategory::CategoryCount;
    std::vector<CleanCategory> filterCategories_;

    std::mutex uiQueueMutex_;
    std::vector<std::function<void()>> uiQueue_;

    // Scan progress arrives per item, far faster than the UI needs it, so the
    // worker overwrites one slot instead of queueing thousands of closures.
    std::mutex progressMutex_;
    std::string pendingStatus_;
    bool pendingStatusDirty_ = false;
    // Which tab's status line the pending text belongs to.
    bool pendingStatusIsAlbum_ = false;
    std::atomic<bool> working_{false};
    UltraCanvas::TimerId uiTimer_ = 0;
};

} // namespace UltraCleaner
