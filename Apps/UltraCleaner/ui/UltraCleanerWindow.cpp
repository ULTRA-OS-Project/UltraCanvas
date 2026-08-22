// Apps/UltraCleaner/ui/UltraCleanerWindow.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerWindow.h"

#include "UltraCleanerRules.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasModalDialog.h"

#include <cstring>
#include <ctime>
#include <thread>

using namespace UltraCanvas;

namespace UltraCleaner {
namespace {

// Detail rows are addressed by node id: "item<index into report_.items>".
constexpr const char* kItemNodePrefix = "item";

constexpr float kWindowWidth  = 1060.0f;
constexpr float kWindowHeight = 760.0f;

// The dropdown's order, and what each entry means to the remover.
const RemovalMode kModes[] = {
    RemovalMode::Simulate,
    RemovalMode::MoveToTrash,
    RemovalMode::DeletePermanently
};

std::string FormatTimestamp(int64_t seconds) {
    if (seconds <= 0) return "—";
    const std::time_t when = static_cast<std::time_t>(seconds);
    std::tm local{};
#if defined(_WIN32) || defined(_WIN64)
    localtime_s(&local, &when);
#else
    localtime_r(&when, &local);
#endif
    char buffer[24];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", &local);
    return buffer;
}

} // namespace

UltraCleanerWindow::~UltraCleanerWindow() {
    // The worker captures `this`; make sure nothing is still running when the
    // window goes away.
    scanner_.RequestCancel();
    remover_.RequestCancel();
    if (uiTimer_ != 0) {
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            app->StopTimer(uiTimer_);
        }
    }
}

bool UltraCleanerWindow::Initialize() {
    WindowConfig config;
    config.title  = "UltraCleaner";
    config.width  = static_cast<int>(kWindowWidth);
    config.height = static_cast<int>(kWindowHeight);
    config.minWidth  = 820;
    config.minHeight = 560;
    window_ = CreateWindow(config);
    if (!window_) return false;

    auto title = CreateLabel("ucTitle", 16, 10, 260, 26, "UltraCleaner");
    title->SetFontSize(18);
    window_->AddChild(title);

    const auto rules = RulesForCurrentPlatform();
    window_->AddChild(CreateLabel(
        "ucSubtitle", 180, 14, 520, 22,
        PlatformName(CurrentPlatform()) + " · " + std::to_string(rules.size()) +
        " cleanup rules · nothing is removed until you say so"));

    window_->AddChild(BuildToolbar());

    auto categories = CreateGroupBox("ucCategoryBox", 12, 92, 404, 566,
                                     "What to clean");
    // A GroupBox folds its title band into its own padding, so its children
    // are laid out rather than positioned: an absolute y would put them
    // under the caption. The panel takes whatever is left below it.
    categories->layout.SetFlexColumn()
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    // The GroupBox is a container too; leave the scrolling to the panel
    // inside it rather than having both grow a scrollbar.
    ContainerStyle plainBox;
    plainBox.autoShowScrollbars = false;
    categories->SetContainerStyle(plainBox);
    auto categoryList = categoryPanel_.Build(0, 0, 376, 520);
    categories->AddChild(categoryList);
    categoryList->layoutItem.SetFlexGrow(1);
    categoryList->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    categoryPanel_.onCategoryToggled = [this](CleanCategory category, bool on) {
        ApplyCategorySelection(category, on);
    };
    window_->AddChild(categories);

    window_->AddChild(BuildDetailPanel());
    BuildStatusBar();

    // Worker threads queue their results here; this timer applies them on the
    // UI thread.
    if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
        uiTimer_ = app->StartTimer(100, /*periodic=*/true,
                                   [this](TimerId) { DrainUiQueue(); });
    }
    RefreshSummary();
    return true;
}

void UltraCleanerWindow::Show() {
    if (window_) window_->Show();
}

