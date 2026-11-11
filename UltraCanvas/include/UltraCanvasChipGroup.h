// include/UltraCanvasChipGroup.h
// A group of chips with one selection rule: single (one chip at a time, as a
// row of categories or filters) or multiple (any number, as a set of tags
// to match). The chips are real UltraCanvasChip children laid out in a
// wrapping row, so each keeps its own hover, focus, keyboard (Space / Return)
// and theming; the group only decides what a click means for the others
// and tells the caller what is selected now.
//
//   auto group = CreateChipGroup("categories");
//   group->AddChip("All"); group->AddChip("Video"); group->AddChip("Audio");
//   group->SetSelectedIndex(0);
//   group->onSelectionChanged = [](int index) { /* -1: nothing */ };
//
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once
#include "UltraCanvasContainer.h"
#include "UltraCanvasChip.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    enum class ChipGroupSelectionMode {
        Single,     // one chip selected at a time (the default)
        Multiple    // any number of chips
    };

    class UltraCanvasChipGroup : public UltraCanvasContainer {
    public:
        AccessibleRole GetAccessibleRole() const override { return AccessibleRole::List; }

        UltraCanvasChipGroup(const std::string& identifier, float x, float y, float w, float h);
        UltraCanvasChipGroup(const std::string& identifier, float w, float h)
                : UltraCanvasChipGroup(identifier, -1, -1, w, h) {}
        explicit UltraCanvasChipGroup(const std::string& identifier)
                : UltraCanvasChipGroup(identifier, -1, -1, -1, -1) {}

        // ----- chips -----
        // Adds a selectable chip and returns its index. The chip's id is
        // "<group id>.chip<index>" unless one is given.
        int AddChip(const std::string& label, const std::string& iconPath = "", const std::string& chipId = "");
        // Adds a chip the caller made; it is made selectable here.
        int AddChip(std::shared_ptr<UltraCanvasChip> chip);
        void RemoveChip(int index);
        void ClearChips();
        int  GetChipCount() const { return static_cast<int>(chips.size()); }
        std::shared_ptr<UltraCanvasChip> GetChip(int index) const;
        // The index of the chip with this label, or -1.
        int IndexOf(const std::string& label) const;

        // ----- selection -----
        void SetSelectionMode(ChipGroupSelectionMode mode);
        ChipGroupSelectionMode GetSelectionMode() const { return selectionMode; }
        // Single mode: whether clicking the selected chip may deselect it,
        // leaving nothing selected (off by default: one is always selected
        // once one was).
        void SetAllowNoSelection(bool allow) { allowNoSelection = allow; }
        bool GetAllowNoSelection() const { return allowNoSelection; }

        // Single mode: the selected chip, -1 for none; setting -1 clears.
        void SetSelectedIndex(int index);
        int  GetSelectedIndex() const;
        // Any mode: every selected index, ascending.
        void SetSelectedIndices(const std::vector<int>& indices);
        std::vector<int> GetSelectedIndices() const;
        bool IsSelected(int index) const;
        void ClearSelection();

        // ----- look -----
        // Applied to every chip, now and later.
        void SetChipStyle(const ChipStyle& style);
        const ChipStyle& GetChipStyle() const { return chipStyle; }
        void SetChipVariant(ChipVariant variant);
        // The gap between chips and between rows (default 6).
        void SetSpacing(float spacing);

        // ----- callbacks -----
        std::function<void(int index)> onSelectionChanged;                      // single mode (-1: none)
        std::function<void(const std::vector<int>&)> onSelectedIndicesChanged;  // any mode

    private:
        void Wire(const std::shared_ptr<UltraCanvasChip>& chip);
        void ChipSelectionChanged(UltraCanvasChip* chip, bool selected);
        void Notify();
        int  IndexOfChip(const UltraCanvasChip* chip) const;

        std::vector<std::shared_ptr<UltraCanvasChip>> chips;
        ChipGroupSelectionMode selectionMode = ChipGroupSelectionMode::Single;
        bool allowNoSelection = false;
        bool hasStyle = false;
        ChipStyle chipStyle;
        ChipVariant chipVariant = ChipVariant::Filled;
        bool updating = false;
        int nextChipNumber = 0;
    };

    inline std::shared_ptr<UltraCanvasChipGroup> CreateChipGroup(const std::string& identifier,
                                                                 float x = -1, float y = -1,
                                                                 float w = -1, float h = -1) {
        return std::make_shared<UltraCanvasChipGroup>(identifier, x, y, w, h);
    }

} // namespace UltraCanvas
