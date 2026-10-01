// include/UltraCanvasToolbar.cpp
// Implementation of comprehensive toolbar component
// Version: 1.6.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "UltraCanvasToolbar.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace UltraCanvas {

// ===== MAIN TOOLBAR IMPLEMENTATION =====

    UltraCanvasToolbar::UltraCanvasToolbar(const std::string& identifier,
                                           float x, float y, float width, float height)
            : UltraCanvasContainer(identifier, x, y, width, height) {

        // Set default background color and border
        SetBackgroundColor(toolbarAppearance.backgroundColor);
        SetBorders(1, Color(180, 180, 180, 255));

        ContainerStyle noScroll;
        noScroll.autoShowScrollbars = false;
        noScroll.forceShowVerticalScrollbar = false;
        noScroll.forceShowHorizontalScrollbar = false;
        SetContainerStyle(noScroll);

        CreateLayout();
    }

    void UltraCanvasToolbar::CreateLayout() {
        if (toolbarOrientation == ToolbarOrientation::Vertical) {
            SetPadding(3, 5);
            layout.SetFlexColumn();
        } else {
            SetPadding(5, 3);
            layout.SetFlexRow();
        }
        layout.SetFlexGap(toolbarAppearance.itemSpacing);
        layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        AdoptThicknessAsMinimum();
    }

    void UltraCanvasToolbar::AdoptThicknessAsMinimum() {
        // A toolbar's thickness is decided by what is in it - the button box,
        // the icon inside it, this class's own padding and border - and none of
        // that is knowable to the host that picks a number in a constructor
        // call. A number that is too small does not scroll or wrap: the
        // buttons are simply cut off at the bottom edge, which is how
        // UltraPaint's 38 px toolbar clipped 32 px buttons that sit in 5 px of
        // padding inside a 1 px border.
        //
        // So the constructed thickness is kept as a floor and the size itself
        // goes back to auto: a host still gets at least the bar it asked for
        // (a 48 px toolbar stays 48 px even when its items are small), and the
        // items are never clipped by it.
        CSSLayout::BoxConstraints limits = boxConstraints.value_or(CSSLayout::BoxConstraints{});
        if (toolbarOrientation == ToolbarOrientation::Vertical) {
            if (!size.width.isAuto()) {
                limits.minWidth = size.width;
                size.width = CSSLayout::Dimension::Auto();
            }
            // An orientation flip must not leave the other axis pinned by the
            // floor set for the shape this toolbar no longer has.
            limits.minHeight = CSSLayout::Dimension::Auto();
        } else {
            if (!size.height.isAuto()) {
                limits.minHeight = size.height;
                size.height = CSSLayout::Dimension::Auto();
            }
            limits.minWidth = CSSLayout::Dimension::Auto();
        }
        boxConstraints = limits;
        InvalidateLayout();
    }

    void UltraCanvasToolbar::SetThickness(float px) {
        if (toolbarOrientation == ToolbarOrientation::Vertical) {
            size.width = CSSLayout::Dimension::Px(px);
        } else {
            size.height = CSSLayout::Dimension::Px(px);
        }
        AdoptThicknessAsMinimum();
    }

    void UltraCanvasToolbar::SetOrientation(ToolbarOrientation orient) {
        if (toolbarOrientation != orient) {
            toolbarOrientation = orient;
            CreateLayout(); // Recreate layout with new orientation
            InvalidateLayout();
        }
    }

    void UltraCanvasToolbar::SetToolbarPosition(ToolbarPosition pos) {
        toolbarPosition = pos;
        if (onPositionChanged) {
            onPositionChanged(pos);
        }
    }

    void UltraCanvasToolbar::SetAppearance(const ToolbarAppearance& app) {
        toolbarAppearance = app;

        // Update toolbar container appearance
        SetBackgroundColor(toolbarAppearance.backgroundColor);

        // Update border based on appearance
        if (toolbarAppearance.style == ToolbarStyle::Flat) {
            SetBorders(0, Colors::Transparent);
        } else if (toolbarAppearance.style == ToolbarStyle::Docked) {
            SetBorders(1, Color(180, 180, 180, 180), 12);
        } else {
            SetBorders(1, Color(180, 180, 180, 255));
        }

        layout.SetFlexGap(toolbarAppearance.itemSpacing);

        // Re-style existing child widgets
        ApplyAppearanceToChildren();
    }

    void UltraCanvasToolbar::SetOverflowMode(ToolbarOverflowMode mode) {
        overflowMode = mode;
        for (auto& c : Children()) {
            ApplyOverflowToChild(std::static_pointer_cast<UltraCanvasUIElement>(c));
        }
        HandleOverflow();
        UpdatePointerFilter();
        InvalidateLayout();
    }

    void UltraCanvasToolbar::SetScrollHints(bool show) {
        if (scrollHints == show) return;
        scrollHints = show;
        UpdatePointerFilter();
        RequestRedraw();
    }

    void UltraCanvasToolbar::ApplyOverflowToChild(const std::shared_ptr<UltraCanvasUIElement>& child) {
        if (!child || !IsItem(child.get())) return;
        // A flex item shrinks to fit by default, which is right for every mode
        // but one: a scrolling toolbar keeps its items at their size and lets
        // the ones past the edge scroll into view instead of squeezing all of
        // them into the box.
        if (overflowMode == ToolbarOverflowMode::Scroll) {
            child->layoutItem.SetFlexShrink(0.0f);
        }
    }

    void UltraCanvasToolbar::SetVisibility(ToolbarVisibility vis) {
        toolbarVisibility = vis;
    }

    void UltraCanvasToolbar::SetDragMode(ToolbarDragMode mode) {
        toolbarDragMode = mode;
    }

