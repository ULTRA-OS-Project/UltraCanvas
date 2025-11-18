// include/UltraCanvasToolbar.cpp
// Implementation of comprehensive toolbar component
// Version: 1.1.0
// Last Modified: 2025-11-13
// Author: UltraCanvas Framework

#include "UltraCanvasToolbar.h"
#include "UltraCanvasImageLoader.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

// ===== TOOLBAR SEPARATOR IMPLEMENTATION =====
// Simplified to use UltraCanvasUIElement instead of UltraCanvasContainer

    UltraCanvasToolbarSeparator::UltraCanvasToolbarSeparator(const std::string& id, bool vertical)
            : UltraCanvasToolbarItem(ToolbarItemType::Separator, id)
            , isVertical(vertical) {

        // Create separator widget as a simple UIElement
        widget = std::make_shared<UltraCanvasUIElement>(
                "sep_" + id, 0, 0, 0,
                isVertical ? thickness : length,
                isVertical ? length : thickness
        );

        // Set background color for the separator
        widget->SetBackgroundColor(color);
    }

    void UltraCanvasToolbarSeparator::UpdateAppearance(const ToolbarAppearance& appearance) {
        color = appearance.separatorColor;
        if (widget) {
            widget->SetBackgroundColor(color);
        }
    }

    int UltraCanvasToolbarSeparator::GetPreferredWidth() const {
        return isVertical ? thickness : length;
    }

    int UltraCanvasToolbarSeparator::GetPreferredHeight() const {
        return isVertical ? length : thickness;
    }

// ===== TOOLBAR BUTTON IMPLEMENTATION =====

    UltraCanvasToolbarButton::UltraCanvasToolbarButton(const std::string& id,
                                                       const std::string& txt,
                                                       const std::string& icon)
            : UltraCanvasToolbarItem(ToolbarItemType::Button, id)
            , text(txt)
            , iconPath(icon) {

        // Create button widget
        auto button = std::make_shared<UltraCanvasButton>(
                "btn_" + id, 0, 0, 0, 32, 32
        );
        button->SetText(text);

        if (!iconPath.empty()) {
            button->SetIcon(iconPath);
        }

        widget = button;
    }

    void UltraCanvasToolbarButton::SetText(const std::string& txt) {
        text = txt;
        if (widget) {
            auto button = std::dynamic_pointer_cast<UltraCanvasButton>(widget);
            if (button) {
                button->SetText(text);
            }
        }
    }

    void UltraCanvasToolbarButton::SetIcon(const std::string& icon) {
        iconPath = icon;
        if (widget) {
            auto button = std::dynamic_pointer_cast<UltraCanvasButton>(widget);
            if (button) {
                button->SetIcon(iconPath);
            }
        }
    }

    void UltraCanvasToolbarButton::SetTooltip(const std::string& tip) {
        tooltip = tip;
        if (widget) {
            auto button = std::dynamic_pointer_cast<UltraCanvasButton>(widget);
            if (button) {
                button->SetTooltip(tip);
            }
        }
    }
    void UltraCanvasToolbarButton::SetChecked(bool checked) {
        if (widget) {
            auto button = std::dynamic_pointer_cast<UltraCanvasButton>(widget);
            if (button) {
                button->SetPressed(checked);
            }
        }
    }

    void UltraCanvasToolbarButton::SetToggleMode(bool canToggled) {
        if (widget) {
            auto button = std::dynamic_pointer_cast<UltraCanvasButton>(widget);
            if (button) {
                button->SetCanToggled(canToggled);
            }
        }
    }

    void UltraCanvasToolbarButton::SetOnClick(std::function<void()> callback) {
        onClickCallback = callback;
        if (widget) {
            auto button = std::dynamic_pointer_cast<UltraCanvasButton>(widget);
            if (button) {
                button->onClick = callback;
            }
        }
    }

    void UltraCanvasToolbarButton::SetOnToggle(std::function<void(bool)> callback) {
        onToggleCallback = callback;
        if (widget && isToggle) {
            auto button = std::dynamic_pointer_cast<UltraCanvasButton>(widget);
            if (button) {
                button->onToggle = [this, callback](bool isPressed) {
                    isChecked = isPressed;
                    if (callback) {
                        callback(isChecked);
                    }
                };
            }
        }
    }

    void UltraCanvasToolbarButton::SetBadge(const std::string& text, const Color& color) {
        hasBadge = true;
        badgeText = text;
        badgeColor = color;
    }

    void UltraCanvasToolbarButton::ClearBadge() {
        hasBadge = false;
        badgeText.clear();
    }

    void UltraCanvasToolbarButton::UpdateAppearance(const ToolbarAppearance& appearance) {
        if (widget) {
            auto button = std::dynamic_pointer_cast<UltraCanvasButton>(widget);
            if (button) {
                // Update button appearance based on toolbar appearance
                ButtonStyle style = button->GetStyle();
                style.fontSize = appearance.iconSize == ToolbarIconSize::Small ? 10.0f : 12.0f;
                button->SetStyle(style);
                button->SetIconSize(20, 20);
            }
        }
    }

    int UltraCanvasToolbarButton::GetPreferredWidth() const {
        if (widget) {
            return static_cast<int>(widget->GetWidth());
        }
        return 80;
    }

    int UltraCanvasToolbarButton::GetPreferredHeight() const {
        if (widget) {
            return static_cast<int>(widget->GetHeight());
        }
        return 32;
    }

