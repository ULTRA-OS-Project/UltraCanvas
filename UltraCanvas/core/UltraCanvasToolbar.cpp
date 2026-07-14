// include/UltraCanvasToolbar.cpp
// Implementation of comprehensive toolbar component
// Version: 1.3.0
// Last Modified: 2026-06-01
// Author: UltraCanvas Framework

#include "UltraCanvasToolbar.h"
#include <algorithm>
#include <cmath>

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
        HandleOverflow();
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
        // may be unmapped).
        for (auto it = widgetMap.begin(); it != widgetMap.end(); ++it) {
            if (it->second == widget) {
                widgetMap.erase(it);
                break;
            }
        }

        RemoveChild(widget);
        InvalidateLayout();
    }

    void UltraCanvasToolbar::ClearItems() {
        widgetMap.clear();
        ClearChildren();
        InvalidateLayout();
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasToolbar::GetWidget(const std::string& identifier) {
        auto it = widgetMap.find(identifier);
        return (it != widgetMap.end()) ? it->second : nullptr;
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasToolbar::GetWidgetAt(int index) {
        const auto& kids = Children();
        if (index < 0 || index >= static_cast<int>(kids.size())) {
            return nullptr;
        }
        return std::static_pointer_cast<UltraCanvasUIElement>(kids[index]);
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
        auto button = std::make_shared<UltraCanvasButton>(id, 0, 0, 32, 32, text);
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

        // Handle dragging
        if (toolbarDragMode != ToolbarDragMode::DragNone) {
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
            SetDragMode(ToolbarDragMode::ReorderItems);
        } else if (toolbarDragMode == ToolbarDragMode::ReorderItems) {
            SetDragMode(ToolbarDragMode::DragNone);
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
        return *this;
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
