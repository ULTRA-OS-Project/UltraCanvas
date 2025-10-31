// core/UltraCanvasDropdown.cpp
// Interactive dropdown/combobox component with icon support and multi-selection
// Version: 2.0.0
// Last Modified: 2025-10-30
// Author: UltraCanvas Framework
#include "UltraCanvasDropdown.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasImageLoader.h"

namespace UltraCanvas {
    const int scrollbarWidth = 12;

    UltraCanvasDropdown::UltraCanvasDropdown(const std::string &identifier, long id, long x, long y,
                                             long w,
                                             long h)
            : UltraCanvasUIElement(identifier, id, x, y, w, h) {
    }

    void UltraCanvasDropdown::AddItem(const std::string &text) {
        items.emplace_back(text);
        needCalculateDimensions = true;
    }

    void UltraCanvasDropdown::AddItem(const std::string &text, const std::string &value) {
        items.emplace_back(text, value);
        needCalculateDimensions = true;
    }

    void UltraCanvasDropdown::AddItem(const std::string &text, const std::string &value, const std::string &iconPath) {
        items.emplace_back(text, value, iconPath);
        needCalculateDimensions = true;
    }

    void UltraCanvasDropdown::AddItem(const DropdownItem &item) {
        items.push_back(item);
        needCalculateDimensions = true;
    }

    void UltraCanvasDropdown::AddSeparator() {
        DropdownItem separator;
        separator.separator = true;
        separator.enabled = false;
        items.push_back(separator);
        needCalculateDimensions = true;
    }

    void UltraCanvasDropdown::ClearItems() {
        items.clear();
        selectedIndex = -1;
        hoveredIndex = -1;
        scrollOffset = 0;
        selectedIndices.clear();
        needCalculateDimensions = true;
    }

    void UltraCanvasDropdown::RemoveItem(int index) {
        if (index >= 0 && index < (int)items.size()) {
            items.erase(items.begin() + index);

            // Update single selection
            if (selectedIndex == index) {
                selectedIndex = -1;
            } else if (selectedIndex > index) {
                selectedIndex--;
            }

            // Update multi-selection indices
            if (multiSelectEnabled) {
                std::set<int> newSelectedIndices;
                for (int idx : selectedIndices) {
                    if (idx < index) {
                        newSelectedIndices.insert(idx);
                    } else if (idx > index) {
                        newSelectedIndices.insert(idx - 1);
                    }
                }
                selectedIndices = newSelectedIndices;
            }

            needCalculateDimensions = true;
        }
    }

    void UltraCanvasDropdown::SetSelectedIndex(int index) {
        if (multiSelectEnabled) {
            // In multi-select mode, use SetItemSelected instead
            return;
        }

        if (index >= -1 && index < (int)items.size()) {
            if (selectedIndex != index) {
                selectedIndex = index;

                if (index >= 0) {
                    EnsureItemVisible(index);
                    if (onSelectionChanged) {
                        onSelectionChanged(index, items[index]);
                    }
                    UCEvent ev;
                    ev.type = UCEventType::DropdownSelect;
                    ev.targetElement = this;
                    ev.userDataInt = index;
                    UltraCanvasApplication::GetInstance()->PushEvent(ev);
                }
            }
        }
    }