// ===== ITEM MANAGEMENT =====

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasToolbar::RegisterWidget(
            const std::string& id, std::shared_ptr<UltraCanvasUIElement> w) {
        if (!w) return w;
        if (!id.empty()) {
            widgetMap[id] = w;
        }
        AddChild(w);
        ApplyOverflowToChild(w);
        if (!id.empty() && onItemAdded) {
            onItemAdded(id);
        }
        InvalidateLayout();
        return w;
    }

    void UltraCanvasToolbar::RemoveItem(const std::string& identifier) {
        auto it = widgetMap.find(identifier);
        if (it == widgetMap.end()) return;

        auto widget = it->second;
        widgetMap.erase(it);
        ClearItemBadge(identifier);

        if (widget) {
            RemoveChild(widget);
        }

        if (onItemRemoved) {
            onItemRemoved(identifier);
        }

        InvalidateLayout();
    }

    void UltraCanvasToolbar::RemoveItemAt(int index) {
        auto widget = GetWidgetAt(index);
        if (!widget) return;

        // Drop any map entry that points at this widget (spacers/separators
        // may be unmapped), and the badge it carried.
        for (auto it = widgetMap.begin(); it != widgetMap.end(); ++it) {
            if (it->second == widget) {
                const std::string id = it->first;
                widgetMap.erase(it);
                ClearItemBadge(id);
                break;
            }
        }

        RemoveChild(widget);
        InvalidateLayout();
    }

    void UltraCanvasToolbar::ClearItems() {
        widgetMap.clear();
        badgeMap.clear();
        badgeSet.clear();
        ClearChildren();
        InvalidateLayout();
    }

    bool UltraCanvasToolbar::IsItem(const CSSLayout::Element* child) const {
        return child && badgeSet.count(static_cast<const UltraCanvasUIElement*>(child)) == 0;
    }

    int UltraCanvasToolbar::GetItemCount() const {
        int n = 0;
        for (auto& c : Children()) {
            if (IsItem(c.get())) ++n;
        }
        return n;
    }

    std::vector<std::shared_ptr<UltraCanvasUIElement>> UltraCanvasToolbar::GetItems() const {
        std::vector<std::shared_ptr<UltraCanvasUIElement>> items;
        items.reserve(Children().size());
        for (auto& c : Children()) {
            if (IsItem(c.get())) items.push_back(std::static_pointer_cast<UltraCanvasUIElement>(c));
        }
        return items;
    }

    int UltraCanvasToolbar::GetItemIndex(const std::string& identifier) const {
        auto it = widgetMap.find(identifier);
        if (it == widgetMap.end()) return -1;
        int index = 0;
        for (auto& c : Children()) {
            if (!IsItem(c.get())) continue;
            if (c.get() == it->second.get()) return index;
            ++index;
        }
        return -1;
    }

    std::vector<std::string> UltraCanvasToolbar::GetItemOrder() const {
        std::vector<std::string> order;
        for (const auto& item : GetItems()) {
            std::string id;
            for (const auto& entry : widgetMap) {
                if (entry.second == item) { id = entry.first; break; }
            }
            order.push_back(id);
        }
        return order;
    }

    bool UltraCanvasToolbar::MoveItem(int fromIndex, int toIndex) {
        auto items = GetItems();
        const int n = static_cast<int>(items.size());
        if (fromIndex < 0 || fromIndex >= n || toIndex < 0 || toIndex >= n) return false;
        if (fromIndex == toIndex) return true;

        auto moved = items[fromIndex];
        items.erase(items.begin() + fromIndex);
        items.insert(items.begin() + toIndex, moved);

        // Rank every child: items by their new place, badges after them in
        // the order they had (the z-order sort at Arrange keeps them on top
        // anyway). A stable sort by rank is the reorder primitive the layout
        // element offers.
        std::unordered_map<const CSSLayout::Element*, int> rank;
        for (int i = 0; i < n; ++i) rank[items[i].get()] = i;
        int next = n;
        for (auto& c : Children()) {
            if (!IsItem(c.get())) rank[c.get()] = next++;
        }
        SortChildren([&rank](const std::shared_ptr<CSSLayout::Element>& a,
                             const std::shared_ptr<CSSLayout::Element>& b) {
            return rank[a.get()] < rank[b.get()];
        });
        InvalidateLayout();
        RequestRedraw();
        if (onItemReordered) onItemReordered(fromIndex, toIndex);
        return true;
    }