// ===== TOOLBAR DROPDOWN IMPLEMENTATION =====

    UltraCanvasToolbarDropdown::UltraCanvasToolbarDropdown(const std::string& id,
                                                           const std::string& txt)
            : UltraCanvasToolbarItem(ToolbarItemType::Dropdown, id)
            , text(txt) {

        // Create dropdown widget
        auto dropdown = std::make_shared<UltraCanvasDropdown>(
                "dd_" + id, 0, 0, 0, 120, 24
        );

        widget = dropdown;
    }

    void UltraCanvasToolbarDropdown::SetText(const std::string& txt) {
        text = txt;
    }

    void UltraCanvasToolbarDropdown::AddItem(const std::string& item) {
        items.push_back(item);
        if (widget) {
            auto dropdown = std::dynamic_pointer_cast<UltraCanvasDropdown>(widget);
            if (dropdown) {
                dropdown->AddItem(item);
            }
        }
    }

    void UltraCanvasToolbarDropdown::SetItems(const std::vector<std::string>& itemList) {
        items = itemList;
        if (widget) {
            auto dropdown = std::dynamic_pointer_cast<UltraCanvasDropdown>(widget);
            if (dropdown) {
                dropdown->ClearItems();
                for (const auto& item : items) {
                    dropdown->AddItem(item);
                }
            }
        }
    }

    void UltraCanvasToolbarDropdown::SetSelectedIndex(int index) {
        selectedIndex = index;
        if (widget) {
            auto dropdown = std::dynamic_pointer_cast<UltraCanvasDropdown>(widget);
            if (dropdown && index >= 0 && index < static_cast<int>(items.size())) {
                dropdown->SetSelectedIndex(index);
            }
        }
    }

    void UltraCanvasToolbarDropdown::SetOnSelect(std::function<void(const std::string&)> callback) {
        onSelectCallback = callback;
        if (widget) {
            auto dropdown = std::dynamic_pointer_cast<UltraCanvasDropdown>(widget);
            if (dropdown) {
                dropdown->onSelectionChanged = [this, callback](int index, const DropdownItem& ddItem) {
                    if (index >= 0 && index < static_cast<int>(items.size())) {
                        selectedIndex = index;
                        if (this->onSelectCallback) {
                            this->onSelectCallback(items[index]);
                        }
                    }
                };
            }
        }
    }

    void UltraCanvasToolbarDropdown::UpdateAppearance(const ToolbarAppearance& appearance) {
        // Update dropdown appearance if needed
    }

    int UltraCanvasToolbarDropdown::GetPreferredWidth() const {
        if (widget) {
            return static_cast<int>(widget->GetWidth());
        }
        return 120;
    }

    int UltraCanvasToolbarDropdown::GetPreferredHeight() const {
        if (widget) {
            return static_cast<int>(widget->GetHeight());
        }
        return 24;
    }

