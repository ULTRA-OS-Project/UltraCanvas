// Apps/UltraCleaner/ui/UltraCleanerCategoryPanel.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraCleanerCategoryPanel.h"

#include <string>

using namespace UltraCanvas;

namespace UltraCleaner {
namespace {

// How many of a category's items are ticked — the checkbox needs all three
// answers, not just "any".
struct Tally {
    size_t total = 0;
    size_t selected = 0;
};

Tally TallyFor(const ScanReport& report, CleanCategory category) {
    Tally tally;
    for (const auto& item : report.items) {
        if (item.category != category) continue;
        ++tally.total;
        if (item.selected) ++tally.selected;
    }
    return tally;
}

CheckedState StateFor(const Tally& tally) {
    if (tally.total == 0 || tally.selected == 0) return CheckedState::Unchecked;
    if (tally.selected == tally.total) return CheckedState::Checked;
    return CheckedState::Indeterminate;
}

} // namespace

std::shared_ptr<UltraCanvasContainer> CategoryPanel::Build(float x, float y,
                                                           float width,
                                                           float height) {
    root_ = CreateContainer("ucCategories", x, y, width, height);
    // Block, not flex. A wrapping label can only report the height its text
    // needs when it is measured against a definite width, and the flex path
    // short-circuits on the intrinsic size — which for a label is a single
    // unbounded line. Block layout takes the definite-width measure, so the
    // descriptions below get the height they actually occupy instead of one
    // line's worth, and stop being overdrawn by the next category's row.
    root_->layout.SetDisplay(CSSLayout::DisplayType::Block);
    root_->SetPadding(4);
    ShowMessage("Press “Scan” to look for files that can go.");
    return root_;
}

void CategoryPanel::ShowMessage(const std::string& message) {
    if (!root_) return;
    root_->ClearChildren();
    rows_.clear();

    auto label = CreateLabel("ucCatMessage", 0, 0, 320, 60, message);
    label->SetWrap(TextWrap::WrapWord);
    label->SetElementSize(CSSLayout::Dimension::Pct(100),
                          CSSLayout::Dimension::Auto());
    root_->AddChild(label);
    label->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
}

void CategoryPanel::SetReport(const ScanReport& report) {
    if (!root_) return;
    root_->ClearChildren();
    rows_.clear();

    if (report.categories.empty()) {
        ShowMessage("Nothing to clean — this system is already tidy.");
        return;
    }

    for (const auto& summary : report.categories) {
        // The category key is already unique, so it is the element id too.
        const std::string id = "ucCat" + std::string(CategoryKey(summary.category));
        const Tally tally = TallyFor(report, summary.category);

        auto line = CreateContainer(id + "Row", 0, 0, 0, 26);
        line->layout.SetFlexRow()
                    .SetFlexGap(8)
                    .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        // A 26-pixel strip is not a scrolling region: without this a row
        // whose label and badge do not fit grows a scrollbar inside itself.
        ContainerStyle plainRow;
        plainRow.autoShowScrollbars = false;
        line->SetContainerStyle(plainRow);

        Row row;
        row.category = summary.category;
        row.safeByDefault = summary.safeByDefault;

        row.checkbox = UltraCanvasCheckbox::CreateCheckbox(
            id + "Check", 0, 0, 210, 24, CategoryTitle(summary.category),
            /*checked=*/false);
        // Indeterminate is a state the panel *shows* (some of the category's
        // paths were dropped in the detail list) but never one a click walks
        // into: leaving allowIndeterminate off keeps the click a plain
        // on/off toggle, while SetCheckState below still paints the third
        // state. A click on a partly-ticked category clears it; the next
        // click ticks the whole thing.
        row.checkbox->SetCheckState(StateFor(tally));
        const CleanCategory category = summary.category;
        row.checkbox->onStateChanged =
            [this, category](CheckedState, CheckedState newState) {
                if (suppressCallbacks_ || !onCategoryToggled) return;
                onCategoryToggled(category, newState != CheckedState::Unchecked);
            };
        line->AddChild(row.checkbox);
        row.checkbox->layoutItem.SetFlexGrow(1);

        row.sizeBadge = CreateBadge(id + "Size", 0, 0,
                                    FormatByteSize(summary.totalBytes),
                                    summary.safeByDefault ? BadgeVariant::Info
                                                          : BadgeVariant::Warning);
        line->AddChild(row.sizeBadge);
        // The size is the number the row exists to show; when the row is
        // tight it is the title that gives way, not the badge.
        row.sizeBadge->layoutItem.SetFlexShrink(0);

        root_->AddChild(line);
        line->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        line->layoutItem.SetFlexShrink(0);

        // What the category is, under its row. The height must come from the
        // wrapped text, never from a guess: a fixed two-line box is only two
        // lines at one particular width and font, and the moment the text
        // needs a third line it spills out and the next category's row is
        // drawn on top of it.
        row.detail = CreateLabel(id + "Detail", 0, 0, 330, 46,
                                 std::to_string(summary.itemCount) +
                                 (summary.itemCount == 1 ? " item · " : " items · ") +
                                 CategoryDescription(summary.category));
        row.detail->SetWrap(TextWrap::WrapWord);
        row.detail->SetFontSize(11);
        // A definite width and an automatic height: the label can only
        // report the height its wrapped text needs if it is first told what
        // width to wrap against. Auto width would measure one long line and
        // give back a one-line height, which is exactly the overlap this
        // replaces.
        row.detail->SetElementSize(CSSLayout::Dimension::Auto(),
                                   CSSLayout::Dimension::Auto());
        root_->AddChild(row.detail);
        row.detail->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        row.detail->layoutItem.SetFlexShrink(0);

        rows_.push_back(std::move(row));
    }
}

void CategoryPanel::RefreshFromReport(const ScanReport& report) {
    suppressCallbacks_ = true;
    for (auto& row : rows_) {
        const Tally tally = TallyFor(report, row.category);
        if (row.checkbox) row.checkbox->SetCheckState(StateFor(tally));
    }
    suppressCallbacks_ = false;
}

} // namespace UltraCleaner