// ===== ITEM BADGES =====

    void UltraCanvasToolbar::SetItemBadgeCorner(BadgeCorner corner, float offsetX, float offsetY) {
        badgeCorner  = corner;
        badgeOffsetX = offsetX;
        badgeOffsetY = offsetY;
    }

    std::shared_ptr<UltraCanvasBadge> UltraCanvasToolbar::EnsureItemBadge(const std::string& id) {
        auto found = badgeMap.find(id);
        if (found != badgeMap.end()) return found->second;
        auto item = GetWidget(id);
        if (!item) return nullptr;

        auto badge = std::make_shared<UltraCanvasBadge>(id + ".badge");
        badge->SetOverlayRing(false);
        badgeMap[id] = badge;
        badgeSet.insert(badge.get());
        AddChild(badge);
        // Anchoring takes the badge out of the flex flow; it follows the item
        // from then on, through reorders and scrolling alike.
        badge->AnchorTo(item, badgeCorner, badgeOffsetX, badgeOffsetY);
        return badge;
    }

    std::shared_ptr<UltraCanvasBadge> UltraCanvasToolbar::SetItemBadge(
            const std::string& id, const std::string& text, std::optional<Color> color) {
        auto badge = EnsureItemBadge(id);
        if (!badge) return nullptr;
        badge->SetText(text);
        if (color) badge->SetColor(*color);
        InvalidateLayout();
        RequestRedraw();
        return badge;
    }

    std::shared_ptr<UltraCanvasBadge> UltraCanvasToolbar::SetItemBadgeCount(
            const std::string& id, int count, std::optional<Color> color) {
        auto badge = EnsureItemBadge(id);
        if (!badge) return nullptr;
        badge->SetCount(count);
        if (color) badge->SetColor(*color);
        InvalidateLayout();
        RequestRedraw();
        return badge;
    }

    std::shared_ptr<UltraCanvasBadge> UltraCanvasToolbar::SetItemBadgeDot(
            const std::string& id, const Color& color) {
        auto badge = EnsureItemBadge(id);
        if (!badge) return nullptr;
        badge->SetDot(true);
        badge->SetColor(color);
        InvalidateLayout();
        RequestRedraw();
        return badge;
    }

    std::shared_ptr<UltraCanvasBadge> UltraCanvasToolbar::GetItemBadge(const std::string& id) {
        auto found = badgeMap.find(id);
        return found != badgeMap.end() ? found->second : nullptr;
    }

    void UltraCanvasToolbar::ClearItemBadge(const std::string& id) {
        auto found = badgeMap.find(id);
        if (found == badgeMap.end()) return;
        auto badge = found->second;
        badgeMap.erase(found);
        badgeSet.erase(badge.get());
        RemoveChild(badge);
        InvalidateLayout();
        RequestRedraw();
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasToolbar::GetWidget(const std::string& identifier) {
        auto it = widgetMap.find(identifier);
        return (it != widgetMap.end()) ? it->second : nullptr;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasToolbar::GetWidgetAt(int index) {
        if (index < 0) return nullptr;
        int i = 0;
        for (auto& c : Children()) {
            if (!IsItem(c.get())) continue;
            if (i == index) return std::static_pointer_cast<UltraCanvasUIElement>(c);
            ++i;
        }
        return nullptr;
    }

// ===== CONVENIENCE METHODS =====

    std::shared_ptr<UltraCanvasButton> UltraCanvasToolbar::AddButton(
            const std::string& id, const std::string& text,
            const std::string& icon, std::function<void()> onClick) {
        auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 32, text);
        if (!icon.empty()) {
            button->SetIcon(icon);
            button->SetIconSize(24, 24);
        }
        if (onClick) {
            button->onClick = onClick;
        }
        ApplyButtonAppearance(button);
        RegisterWidget(id, button);
        return button;
    }

    std::shared_ptr<UltraCanvasButton> UltraCanvasToolbar::AddToggleButton(
            const std::string& id, const std::string& text,
            const std::string& icon, std::function<void(bool)> onToggle) {
        auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 32, text);
        if (!icon.empty()) {
            button->SetIcon(icon);
            button->SetIconSize(24, 24);
        }
        button->SetCanToggled(true);
        if (onToggle) {
            button->onToggle = std::move(onToggle);
        }
        ApplyButtonAppearance(button);
        RegisterWidget(id, button);
        return button;
    }

    std::shared_ptr<UltraCanvasDropdown> UltraCanvasToolbar::AddDropdownButton(
            const std::string& id, const std::string& text,
            const std::vector<std::string>& items,
            std::function<void(const std::string&)> onSelect) {
        auto dropdown = std::make_shared<UltraCanvasDropdown>(id, 0, 0, 120, 24);
        for (const auto& s : items) {
            dropdown->AddItem(s);
        }
        if (onSelect) {
            // Map the selected index back to its string for the public callback.
            auto captured = items;
            dropdown->onSelectionChanged =
                    [onSelect, captured](int index, const DropdownItem&) {
                        if (index >= 0 && index < static_cast<int>(captured.size())) {
                            onSelect(captured[index]);
                        }
                    };
        }
        RegisterWidget(id, dropdown);
        return dropdown;
    }

    std::shared_ptr<UltraCanvasSeparator> UltraCanvasToolbar::AddSeparator(const std::string& id) {
        bool vertical = (toolbarOrientation == ToolbarOrientation::Horizontal);
        auto sep = std::make_shared<UltraCanvasSeparator>(
                vertical, 1, 24, toolbarAppearance.separatorColor);
        RegisterWidget(id, sep);
        return sep;
    }

    std::shared_ptr<UltraCanvasSpacer> UltraCanvasToolbar::AddSpacer(int size) {
        return UltraCanvasContainer::AddSpacer(static_cast<float>(size));
    }

    std::shared_ptr<UltraCanvasSpacer> UltraCanvasToolbar::AddStretch(float stretch) {
        return UltraCanvasContainer::AddStretchSpacer(stretch);
    }

    std::shared_ptr<UltraCanvasLabel> UltraCanvasToolbar::AddLabel(
            const std::string& id, const std::string& text) {
        auto label = std::make_shared<UltraCanvasLabel>(id, 0, 0, 80, 24, text);
        label->SetAlignment(TextAlignment::Left);
        RegisterWidget(id, label);
        return label;
    }

    std::shared_ptr<UltraCanvasTextInput> UltraCanvasToolbar::AddSearchBox(
            const std::string& id, const std::string& placeholder,
            std::function<void(const std::string&)> onTextChange) {
        auto searchBox = std::make_shared<UltraCanvasTextInput>(id, 0, 0, 150, 24);
        searchBox->SetPlaceholder(placeholder);
        if (onTextChange) {
            searchBox->onTextChanged = onTextChange;
        }
        RegisterWidget(id, searchBox);
        return searchBox;
    }

    std::shared_ptr<UltraCanvasAutoComplete> UltraCanvasToolbar::AddAutoComplete(
            const std::string& id, const std::string& placeholder,
            std::function<void(const std::string&)> onTextChange) {
        auto autoComplete = std::make_shared<UltraCanvasAutoComplete>(id, 0, 0, 150, 24);
        autoComplete->SetPlaceholder(placeholder);
        if (onTextChange) {
            autoComplete->onTextChanged = onTextChange;
        }
        RegisterWidget(id, autoComplete);
        return autoComplete;
    }