// ===== TOOLBAR LABEL IMPLEMENTATION =====

    UltraCanvasToolbarLabel::UltraCanvasToolbarLabel(const std::string& id,
                                                     const std::string& txt)
            : UltraCanvasToolbarItem(ToolbarItemType::Label, id)
            , text(txt) {

        // Create label widget
        auto label = std::make_shared<UltraCanvasLabel>(
                "lbl_" + id, 0, 0, 0, 80, 24
        );
        label->SetText(text);
        label->SetAlignment(alignment);

        widget = label;
    }

    void UltraCanvasToolbarLabel::SetText(const std::string& txt) {
        text = txt;
        if (widget) {
            auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (label) {
                label->SetText(text);
            }
        }
    }

    void UltraCanvasToolbarLabel::SetAlignment(TextAlignment align) {
        alignment = align;
        if (widget) {
            auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (label) {
                label->SetAlignment(alignment);
            }
        }
    }

    void UltraCanvasToolbarLabel::SetTextColor(const Color& color) {
        textColor = color;
        if (widget) {
            auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (label) {
                label->SetTextColor(color);
            }
        }
    }

    void UltraCanvasToolbarLabel::SetFontSize(float size) {
        fontSize = size;
        if (widget) {
            auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (label) {
                label->SetFontSize(size);
            }
        }
    }

    void UltraCanvasToolbarLabel::SetFontWeight(FontWeight weight) {
        fontWeight = weight;
        if (widget) {
            auto label = std::dynamic_pointer_cast<UltraCanvasLabel>(widget);
            if (label) {
                label->SetFontWeight(weight);
            }
        }
    }

    void UltraCanvasToolbarLabel::UpdateAppearance(const ToolbarAppearance& appearance) {
        // Update label appearance if needed
    }

    int UltraCanvasToolbarLabel::GetPreferredWidth() const {
        if (widget) {
            return static_cast<int>(widget->GetWidth());
        }
        return 80;
    }

    int UltraCanvasToolbarLabel::GetPreferredHeight() const {
        if (widget) {
            return static_cast<int>(widget->GetHeight());
        }
        return 24;
    }

