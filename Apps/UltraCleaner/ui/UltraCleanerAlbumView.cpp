// Apps/UltraCleaner/ui/UltraCleanerAlbumView.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerAlbumView.h"

#include "UltraCanvasImageElement.h"

#include <filesystem>

using namespace UltraCanvas;

namespace UltraCleaner {
namespace {

constexpr float kThumbnailSize = 132.0f;
// Reviewing is done by eye, and nobody scrolls past a few dozen groups. The
// rest are still counted and reported — a silently truncated list reads as
// "that was everything", which it is not.
constexpr size_t kMaxGroupsShown = 40;

std::string FileNameOf(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

} // namespace

std::shared_ptr<UltraCanvasContainer> AlbumView::Build(float width, float height) {
    root_ = CreateContainer("ucAlbumRoot", 0, 0, width, height);
    root_->layout.SetFlexColumn()
                 .SetFlexGap(8)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->SetPadding(10);

    // ---- folder row ----
    auto folderRow = CreateContainer("ucAlbumFolderRow", 0, 0, 0, 32);
    folderRow->layout.SetFlexRow()
                     .SetFlexGap(8)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    auto chooseButton = CreateButton("ucAlbumChoose", 0, 0, 152, 28, "Choose folder…");
    chooseButton->onClick = [this]() { if (onChooseFolder) onChooseFolder(); };
    folderRow->AddChild(chooseButton);

    folderLabel_ = CreateLabel("ucAlbumFolder", 0, 0, 420, 24,
                               "No folder chosen yet.");
    folderRow->AddChild(folderLabel_);
    folderLabel_->layoutItem.SetFlexGrow(1);
    root_->AddChild(folderRow);
    folderRow->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // ---- controls row ----
    auto controls = CreateContainer("ucAlbumControls", 0, 0, 0, 34);
    controls->layout.SetFlexRow()
                    .SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);

    scanButton_ = CreateButton("ucAlbumScan", 0, 0, 110, 28, "Scan");
    scanButton_->onClick = [this]() { if (onScan) onScan(); };
    controls->AddChild(scanButton_);

    stopButton_ = CreateButton("ucAlbumStop", 0, 0, 80, 28, "Stop");
    stopButton_->onClick = [this]() { if (onStop) onStop(); };
    stopButton_->SetDisabled(true);
    controls->AddChild(stopButton_);

    controls->AddChild(CreateLabel("ucAlbumLevelLabel", 0, 0, 46, 24, "Find:"));

    levelDropdown_ = CreateDropdown("ucAlbumLevel", 0, 0, 190, 28);
    levelDropdown_->AddItem(SimilarityLevelName(SimilarityLevel::Duplicates));
    levelDropdown_->AddItem(SimilarityLevelName(SimilarityLevel::Moments));
    levelDropdown_->AddItem(SimilarityLevelName(SimilarityLevel::Scenes));
    levelDropdown_->SetSelectedIndex(1, /*runNotifications=*/false);
    levelDropdown_->onSelectionChanged = [this](int index, const DropdownItem&) {
        level_ = index == 0   ? SimilarityLevel::Duplicates
                 : index == 2 ? SimilarityLevel::Scenes
                              : SimilarityLevel::Moments;
        if (levelHint_) levelHint_->SetText(SimilarityLevelDescription(level_));
        if (onLevelChanged) onLevelChanged(level_);
    };
    controls->AddChild(levelDropdown_);