// ===== APPEARANCE =====

    void UltraCanvasToolbar::ApplyButtonAppearance(const std::shared_ptr<UltraCanvasButton>& button) {
        if (!button) return;

        ButtonStyle style = button->GetStyle();
        style.fontSize = (toolbarAppearance.iconSize == ToolbarIconSize::Small) ? 10.0f : 12.0f;
        style.borderWidth = (toolbarAppearance.style == ToolbarStyle::Flat) ? 0 : 1;

        style.hoverColor = toolbarAppearance.hoverBackgroundColor;
        style.normalTextColor = toolbarAppearance.foregroundColor;
        style.hoverTextColor = toolbarAppearance.foregroundColor;
        style.disabledColor = toolbarAppearance.disabledBackgroundColor;
        // Grey the icon/text itself when disabled instead of reusing the
        // background colour, so inactive icons read as greyed-out rather than
        // staying full-strength black.
        style.disabledTextColor = toolbarAppearance.disabledForegroundColor;
        style.useIconAsMask = true;
        if (toolbarAppearance.style == ToolbarStyle::Flat) {
            style.normalColor = Colors::Transparent;
            // Flat toolbars have no button box; keep disabled buttons flat too so
            // inactive icons don't sit on a darker background patch.
            style.disabledColor = Colors::Transparent;
        }
        button->SetStyle(style);
        button->SetIconSize(20, 20);
    }

    void UltraCanvasToolbar::ApplyAppearanceToChildren() {
        for (auto& c : Children()) {
            auto w = std::static_pointer_cast<UltraCanvasUIElement>(c);
            if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(w)) {
                ApplyButtonAppearance(button);
            } else if (auto sep = std::dynamic_pointer_cast<UltraCanvasSeparator>(w)) {
                sep->SetColor(toolbarAppearance.separatorColor);
            }
        }
    }

// ===== LAYOUT =====

    void UltraCanvasToolbar::HandleOverflow() {
        // TODO: Implement overflow handling based on overflowMode
    }