// ===== MAIN TOOLBAR IMPLEMENTATION =====

    UltraCanvasToolbar::UltraCanvasToolbar(const std::string& identifier, long id,
                                           long x, long y, long width, long height)
            : UltraCanvasContainer(identifier, id, x, y, width, height) {

        // Set default background color and border
        SetBackgroundColor(appearance.backgroundColor);
        SetBorders(1, Color(180, 180, 180, 255));
        CreateLayout();
    }

    void UltraCanvasToolbar::CreateLayout() {
        // Create box layout based on orientation
        if (orientation == ToolbarOrientation::Vertical) {
            SetPadding(5,3);
            if (!boxLayout) {
                boxLayout = CreateVBoxLayout(this);
            } else {
                boxLayout->SetDirection(BoxLayoutDirection::Vertical);
            }
        } else {
            SetPadding(3,5);
            if (!boxLayout) {
                boxLayout = CreateHBoxLayout(this);
            } else {
                boxLayout->SetDirection(BoxLayoutDirection::Horizontal);
            }
        }
        boxLayout->SetSpacing(static_cast<int>(appearance.itemSpacing));
    }

    void UltraCanvasToolbar::SetOrientation(ToolbarOrientation orient) {
        if (orientation != orient) {
            orientation = orient;
            CreateLayout(); // Recreate layout with new orientation
            InvalidateLayout();
        }
    }

    void UltraCanvasToolbar::SetToolbarPosition(ToolbarPosition pos) {
        position = pos;
        if (onPositionChanged) {
            onPositionChanged(pos);
        }
    }

    void UltraCanvasToolbar::SetStyle(ToolbarStyle st) {
        style = st;

        // Update appearance based on style
        switch (style) {
            case ToolbarStyle::Flat:
                SetAppearance(ToolbarAppearance::Flat());
                break;
            case ToolbarStyle::Docked:
                SetAppearance(ToolbarAppearance::MacOSDock());
                break;
            case ToolbarStyle::Ribbon:
                SetAppearance(ToolbarAppearance::Ribbon());
                break;
            default:
                break;
        }
    }

    void UltraCanvasToolbar::SetAppearance(const ToolbarAppearance& app) {
        appearance = app;

        // Update toolbar container appearance
        SetBackgroundColor(appearance.backgroundColor);

        // Update border based on appearance
        if (style == ToolbarStyle::Flat) {
            SetBorders(0, Colors::Transparent);
        } else if (style == ToolbarStyle::Docked) {
            SetBorders(1, Color(180, 180, 180, 180), 12);
        } else {
            SetBorders(1, Color(180, 180, 180, 255));
        }

        // Update layout spacing
        if (boxLayout) {
            boxLayout->SetSpacing(static_cast<int>(appearance.itemSpacing));
        }

        // Update all items
        UpdateItemAppearances();
    }

    void UltraCanvasToolbar::SetOverflowMode(ToolbarOverflowMode mode) {
        overflowMode = mode;
        HandleOverflow();
    }

    void UltraCanvasToolbar::SetVisibility(ToolbarVisibility vis) {
        visibility = vis;
    }

    void UltraCanvasToolbar::SetDragMode(ToolbarDragMode mode) {
        dragMode = mode;
    }

// ===== ITEM MANAGEMENT =====

    void UltraCanvasToolbar::AddItem(const ToolbarItemDescriptor& descriptor) {
        auto item = CreateToolbarItem(descriptor);
        if (item) {
            AddItem(item);
        }
    }

    void UltraCanvasToolbar::AddItem(std::shared_ptr<UltraCanvasToolbarItem> item) {
        if (!item) return;

        items.push_back(item);
        itemMap[item->GetIdentifier()] = item;

        // Add widget to layout
        if (item->GetWidget() && boxLayout) {
            boxLayout->AddUIElement(item->GetWidget(), 0)->SetCrossAlignment(LayoutAlignment::Center);
        }

        item->UpdateAppearance(appearance);

        if (onItemAdded) {
            onItemAdded(item->GetIdentifier());
        }

        InvalidateLayout();
    }

    void UltraCanvasToolbar::InsertItem(int index, const ToolbarItemDescriptor& descriptor) {
        auto item = CreateToolbarItem(descriptor);
        if (item) {
            InsertItem(index, item);
        }
    }

    void UltraCanvasToolbar::InsertItem(int index, std::shared_ptr<UltraCanvasToolbarItem> item) {
        if (!item || index < 0 || index > static_cast<int>(items.size())) return;

        items.insert(items.begin() + index, item);
        itemMap[item->GetIdentifier()] = item;

        // Add widget to layout at specific index
        if (item->GetWidget() && boxLayout) {
            auto elem = boxLayout->InsertUIElement(item->GetWidget(), index);
            static_cast<UltraCanvasBoxLayoutItem*>(elem)->SetCrossAlignment(LayoutAlignment::Center);
        }

        item->UpdateAppearance(appearance);

        if (onItemAdded) {
            onItemAdded(item->GetIdentifier());
        }

        InvalidateLayout();
    }

    void UltraCanvasToolbar::RemoveItem(const std::string& identifier) {
        auto it = itemMap.find(identifier);
        if (it != itemMap.end()) {
            auto item = it->second;

            // Remove from vector
            items.erase(std::remove(items.begin(), items.end(), item), items.end());

            // Remove from map
            itemMap.erase(it);

            // Remove widget from layout
            if (item->GetWidget() && boxLayout) {
                boxLayout->RemoveUIElement(item->GetWidget());
            }

            if (onItemRemoved) {
                onItemRemoved(identifier);
            }

            InvalidateLayout();
        }
    }

    void UltraCanvasToolbar::RemoveItemAt(int index) {
        if (index >= 0 && index < static_cast<int>(items.size())) {
            auto item = items[index];
            RemoveItem(item->GetIdentifier());
        }
    }

    void UltraCanvasToolbar::ClearItems() {
        items.clear();
        itemMap.clear();

        if (boxLayout) {
            boxLayout->ClearItems();
        }

        InvalidateLayout();
    }

    std::shared_ptr<UltraCanvasToolbarItem> UltraCanvasToolbar::GetItem(const std::string& identifier) {
        auto it = itemMap.find(identifier);
        return (it != itemMap.end()) ? it->second : nullptr;
    }

    std::shared_ptr<UltraCanvasToolbarItem> UltraCanvasToolbar::GetItemAt(int index) {
        if (index >= 0 && index < static_cast<int>(items.size())) {
            return items[index];
        }
        return nullptr;
    }