std::shared_ptr<UltraCanvasContainer> UltraCleanerWindow::BuildToolbar() {
    auto bar = CreateContainer("ucToolbar", 12, 48, 1032, 36);
    bar->layout.SetFlexRow()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    scanButton_ = CreateButton("ucScan", 0, 0, 110, 28, "Scan");
    scanButton_->onClick = [this]() { StartScan(); };
    bar->AddChild(scanButton_);

    stopButton_ = CreateButton("ucStop", 0, 0, 80, 28, "Stop");
    stopButton_->onClick = [this]() { StopWork(); };
    stopButton_->SetDisabled(true);
    bar->AddChild(stopButton_);

    bar->AddChild(CreateLabel("ucModeLabel", 0, 0, 60, 24, "Then:"));

    modeDropdown_ = CreateDropdown("ucMode", 0, 0, 210, 28);
    modeDropdown_->AddItem("Simulate — change nothing");
    modeDropdown_->AddItem("Move to Trash");
    modeDropdown_->AddItem("Delete permanently");
    modeDropdown_->SetSelectedIndex(0, /*runNotifications=*/false);
    bar->AddChild(modeDropdown_);

    cleanButton_ = CreateButton("ucClean", 0, 0, 120, 28, "Clean…");
    cleanButton_->onClick = [this]() { StartClean(); };
    bar->AddChild(cleanButton_);

    bar->AddStretchSpacer(1);

    // These go through the window rather than the panel: setting every item
    // at once and refreshing the views once is much cheaper than firing one
    // category callback per row.
    auto safeButton = CreateButton("ucSelectSafe", 0, 0, 130, 28, "Tick safe ones");
    safeButton->onClick = [this]() { SelectSafeDefaults(); };
    bar->AddChild(safeButton);

    auto noneButton = CreateButton("ucSelectNone", 0, 0, 100, 28, "Tick none");
    noneButton->onClick = [this]() { SelectNone(); };
    bar->AddChild(noneButton);

    return bar;
}

std::shared_ptr<UltraCanvasContainer> UltraCleanerWindow::BuildDetailPanel() {
    auto box = CreateGroupBox("ucDetailBox", 428, 92, 616, 566,
                              "Exactly what would go");
    box->layout.SetFlexColumn()
               .SetFlexGap(6)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    ContainerStyle plainBox;
    plainBox.autoShowScrollbars = false;
    box->SetContainerStyle(plainBox);

    auto filterRow = CreateContainer("ucFilterRow", 0, 0, 0, 30);
    filterRow->layout.SetFlexRow()
                     .SetFlexGap(8)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    filterRow->AddChild(CreateLabel("ucFilterLabel", 0, 0, 54, 24, "Show:"));

    categoryFilter_ = CreateDropdown("ucFilter", 0, 0, 236, 28);
    categoryFilter_->AddItem("All categories");
    categoryFilter_->SetSelectedIndex(0, /*runNotifications=*/false);
    categoryFilter_->onSelectionChanged = [this](int index, const DropdownItem&) {
        filterCategory_ = (index <= 0 ||
                           index > static_cast<int>(filterCategories_.size()))
                              ? CleanCategory::CategoryCount
                              : filterCategories_[static_cast<size_t>(index - 1)];
        RefreshDetailList();
    };
    filterRow->AddChild(categoryFilter_);

    auto hint = CreateLabel("ucFilterHint", 0, 0, 260, 22,
                            "Double-click a row to keep or drop it.");
    hint->SetFontSize(11);
    filterRow->AddChild(hint);
    hint->layoutItem.SetFlexGrow(1);
    box->AddChild(filterRow);
    filterRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    detailTree_ = std::make_shared<UltraCanvasColumnsTreeView>(
        "ucDetail", 0, 0, 584, 470);
    detailTree_->SetDisplayMode(TreeDisplayMode::Columns);
    detailTree_->SetSelectionMode(TreeSelectionMode::Single);
    detailTree_->SetShowColumnHeader(true);
    detailTree_->SetRowHeight(22);
    detailTree_->SetColumns({
        { "location", "Location", 0,   180, 3.0f, TextAlignment::Left,
          Color(40, 40, 40), Colors::Transparent, 0, /*isTreeColumn=*/true },
        { "clean",    "Clean",    56,  0,   1.0f, TextAlignment::Center,
          Color(40, 40, 40), Colors::Transparent, 0, false },
        { "size",     "Size",     84,  0,   1.0f, TextAlignment::Right,
          Color(40, 40, 40), Colors::Transparent, 0, false },
        { "changed",  "Changed",  116, 0,   1.0f, TextAlignment::Left,
          Color(40, 40, 40), Colors::Transparent, 0, false },
        { "rule",     "Rule",     130, 0,   1.0f, TextAlignment::Left,
          Color(90, 90, 90), Colors::Transparent, 0, false },
    });
    // The node id carries the item's index in report_.items, which is what a
    // double-click needs to know.
    detailTree_->onNodeDoubleClicked = [this](TreeNode* node) {
        if (!node) return;
        const std::string& id = node->data.nodeId;
        if (id.rfind(kItemNodePrefix, 0) != 0) return;   // a group header
        ToggleItem(static_cast<size_t>(
            std::strtoull(id.c_str() + std::strlen(kItemNodePrefix), nullptr, 10)));
    };
    box->AddChild(detailTree_);
    detailTree_->layoutItem.SetFlexGrow(1);
    detailTree_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    return box;
}