// ===== RENDERING =====

    void UltraCanvasToolbar::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        // Render shadow if enabled (for Docked style)
        if (toolbarAppearance.hasShadow) {
            RenderShadow(ctx);
        }
        // Use base class rendering for background and border
        UltraCanvasContainer::Render(ctx, dirtyRect);

        // Where the items run past an edge, say so over that edge.
        if (overflowMode == ToolbarOverflowMode::Scroll) {
            RenderScrollHints(ctx);
        }

        // Render dock magnification effect if enabled
        if (toolbarAppearance.enableMagnification && hoveredItemIndex >= 0) {
            RenderDockMagnification(ctx);
        }
    }

    bool UltraCanvasToolbar::OnEvent(const UCEvent& event) {
        // Handle auto-hide behavior
        if (toolbarVisibility == ToolbarVisibility::AutoHide || toolbarVisibility == ToolbarVisibility::OnHover) {
            if (event.type == UCEventType::MouseEnter) {
                isHovered = true;
                ShowToolbar();
            } else if (event.type == UCEventType::MouseLeave) {
                isHovered = false;
                HideToolbar();
            }
        }

        // A scrolling toolbar answers the wheel along its own axis. Items that
        // outgrow the box are laid out past its edge (ApplyOverflowToChild
        // stops them shrinking) and the container's scroll offset brings
        // them into view; no scrollbar is shown, the wheel is the control.
        if (overflowMode == ToolbarOverflowMode::Scroll && event.type == UCEventType::MouseWheel) {
            const int step = 40 * -event.wheelDelta;   // one notch, natural direction
            const bool scrolled = (toolbarOrientation == ToolbarOrientation::Vertical)
                    ? ScrollByVertical(step) : ScrollByHorizontal(step);
            if (scrolled) {
                RequestRedraw();
                return true;
            }
        }

        // Handle dragging of the toolbar itself. Item reordering is not this:
        // it is driven by the window-level watch (HandleReorderEvent), because
        // the item's own button consumes the press before it could bubble here.
        if (toolbarDragMode == ToolbarDragMode::Movable || toolbarDragMode == ToolbarDragMode::Both) {
            if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
                BeginDrag(Point2Di(event.pointer.x, event.pointer.y));
                return true;
            } else if (event.type == UCEventType::MouseMove && isDragging) {
                UpdateDrag(Point2Di(event.pointer.x, event.pointer.y));
                return true;
            } else if (event.type == UCEventType::MouseUp && isDragging) {
                EndDrag();
                return true;
            }
        }

        // Track mouse position for magnification
        if (toolbarAppearance.enableMagnification && event.type == UCEventType::MouseMove) {
            mousePosition = Point2Di(event.pointer.x, event.pointer.y);
            CalculateMagnification();
        }

        return UltraCanvasContainer::OnEvent(event);
    }

// ===== AUTO-HIDE =====

    void UltraCanvasToolbar::ShowToolbar() {
        if (toolbarVisibility != ToolbarVisibility::AlwaysVisible) {
            isAutoHidden = false;
            SetVisible(true);
            if (onToolbarShow) {
                onToolbarShow();
            }
        }
    }

    void UltraCanvasToolbar::HideToolbar() {
        if (toolbarVisibility == ToolbarVisibility::AutoHide || toolbarVisibility == ToolbarVisibility::OnHover) {
            isAutoHidden = true;
            SetVisible(false);
            if (onToolbarHide) {
                onToolbarHide();
            }
        }
    }

// ===== DRAG & DROP =====

    void UltraCanvasToolbar::EnableItemReordering(bool enable) {
        if (enable) {
            SetDragMode(toolbarDragMode == ToolbarDragMode::Movable
                        ? ToolbarDragMode::Both : ToolbarDragMode::ReorderItems);
        } else {
            if (toolbarDragMode == ToolbarDragMode::ReorderItems) {
                SetDragMode(ToolbarDragMode::DragNone);
            } else if (toolbarDragMode == ToolbarDragMode::Both) {
                SetDragMode(ToolbarDragMode::Movable);
            }
            reorderCandidate = -1;
            reorderActive = false;
        }
        UpdatePointerFilter();
    }

    void UltraCanvasToolbar::SetWindow(UltraCanvasWindowBase* win) {
        if (win != GetWindow() && pointerFilterInstalled) {
            if (auto* old = GetWindow()) old->UnInstallWindowEventFilter(PointerFilterId());
            pointerFilterInstalled = false;
            reorderCandidate = -1;
            reorderActive = false;
        }
        UltraCanvasContainer::SetWindow(win);
        UpdatePointerFilter();
    }

    std::string UltraCanvasToolbar::PointerFilterId() const {
        std::ostringstream id;
        id << "toolbar.pointer." << GetIdentifier() << '.' << static_cast<const void*>(this);
        return id.str();
    }

    bool UltraCanvasToolbar::NeedsPointerFilter() const {
        return IsItemReorderingEnabled() ||
               (overflowMode == ToolbarOverflowMode::Scroll && scrollHints);
    }

    void UltraCanvasToolbar::UpdatePointerFilter() {
        auto* win = GetWindow();
        const bool needed = win && NeedsPointerFilter();
        if (needed && !pointerFilterInstalled) {
            win->InstallEventFilter(PointerFilterId(),
                                    [this](const UCEvent& e) { return HandlePointerEvent(e); },
                                    {UCEventType::MouseDown, UCEventType::MouseMove, UCEventType::MouseUp});
            pointerFilterInstalled = true;
        } else if (!needed && pointerFilterInstalled) {
            if (win) win->UnInstallWindowEventFilter(PointerFilterId());
            pointerFilterInstalled = false;
            reorderCandidate = -1;
            reorderActive = false;
        }
    }

    bool UltraCanvasToolbar::HandlePointerEvent(const UCEvent& event) {
        return HandleScrollHintEvent(event) || HandleReorderEvent(event);
    }