// ===== CONVENIENCE METHODS =====

    void UltraCanvasToolbar::AddButton(const std::string& id, const std::string& text,
                                       const std::string& icon, std::function<void()> onClick) {
        AddItem(ToolbarItemDescriptor::CreateButton(id, text, icon, onClick));
    }

    void UltraCanvasToolbar::AddToggleButton(const std::string& id, const std::string& text,
                                             const std::string& icon, std::function<void(bool)> onToggle) {
        AddItem(ToolbarItemDescriptor::CreateToggleButton(id, text, icon, onToggle));
    }

    void UltraCanvasToolbar::AddDropdownButton(const std::string& id, const std::string& text,
                                               const std::vector<std::string>& items,
                                               std::function<void(const std::string&)> onSelect) {
        AddItem(ToolbarItemDescriptor::CreateDropdown(id, text, items, onSelect));
    }

    void UltraCanvasToolbar::AddSeparator(const std::string& id) {
        auto sep = std::make_shared<UltraCanvasToolbarSeparator>(
                id.empty() ? "sep_" + std::to_string(items.size()) : id,
                orientation == ToolbarOrientation::Horizontal
        );
        AddItem(sep);
    }

    void UltraCanvasToolbar::AddSpacer(int size) {
        // Use layout's AddSpacing directly instead of creating a widget
        if ( boxLayout) {
            boxLayout->AddSpacing(size);
        }
    }

    void UltraCanvasToolbar::AddStretch(float stretch) {
        // Use layout's AddStretch directly instead of creating a widget
        if (boxLayout) {
            boxLayout->AddStretch(static_cast<int>(stretch));
        }
    }

    void UltraCanvasToolbar::AddLabel(const std::string& id, const std::string& text) {
        AddItem(ToolbarItemDescriptor::CreateLabel(id, text));
    }

    void UltraCanvasToolbar::AddSearchBox(const std::string& id, const std::string& placeholder,
                                          std::function<void(const std::string&)> onTextChange) {
        auto searchBox = std::make_shared<UltraCanvasTextInput>(
                "search_" + id, 0, 0, 0, 150, 24
        );
        searchBox->SetPlaceholder(placeholder);

        if (onTextChange) {
            searchBox->onTextChanged = onTextChange;
        }

        auto item = std::make_shared<UltraCanvasToolbarButton>(id, "", "");
        item->widget = searchBox;
        AddItem(item);
    }

// ===== LAYOUT =====

    void UltraCanvasToolbar::HandleOverflow() {
        // TODO: Implement overflow handling based on overflowMode
    }

