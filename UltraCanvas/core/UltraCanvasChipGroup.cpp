// core/UltraCanvasChipGroup.cpp
// A row of chips with one selection rule (UltraCanvasChipGroup.h).
// Version: 1.0.0
// Author: UltraCanvas Framework
#include "UltraCanvasChipGroup.h"
#include <algorithm>

namespace UltraCanvas {

    UltraCanvasChipGroup::UltraCanvasChipGroup(const std::string& identifier, float x, float y, float w, float h)
            : UltraCanvasContainer(identifier, x, y, w, h) {
        layout.SetFlexRow()
              .SetFlexWrap(CSSLayout::FlexWrap::Wrap)
              .SetFlexGap(6.0f)
              .SetFlexAlignItems(CSSLayout::AlignItems::Center);
        if (h <= 0) size.height = CSSLayout::Dimension::Auto();
        SetBackgroundColor(Colors::Transparent);
    }

    // ----- chips -----
    int UltraCanvasChipGroup::AddChip(const std::string& label, const std::string& iconPath, const std::string& chipId) {
        const std::string id = chipId.empty() ? GetIdentifier() + ".chip" + std::to_string(nextChipNumber) : chipId;
        ++nextChipNumber;
        auto chip = CreateChip(id, 0, 0, label, false);
        if (!iconPath.empty()) chip->SetIcon(iconPath);
        return AddChip(chip);
    }

    int UltraCanvasChipGroup::AddChip(std::shared_ptr<UltraCanvasChip> chip) {
        if (!chip) return -1;
        chip->SetSelectable(true);
        chip->SetAutoWidth(true);
        chip->layoutItem.SetFlexShrink(0);
        if (hasStyle) chip->SetStyle(chipStyle);
        chip->SetVariant(chipVariant);
        Wire(chip);
        chips.push_back(chip);
        AddChild(chip);
        InvalidateLayout();
        RequestRedraw();
        return static_cast<int>(chips.size()) - 1;
    }

    void UltraCanvasChipGroup::Wire(const std::shared_ptr<UltraCanvasChip>& chip) {
        UltraCanvasChip* raw = chip.get();
        chip->onSelectedChanged = [this, raw](bool selected) { ChipSelectionChanged(raw, selected); };
    }

    void UltraCanvasChipGroup::RemoveChip(int index) {
        if (index < 0 || index >= static_cast<int>(chips.size())) return;
        const bool wasSelected = chips[index]->IsSelected();
        chips[index]->onSelectedChanged = nullptr;
        RemoveChild(chips[index]);
        chips.erase(chips.begin() + index);
        InvalidateLayout();
        RequestRedraw();
        if (wasSelected) Notify();
    }

    void UltraCanvasChipGroup::ClearChips() {
        const bool had = !GetSelectedIndices().empty();
        for (auto& c : chips) { c->onSelectedChanged = nullptr; RemoveChild(c); }
        chips.clear();
        InvalidateLayout();
        RequestRedraw();
        if (had) Notify();
    }

    std::shared_ptr<UltraCanvasChip> UltraCanvasChipGroup::GetChip(int index) const {
        if (index < 0 || index >= static_cast<int>(chips.size())) return nullptr;
        return chips[index];
    }

    int UltraCanvasChipGroup::IndexOf(const std::string& label) const {
        for (size_t i = 0; i < chips.size(); ++i) if (chips[i]->GetLabel() == label) return static_cast<int>(i);
        return -1;
    }

    int UltraCanvasChipGroup::IndexOfChip(const UltraCanvasChip* chip) const {
        for (size_t i = 0; i < chips.size(); ++i) if (chips[i].get() == chip) return static_cast<int>(i);
        return -1;
    }

    // ----- selection -----
    void UltraCanvasChipGroup::SetSelectionMode(ChipGroupSelectionMode mode) {
        if (selectionMode == mode) return;
        selectionMode = mode;
        if (mode == ChipGroupSelectionMode::Single) {
            // Keep the first selected chip, drop the rest.
            const std::vector<int> sel = GetSelectedIndices();
            if (sel.size() > 1) SetSelectedIndex(sel.front());
        }
    }

    void UltraCanvasChipGroup::SetSelectedIndex(int index) {
        updating = true;
        bool changed = false;
        for (size_t i = 0; i < chips.size(); ++i) {
            const bool want = static_cast<int>(i) == index;
            if (chips[i]->IsSelected() != want) { chips[i]->SetSelected(want); changed = true; }
        }
        updating = false;
        if (changed) Notify();
    }

    int UltraCanvasChipGroup::GetSelectedIndex() const {
        for (size_t i = 0; i < chips.size(); ++i) if (chips[i]->IsSelected()) return static_cast<int>(i);
        return -1;
    }

    void UltraCanvasChipGroup::SetSelectedIndices(const std::vector<int>& indices) {
        if (selectionMode == ChipGroupSelectionMode::Single) {
            SetSelectedIndex(indices.empty() ? -1 : indices.front());
            return;
        }
        updating = true;
        bool changed = false;
        for (size_t i = 0; i < chips.size(); ++i) {
            const bool want = std::find(indices.begin(), indices.end(), static_cast<int>(i)) != indices.end();
            if (chips[i]->IsSelected() != want) { chips[i]->SetSelected(want); changed = true; }
        }
        updating = false;
        if (changed) Notify();
    }

    std::vector<int> UltraCanvasChipGroup::GetSelectedIndices() const {
        std::vector<int> out;
        for (size_t i = 0; i < chips.size(); ++i) if (chips[i]->IsSelected()) out.push_back(static_cast<int>(i));
        return out;
    }

    bool UltraCanvasChipGroup::IsSelected(int index) const {
        return index >= 0 && index < static_cast<int>(chips.size()) && chips[index]->IsSelected();
    }

    void UltraCanvasChipGroup::ClearSelection() { SetSelectedIndices({}); }

    void UltraCanvasChipGroup::ChipSelectionChanged(UltraCanvasChip* chip, bool selected) {
        if (updating) return;
        const int index = IndexOfChip(chip);
        if (index < 0) return;
        if (selectionMode == ChipGroupSelectionMode::Single) {
            if (selected) {
                // The others go.
                updating = true;
                for (size_t i = 0; i < chips.size(); ++i) {
                    if (static_cast<int>(i) != index && chips[i]->IsSelected()) chips[i]->SetSelected(false);
                }
                updating = false;
            } else if (!allowNoSelection) {
                // Clicking the selected chip keeps it selected.
                updating = true;
                chip->SetSelected(true);
                updating = false;
                return;
            }
        }
        Notify();
    }

    void UltraCanvasChipGroup::Notify() {
        if (onSelectionChanged) onSelectionChanged(GetSelectedIndex());
        if (onSelectedIndicesChanged) onSelectedIndicesChanged(GetSelectedIndices());
    }

    // ----- look -----
    void UltraCanvasChipGroup::SetChipStyle(const ChipStyle& s) {
        chipStyle = s;
        hasStyle = true;
        for (auto& c : chips) c->SetStyle(s);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasChipGroup::SetChipVariant(ChipVariant variant) {
        chipVariant = variant;
        for (auto& c : chips) c->SetVariant(variant);
        RequestRedraw();
    }

    void UltraCanvasChipGroup::SetSpacing(float spacing) {
        layout.SetFlexGap(spacing);
        InvalidateLayout();
        RequestRedraw();
    }

} // namespace UltraCanvas
