// include/UltraCanvasToolGroupButton.h
// A tool group: several tools sharing one slot of a tool palette or toolbar,
// as the selection tools, the brushes or the shapes do in a paint program.
//
// The slot shows one tool of the group and a click on it picks that tool.
// Resting the pointer on the slot folds out a bar beside it with every tool
// of the group; a click in the bar picks that tool, which from then on is
// the one the slot shows - or, with SetSlotShowsLastPicked(false), the slot
// keeps showing the group's first, main tool. A small triangle in the slot's
// corner says there is more than one. The bar folds back when the pointer
// leaves both the slot and the bar, on Escape and on a click elsewhere. A
// group of one tool is an ordinary toggle button: no triangle, no bar.
//
// The slot is a toggle button. A pick presses it; the host releases it when
// a tool outside the group becomes active (SetPressed(false)) and, when a
// tool of the group is picked some other way - a shortcut key - shows that
// one with SetCurrentItem. While the slot is pressed the bar marks the
// current tool - the one last picked or set.
//
// UltraCanvasToolbar::AddToolGroup adds one to a toolbar with the toolbar's
// button appearance, as AddToggleButton does for a single tool.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_TOOL_GROUP_BUTTON_H
#define ULTRACANVAS_TOOL_GROUP_BUTTON_H

#include "UltraCanvasButton.h"
#include "UltraCanvasTimer.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    class UltraCanvasContainer;
    class UltraCanvasToolGroupFoldOut;

    // One tool of a group.
    struct ToolGroupItem {
        std::string id;          // handed back by onItemSelected
        std::string iconPath;
        std::string tooltip;     // its name (and shortcut): the bar's button and, while current, the slot show it
    };

    // The side of the slot the bar folds out to. The bar runs along that
    // side's direction: a row to the right or left, a column below or above.
    enum class ToolGroupFoldOutSide {
        Auto = 0,     // right of a slot in a vertical toolbar, below one in any other
        Right = 1,
        Left = 2,
        Bottom = 3,   // below the slot (X11's headers #define Above and Below)
        Top = 4       // above it
    };
    // Whichever side is asked for, a bar the window has no room for there
    // folds out to the opposite side instead.

    class UltraCanvasToolGroupButton : public UltraCanvasButton {
    public:
        UltraCanvasToolGroupButton(const std::string& identifier,
                                   float x, float y, float w, float h);
        UltraCanvasToolGroupButton(const std::string& identifier, float w, float h)
            : UltraCanvasToolGroupButton(identifier, -1, -1, w, h) {}
        ~UltraCanvasToolGroupButton() override;

        // ===== ITEMS =====
        // The group's tools, in the bar's order. The first becomes current
        // unless the current one is among the new items.
        void SetItems(const std::vector<ToolGroupItem>& groupItems);
        void AddItem(const ToolGroupItem& item);
        const std::vector<ToolGroupItem>& GetItems() const { return items; }
        int GetItemCount() const { return static_cast<int>(items.size()); }
        bool HasItem(const std::string& itemId) const;

        // ===== CURRENT ITEM =====
        // The tool last picked or set: the one the bar marks while the slot
        // is pressed and, while the slot shows the last picked tool, the one
        // it shows - its icon and tooltip become the slot's - and a click on
        // it picks. Returns false, changing nothing, when no item has this
        // id. Fires nothing.
        bool SetCurrentItem(const std::string& itemId);
        // "" while the group is empty.
        std::string GetCurrentItem() const;
        // The tool the slot shows and a click on it picks: the current one,
        // or the first while SetSlotShowsLastPicked(false).
        std::string GetSlotItem() const;
        // True (the default): the slot shows the tool last picked, in the bar
        // or with SetCurrentItem. False: it always shows the first, main
        // tool of the group, whichever of its tools is in use.
        void SetSlotShowsLastPicked(bool showLastPicked);
        bool GetSlotShowsLastPicked() const { return slotShowsLastPicked; }

        // ===== THE FOLD-OUT BAR =====
        void SetFoldOutSide(ToolGroupFoldOutSide side) { foldOutSide = side; }
        ToolGroupFoldOutSide GetFoldOutSide() const { return foldOutSide; }
        // How long the pointer rests on the slot before the bar folds out,
        // in milliseconds; 0 folds it out at once. 150 by default, so a
        // pointer passing over the palette on its way to the canvas does not
        // flash every group's bar.
        void SetFoldOutDelay(int milliseconds) { foldOutDelayMs = milliseconds < 0 ? 0 : milliseconds; }
        int GetFoldOutDelay() const { return foldOutDelayMs; }
        // Folds the bar out now (a group of one has nothing to fold out, and
        // a slot that is in no window nowhere to show it) or back.
        void OpenFoldOut();
        void CloseFoldOut();
        bool IsFoldOutOpen() const;
        // The bar while it is folded out, nullptr otherwise. Its children
        // are the tools' buttons, in the items' order.
        std::shared_ptr<UltraCanvasContainer> GetFoldOut() const;

        // ===== CALLBACKS =====
        // A tool was picked: by a click on the slot (its current tool) or on
        // a tool in the bar.
        std::function<void(const std::string& itemId)> onItemSelected;

        // ===== OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;

    private:
        friend class UltraCanvasToolGroupFoldOut;

        std::vector<ToolGroupItem> items;
        int currentIndex = -1;
        bool slotShowsLastPicked = true;
        ToolGroupFoldOutSide foldOutSide = ToolGroupFoldOutSide::Auto;
        int foldOutDelayMs = 150;
        TimerId foldOutTimer = InvalidTimerId;
        // The pointer is on the slot: what the fold-out delay waits on.
        bool pointerOnSlot = false;
        // A click on the slot picked its tool: the bar stays in until the
        // pointer has left the slot, instead of folding out again under a
        // pointer that merely rests there after the click.
        bool foldOutHeldBack = false;
        std::shared_ptr<UltraCanvasToolGroupFoldOut> foldOut;

        // The index of the item the slot shows, -1 for none.
        int SlotIndex() const;
        void ApplySlotItem();
        // Makes items[index] current, presses the slot and fires onItemSelected.
        void PickItem(int index);
        void ScheduleFoldOut();
        void CancelFoldOutTimer();
        // The side the bar goes to once Auto and a window without room for it
        // are taken into account, and where its top-left corner lands there.
        ToolGroupFoldOutSide ResolveSide() const;
        Point2Df FoldOutPosition(ToolGroupFoldOutSide side, const Size2Df& barSize) const;
        // Whether a pointer at this window position is on the slot or on the
        // bar - while it is, the bar stays out.
        bool PointerOnGroup(const Point2Df& pointerWindow) const;
    };

    inline std::shared_ptr<UltraCanvasToolGroupButton> CreateToolGroupButton(
            const std::string& identifier, float x, float y, float w, float h,
            const std::vector<ToolGroupItem>& items = {}) {
        auto button = std::make_shared<UltraCanvasToolGroupButton>(identifier, x, y, w, h);
        if (!items.empty()) button->SetItems(items);
        return button;
    }

} // namespace UltraCanvas

#endif // ULTRACANVAS_TOOL_GROUP_BUTTON_H