// ===== RENDERING =====

    void UltraCanvasToolbar::Render(IRenderContext* ctx) {
        if (!IsVisible()) return;

        ctx->PushState();

        // Render shadow if enabled (for Docked style)
        if (appearance.hasShadow) {
            RenderShadow(ctx);
        }
        // Use base class rendering for background and border
        UltraCanvasContainer::Render(ctx);

        // Render dock magnification effect if enabled
        if (appearance.enableMagnification && hoveredItemIndex >= 0) {
            RenderDockMagnification(ctx);
        }

        ctx->PopState();
    }

    bool UltraCanvasToolbar::OnEvent(const UCEvent& event) {
        // Handle auto-hide behavior
        if (visibility == ToolbarVisibility::AutoHide || visibility == ToolbarVisibility::OnHover) {
            if (event.type == UCEventType::MouseEnter) {
                isHovered = true;
                ShowToolbar();
            } else if (event.type == UCEventType::MouseLeave) {
                isHovered = false;
                HideToolbar();
            }
        }

        // Handle dragging
        if (dragMode != ToolbarDragMode::DragNone) {
            if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
                BeginDrag(Point2Di(event.x, event.y));
                return true;
            } else if (event.type == UCEventType::MouseMove && isDragging) {
                UpdateDrag(Point2Di(event.x, event.y));
                return true;
            } else if (event.type == UCEventType::MouseUp && isDragging) {
                EndDrag();
                return true;
            }
        }

        // Track mouse position for magnification
        if (appearance.enableMagnification && event.type == UCEventType::MouseMove) {
            mousePosition = Point2Di(event.x, event.y);
            CalculateMagnification();
        }

        return UltraCanvasContainer::OnEvent(event);
    }