// ===== SCROLL HINTS =====

    bool UltraCanvasToolbar::ScrollHintsActive() const {
        if (overflowMode != ToolbarOverflowMode::Scroll || !scrollHints) return false;
        const auto& bar = (toolbarOrientation == ToolbarOrientation::Vertical)
                ? verticalScrollbar : horizontalScrollbar;
        return bar && bar->GetMaxScrollPosition() > 0;
    }

    int UltraCanvasToolbar::ScrollHintAtLocal(const Point2Df& local) const {
        if (!ScrollHintsActive()) return 0;
        const Rect2Df b = GetLocalBounds();
        if (!b.Contains(local)) return 0;
        const bool vertical = toolbarOrientation == ToolbarOrientation::Vertical;
        const auto& bar = vertical ? verticalScrollbar : horizontalScrollbar;
        const int pos = bar->GetScrollPosition();
        const int max = bar->GetMaxScrollPosition();
        const float along = vertical ? local.y - b.y : local.x - b.x;
        const float length = vertical ? b.height : b.width;
        if (pos > 0 && along < ScrollHintSize) return -1;
        if (pos < max && along >= length - ScrollHintSize) return +1;
        return 0;
    }

    bool UltraCanvasToolbar::HandleScrollHintEvent(const UCEvent& event) {
        if (event.type != UCEventType::MouseDown || event.button != UCMouseButton::Left) return false;
        if (!IsVisible() || reorderActive || !ScrollHintsActive()) return false;
        const Point2Df local = MapToLocal(Point2Df(static_cast<float>(event.pointerWindow.x),
                                                   static_cast<float>(event.pointerWindow.y)));
        const int direction = ScrollHintAtLocal(local);
        if (direction == 0) return false;
        // A page: what is in view less the hint over its edge, so the item
        // under the chevron is the first one shown after the step.
        const bool vertical = toolbarOrientation == ToolbarOrientation::Vertical;
        const Rect2Di content = GetContentArea();
        const int page = std::max(1, static_cast<int>(
                (vertical ? content.height : content.width) - ScrollHintSize));
        if (vertical) ScrollByVertical(direction * page);
        else          ScrollByHorizontal(direction * page);
        RequestRedraw();
        return true;   // the item under the chevron does not get the press
    }

    void UltraCanvasToolbar::RenderScrollHints(IRenderContext* ctx) {
        if (!ctx || !ScrollHintsActive()) return;
        const bool vertical = toolbarOrientation == ToolbarOrientation::Vertical;
        const auto& bar = vertical ? verticalScrollbar : horizontalScrollbar;
        const int pos = bar->GetScrollPosition();
        const int max = bar->GetMaxScrollPosition();
        const Rect2Df b = GetLocalBounds();
        const double s = ScrollHintSize;

        // The strip covers the clipped edge of the item under it in the
        // toolbar's own colour; the triangle points the way the items go.
        auto chevron = [&](const Rect2Dd& strip, int direction) {
            ctx->SetFillPaint(toolbarAppearance.backgroundColor);
            ctx->FillRectangle(strip);
            const double cx = strip.x + strip.width / 2.0;
            const double cy = strip.y + strip.height / 2.0;
            const double half = 5.0, depth = 3.0;
            ctx->ClearPath();
            if (vertical) {
                const double tip = cy + direction * depth, base = cy - direction * depth;
                ctx->MoveTo(cx - half, base);
                ctx->LineTo(cx, tip);
                ctx->LineTo(cx + half, base);
            } else {
                const double tip = cx + direction * depth, base = cx - direction * depth;
                ctx->MoveTo(base, cy - half);
                ctx->LineTo(tip, cy);
                ctx->LineTo(base, cy + half);
            }
            ctx->ClosePath();
            ctx->SetFillPaint(toolbarAppearance.foregroundColor);
            ctx->FillPathPreserve();
            ctx->ClearPath();
        };
        if (vertical) {
            if (pos > 0)   chevron(Rect2Dd(b.x, b.y, b.width, s), -1);
            if (pos < max) chevron(Rect2Dd(b.x, b.y + b.height - s, b.width, s), +1);
        } else {
            if (pos > 0)   chevron(Rect2Dd(b.x, b.y, s, b.height), -1);
            if (pos < max) chevron(Rect2Dd(b.x + b.width - s, b.y, s, b.height), +1);
        }
    }

    int UltraCanvasToolbar::ItemIndexAtLocal(const Point2Df& local) const {
        // Children's bounds are in this container's frame, before the scroll
        // offset that Render subtracts; put the pointer into the same frame.
        auto* self = const_cast<UltraCanvasToolbar*>(this);
        const Point2Df p(local.x + self->GetHorizontalScrollPosition(),
                         local.y + self->GetVerticalScrollPosition());
        int index = 0;
        for (auto& c : Children()) {
            if (!IsItem(c.get())) continue;
            auto* child = static_cast<UltraCanvasUIElement*>(c.get());
            if (child->IsVisible() && child->GetBounds().Contains(p)) return index;
            ++index;
        }
        return -1;
    }

    bool UltraCanvasToolbar::HandleReorderEvent(const UCEvent& event) {
        if (!IsItemReorderingEnabled() || !IsVisible()) return false;
        // The filter sees every dispatch of a pointer event, with `pointer`
        // already mapped to the target element; the window position is the
        // one frame every dispatch of one event shares.
        const Point2Df local = MapToLocal(Point2Df(static_cast<float>(event.pointerWindow.x),
                                                   static_cast<float>(event.pointerWindow.y)));
        auto* app = UltraCanvasApplication::GetInstance();

        switch (event.type) {
            case UCEventType::MouseDown: {
                if (event.button != UCMouseButton::Left) return false;
                if (reorderActive) return true;
                if (!GetLocalBounds().Contains(local)) { reorderCandidate = -1; return false; }
                const int index = ItemIndexAtLocal(local);
                if (index < 0) { reorderCandidate = -1; return false; }
                auto item = GetWidgetAt(index);
                // Spacers and separators take no events and are not dragged.
                if (!item || !item->Contains(item->MapToLocal(Point2Df(
                        static_cast<float>(event.pointerWindow.x),
                        static_cast<float>(event.pointerWindow.y))))) {
                    reorderCandidate = -1;
                    return false;
                }
                reorderCandidate = index;
                reorderIndex = index;
                reorderStart = event.pointerWindow;
                return false;   // the item still gets its press
            }
            case UCEventType::MouseMove: {
                if (reorderCandidate < 0) return false;
                if (!reorderActive) {
                    const int dx = event.pointerWindow.x - reorderStart.x;
                    const int dy = event.pointerWindow.y - reorderStart.y;
                    const int along = (toolbarOrientation == ToolbarOrientation::Vertical) ? dy : dx;
                    if (std::abs(along) < ReorderDragThreshold) return false;
                    reorderActive = true;
                    // The press becomes a drag: a plain button would otherwise
                    // stay drawn pressed, since the release never reaches it.
                    if (auto button = std::dynamic_pointer_cast<UltraCanvasButton>(GetWidgetAt(reorderIndex))) {
                        if (!button->CanToggle()) button->SetPressed(false);
                    }
                    if (app) app->CaptureMouse(this);
                }
                const int target = ItemIndexAtLocal(local);
                if (target >= 0 && target != reorderIndex) {
                    // Move without the public callback: one notification on
                    // release says where the item ended up.
                    auto saved = std::move(onItemReordered);
                    onItemReordered = nullptr;
                    MoveItem(reorderIndex, target);
                    onItemReordered = std::move(saved);
                    reorderIndex = target;
                }
                return true;
            }
            case UCEventType::MouseUp: {
                if (reorderCandidate < 0) return false;
                const bool wasDragging = reorderActive;
                const int from = reorderCandidate;
                const int to = reorderIndex;
                reorderCandidate = -1;
                reorderIndex = -1;
                reorderActive = false;
                if (!wasDragging) return false;   // a click: the item handles it
                if (app) app->ReleaseMouse();
                if (from != to && onItemReordered) onItemReordered(from, to);
                RequestRedraw();
                return true;
            }
            default:
                return false;
        }
    }

    void UltraCanvasToolbar::BeginDrag(const Point2Di& startPos) {
        isDragging = true;
        dragStartPos = startPos;
        originalPos = Point2Di(static_cast<int>(GetX()), static_cast<int>(GetY()));
    }

    void UltraCanvasToolbar::UpdateDrag(const Point2Di& currentPos) {
        if (!isDragging) return;

        int deltaX = currentPos.x - dragStartPos.x;
        int deltaY = currentPos.y - dragStartPos.y;

        SetPosition(originalPos.x + deltaX, originalPos.y + deltaY);
    }

    void UltraCanvasToolbar::EndDrag() {
        isDragging = false;
    }

