// Apps/UltraCleaner/ui/UltraCleanerRegistryView.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerRegistryView.h"

#include <cstdlib>
#include <cstring>

using namespace UltraCanvas;

namespace UltraCleaner {
namespace {

// Rows are addressed by node id: "reg<index into report_.findings>".
constexpr const char* kFindingPrefix = "reg";
constexpr const char* kRootId = "ucRegRoot";

std::string KindNodeId(RegistryIssueKind kind) {
    return "ucRegKind" + std::to_string(static_cast<int>(kind));
}

} // namespace

std::shared_ptr<UltraCanvasContainer> RegistryView::Build(float width, float height) {
    root_ = CreateContainer("ucRegPage", 0, 0, width, height);
    root_->layout.SetFlexColumn()
                 .SetFlexGap(8)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root_->SetPadding(12);
    ContainerStyle plain;
    plain.autoShowScrollbars = false;
    root_->SetContainerStyle(plain);

    auto intro = CreateLabel(
        "ucRegIntro", 0, 0, width - 30, 40,
        "Finds registry entries that point at programs, files and folders that "
        "are gone — leftovers of software deleted by hand or uninstalled badly. "
        "It only reports an entry whose target is provably missing from a fixed "
        "disk, never touches Windows' own folder, and backs up everything to a "
        ".reg file before removing it. It will not make Windows faster: nothing "
        "honest can, by editing the registry.");
    intro->SetWrap(TextWrap::WrapWord);
    intro->SetFontSize(11);
    intro->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Auto());
    root_->AddChild(intro);
    intro->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    intro->layoutItem.SetFlexShrink(0);

    // ---- toolbar ----
    auto bar = CreateContainer("ucRegToolbar", 0, 0, 0, 36);
    bar->layout.SetFlexRow()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    bar->SetElementSize(CSSLayout::Dimension::Auto(), CSSLayout::Dimension::Px(36));
    bar->SetContainerStyle(plain);

    scanButton_ = CreateButton("ucRegScan", 0, 0, 110, 28, "Scan");
    scanButton_->onClick = [this]() { if (onScan) onScan(); };
    bar->AddChild(scanButton_);
    scanButton_->layoutItem.SetFlexShrink(0);

    stopButton_ = CreateButton("ucRegStop", 0, 0, 80, 28, "Stop");
    stopButton_->onClick = [this]() { if (onStop) onStop(); };
    stopButton_->SetDisabled(true);
    bar->AddChild(stopButton_);
    stopButton_->layoutItem.SetFlexShrink(0);

    cleanButton_ = CreateButton("ucRegClean", 0, 0, 170, 28, "Back up and clean…");
    cleanButton_->onClick = [this]() { if (onClean) onClean(); };
    bar->AddChild(cleanButton_);
    cleanButton_->layoutItem.SetFlexShrink(0);

    restoreButton_ = CreateButton("ucRegRestore", 0, 0, 160, 28, "Restore a backup…");
    restoreButton_->onClick = [this]() { if (onRestore) onRestore(); };
    bar->AddChild(restoreButton_);
    restoreButton_->layoutItem.SetFlexShrink(0);

    bar->AddStretchSpacer(1);

    auto safeButton = CreateButton("ucRegSafe", 0, 0, 130, 28, "Tick safe ones");
    safeButton->onClick = [this]() { SelectDefaults(); };
    bar->AddChild(safeButton);
    safeButton->layoutItem.SetFlexShrink(0);

    auto noneButton = CreateButton("ucRegNone", 0, 0, 100, 28, "Tick none");
    noneButton->onClick = [this]() { SelectNone(); };
    bar->AddChild(noneButton);
    noneButton->layoutItem.SetFlexShrink(0);

    root_->AddChild(bar);
    bar->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    bar->layoutItem.SetFlexShrink(0);

    // ---- the list ----
    tree_ = std::make_shared<UltraCanvasColumnsTreeView>("ucRegTree", 0, 0);
    tree_->SetDisplayMode(TreeDisplayMode::Columns);
    tree_->SetSelectionMode(TreeSelectionMode::Single);
    tree_->SetShowColumnHeader(true);
    tree_->SetRowHeight(22);
    // The entry's name and the missing file are the two things read; the
    // key is long and sits last, where it can take whatever is left.
    tree_->SetColumns({
        { "entry",    "Entry",          240, 160, 0.0f, TextAlignment::Left,
          Color(40, 40, 40), Colors::Transparent, 0, /*isTreeColumn=*/true },
        { "clean",    "Clean",          56,  48,  0.0f, TextAlignment::Center,
          Color(40, 40, 40), Colors::Transparent, 0, false },
        { "missing",  "Missing file",   260, 140, 0.0f, TextAlignment::Left,
          Color(40, 40, 40), Colors::Transparent, 0, false },
        { "location", "Registry entry", 0,   200, 1.0f, TextAlignment::Left,
          Color(90, 90, 90), Colors::Transparent, 0, false },
    });
    TreeColumnStyle columnStyle = tree_->GetColumnStyle();
    columnStyle.columnGap             = 6;
    columnStyle.showColumnSeparators  = true;
    columnStyle.columnSeparatorColor  = Color(224, 224, 228);
    columnStyle.headerBackground      = Color(238, 238, 242);
    columnStyle.headerTextColor       = Color(52, 52, 60);
    columnStyle.headerBorderColor     = Color(176, 176, 184);
    columnStyle.headerHeight          = 24;
    columnStyle.groupHeaderBackground = Color(70, 74, 84);
    tree_->SetColumnStyle(columnStyle);
    tree_->onNodeDoubleClicked = [this](TreeNode* node) {
        if (!node) return;
        const std::string& id = node->data.nodeId;
        if (id.rfind(kFindingPrefix, 0) != 0) return;   // a group header
        Toggle(static_cast<size_t>(
            std::strtoull(id.c_str() + std::strlen(kFindingPrefix), nullptr, 10)));
    };
    root_->AddChild(tree_);
    tree_->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    tree_->layoutItem.SetFlexGrow(1);
    tree_->layoutItem.SetFlexShrink(1);
    tree_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    summaryLabel_ = CreateLabel("ucRegSummary", 0, 0, 0, 0, "Not scanned yet.");
    summaryLabel_->SetElementSize(CSSLayout::Dimension::Pct(100),
                                  CSSLayout::Dimension::Auto());
    root_->AddChild(summaryLabel_);
    summaryLabel_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    summaryLabel_->layoutItem.SetFlexShrink(0);