void UltraCleanerWindow::BuildStatusBar() {
    summaryLabel_ = CreateLabel("ucSummary", 16, 668, 1020, 24,
                                "Nothing scanned yet.");
    window_->AddChild(summaryLabel_);

    statusLabel_ = CreateLabel("ucStatus", 16, 694, 1020, 44, "");
    statusLabel_->SetWrap(TextWrap::WrapWord);
    statusLabel_->SetFontSize(11);
    window_->AddChild(statusLabel_);
}

// ===== ACTIONS =====

void UltraCleanerWindow::StartScan() {
    if (working_.exchange(true)) return;

    SetBusy(true);
    SetStatus("Scanning…");
    categoryPanel_.ShowMessage("Scanning…");
    report_ = ScanReport{};
    RefreshDetailList();

    scanner_.ResetCancel();
    std::thread([this]() {
        // Progress arrives fast; only the last one before each UI tick
        // matters, so the callback just overwrites the status text.
        ScanReport report = scanner_.Scan(
            RulesForCurrentPlatform(), ScanOptions{},
            [this](const ScanProgress& progress) {
                // Called once per item found: overwrite one slot instead of
                // queueing a closure the UI would only throw away.
                std::lock_guard<std::mutex> lock(progressMutex_);
                pendingStatus_ = "Scanning " + progress.currentRuleTitle + " — " +
                                 std::to_string(progress.itemsFound) +
                                 " items, " + FormatByteSize(progress.bytesFound) +
                                 " so far";
                pendingStatusDirty_ = true;
            });

        RunOnUiThread([this, report = std::move(report)]() mutable {
            report_ = std::move(report);
            working_ = false;
            SetBusy(false);
            categoryPanel_.SetReport(report_);
            RefreshCategoryFilter();
            RefreshDetailList();
            RefreshSummary();

            std::string status =
                report_.cancelled ? "Scan stopped early. " : "Scan finished. ";
            status += std::to_string(report_.totalItems) + " removable items, " +
                      FormatByteSize(report_.totalBytes) + " in total.";
            if (report_.unreadablePaths > 0) {
                status += "  " + std::to_string(report_.unreadablePaths) +
                          " location(s) could not be read — a run with more "
                          "rights would see more.";
            }
            SetStatus(status);
        });
    }).detach();
}

void UltraCleanerWindow::StopWork() {
    scanner_.RequestCancel();
    remover_.RequestCancel();
    SetStatus("Stopping…");
}

void UltraCleanerWindow::StartClean() {
    if (working_) return;
    if (SelectedItemCount() == 0) {
        UltraCanvasDialogManager::ShowInformation(
            "Nothing is ticked. Tick a category on the left first.",
            "Nothing to clean", nullptr, window_.get());
        return;
    }

    const int index = modeDropdown_ ? modeDropdown_->GetSelectedIndex() : 0;
    const RemovalMode mode =
        (index >= 0 && index < 3) ? kModes[index] : RemovalMode::Simulate;

    if (mode == RemovalMode::Simulate) {
        ConfirmAndClean(mode);   // a simulation needs no warning
        return;
    }

    const std::string what =
        std::to_string(SelectedItemCount()) + " items (" +
        FormatByteSize(SelectedBytes()) + ")";
    const std::string message =
        mode == RemovalMode::MoveToTrash
            ? "Move " + what + " to the trash?\n\nThey stay recoverable until "
              "the trash is emptied."
            : "Permanently delete " + what + "?\n\nThis cannot be undone.";

    UltraCanvasDialogManager::ShowConfirmation(
        message, mode == RemovalMode::MoveToTrash ? "Move to Trash"
                                                  : "Delete permanently",
        [this, mode](bool confirmed) {
            if (confirmed) ConfirmAndClean(mode);
        },
        window_.get());
}