// ===== INTERNAL HELPERS =====

    void UltraCanvasToolbar::CreateOverflowMenu() {
        // TODO: Implement overflow menu creation
    }

    void UltraCanvasToolbar::UpdateOverflowButton() {
        // TODO: Implement overflow button update
    }

    void UltraCanvasToolbar::CalculateMagnification() {
        // TODO: Implement magnification calculation for dock-style
    }

    void UltraCanvasToolbar::RenderDockMagnification(IRenderContext* ctx) {
        // TODO: Implement dock magnification rendering
    }

    void UltraCanvasToolbar::RenderShadow(IRenderContext* ctx) {
        // Element-local space — ctx already translated to element origin
        Rect2Di bounds = GetLocalBounds();

        // Draw shadow
        ctx->SetFillPaint(toolbarAppearance.shadowColor);
        ctx->FillRoundedRectangle(Rect2Dd(
                                          toolbarAppearance.shadowOffset.x,
                                          toolbarAppearance.shadowOffset.y,
                                          finalBounds.width,
                                          finalBounds.height
                                  ),
                GetBorderTopWidth()
        );
    }

// ===== TOOLBAR BUILDER IMPLEMENTATION =====

    UltraCanvasToolbarBuilder::UltraCanvasToolbarBuilder(const std::string& identifier) {
        toolbar = std::make_shared<UltraCanvasToolbar>(identifier, 0, 0, 800, 48);
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::SetOrientation(ToolbarOrientation orient) {
        toolbar->SetOrientation(orient);
        ApplyThickness();
        return *this;
    }

    void UltraCanvasToolbarBuilder::ApplyThickness() {
        // The builder makes every toolbar 800 x 48 before it knows the shape.
        // Turned vertical, that 800 px width would stay behind as the floor
        // (which gave Texter's side toolbar half the window). SetBounds in
        // SetDimensions sets no size either, so the floor would never be
        // replaced. Either call sets the thickness again from the numbers the
        // host passed, or the default 48 when it passed none.
        const bool vertical = toolbar->GetOrientation() == ToolbarOrientation::Vertical;
        int thickness = 48;
        if (dimensionsSet) {
            thickness = vertical ? requestedWidth : requestedHeight;
        }
        toolbar->SetThickness(static_cast<float>(thickness));
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::SetToolbarPosition(ToolbarPosition pos) {
        toolbar->SetToolbarPosition(pos);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::SetAppearance(const ToolbarAppearance& app) {
        toolbar->SetAppearance(app);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::SetOverflowMode(ToolbarOverflowMode mode) {
        toolbar->SetOverflowMode(mode);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::SetDimensions(int x, int y, int width, int height) {
        if (x > 0 || y > 0) {
            // Non-zero origin: place the toolbar OUT OF FLOW at (x,y), sized w x h — the
            // runtime equivalent of the (id,x,y,w,h) UltraCanvasUIElement constructor. Without
            // this the toolbar stays an in-flow Static child and the parent's CSS layout stacks
            // it at the top-left, ignoring (x,y) (SetBounds only sets finalBounds, which Arrange
            // overwrites). AbsoluteUI (not plain Absolute) so it also grows an auto-sized parent.
            toolbar->SetElementSize({static_cast<float>(width), static_cast<float>(height)});
            toolbar->layoutItem.SetPositionInsets({ CSSLayout::Dimension::Px(static_cast<float>(y)),
                                                    CSSLayout::Dimension::Auto(),
                                                    CSSLayout::Dimension::Auto(),
                                                    CSSLayout::Dimension::Px(static_cast<float>(x)) });
            toolbar->layoutItem.SetPositionType(CSSLayout::PositionType::AbsoluteUI);
            toolbar->InvalidateLayout();
        } else {
            // Zero origin: in-flow size hint; the parent/HBox controls placement (Texter, presets).
            toolbar->SetBounds(Rect2Di(x, y, width, height));
        }
        dimensionsSet = true;
        requestedWidth = width;
        requestedHeight = height;
        ApplyThickness();
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::AddButton(const std::string& id,
                                                                    const std::string& text,
                                                                    const std::string& icon,
                                                                    std::function<void()> onClick) {
        toolbar->AddButton(id, text, icon, onClick);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::AddToggleButton(const std::string& id,
                                                                          const std::string& text,
                                                                          const std::string& icon,
                                                                          std::function<void(bool)> onToggle) {
        toolbar->AddToggleButton(id, text, icon, onToggle);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::AddDropdownButton(const std::string& id,
                                                                            const std::string& text,
                                                                            const std::vector<std::string>& items,
                                                                            std::function<void(const std::string&)> onSelect) {
        toolbar->AddDropdownButton(id, text, items, onSelect);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::AddSeparator(const std::string& id) {
        toolbar->AddSeparator(id);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::AddSpacer(int size) {
        toolbar->AddSpacer(size);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::AddStretch(float stretch) {
        toolbar->AddStretch(stretch);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::AddLabel(const std::string& id,
                                                                   const std::string& text) {
        toolbar->AddLabel(id, text);
        return *this;
    }

    std::shared_ptr<UltraCanvasToolbar> UltraCanvasToolbarBuilder::Build() {
        return toolbar;
    }

// ===== PRESET TOOLBAR FACTORIES =====

    namespace ToolbarPresets {

        std::shared_ptr<UltraCanvasToolbar> CreateStandardToolbar(const std::string& identifier) {
            return UltraCanvasToolbarBuilder(identifier)
                    .SetOrientation(ToolbarOrientation::Horizontal)
                    .SetAppearance(ToolbarAppearance::Default())
                    .SetDimensions(0, 0, 800, 36)
                    .Build();
        }

        std::shared_ptr<UltraCanvasToolbar> CreateDockStyleToolbar(const std::string& identifier) {
            return UltraCanvasToolbarBuilder(identifier)
                    .SetOrientation(ToolbarOrientation::Horizontal)
                    .SetAppearance(ToolbarAppearance::MacOSDock())
                    .SetDimensions(0, 0, 600, 64)
                    .Build();
        }

        std::shared_ptr<UltraCanvasToolbar> CreateRibbonToolbar(const std::string& identifier) {
            return UltraCanvasToolbarBuilder(identifier)
                    .SetOrientation(ToolbarOrientation::Horizontal)
                    .SetAppearance(ToolbarAppearance::Ribbon())
                    .SetDimensions(0, 0, 1024, 100)
                    .Build();
        }

        std::shared_ptr<UltraCanvasToolbar> CreateSidebarToolbar(const std::string& identifier) {
            return UltraCanvasToolbarBuilder(identifier)
                    .SetOrientation(ToolbarOrientation::Vertical)
                    .SetAppearance(ToolbarAppearance::Sidebar())
                    .SetDimensions(0, 0, 48, 600)
                    .Build();
        }

        std::shared_ptr<UltraCanvasToolbar> CreateStatusBar(const std::string& identifier) {
            return UltraCanvasToolbarBuilder(identifier)
                    .SetOrientation(ToolbarOrientation::Horizontal)
                    .SetAppearance(ToolbarAppearance::StatusBar())
                    .SetToolbarPosition(ToolbarPosition::Bottom)
                    .SetDimensions(0, 0, 1024, 24)
                    .Build();
        }

    } // namespace ToolbarPresets

} // namespace UltraCanvas
