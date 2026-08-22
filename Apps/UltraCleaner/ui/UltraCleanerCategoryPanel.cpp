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
    root_->layout.SetFlexColumn()
                 .SetFlexGap(2)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
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
    root_->AddChild(label);
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

        root_->AddChild(line);
        line->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

        // What the category is, under its row. Two wrapped lines at this
        // width, so the box is tall enough not to run into the next row.
        row.detail = CreateLabel(id + "Detail", 0, 0, 330, 46,
                                 std::to_string(summary.itemCount) +
                                 (summary.itemCount == 1 ? " item · " : " items · ") +
                                 CategoryDescription(summary.category));
        row.detail->SetWrap(TextWrap::WrapWord);
        row.detail->SetFontSize(11);
        root_->AddChild(row.detail);
        row.detail->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);

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
