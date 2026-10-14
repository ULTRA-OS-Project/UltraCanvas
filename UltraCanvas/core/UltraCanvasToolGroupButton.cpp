// core/UltraCanvasToolGroupButton.cpp
// A tool group: several tools sharing one slot of a tool palette or toolbar,
// with a bar of all of them folding out while the pointer rests on the slot.
// Version: 1.0.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "UltraCanvasToolGroupButton.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasToolbar.h"
#include "UltraCanvasTooltipManager.h"
#include "UltraCanvasWindow.h"
#include <algorithm>

namespace UltraCanvas {

    namespace {
        // The bar: a 1 px border round 4 px of padding, its buttons 2 px apart.
        constexpr float kFoldOutBorder = 1.0f;
        constexpr float kFoldOutPadding = 4.0f;
        constexpr float kFoldOutGap = 2.0f;
        constexpr float kFoldOutRadius = 6.0f;

        bool IsRow(ToolGroupFoldOutSide side) {
            return side == ToolGroupFoldOutSide::Right || side == ToolGroupFoldOutSide::Left;
        }
    }

    // ===== THE FOLD-OUT BAR =====
    // A row (or column) of the group's tools, open as a popup on the slot's
    // window. The window hands a popup every pointer event that is neither
    // on it nor on its owner - the slot - so this is where the pointer is
    // seen to have left the group, and the bar folds back.
    class UltraCanvasToolGroupFoldOut : public UltraCanvasContainer {
    public:
        UltraCanvasToolGroupFoldOut(const std::string& identifier, UltraCanvasToolGroupButton* slot)
            : UltraCanvasContainer(identifier, 0, 0, 0, 0), owner(slot) {
            ContainerStyle noScroll;
            noScroll.autoShowScrollbars = false;
            noScroll.forceShowVerticalScrollbar = false;
            noScroll.forceShowHorizontalScrollbar = false;
            SetContainerStyle(noScroll);
            SetBackgroundColor(Color(249, 249, 249, 255));
            SetBorders(kFoldOutBorder, Color(0, 0, 0, 36), kFoldOutRadius);
            SetPadding(kFoldOutPadding);
            layout.SetFlexGap(kFoldOutGap);
            layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        }

        bool OnEvent(const UCEvent& event) override {
            // On the move itself, not the MouseEnter the window sends just
            // before it: the window still delivers the move to the bar after
            // the enter, which a bar already closed would no longer take.
            if (event.type == UCEventType::MouseMove &&
                owner && !owner->PointerOnGroup(event.pointerWindow)) {
                owner->CloseFoldOut();
                return true;
            }
            return UltraCanvasContainer::OnEvent(event);
        }

        PopupShadow GetPopupShadow() const override {
            PopupShadow shadow;
            shadow.color = Color(0, 0, 0, 56);
            shadow.blur = 12;
            shadow.offset = Point2Di(0, 3);
            shadow.cornerRadius = kFoldOutRadius;
            return shadow;
        }

        // The slot this bar belongs to; cleared when the slot goes first.
        UltraCanvasToolGroupButton* owner;
    };

    // ===== CONSTRUCTION =====

    UltraCanvasToolGroupButton::UltraCanvasToolGroupButton(const std::string& identifier,
                                                           float x, float y, float w, float h)
            : UltraCanvasButton(identifier, x, y, w, h) {
        SetCanToggled(true);
    }

    UltraCanvasToolGroupButton::~UltraCanvasToolGroupButton() {
        CancelFoldOutTimer();
        if (foldOut) {
            foldOut->owner = nullptr;
            // A window closes its popups before its elements go, so a bar
            // still open here is on a window that is still there.
            if (foldOut->IsPopupElement()) {
                if (auto* win = foldOut->GetWindow()) win->ClosePopup(*foldOut);
            }
        }
    }

    // ===== ITEMS =====