// ===== AUTO-HIDE =====

    void UltraCanvasToolbar::ShowToolbar() {
        if (visibility != ToolbarVisibility::AlwaysVisible) {
            isAutoHidden = false;
            SetVisible(true);
            if (onToolbarShow) {
                onToolbarShow();
            }
        }
    }

    void UltraCanvasToolbar::HideToolbar() {
        if (visibility == ToolbarVisibility::AutoHide || visibility == ToolbarVisibility::OnHover) {
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
        } else if (dragMode == ToolbarDragMode::ReorderItems) {
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

    void UltraCanvasToolbar::UpdateItemAppearances() {
        for (auto& item : items) {
            item->UpdateAppearance(appearance);
        }
    }

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
        Rect2Di bounds = GetBounds();

        // Draw shadow
        ctx->SetFillPaint(appearance.shadowColor);
        ctx->FillRoundedRectangle(
                bounds.x + appearance.shadowOffset.x,
                bounds.y + appearance.shadowOffset.y,
                bounds.width,
                bounds.height,
                static_cast<int>(GetBorderTopWidth())
        );
    }

    std::shared_ptr<UltraCanvasToolbarItem> UltraCanvasToolbar::CreateToolbarItem(
            const ToolbarItemDescriptor& descriptor) {

        std::shared_ptr<UltraCanvasToolbarItem> item;

        switch (descriptor.type) {
            case ToolbarItemType::Button:
            case ToolbarItemType::ToggleButton: {
                auto button = std::make_shared<UltraCanvasToolbarButton>(
                        descriptor.identifier,
                        descriptor.text,
                        descriptor.iconPath
                );
                button->SetToggleMode(descriptor.isToggle);
                button->SetChecked(descriptor.isChecked);
                button->SetEnabled(descriptor.isEnabled);
                button->SetVisible(descriptor.isVisible);
                button->SetVisibilityPriority(descriptor.visibilityPriority);
                button->SetTooltip(descriptor.tooltip);

                if (descriptor.onClick) {
                    button->SetOnClick(descriptor.onClick);
                }
                if (descriptor.onToggle) {
                    button->SetOnToggle(descriptor.onToggle);
                }
                if (descriptor.hasBadge) {
                    button->SetBadge(descriptor.badgeText, descriptor.badgeColor);
                }

                item = button;
                break;
            }

            case ToolbarItemType::Dropdown: {
                auto dropdown = std::make_shared<UltraCanvasToolbarDropdown>(
                        descriptor.identifier,
                        descriptor.text
                );
                dropdown->SetItems(descriptor.dropdownItems);
                if (descriptor.onDropdownSelect) {
                    dropdown->SetOnSelect(descriptor.onDropdownSelect);
                }
                item = dropdown;
                break;
            }

            case ToolbarItemType::Separator: {
                bool vertical = (orientation == ToolbarOrientation::Horizontal);
                item = std::make_shared<UltraCanvasToolbarSeparator>(
                        descriptor.identifier,
                        vertical
                );
                break;
            }

            case ToolbarItemType::Spacer: {
                // Spacers are now handled directly by AddSpacing/AddStretch
                // This case should not be reached in normal usage
                break;
            }

            case ToolbarItemType::Label: {
                auto label = std::make_shared<UltraCanvasToolbarLabel>(
                        descriptor.identifier,
                        descriptor.text
                );
                item = label;
                break;
            }

            default:
                break;
        }

        return item;
    }

// ===== TOOLBAR BUILDER IMPLEMENTATION =====

    UltraCanvasToolbarBuilder::UltraCanvasToolbarBuilder(const std::string& identifier, long id) {
        toolbar = std::make_shared<UltraCanvasToolbar>(identifier, id, 0, 0, 800, 48);
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::SetOrientation(ToolbarOrientation orient) {
        toolbar->SetOrientation(orient);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::SetToolbarPosition(ToolbarPosition pos) {
        toolbar->SetToolbarPosition(pos);
        return *this;
    }

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::SetStyle(ToolbarStyle style) {
        toolbar->SetStyle(style);
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
        toolbar->SetBounds(Rect2Di(x, y, width, height));
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

    UltraCanvasToolbarBuilder& UltraCanvasToolbarBuilder::AddFlexSpacer(float stretch) {
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
                    .SetStyle(ToolbarStyle::Standard)
                    .SetDimensions(0, 0, 800, 36)
                    .Build();
        }

        std::shared_ptr<UltraCanvasToolbar> CreateDockStyleToolbar(const std::string& identifier) {
            return UltraCanvasToolbarBuilder(identifier)
                    .SetOrientation(ToolbarOrientation::Horizontal)
                    .SetStyle(ToolbarStyle::Docked)
                    .SetAppearance(ToolbarAppearance::MacOSDock())
                    .SetDimensions(0, 0, 600, 64)
                    .Build();
        }

        std::shared_ptr<UltraCanvasToolbar> CreateRibbonToolbar(const std::string& identifier) {
            return UltraCanvasToolbarBuilder(identifier)
                    .SetOrientation(ToolbarOrientation::Horizontal)
                    .SetStyle(ToolbarStyle::Ribbon)
                    .SetAppearance(ToolbarAppearance::Ribbon())
                    .SetDimensions(0, 0, 1024, 100)
                    .Build();
        }

        std::shared_ptr<UltraCanvasToolbar> CreateSidebarToolbar(const std::string& identifier) {
            return UltraCanvasToolbarBuilder(identifier)
                    .SetOrientation(ToolbarOrientation::Vertical)
                    .SetStyle(ToolbarStyle::Sidebar)
                    .SetDimensions(0, 0, 48, 600)
                    .Build();
        }

        std::shared_ptr<UltraCanvasToolbar> CreateStatusBar(const std::string& identifier) {
            return UltraCanvasToolbarBuilder(identifier)
                    .SetOrientation(ToolbarOrientation::Horizontal)
                    .SetStyle(ToolbarStyle::StatusBar)
                    .SetToolbarPosition(ToolbarPosition::Bottom)
                    .SetDimensions(0, 0, 1024, 24)
                    .Build();
        }

    } // namespace ToolbarPresets

} // namespace UltraCanvas