void UltraCleanerWindow::ConfirmAndClean(RemovalMode mode) {
    if (working_.exchange(true)) return;

    SetBusy(true);
    SetStatus(mode == RemovalMode::Simulate ? "Simulating…" : "Cleaning…");

    RemovalOptions options;
    options.mode = mode;

    remover_.ResetCancel();
    // The worker gets its own copy of the report: the UI thread keeps editing
    // ticks while this runs.
    std::thread([this, options, report = report_]() {
        RemovalReport result = remover_.Remove(
            report, options,
            [this](size_t done, size_t total, const std::string& path) {
                std::lock_guard<std::mutex> lock(progressMutex_);
                pendingStatus_ = "Cleaning " + std::to_string(done) + " of " +
                                 std::to_string(total) + " — " + path;
                pendingStatusDirty_ = true;
            });

        RunOnUiThread([this, result, options]() {
            working_ = false;
            SetBusy(false);

            std::string message;
            if (result.simulated) {
                message = "Simulation: " + std::to_string(result.removedItems) +
                          " items would go, freeing " +
                          FormatByteSize(result.freedBytes) + ".";
            } else {
                message = std::to_string(result.removedItems) + " items " +
                          (options.mode == RemovalMode::MoveToTrash
                               ? "moved to the trash"
                               : "removed") +
                          ", " + FormatByteSize(result.freedBytes) + " freed.";
            }
            if (result.skippedMissing > 0) {
                message += "  " + std::to_string(result.skippedMissing) +
                           " had already gone.";
            }
            if (result.refusedByGuard > 0) {
                message += "  " + std::to_string(result.refusedByGuard) +
                           " were refused by the safety check.";
            }
            if (result.cancelled) message += "  Stopped early.";
            SetStatus(message);

            if (!result.failures.empty()) {
                std::string detail;
                size_t shown = 0;
                for (const auto& failure : result.failures) {
                    if (shown++ >= 12) break;
                    detail += failure.path + "\n    " + failure.reason + "\n";
                }
                if (result.failures.size() > shown) {
                    detail += "…and " +
                              std::to_string(result.failures.size() - shown) +
                              " more.";
                }
                UltraCanvasDialogManager::ShowWarning(
                    message + "\n\nSome items could not be removed:\n" + detail,
                    "Finished with warnings", nullptr, window_.get());
            } else if (!result.simulated) {
                UltraCanvasDialogManager::ShowInformation(
                    message, "Cleanup finished", nullptr, window_.get());
            } else {
                UltraCanvasDialogManager::ShowInformation(
                    message + "\n\nNothing was changed. Pick “Move to Trash” "
                              "or “Delete permanently” to act on it.",
                    "Simulation finished", nullptr, window_.get());
            }

            // What is on disk has changed; the old report no longer describes it.
            if (!result.simulated && result.removedItems > 0) {
                StartScan();
            }
        });
    }).detach();
}

// ===== VIEW UPDATES =====

void UltraCleanerWindow::RefreshCategoryFilter() {
    if (!categoryFilter_) return;
    categoryFilter_->ClearItems();
    filterCategories_.clear();

    categoryFilter_->AddItem("All categories");
    for (const auto& summary : report_.categories) {
        filterCategories_.push_back(summary.category);
        categoryFilter_->AddItem(CategoryTitle(summary.category) + "  (" +
                                 FormatByteSize(summary.totalBytes) + ")");
    }
    filterCategory_ = CleanCategory::CategoryCount;
    categoryFilter_->SetSelectedIndex(0, /*runNotifications=*/false);
}

void UltraCleanerWindow::RefreshDetailList() {
    if (!detailTree_) return;

    // A group-header root that says what the list is currently showing —
    // rebuilding from the root also drops the old selection and hover.
    uint64_t shownBytes = 0;
    size_t shownItems = 0;
    for (const auto& item : report_.items) {
        if (filterCategory_ != CleanCategory::CategoryCount &&
            item.category != filterCategory_) {
            continue;
        }
        ++shownItems;
        shownBytes += item.sizeBytes;
    }

    TreeNodeData rootData("ucDetailRoot",
        (filterCategory_ == CleanCategory::CategoryCount
             ? std::string("All categories")
             : CategoryTitle(filterCategory_)) +
        " — " + std::to_string(shownItems) +
        (shownItems == 1 ? " item, " : " items, ") + FormatByteSize(shownBytes));
    rootData.isGroupHeader = true;
    detailTree_->SetRootNode(rootData);

    for (size_t index = 0; index < report_.items.size(); ++index) {
        const CleanItem& item = report_.items[index];
        if (filterCategory_ != CleanCategory::CategoryCount &&
            item.category != filterCategory_) {
            continue;
        }
        TreeNodeData node(kItemNodePrefix + std::to_string(index), item.path);
        node.tooltip = CategoryTitle(item.category) + " · " +
                       (item.isDirectory ? "folder" : "file");
        node.SetCell("clean", item.selected ? "yes" : "—");
        // A directory's size is the sum of what is inside it, so mark it as a
        // total rather than a file size.
        node.SetCell("size", FormatByteSize(item.sizeBytes) +
                             (item.isDirectory ? " *" : ""));
        node.SetCell("changed", FormatTimestamp(item.modifiedAt));
        node.SetCell("rule", item.ruleId);
        node.textColor = item.selected ? Color(40, 40, 40) : Color(150, 150, 150);
        detailTree_->AddNode("ucDetailRoot", node);
    }
    detailTree_->ExpandAll();
}

