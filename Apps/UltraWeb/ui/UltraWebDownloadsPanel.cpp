// Apps/UltraWeb/ui/UltraWebDownloadsPanel.cpp
// The downloads panel (UltraWebDownloadsPanel.h).
// Version: 0.2.0 - long names cut with "…" (the whole name as the tooltip)
// Version: 0.1.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraWebDownloadsPanel.h"

using namespace UltraCanvas;

namespace UltraWeb {

namespace {

constexpr float kPanelHeight = 170;
constexpr float kRowHeight = 30;
constexpr float kButtonHeight = 26;
// A longer name is cut with "…" in the middle, so the state keeps its room.
constexpr size_t kNameChars = 36;

std::shared_ptr<UltraCanvasContainer> MakeRowBox(const std::string& id, float height) {
    auto box = CreateContainer(id, 0, 0, 0, 0);
    box->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
    box->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(height));
    box->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    box->layoutItem.SetFlexGrow(0);
    box->layoutItem.SetFlexShrink(0);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    box->SetContainerStyle(plain);
    return box;
}

std::shared_ptr<UltraCanvasButton> MakeButton(const std::string& id, const std::string& text, float width) {
    auto button = CreateButton(id, 0, 0, width, kButtonHeight, text);
    button->layoutItem.SetFlexShrink(0);
    return button;
}

} // namespace

UltraWebDownloadsPanel::UltraWebDownloadsPanel(DownloadList& list, std::function<void(std::function<void()>)> post)
    : list_(list), post_(std::move(post)) {
    root_ = CreateContainer("uwDownloads", 0, 0, 0, 0);
    root_->layout.SetFlexColumn().SetFlexGap(4).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(kPanelHeight));
    root_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    root_->layoutItem.SetFlexGrow(0);
    root_->layoutItem.SetFlexShrink(0);
    root_->SetPadding(4);
    root_->SetBorders(1, Color(200, 200, 200));
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    root_->SetContainerStyle(plain);

    auto header = MakeRowBox("uwDownloadsHeader", kRowHeight);
    auto title = CreateLabel("uwDownloadsTitle", "Downloads");
    title->SetFontSize(13);
    title->layoutItem.SetFlexGrow(1);
    title->layoutItem.SetFlexShrink(1);
    header->AddChild(title);
    clearButton_ = MakeButton("uwDownloadsClear", "Clear finished", 124);
    clearButton_->SetOnClick([this]() { list_.ClearFinished(); });
    header->AddChild(clearButton_);
    closeButton_ = MakeButton("uwDownloadsClose", "Close", 64);
    closeButton_->SetOnClick([this]() { if (onClose) onClose(); });
    header->AddChild(closeButton_);
    root_->AddChild(header);

    // The rows scroll; the header stays.
    rows_ = CreateScrollableContainer("uwDownloadsRows", 0, 0, 0, 0, true, false);
    ContainerStyle scrolling = rows_->GetContainerStyle();
    scrolling.forceShowVerticalScrollbar = false;
    scrolling.autoShowHorizontalScrollbar = false;
    rows_->SetContainerStyle(scrolling);
    rows_->layout.SetFlexColumn().SetFlexGap(2).SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    rows_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    rows_->layoutItem.SetFlexGrow(1);
    rows_->layoutItem.SetFlexShrink(1);
    rows_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    root_->AddChild(rows_);

    empty_ = CreateLabel("uwDownloadsEmpty", "Nothing downloaded yet.");
    empty_->SetTextColor(Color(110, 110, 110));
    Rebuild();
}

const UltraWebDownloadsPanel::Row* UltraWebDownloadsPanel::RowFor(int id) const {
    auto it = rowById_.find(id);
    return it == rowById_.end() ? nullptr : &it->second;
}

void UltraWebDownloadsPanel::Update(int id, bool progressOnly) {
    if (progressOnly) {
        auto it = rowById_.find(id);
        const DownloadEntry* entry = list_.Find(id);
        if (it != rowById_.end() && entry) {
            it->second.state->SetText(DownloadList::Describe(*entry));
            it->second.state->RequestRedraw();
            return;
        }
    }
    ScheduleRebuild();
}

void UltraWebDownloadsPanel::ScheduleRebuild() {
    if (rebuildPending_) return;
    rebuildPending_ = true;
    std::weak_ptr<bool> alive = alive_;
    post_([this, alive]() {
        if (alive.expired()) return;
        rebuildPending_ = false;
        Rebuild();
    });
}

void UltraWebDownloadsPanel::Rebuild() {
    rows_->ClearChildren();
    rowById_.clear();
    const auto& entries = list_.Entries();
    if (entries.empty()) rows_->AddChild(empty_);
    bool anyFinished = false;
    for (const DownloadEntry& entry : entries) {
        Row row = MakeRow(entry);
        rows_->AddChild(row.box);
        rowById_[entry.id] = row;
        if (entry.state != DownloadEntry::State::Running) anyFinished = true;
    }
    clearButton_->SetDisabled(!anyFinished);
    rows_->InvalidateLayout();
    root_->InvalidateLayout();
    root_->RequestRedraw();
}

UltraWebDownloadsPanel::Row UltraWebDownloadsPanel::MakeRow(const DownloadEntry& entry) {
    const std::string id = "uwDownload" + std::to_string(entry.id);
    const int downloadId = entry.id;
    Row row;
    row.box = MakeRowBox(id, kRowHeight);

    const std::string shown = DownloadList::Ellipsize(entry.name, kNameChars);
    row.name = CreateLabel(id + "Name", shown);
    row.name->SetFontSize(13);
    if (shown != entry.name) row.name->SetTooltip(entry.name);
    row.name->layoutItem.SetFlexShrink(1);
    row.box->AddChild(row.name);

    row.state = CreateLabel(id + "State", DownloadList::Describe(entry));
    if (entry.state == DownloadEntry::State::Failed) row.state->SetTextColor(Color(176, 32, 32));
    else row.state->SetTextColor(Color(90, 90, 90));
    row.state->layoutItem.SetFlexGrow(1);
    row.state->layoutItem.SetFlexShrink(1);
    row.box->AddChild(row.state);

    switch (entry.state) {
        case DownloadEntry::State::Running:
            row.cancel = MakeButton(id + "Cancel", "Cancel", 70);
            row.cancel->SetOnClick([this, downloadId]() { if (onCancel) onCancel(downloadId); });
            row.box->AddChild(row.cancel);
            break;
        case DownloadEntry::State::Saved:
            row.open = MakeButton(id + "Open", "Open", 64);
            row.open->SetOnClick([this, downloadId]() {
                if (const DownloadEntry* e = list_.Find(downloadId)) if (onOpen) onOpen(*e);
            });
            row.box->AddChild(row.open);
            row.showFolder = MakeButton(id + "Folder", "Show folder", 112);
            row.showFolder->SetOnClick([this, downloadId]() {
                if (const DownloadEntry* e = list_.Find(downloadId)) if (onShowFolder) onShowFolder(*e);
            });
            row.box->AddChild(row.showFolder);
            break;
        case DownloadEntry::State::Failed:
        case DownloadEntry::State::Cancelled:
            break;
    }
    if (entry.state != DownloadEntry::State::Running) {
        row.remove = MakeButton(id + "Remove", "Remove", 76);
        row.remove->SetTooltip("Remove from the list (the file stays)");
        row.remove->SetOnClick([this, downloadId]() { list_.Remove(downloadId); });
        row.box->AddChild(row.remove);
    }
    return row;
}

} // namespace UltraWeb