    const DropdownItem* UltraCanvasDropdown::GetSelectedItem() const {
        if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
            return &items[selectedIndex];
        }
        return nullptr;
    }

    const DropdownItem* UltraCanvasDropdown::GetItem(int index) const {
        if (index >= 0 && index < (int)items.size()) {
            return &items[index];
        }
        return nullptr;
    }

    // ===== NEW: Multi-selection methods =====

    void UltraCanvasDropdown::SetMultiSelectEnabled(bool enabled) {
        if (multiSelectEnabled != enabled) {
            multiSelectEnabled = enabled;

            if (enabled) {
                // Switch from single to multi-select
                selectedIndices.clear();
                if (selectedIndex >= 0) {
                    selectedIndices.insert(selectedIndex);
                    items[selectedIndex].selected = true;
                }
            } else {
                // Switch from multi to single-select
                if (!selectedIndices.empty()) {
                    selectedIndex = *selectedIndices.begin();
                } else {
                    selectedIndex = -1;
                }

                // Clear multi-select state
                for (auto& item : items) {
                    item.selected = false;
                }
                selectedIndices.clear();
            }

            RequestRedraw();
        }
    }

    void UltraCanvasDropdown::SetItemSelected(int index, bool selected) {
        if (!multiSelectEnabled || index < 0 || index >= (int)items.size()) {
            return;
        }

        if (items[index].separator || !items[index].enabled) {
            return;
        }

        bool changed = false;

        if (selected) {
            if (selectedIndices.insert(index).second) {
                items[index].selected = true;
                changed = true;
            }
        } else {
            if (selectedIndices.erase(index) > 0) {
                items[index].selected = false;
                changed = true;
            }
        }

        if (changed) {
            if (onMultiSelectionChanged) {
                std::vector<int> indices(selectedIndices.begin(), selectedIndices.end());
                onMultiSelectionChanged(indices);
            }

            if (onSelectedItemsChanged) {
                onSelectedItemsChanged(GetSelectedItems());
            }

            RequestRedraw();
        }
    }

    bool UltraCanvasDropdown::IsItemSelected(int index) const {
        if (!multiSelectEnabled) {
            return index == selectedIndex;
        }
        return selectedIndices.find(index) != selectedIndices.end();
    }

    void UltraCanvasDropdown::SelectAll() {
        if (!multiSelectEnabled) {
            return;
        }

        selectedIndices.clear();
        for (int i = 0; i < (int)items.size(); ++i) {
            if (!items[i].separator && items[i].enabled) {
                selectedIndices.insert(i);
                items[i].selected = true;
            }
        }

        if (onMultiSelectionChanged) {
            std::vector<int> indices(selectedIndices.begin(), selectedIndices.end());
            onMultiSelectionChanged(indices);
        }

        if (onSelectedItemsChanged) {
            onSelectedItemsChanged(GetSelectedItems());
        }

        RequestRedraw();
    }

    void UltraCanvasDropdown::DeselectAll() {
        if (!multiSelectEnabled) {
            return;
        }

        selectedIndices.clear();
        for (auto& item : items) {
            item.selected = false;
        }

        if (onMultiSelectionChanged) {
            std::vector<int> empty;
            onMultiSelectionChanged(empty);
        }

        if (onSelectedItemsChanged) {
            std::vector<DropdownItem> empty;
            onSelectedItemsChanged(empty);
        }

        RequestRedraw();
    }

    std::vector<int> UltraCanvasDropdown::GetSelectedIndices() const {
        if (!multiSelectEnabled) {
            if (selectedIndex >= 0) {
                return {selectedIndex};
            }
            return {};
        }
        return std::vector<int>(selectedIndices.begin(), selectedIndices.end());
    }

    std::vector<DropdownItem> UltraCanvasDropdown::GetSelectedItems() const {
        std::vector<DropdownItem> result;

        if (!multiSelectEnabled) {
            if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
                result.push_back(items[selectedIndex]);
            }
        } else {
            for (int idx : selectedIndices) {
                if (idx >= 0 && idx < (int)items.size()) {
                    result.push_back(items[idx]);
                }
            }
        }

        return result;
    }

    void UltraCanvasDropdown::OpenDropdown() {
        if (!dropdownOpen && !items.empty()) {
            dropdownOpen = true;
            CalculateDropdownDimensions();

            // Ensure selected item is visible when opening
            if (selectedIndex >= 0) {
                EnsureItemVisible(selectedIndex);
            }

            if (onDropdownOpened) {
                onDropdownOpened();
            }
            AddThisPopupElementToWindow();
            RequestRedraw();
        }
    }

    void UltraCanvasDropdown::CloseDropdown() {
        if (dropdownOpen) {
            dropdownOpen = false;
            hoveredIndex = -1;

            if (onDropdownClosed) {
                onDropdownClosed();
            }
            RemoveThisPopupElementFromWindow();
            RequestRedraw();
        }
    }

    void UltraCanvasDropdown::SetStyle(const DropdownStyle &newStyle) {
        style = newStyle;
        needCalculateDimensions = true;
        RequestRedraw();
    }

    Rect2Di UltraCanvasDropdown::GetActualBounds() {
        Rect2Di baseBounds = GetBounds();

        if (dropdownOpen) {
            Rect2Di listRect = CalculatePopupPosition();

            int minX = std::min(baseBounds.x, listRect.x);
            int minY = std::min(baseBounds.y, listRect.y);
            int maxX = std::max(baseBounds.x + baseBounds.width, listRect.x + listRect.width);
            int maxY = std::max(baseBounds.y + baseBounds.height, listRect.y + listRect.height);

            return Rect2Di(minX, minY, maxX - minX, maxY - minY);
        }

        return baseBounds;
    }

    void UltraCanvasDropdown::CalculateDropdownDimensions() {
        if (items.empty()) {
            dropdownHeight = 0;
            dropdownWidth = 0;
            needScrollbar = false;
            needCalculateDimensions = false;
            return;
        }

        auto ctx = GetRenderContext();
        if (!ctx) return;

        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.fontSize);

        // Calculate maximum width needed
        int maxTextWidth = 0;
        for (const auto& item : items) {
            if (item.separator) continue;

            int textWidth, textHeight;
            ctx->GetTextLineDimensions(item.text, textWidth, textHeight);

            // Add icon width if present
            if (!item.iconPath.empty()) {
                textWidth += static_cast<int>(style.iconSize + style.iconPadding * 2);
            }

            // Add checkbox width if multi-select
            if (multiSelectEnabled) {
                textWidth += static_cast<int>(style.checkboxSize + style.checkboxPadding * 2);
            }

            maxTextWidth = std::max(maxTextWidth, textWidth);
        }

        // Add padding
        dropdownWidth = maxTextWidth + (int)(style.paddingLeft + style.paddingRight + scrollbarWidth);
        dropdownWidth = std::max(dropdownWidth, GetBounds().width);
        dropdownWidth = std::min(dropdownWidth, style.maxItemWidth);

        // Calculate height
        int visibleItems = std::min((int)items.size(), style.maxVisibleItems);
        maxDropdownHeight = (int)(visibleItems * style.itemHeight + 2);
        dropdownHeight = (int)(std::min((int)items.size(), visibleItems) * style.itemHeight + 2);

        needScrollbar = (int)items.size() > style.maxVisibleItems;
        needCalculateDimensions = false;
    }

    Rect2Di UltraCanvasDropdown::CalculatePopupPosition() {
        if (needCalculateDimensions) {
            CalculateDropdownDimensions();
        }
        Point2Di globalPos = GetPositionInWindow();

        Rect2Di buttonRect = GetBounds();
        Rect2Di listRect(globalPos.x, globalPos.y + buttonRect.height,
                         dropdownWidth, dropdownHeight);
        if (listRect.x + listRect.width > window->GetWidth()) {
            listRect.x = window->GetWidth() - dropdownWidth;
        }
        if (listRect.y + listRect.height > window->GetHeight()) {
            listRect.y -= (buttonRect.height + dropdownHeight);
        }
        return listRect;
    }

    void UltraCanvasDropdown::Render() {
        auto ctx = GetRenderContext();
        if (!ctx) return;

        ctx->PushState();

        RenderButton(ctx);

        ctx->PopState();
    }

    void UltraCanvasDropdown::RenderPopupContent() {
        if (!dropdownOpen || items.empty()) return;

        auto ctx = GetRenderContext();
        if (!ctx) return;

        ctx->PushState();

        Rect2Di listRect = CalculatePopupPosition();

        // Draw shadow
        if (style.hasShadow) {
            Rect2Di shadowRect(
                    listRect.x + style.shadowOffset.x,
                    listRect.y + style.shadowOffset.y,
                    listRect.width,
                    listRect.height
            );
            ctx->DrawFilledRectangle(shadowRect, style.shadowColor, 0.0f, Colors::Transparent);
        }

        // Draw list background and border
        ctx->DrawFilledRectangle(listRect, style.listBackgroundColor, style.borderWidth, style.listBorderColor);

        // Set clipping for items
        ctx->SetClipRect(Rect2Di(listRect.x + 1, listRect.y + 1, listRect.width - 2, listRect.height - 2));

        // Render visible items
        int visibleItems = std::min((int)items.size(), style.maxVisibleItems);
        int startIndex = scrollOffset;
        int endIndex = std::min(startIndex + visibleItems, (int)items.size());

        for (int i = startIndex; i < endIndex; i++) {
            RenderDropdownItem(i, listRect, i - startIndex, ctx);
        }

        ctx->ClearClipRect();

        // Draw scrollbar if needed
        if (needScrollbar) {
            RenderScrollbar(listRect, ctx);
        }

        ctx->PopState();
    }

    std::string UltraCanvasDropdown::GetDisplayText() const {
        if (multiSelectEnabled) {
            int count = static_cast<int>(selectedIndices.size());
            if (count == 0) {
                return "Select items...";
            } else if (count == 1) {
                int idx = *selectedIndices.begin();
                if (idx >= 0 && idx < (int)items.size()) {
                    return items[idx].text;
                }
            } else {
                return std::to_string(count) + " items selected";
            }
        } else {
            if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
                return items[selectedIndex].text;
            }
        }
        return "";
    }

    void UltraCanvasDropdown::RenderButton(IRenderContext *ctx) {
        Rect2Di buttonRect = GetBounds();

        // Determine button state colors
        Color bgColor = style.normalColor;
        Color textColor = style.normalTextColor;
        Color borderColor = style.borderColor;

        if (!IsActive()) {
            bgColor = style.disabledColor;
            textColor = style.disabledTextColor;
        } else if (buttonPressed || dropdownOpen) {
            bgColor = style.pressedColor;
        } else if (IsHovered()) {
            bgColor = style.hoverColor;
        }

        if (IsFocused() && !dropdownOpen) {
            borderColor = style.focusBorderColor;
        }

        // Draw button background
        ctx->DrawFilledRectangle(buttonRect, bgColor, style.borderWidth, borderColor);

        // Get display text
        std::string displayText = GetDisplayText();

        if (!displayText.empty()) {
            ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
            ctx->SetFontSize(style.fontSize);
            ctx->SetTextPaint(textColor);

            int textWidth, textHeight;
            ctx->GetTextLineDimensions(displayText, textWidth, textHeight);

            // Calculate text position
            int textX = buttonRect.x + (int)style.paddingLeft;
            int textY = buttonRect.y + (buttonRect.height - textHeight) / 2;

            // Render icon if single selection and item has icon
            if (!multiSelectEnabled && selectedIndex >= 0 && selectedIndex < (int)items.size()) {
                const auto& item = items[selectedIndex];
                if (!item.iconPath.empty()) {
                    Rect2Di iconRect(
                            textX,
                            buttonRect.y + (buttonRect.height - (int)style.iconSize) / 2,
                            (int)style.iconSize,
                            (int)style.iconSize
                    );
                    RenderItemIcon(item.iconPath, iconRect, ctx);
                    textX += (int)(style.iconSize + style.iconPadding);
                }
            }

            ctx->DrawText(displayText, Point2Di(textX, textY));
        }

        // Draw dropdown arrow
        RenderDropdownArrow(buttonRect, textColor, ctx);

        // Draw focus indicator
        if (IsFocused() && !dropdownOpen) {
            Rect2Di focusRect(buttonRect.x + 1, buttonRect.y + 1,
                              buttonRect.width - 2, buttonRect.height - 2);
            ctx->DrawFilledRectangle(focusRect, Colors::Transparent, 1.0, style.focusBorderColor);
        }
    }

    void UltraCanvasDropdown::RenderDropdownArrow(const Rect2Di &buttonRect, const Color &color, IRenderContext *ctx) {
        float arrowX = buttonRect.x + buttonRect.width - (style.arrowSize + style.arrowSize);
        float arrowY = buttonRect.y + (buttonRect.height - style.arrowSize) / 2 + 2;

        float arrowCenterX = arrowX + style.arrowSize / 2;
        float arrowBottom = arrowY + style.arrowSize / 2;

        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(1.0f);

        // Draw down arrow using lines
        ctx->DrawLine(arrowX, arrowY, arrowCenterX, arrowBottom);
        ctx->DrawLine(arrowCenterX, arrowBottom, arrowX + style.arrowSize, arrowY);
    }

    void UltraCanvasDropdown::RenderDropdownItem(int itemIndex, const Rect2Di &listRect, int visualIndex,
                                                 IRenderContext *ctx) {
        const DropdownItem& item = items[itemIndex];

        float itemY = listRect.y + 1 + visualIndex * style.itemHeight;
        Rect2Di itemRect(listRect.x + 1, itemY, listRect.width - 2, style.itemHeight - 2);

        ctx->PushState();

        if (item.separator) {
            // Draw separator line
            float sepY = itemY + style.itemHeight / 2;
            ctx->SetStrokePaint(style.listBorderColor);
            ctx->DrawLine(Point2Di(itemRect.x + 4, sepY), Point2Di(itemRect.x + itemRect.width - 4, sepY));
            ctx->PopState();
            return;
        }

        // Determine item colors
        Color bgColor = Colors::Transparent;
        Color textColor = item.enabled ? style.normalTextColor : style.disabledTextColor;

        bool isSelected = IsItemSelected(itemIndex);

        if (isSelected && !multiSelectEnabled) {
            bgColor = style.itemSelectedColor;
        } else if (itemIndex == hoveredIndex && item.enabled) {
            bgColor = style.itemHoverColor;
        }

        // Draw item background
        if (bgColor.a > 0) {
            ctx->DrawFilledRectangle(itemRect, bgColor, 0.0f, Colors::Transparent);
        }

        // Set up text rendering
        ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
        ctx->SetFontSize(style.fontSize);
        ctx->SetTextPaint(textColor);

        int currentX = itemRect.x + 4;

        // Render checkbox for multi-select
        if (multiSelectEnabled) {
            Rect2Di checkboxRect(
                    currentX,
                    itemRect.y + (itemRect.height - (int)style.checkboxSize) / 2,
                    (int)style.checkboxSize,
                    (int)style.checkboxSize
            );
            RenderCheckbox(isSelected, checkboxRect, ctx);
            currentX += (int)(style.checkboxSize + style.checkboxPadding);
        }

        // Render icon if present
        if (!item.iconPath.empty()) {
            Rect2Di iconRect(
                    currentX,
                    itemRect.y + (itemRect.height - (int)style.iconSize) / 2,
                    (int)style.iconSize,
                    (int)style.iconSize
            );
            RenderItemIcon(item.iconPath, iconRect, ctx);
            currentX += (int)(style.iconSize + style.iconPadding);
        }

        // Render text
        int textWidth, textHeight;
        ctx->GetTextLineDimensions(item.text, textWidth, textHeight);
        int textY = itemRect.y + (itemRect.height - textHeight) / 2;
        ctx->DrawText(item.text, Point2Di(currentX, textY));

        ctx->PopState();
    }

    void UltraCanvasDropdown::RenderItemIcon(const std::string& iconPath, const Rect2Di& iconRect, IRenderContext* ctx) {
        if (iconPath.empty()) {
            return;
        }

        auto icon = GetImageFromFile(iconPath);
        if (icon && icon->IsValid()) {
            ctx->DrawImage(icon, iconRect);
        }
    }

    void UltraCanvasDropdown::RenderCheckbox(bool checked, const Rect2Di& checkboxRect, IRenderContext* ctx) {
        // Draw checkbox border
        ctx->DrawFilledRectangle(checkboxRect, Colors::White, 1.0f, style.checkboxBorderColor);

        if (checked) {
            // Draw checkmark background
            Rect2Di innerRect(
                    checkboxRect.x + 2,
                    checkboxRect.y + 2,
                    checkboxRect.width - 4,
                    checkboxRect.height - 4
            );
            ctx->DrawFilledRectangle(innerRect, style.checkboxCheckedColor, 0.0f, Colors::Transparent);

            // Draw checkmark
            ctx->SetStrokePaint(style.checkmarkColor);
            ctx->SetStrokeWidth(2.0f);

            int cx = checkboxRect.x + checkboxRect.width / 2;
            int cy = checkboxRect.y + checkboxRect.height / 2;

            // Simple checkmark lines
            ctx->DrawLine(
                    Point2Di(checkboxRect.x + 3, cy),
                    Point2Di(cx - 1, checkboxRect.y + checkboxRect.height - 4)
            );
            ctx->DrawLine(
                    Point2Di(cx - 1, checkboxRect.y + checkboxRect.height - 4),
                    Point2Di(checkboxRect.x + checkboxRect.width - 3, checkboxRect.y + 3)
            );
        }
    }

    void UltraCanvasDropdown::RenderScrollbar(const Rect2Di &listRect, IRenderContext *ctx) {
        int scrollbarX = listRect.x + listRect.width - scrollbarWidth - 1;
        Rect2Di scrollbarRect(scrollbarX, listRect.y + 1, scrollbarWidth, listRect.height - 2);

        // Draw scrollbar background
        ctx->DrawFilledRectangle(scrollbarRect, Color(240, 240, 240, 255), 0.0f, Colors::Transparent);

        // Calculate thumb size and position
        float thumbRatio = (float)style.maxVisibleItems / (float)items.size();
        int thumbHeight = std::max(20, (int)(scrollbarRect.height * thumbRatio));

        float scrollRatio = (float)scrollOffset / (float)(items.size() - style.maxVisibleItems);
        int thumbY = scrollbarRect.y + (int)(scrollRatio * (scrollbarRect.height - thumbHeight));

        Rect2Di thumbRect(scrollbarX + 2, thumbY, scrollbarWidth - 4, thumbHeight);

        // Draw thumb
        ctx->DrawFilledRectangle(thumbRect, Color(180, 180, 180, 255), 0.0f, Colors::Transparent);
    }

    void UltraCanvasDropdown::EnsureItemVisible(int index) {
        if (index < 0 || index >= (int)items.size()) return;

        int visibleItems = std::min((int)items.size(), style.maxVisibleItems);

        if (index < scrollOffset) {
            scrollOffset = index;
        } else if (index >= scrollOffset + visibleItems) {
            scrollOffset = index - visibleItems + 1;
        }

        int maxScroll = std::max(0, (int)items.size() - visibleItems);
        scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
        RequestRedraw();
    }

    int UltraCanvasDropdown::GetItemAtPosition(int x, int y) {
        if (!dropdownOpen) return -1;

        Rect2Di listRect = CalculatePopupPosition();

        // Check if point is within dropdown list bounds
        if (x < listRect.x || x >= listRect.x + listRect.width ||
            y < listRect.y || y >= listRect.y + listRect.height) {
            return -1;
        }

        // Calculate which item was clicked
        int relativeY = y - (int)listRect.y - 1;
        int itemIndex = scrollOffset + relativeY / (int)style.itemHeight;

        // Validate item index
        if (itemIndex >= 0 && itemIndex < (int)items.size()) {
            return itemIndex;
        }

        return -1;
    }

    bool UltraCanvasDropdown::HandleMouseDown(const UCEvent &event) {
        Rect2Di buttonRect = GetBounds();

        if (buttonRect.Contains(event.x, event.y)) {
            // Clicked on button
            buttonPressed = true;
            SetFocus(true);

            if (dropdownOpen) {
                CloseDropdown();
            } else {
                OpenDropdown();
            }
            return true;
        } else if (dropdownOpen) {
            // Clicked somewhere else while dropdown is open
            int itemIndex = GetItemAtPosition(event.windowX, event.windowY);

            if (itemIndex >= 0 && items[itemIndex].enabled && !items[itemIndex].separator) {
                if (multiSelectEnabled) {
                    // Toggle selection in multi-select mode
                    bool currentlySelected = IsItemSelected(itemIndex);
                    SetItemSelected(itemIndex, !currentlySelected);
                    // Don't close dropdown in multi-select mode
                } else {
                    // Single select - select and close
                    SetSelectedIndex(itemIndex);
                    CloseDropdown();
                }
                return true;
            }
            CloseDropdown();
        }
        return false;
    }

    bool UltraCanvasDropdown::HandleMouseUp(const UCEvent &event) {
        buttonPressed = false;
        return false;
    }

    bool UltraCanvasDropdown::HandleMouseMove(const UCEvent &event) {
        if (!dropdownOpen) {
            return false;
        }

        int newHoveredIndex = GetItemAtPosition(event.windowX, event.windowY);

        if (newHoveredIndex != hoveredIndex) {
            hoveredIndex = newHoveredIndex;

            if (hoveredIndex >= 0 && onItemHovered) {
                onItemHovered(hoveredIndex, items[hoveredIndex]);
            }

            RequestRedraw();
        }

        return dropdownOpen;
    }

    void UltraCanvasDropdown::HandleMouseLeave(const UCEvent &event) {
        if (hoveredIndex != -1) {
            hoveredIndex = -1;
            RequestRedraw();
        }
    }

    void UltraCanvasDropdown::HandleKeyDown(const UCEvent &event) {
        if (!dropdownOpen || items.empty()) {
            return;
        }

        int newSelectedIndex = selectedIndex;

        switch (event.virtualKey) {
            case UCKeys::Up:
                if (newSelectedIndex > 0) {
                    newSelectedIndex--;
                    while (newSelectedIndex >= 0 &&
                           (items[newSelectedIndex].separator || !items[newSelectedIndex].enabled)) {
                        newSelectedIndex--;
                    }
                }
                break;

            case UCKeys::Down:
                if (newSelectedIndex < (int)items.size() - 1) {
                    newSelectedIndex++;
                    while (newSelectedIndex < (int)items.size() &&
                           (items[newSelectedIndex].separator || !items[newSelectedIndex].enabled)) {
                        newSelectedIndex++;
                    }
                }
                break;

            case UCKeys::Home:
                newSelectedIndex = 0;
                while (newSelectedIndex < (int)items.size() &&
                       (items[newSelectedIndex].separator || !items[newSelectedIndex].enabled)) {
                    newSelectedIndex++;
                }
                break;

            case UCKeys::End:
                newSelectedIndex = (int)items.size() - 1;
                while (newSelectedIndex >= 0 &&
                       (items[newSelectedIndex].separator || !items[newSelectedIndex].enabled)) {
                    newSelectedIndex--;
                }
                break;

            case UCKeys::Return:
                if (multiSelectEnabled) {
                    // Toggle selection
                    if (selectedIndex >= 0) {
                        bool currentlySelected = IsItemSelected(selectedIndex);
                        SetItemSelected(selectedIndex, !currentlySelected);
                    }
                } else {
                    // Close dropdown
                    CloseDropdown();
                }
                return;

            case UCKeys::Escape:
                CloseDropdown();
                return;

            case UCKeys::Space:
                if (multiSelectEnabled && selectedIndex >= 0) {
                    bool currentlySelected = IsItemSelected(selectedIndex);
                    SetItemSelected(selectedIndex, !currentlySelected);
                }
                return;

            default:
                return;
        }

        if (newSelectedIndex != selectedIndex && newSelectedIndex >= 0 && newSelectedIndex < (int)items.size()) {
            if (!multiSelectEnabled) {
                SetSelectedIndex(newSelectedIndex);
            } else {
                // In multi-select, arrow keys just move the focus, not selection
                selectedIndex = newSelectedIndex;
                EnsureItemVisible(selectedIndex);
                RequestRedraw();
            }
        }
    }

    bool UltraCanvasDropdown::HandleMouseWheel(const UCEvent &event) {
        if (!dropdownOpen || !needScrollbar) {
            return false;
        }

        int delta = event.wheelDelta > 0 ? -1 : 1;
        scrollOffset += delta;

        int visibleItems = std::min((int)items.size(), style.maxVisibleItems);
        int maxScroll = std::max(0, (int)items.size() - visibleItems);
        scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));

        RequestRedraw();
        return true;
    }

    void UltraCanvasDropdown::HandleFocusLost() {
        if (dropdownOpen && !multiSelectEnabled) {
            CloseDropdown();
        }
    }

    bool UltraCanvasDropdown::OnEvent(const UCEvent &event) {
        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);

            case UCEventType::MouseUp:
                return HandleMouseUp(event);

            case UCEventType::MouseMove:
                return HandleMouseMove(event);

            case UCEventType::MouseLeave:
                HandleMouseLeave(event);
                return false;

            case UCEventType::KeyDown:
                HandleKeyDown(event);
                return true;

            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);

            case UCEventType::FocusLost:
                HandleFocusLost();
                return false;

            default:
                return false;
        }
    }
} // namespace UltraCanvas