    void UltraCanvasToolGroupButton::SetItems(const std::vector<ToolGroupItem>& groupItems) {
        const std::string keep = GetCurrentItem();
        CloseFoldOut();
        items = groupItems;
        currentIndex = items.empty() ? -1 : 0;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!keep.empty() && items[i].id == keep) currentIndex = static_cast<int>(i);
        }
        ApplySlotItem();
    }

    void UltraCanvasToolGroupButton::AddItem(const ToolGroupItem& item) {
        CloseFoldOut();
        items.push_back(item);
        if (currentIndex < 0) {
            currentIndex = 0;
            ApplySlotItem();
        } else {
            RequestRedraw();   // a second item brings the corner triangle
        }
    }

    bool UltraCanvasToolGroupButton::HasItem(const std::string& itemId) const {
        return std::any_of(items.begin(), items.end(),
                           [&itemId](const ToolGroupItem& i) { return i.id == itemId; });
    }

    // ===== CURRENT ITEM =====

    bool UltraCanvasToolGroupButton::SetCurrentItem(const std::string& itemId) {
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].id != itemId) continue;
            if (currentIndex != static_cast<int>(i)) {
                currentIndex = static_cast<int>(i);
                if (slotShowsLastPicked) ApplySlotItem();
            }
            return true;
        }
        return false;
    }

    std::string UltraCanvasToolGroupButton::GetCurrentItem() const {
        if (currentIndex < 0 || currentIndex >= static_cast<int>(items.size())) return "";
        return items[currentIndex].id;
    }

    std::string UltraCanvasToolGroupButton::GetSlotItem() const {
        const int index = SlotIndex();
        return index < 0 ? std::string() : items[index].id;
    }

    void UltraCanvasToolGroupButton::SetSlotShowsLastPicked(bool showLastPicked) {
        if (slotShowsLastPicked == showLastPicked) return;
        slotShowsLastPicked = showLastPicked;
        CloseFoldOut();
        ApplySlotItem();
    }

    int UltraCanvasToolGroupButton::SlotIndex() const {
        if (items.empty()) return -1;
        if (!slotShowsLastPicked) return 0;
        return (currentIndex >= 0 && currentIndex < static_cast<int>(items.size())) ? currentIndex : 0;
    }

    void UltraCanvasToolGroupButton::ApplySlotItem() {
        const int index = SlotIndex();
        if (index < 0) {
            SetIcon(std::shared_ptr<UCImage>());
            SetTooltip("");
            return;
        }
        const ToolGroupItem& item = items[index];
        if (item.iconPath.empty()) SetIcon(std::shared_ptr<UCImage>());
        else SetIcon(item.iconPath);
        SetTooltip(item.tooltip);
        RequestRedraw();
    }

    void UltraCanvasToolGroupButton::PickItem(int index) {
        if (index < 0 || index >= static_cast<int>(items.size())) return;
        // A copy: the host's callback may set new items.
        const std::string itemId = items[index].id;
        if (currentIndex != index) {
            currentIndex = index;
            if (slotShowsLastPicked) ApplySlotItem();
        }
        if (!IsPressed()) {
            SetPressed(true);
            NotifyAccessibility(AccessibilityEventType::StateChanged);
        }
        if (onItemSelected) onItemSelected(itemId);
    }

    // ===== THE FOLD-OUT BAR =====

    void UltraCanvasToolGroupButton::ScheduleFoldOut() {
        if (items.size() < 2 || foldOutHeldBack || IsFoldOutOpen() || foldOutTimer != InvalidTimerId) return;
        auto* app = UltraCanvasApplication::GetInstance();
        if (foldOutDelayMs <= 0 || !app) {
            OpenFoldOut();
            return;
        }
        foldOutTimer = app->StartTimer(static_cast<unsigned int>(foldOutDelayMs), false, [this](TimerId) {
            foldOutTimer = InvalidTimerId;
            // Still resting on the slot: a pointer that moved on in the
            // meantime has cancelled the timer, but a press, a hidden slot
            // or a disabled one say no as well.
            if (pointerOnSlot && IsVisible() && !IsDisabled()) OpenFoldOut();
        });
    }

    void UltraCanvasToolGroupButton::CancelFoldOutTimer() {
        if (foldOutTimer == InvalidTimerId) return;
        if (auto* app = UltraCanvasApplication::GetInstance()) app->StopTimer(foldOutTimer);
        foldOutTimer = InvalidTimerId;
    }

    void UltraCanvasToolGroupButton::OpenFoldOut() {
        CancelFoldOutTimer();
        if (items.size() < 2 || IsFoldOutOpen()) return;
        UltraCanvasWindowBase* win = GetWindow();
        if (!win) return;

        if (!foldOut) {
            foldOut = std::make_shared<UltraCanvasToolGroupFoldOut>(GetIdentifier() + "-foldout", this);
        }
        foldOut->ClearChildren();

        const ToolGroupFoldOutSide side = ResolveSide();
        const bool row = IsRow(side);
        if (row) foldOut->layout.SetFlexRow();
        else foldOut->layout.SetFlexColumn();

        // Each tool's button is the slot's twin: its size, its icon size and
        // its style, so the bar reads as the palette continued. The current
        // tool wears the pressed colour while the slot is pressed.
        const float itemW = GetWidth() > 0 ? GetWidth() : 32.0f;
        const float itemH = GetHeight() > 0 ? GetHeight() : 32.0f;
        const ButtonStyle slotStyle = GetStyle();
        for (size_t i = 0; i < items.size(); ++i) {
            const ToolGroupItem& item = items[i];
            auto b = std::make_shared<UltraCanvasButton>(
                    GetIdentifier() + "-item-" + std::to_string(i), 0, 0, itemW, itemH);
            ButtonStyle s = slotStyle;
            if (static_cast<int>(i) == currentIndex && IsPressed()) {
                s.normalColor = slotStyle.pressedColor;
                s.hoverColor = slotStyle.pressedColor;
            }
            b->SetStyle(s);
            if (!item.iconPath.empty()) b->SetIcon(item.iconPath);
            b->SetIconSize(GetIconWidth(), GetIconHeight());
            b->SetTooltip(item.tooltip);
            // A tool's button must not take the keyboard from whatever had it
            // (the canvas): the bar is gone again a moment later.
            b->SetAcceptsFocus(false);
            const int index = static_cast<int>(i);
            b->onClick = [bar = foldOut.get(), index]() {
                if (UltraCanvasToolGroupButton* slot = bar->owner) {
                    slot->CloseFoldOut();
                    slot->PickItem(index);
                }
            };
            foldOut->AddChild(b);
        }

        const float n = static_cast<float>(items.size());
        const float edge = 2.0f * (kFoldOutBorder + kFoldOutPadding);
        const Size2Df barSize = row
                ? Size2Df(edge + n * itemW + (n - 1.0f) * kFoldOutGap, edge + itemH)
                : Size2Df(edge + itemW, edge + n * itemH + (n - 1.0f) * kFoldOutGap);
        foldOut->SetElementSize(barSize);
        const Point2Df pos = FoldOutPosition(side, barSize);

        PopupElementSettings settings;
        settings.popupOwner = weak_from_this();
        settings.closeByEscapeKey = true;
        settings.closeByClickOutside = true;
        // The slot's own tooltip would sit on top of the bar; the bar's
        // buttons have one each.
        UltraCanvasTooltipManager::HideTooltip();
        win->OpenPopup(Point2Di(static_cast<int>(pos.x), static_cast<int>(pos.y)), *foldOut, settings);
        RequestRedraw();
    }

    void UltraCanvasToolGroupButton::CloseFoldOut() {
        CancelFoldOutTimer();
        if (!foldOut || !foldOut->IsPopupElement()) return;
        if (auto* win = foldOut->GetWindow()) win->ClosePopup(*foldOut);
    }

    bool UltraCanvasToolGroupButton::IsFoldOutOpen() const {
        return foldOut && foldOut->IsPopupElement();
    }

    std::shared_ptr<UltraCanvasContainer> UltraCanvasToolGroupButton::GetFoldOut() const {
        if (!IsFoldOutOpen()) return nullptr;
        return foldOut;
    }

    ToolGroupFoldOutSide UltraCanvasToolGroupButton::ResolveSide() const {
        ToolGroupFoldOutSide side = foldOutSide;
        if (side == ToolGroupFoldOutSide::Auto) {
            auto* bar = dynamic_cast<UltraCanvasToolbar*>(GetParentContainer());
            side = (bar && bar->GetOrientation() == ToolbarOrientation::Vertical)
                   ? ToolGroupFoldOutSide::Right : ToolGroupFoldOutSide::Bottom;
        }

        // The opposite side when the window has no room on this one.
        UltraCanvasWindowBase* win = GetWindow();
        if (!win) return side;
        const Rect2Df slot = GetBoundsInWindow();
        const float n = static_cast<float>(items.size());
        const float edge = 2.0f * (kFoldOutBorder + kFoldOutPadding);
        const float alongRow = edge + n * slot.width + (n - 1.0f) * kFoldOutGap;
        const float alongColumn = edge + n * slot.height + (n - 1.0f) * kFoldOutGap;
        switch (side) {
            case ToolGroupFoldOutSide::Right:
                if (slot.x + slot.width + alongRow > win->GetWidth() && slot.x - alongRow >= 0.0f)
                    return ToolGroupFoldOutSide::Left;
                break;
            case ToolGroupFoldOutSide::Left:
                if (slot.x - alongRow < 0.0f && slot.x + slot.width + alongRow <= win->GetWidth())
                    return ToolGroupFoldOutSide::Right;
                break;
            case ToolGroupFoldOutSide::Bottom:
                if (slot.y + slot.height + alongColumn > win->GetHeight() && slot.y - alongColumn >= 0.0f)
                    return ToolGroupFoldOutSide::Top;
                break;
            case ToolGroupFoldOutSide::Top:
                if (slot.y - alongColumn < 0.0f && slot.y + slot.height + alongColumn <= win->GetHeight())
                    return ToolGroupFoldOutSide::Bottom;
                break;
            case ToolGroupFoldOutSide::Auto:
                break;
        }
        return side;
    }

    Point2Df UltraCanvasToolGroupButton::FoldOutPosition(ToolGroupFoldOutSide side, const Size2Df& barSize) const {
        const Rect2Df slot = GetBoundsInWindow();
        // The bar touches the slot, so the pointer never crosses a gap that
        // belongs to neither on its way across; its buttons line up with the
        // slot, the bar's border and padding standing out round them.
        const float inset = kFoldOutBorder + kFoldOutPadding;
        Point2Df pos;
        switch (side) {
            case ToolGroupFoldOutSide::Left:
                pos = Point2Df(slot.x - barSize.width, slot.y - inset);
                break;
            case ToolGroupFoldOutSide::Bottom:
                pos = Point2Df(slot.x - inset, slot.y + slot.height);
                break;
            case ToolGroupFoldOutSide::Top:
                pos = Point2Df(slot.x - inset, slot.y - barSize.height);
                break;
            case ToolGroupFoldOutSide::Right:
            case ToolGroupFoldOutSide::Auto:
            default:
                pos = Point2Df(slot.x + slot.width, slot.y - inset);
                break;
        }
        // Kept inside the window along the slot's edge as well.
        if (const UltraCanvasWindowBase* win = GetWindow()) {
            const float maxX = std::max(0.0f, static_cast<float>(win->GetWidth()) - barSize.width);
            const float maxY = std::max(0.0f, static_cast<float>(win->GetHeight()) - barSize.height);
            pos.x = std::clamp(pos.x, 0.0f, maxX);
            pos.y = std::clamp(pos.y, 0.0f, maxY);
        }
        return pos;
    }

    bool UltraCanvasToolGroupButton::PointerOnGroup(const Point2Df& pointerWindow) const {
        const Rect2Df slot = GetBoundsInWindow();
        if (slot.Contains(pointerWindow)) return true;
        return foldOut && foldOut->IsPopupElement() && foldOut->GetBoundsInWindow().Contains(pointerWindow);
    }

    // ===== RENDERING =====

    void UltraCanvasToolGroupButton::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        UltraCanvasButton::Render(ctx, dirtyRect);
        if (items.size() < 2) return;

        // The group mark: a small right-angled triangle in the bottom-right
        // corner, in the icon's colour, its straight edges on whole pixels.
        Color bg, fg;
        GetCurrentColors(bg, fg);
        const Rect2Df b = GetLocalBounds();
        const double size = std::clamp(std::floor(std::min(b.width, b.height) / 7.0f), 4.0f, 6.0f);
        const double right = std::floor(b.x + b.width) - 2.0;
        const double bottom = std::floor(b.y + b.height) - 2.0;
        ctx->PushState();
        ctx->SetFillPaint(Color(fg.r, fg.g, fg.b, static_cast<uint8_t>(fg.a * 0.7f)));
        ctx->FillLinePath({Point2Dd(right, bottom - size), Point2Dd(right, bottom), Point2Dd(right - size, bottom)});
        ctx->PopState();
    }

    // ===== EVENTS =====

    bool UltraCanvasToolGroupButton::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) {
            pointerOnSlot = false;
            CancelFoldOutTimer();
            return UltraCanvasButton::OnEvent(event);
        }

        switch (event.type) {
            case UCEventType::MouseEnter:
                pointerOnSlot = true;
                ScheduleFoldOut();
                break;

            case UCEventType::MouseMove:
                if (Contains(event.pointer)) {
                    pointerOnSlot = true;
                    ScheduleFoldOut();
                } else {
                    pointerOnSlot = false;
                    foldOutHeldBack = false;
                    CancelFoldOutTimer();
                }
                break;

            case UCEventType::MouseLeave:
                // Only the timer: the bar itself folds back once the pointer
                // is off it too, which the bar sees.
                pointerOnSlot = false;
                foldOutHeldBack = false;
                CancelFoldOutTimer();
                break;

            case UCEventType::MouseDown:
            case UCEventType::MouseDoubleClick:
                if (event.button == UCMouseButton::Left && Contains(event.pointer)) {
                    // A click picks the slot's tool. It never lets a pressed
                    // slot go, as a plain toggle would: picking the active
                    // tool again leaves it active.
                    CloseFoldOut();
                    foldOutHeldBack = true;
                    if (!IsPressed()) UltraCanvasButton::OnEvent(event);
                    PickItem(SlotIndex());
                    return true;
                }
                break;

            case UCEventType::KeyDown:
                if (IsFocused() && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    return true;
                }
                break;

            case UCEventType::KeyUp:
                if (IsFocused() && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    PickItem(SlotIndex());
                    return true;
                }
                break;

            default:
                break;
        }
        return UltraCanvasButton::OnEvent(event);
    }

} // namespace UltraCanvas