    statusLabel_ = CreateLabel("ucRegStatus", 0, 0, 0, 0,
                               "Double-click a row to keep or drop it.");
    statusLabel_->SetWrap(TextWrap::WrapWord);
    statusLabel_->SetFontSize(11);
    statusLabel_->SetElementSize(CSSLayout::Dimension::Pct(100),
                                 CSSLayout::Dimension::Auto());
    root_->AddChild(statusLabel_);
    statusLabel_->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    statusLabel_->layoutItem.SetFlexShrink(0);

    RefreshList();
    return root_;
}

void RegistryView::SetReport(RegistryScanReport report) {
    report_ = std::move(report);
    scanned_ = true;
    RefreshList();
    RefreshSummary();
}

void RegistryView::SetStatus(const std::string& text) {
    if (statusLabel_) statusLabel_->SetText(text);
}

void RegistryView::SetBusy(bool busy) {
    if (scanButton_)    scanButton_->SetDisabled(busy);
    if (cleanButton_)   cleanButton_->SetDisabled(busy);
    if (restoreButton_) restoreButton_->SetDisabled(busy);
    if (stopButton_)    stopButton_->SetDisabled(!busy);
}

void RegistryView::RefreshList() {
    if (!tree_) return;
    TreeNodeData rootData(kRootId,
        scanned_ ? std::to_string(report_.findings.size()) +
                       (report_.findings.size() == 1 ? " entry" : " entries") +
                       " point at something that is gone"
                 : std::string("Not scanned yet"));
    rootData.isGroupHeader = true;
    tree_->SetRootNode(rootData);

    for (const auto& kind : report_.kinds) {
        TreeNodeData group(KindNodeId(kind.kind),
                           std::string(RegistryIssueTitle(kind.kind)) + "  (" +
                               std::to_string(kind.count) + ")");
        group.tooltip = RegistryIssueDescription(kind.kind);
        group.textColor = Color(52, 52, 60);
        tree_->AddNode(kRootId, group);
        for (size_t index = 0; index < report_.findings.size(); ++index) {
            const RegistryFinding& finding = report_.findings[index];
            if (finding.kind != kind.kind) continue;
            TreeNodeData node(kFindingPrefix + std::to_string(index), finding.label);
            node.tooltip = finding.Location();
            node.SetCell("clean", finding.selected ? "yes" : "—");
            node.SetCell("missing", finding.target);
            node.SetCell("location", finding.Location());
            node.textColor = finding.selected ? Color(40, 40, 40) : Color(150, 150, 150);
            tree_->AddNode(KindNodeId(kind.kind), node);
        }
    }
    tree_->ExpandAll();
}

void RegistryView::RefreshRow(size_t index) {
    if (!tree_ || index >= report_.findings.size()) return;
    TreeNode* node = tree_->FindNode(kFindingPrefix + std::to_string(index));
    if (!node) return;
    const RegistryFinding& finding = report_.findings[index];
    node->data.SetCell("clean", finding.selected ? "yes" : "—");
    node->data.textColor = finding.selected ? Color(40, 40, 40) : Color(150, 150, 150);
    tree_->RequestRedraw();
}

void RegistryView::RefreshSummary() {
    if (!summaryLabel_) return;
    if (!scanned_) {
        summaryLabel_->SetText("Not scanned yet.");
        return;
    }
    if (report_.findings.empty()) {
        summaryLabel_->SetText(
            std::to_string(report_.entriesExamined) +
            " entries examined — none point at anything missing. Nothing to do.");
        return;
    }
    std::string text = "Ticked: " + std::to_string(report_.SelectedCount()) + " of " +
                       std::to_string(report_.findings.size()) + " entries · " +
                       std::to_string(report_.entriesExamined) + " examined";
    if (report_.entriesUnjudged > 0) {
        text += " · " + std::to_string(report_.entriesUnjudged) +
                " could not be judged and are left alone";
    }
    summaryLabel_->SetText(text);
}

void RegistryView::Toggle(size_t index) {
    if (index >= report_.findings.size()) return;
    report_.findings[index].selected = !report_.findings[index].selected;
    RefreshRow(index);
    RefreshSummary();
}

void RegistryView::SelectDefaults() {
    for (auto& finding : report_.findings) {
        finding.selected = RegistryIssueSafeByDefault(finding.kind);
    }
    RefreshList();
    RefreshSummary();
}

void RegistryView::SelectNone() {
    for (auto& finding : report_.findings) finding.selected = false;
    RefreshList();
    RefreshSummary();
}

} // namespace UltraCleaner