    cleanButton_ = CreateButton("ucAlbumClean", 0, 0, 150, 28, "Clean ticked…");
    cleanButton_->onClick = [this]() { if (onClean) onClean(); };
    controls->AddChild(cleanButton_);
    controls->AddStretchSpacer(1);
    root_->AddChild(controls);
    controls->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    levelHint_ = CreateLabel("ucAlbumLevelHint", 0, 0, 600, 32,
                             SimilarityLevelDescription(level_));
    levelHint_->SetWrap(TextWrap::WrapWord);
    levelHint_->SetFontSize(11);
    root_->AddChild(levelHint_);
    levelHint_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // ---- the group list ----
    groupList_ = CreateContainer("ucAlbumGroups", 0, 0, width - 24, height - 190);
    groupList_->layout.SetFlexColumn()
                      .SetFlexGap(10)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->AddChild(groupList_);
    groupList_->layoutItem.SetFlexGrow(1);
    groupList_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    statusLabel_ = CreateLabel("ucAlbumStatus", 0, 0, 600, 24,
                               "Choose a folder of photos, then press “Scan”.");
    statusLabel_->SetWrap(TextWrap::WrapWord);
    root_->AddChild(statusLabel_);
    statusLabel_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    return root_;
}

void AlbumView::SetFolder(const std::string& folder) {
    folder_ = folder;
    if (folderLabel_) {
        folderLabel_->SetText(folder.empty() ? "No folder chosen yet." : folder);
    }
}

void AlbumView::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

void AlbumView::SetBusy(bool busy) {
    if (scanButton_)    scanButton_->SetDisabled(busy);
    if (cleanButton_)   cleanButton_->SetDisabled(busy);
    if (stopButton_)    stopButton_->SetDisabled(!busy);
    if (levelDropdown_) levelDropdown_->SetDisabled(busy);
}

void AlbumView::SetReport(const AlbumScanReport& report) {
    RebuildGroups(report);
}

void AlbumView::RebuildGroups(const AlbumScanReport& report) {
    if (!groupList_) return;
    groupList_->ClearChildren();
    groupChecks_.clear();

    if (report.groups.empty()) {
        auto empty = CreateLabel("ucAlbumEmpty", 0, 0, 600, 40,
            "Nothing to review — no duplicates or near-duplicates found here.");
        empty->SetWrap(TextWrap::WrapWord);
        groupList_->AddChild(empty);
        return;
    }

    const size_t shown = std::min(report.groups.size(), kMaxGroupsShown);
    for (size_t g = 0; g < shown; ++g) {
        const PictureGroup& group = report.groups[g];
        const std::string id = "ucAlbumGroup" + std::to_string(g);

        auto block = CreateContainer(id, 0, 0, 0, kThumbnailSize + 72);
        block->layout.SetFlexColumn()
                     .SetFlexGap(4)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

        // Header: one tick for the whole group, plus what it would recover.
        auto header = CreateContainer(id + "Head", 0, 0, 0, 26);
        header->layout.SetFlexRow()
                      .SetFlexGap(8)
                      .SetFlexAlignItems(CSSLayout::AlignItems::Center);

        auto tick = UltraCanvasCheckbox::CreateCheckbox(
            id + "Tick", 0, 0, 460, 24,
            std::string(GroupReasonName(group.reason)) + " — " +
                std::to_string(group.members.size()) + " pictures, " +
                FormatByteSize(group.RecoverableBytes()) + " recoverable",
            /*checked=*/false);
        tick->onStateChanged = [this](CheckedState, CheckedState) {
            if (onSelectionChanged) onSelectionChanged();
        };
        header->AddChild(tick);
        tick->layoutItem.SetFlexGrow(1);
        groupChecks_.push_back(tick);
        block->AddChild(header);
        header->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        header->layoutItem.SetFlexShrink(0);

        // The pictures themselves, keeper first so the eye lands on it.
        auto strip = CreateContainer(id + "Strip", 0, 0, 0, kThumbnailSize + 26);
        strip->layout.SetFlexRow()
                     .SetFlexGap(8)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Start);

        for (size_t i = 0; i < group.members.size(); ++i) {
            const ImageDescriptor& picture = group.members[i];
            const bool isKeeper = (i == group.keeperIndex);
            const std::string cellId = id + "Pic" + std::to_string(i);

            auto cell = CreateContainer(cellId, 0, 0, kThumbnailSize,
                                        kThumbnailSize + 24);
            cell->layout.SetFlexColumn()
                        .SetFlexGap(2)
                        .SetFlexAlignItems(CSSLayout::AlignItems::Center);

            auto thumbnail = CreateImageElement(cellId + "Img", 0, 0,
                                                kThumbnailSize, kThumbnailSize);
            thumbnail->SetFitMode(ImageFitMode::Contain);
            thumbnail->LoadFromFile(picture.path);
            thumbnail->SetTooltip(picture.path);
            cell->AddChild(thumbnail);
            thumbnail->layoutItem.SetFlexShrink(0);

            auto caption = CreateLabel(
                cellId + "Cap", 0, 0, kThumbnailSize, 20,
                (isKeeper ? "KEEP  " : "") + FileNameOf(picture.path));
            caption->SetFontSize(10);
            if (!isKeeper) caption->SetTextColor(Color(140, 140, 140));
            cell->AddChild(caption);

            strip->AddChild(cell);
            cell->layoutItem.SetFlexShrink(0);
        }
        block->AddChild(strip);
        strip->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        strip->layoutItem.SetFlexShrink(0);

        groupList_->AddChild(block);
        block->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        // Without this the column divides its height between the groups and
        // every thumbnail collapses to a letterbox strip; the list is meant
        // to overflow and scroll instead.
        block->layoutItem.SetFlexShrink(0);
    }

    if (report.groups.size() > shown) {
        auto more = CreateLabel(
            "ucAlbumMore", 0, 0, 600, 24,
            std::to_string(report.groups.size() - shown) +
                " further groups are not shown here — clean these first, or "
                "scan a smaller folder.");
        more->SetWrap(TextWrap::WrapWord);
        groupList_->AddChild(more);
    }
}

std::vector<size_t> AlbumView::SelectedGroups() const {
    std::vector<size_t> selected;
    for (size_t i = 0; i < groupChecks_.size(); ++i) {
        if (groupChecks_[i] && groupChecks_[i]->IsChecked()) selected.push_back(i);
    }
    return selected;
}

} // namespace UltraCleaner