void UltraCleanerWindow::RefreshDetailRow(size_t itemIndex) {
    if (!detailTree_ || itemIndex >= report_.items.size()) return;
    TreeNode* node =
        detailTree_->FindNode(kItemNodePrefix + std::to_string(itemIndex));
    if (!node) return;                      // filtered out of the current view

    const CleanItem& item = report_.items[itemIndex];
    node->data.SetCell("clean", item.selected ? "yes" : "—");
    node->data.textColor = item.selected ? Color(40, 40, 40) : Color(150, 150, 150);
    detailTree_->RequestRedraw();
}

void UltraCleanerWindow::RefreshSummary() {
    if (!summaryLabel_) return;
    if (report_.totalItems == 0) {
        summaryLabel_->SetText("Nothing scanned yet — press “Scan”.");
        return;
    }
    summaryLabel_->SetText(
        "Ticked: " + std::to_string(SelectedItemCount()) + " of " +
        std::to_string(report_.totalItems) + " items · " +
        FormatByteSize(SelectedBytes()) + " of " +
        FormatByteSize(report_.totalBytes) + " recoverable" +
        (report_.skippedProtected > 0
             ? "  ·  " + std::to_string(report_.skippedProtected) +
               " candidates refused by the safety check"
             : ""));
}

void UltraCleanerWindow::SetBusy(bool busy) {
    if (scanButton_)   scanButton_->SetDisabled(busy);
    if (cleanButton_)  cleanButton_->SetDisabled(busy);
    if (stopButton_)   stopButton_->SetDisabled(!busy);
    if (modeDropdown_) modeDropdown_->SetDisabled(busy);
}

void UltraCleanerWindow::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

void UltraCleanerWindow::ApplyCategorySelection(CleanCategory category,
                                                bool selected) {
    for (auto& item : report_.items) {
        if (item.category == category) item.selected = selected;
    }
    RefreshDetailList();
    RefreshSummary();
}

void UltraCleanerWindow::ToggleItem(size_t itemIndex) {
    if (itemIndex >= report_.items.size()) return;
    report_.items[itemIndex].selected = !report_.items[itemIndex].selected;
    categoryPanel_.RefreshFromReport(report_);
    RefreshDetailRow(itemIndex);            // one row, not the whole list
    RefreshSummary();
}

void UltraCleanerWindow::SelectSafeDefaults() {
    // Back to what the rules themselves propose. Done here rather than in the
    // panel so every item is set once and the views refresh once, instead of
    // one category callback per row.
    for (auto& item : report_.items) item.selected = item.safeByDefault;
    categoryPanel_.RefreshFromReport(report_);
    RefreshDetailList();
    RefreshSummary();
}

void UltraCleanerWindow::SelectNone() {
    for (auto& item : report_.items) item.selected = false;
    categoryPanel_.RefreshFromReport(report_);
    RefreshDetailList();
    RefreshSummary();
}

size_t UltraCleanerWindow::SelectedItemCount() const {
    size_t count = 0;
    for (const auto& item : report_.items) {
        if (item.selected) ++count;
    }
    return count;
}

uint64_t UltraCleanerWindow::SelectedBytes() const {
    uint64_t bytes = 0;
    for (const auto& item : report_.items) {
        if (item.selected) bytes += item.sizeBytes;
    }
    return bytes;
}

// ===== THREAD PLUMBING =====

void UltraCleanerWindow::RunOnUiThread(std::function<void()> action) {
    std::lock_guard<std::mutex> lock(uiQueueMutex_);
    uiQueue_.push_back(std::move(action));
}

void UltraCleanerWindow::DrainUiQueue() {
    // Progress first, so a completion queued in the same tick has the last
    // word on the status line.
    {
        std::string status;
        bool dirty = false;
        {
            std::lock_guard<std::mutex> lock(progressMutex_);
            dirty = pendingStatusDirty_;
            if (dirty) {
                status.swap(pendingStatus_);
                pendingStatusDirty_ = false;
            }
        }
        if (dirty) SetStatus(status);
    }

    std::vector<std::function<void()>> actions;
    {
        std::lock_guard<std::mutex> lock(uiQueueMutex_);
        actions.swap(uiQueue_);
    }
    for (auto& action : actions) action();
}

} // namespace UltraCleaner
