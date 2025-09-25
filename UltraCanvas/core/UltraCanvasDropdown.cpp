// code/UltraCanvasDropdown.cpp
// Interactive dropdown/combobox component with styling options
// Version: 1.2.4
// Last Modified: 2025-01-17
// Author: UltraCanvas Framework
#include "UltraCanvasDropdown.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasApplication.h"

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
        needCalculateDimensions = true;
    }

    void UltraCanvasDropdown::RemoveItem(int index) {
        if (index >= 0 && index < (int)items.size()) {
            items.erase(items.begin() + index);
            if (selectedIndex == index) {
                selectedIndex = -1;
            } else if (selectedIndex > index) {
                selectedIndex--;
            }
            needCalculateDimensions = true;
        }
    }

    void UltraCanvasDropdown::SetSelectedIndex(int index) {
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

    const DropdownItem *UltraCanvasDropdown::GetSelectedItem() const {
        if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
            return &items[selectedIndex];
        }
        return nullptr;
    }

    Rect2Di UltraCanvasDropdown::GetActualBounds() {
        Rect2Di buttonRect = GetBounds();

        // CRITICAL: If dropdown is open, also check dropdown list area
        if (dropdownOpen) {
            return Rect2Di(buttonRect.x, buttonRect.y + buttonRect.height,
                           dropdownWidth, dropdownHeight);
        } else {
            return buttonRect;
        }
    }

    void UltraCanvasDropdown::OpenDropdown() {
        if (!dropdownOpen && !items.empty()) {
            dropdownOpen = true;
            hoveredIndex = selectedIndex;
            if (onDropdownOpened) {
                onDropdownOpened();
            }
            AddThisPopupElementToWindow();
        }
    }

    void UltraCanvasDropdown::CloseDropdown() {
        if (dropdownOpen) {
            dropdownOpen = false;
            hoveredIndex = -1;
            buttonPressed = false;
            if (onDropdownClosed) {
                onDropdownClosed();
            }
            RemoveThisPopupElementFromWindow();
        }
    }

    void UltraCanvasDropdown::SetStyle(const DropdownStyle &newStyle) {
        style = newStyle;
        needCalculateDimensions = true;
    }

    const DropdownItem *UltraCanvasDropdown::GetItem(int index) const {
        if (index >= 0 && index < (int)items.size()) {
            return &items[index];
        }
        return nullptr;
    }

    void UltraCanvasDropdown::Render() {
        auto ctx = GetRenderContext();
        if (!IsVisible() || !ctx) return;
        std::cout << "UCDropdown::Render - dropdownOpen=" << dropdownOpen << " items=" << items.size() << std::endl;

        ctx->PushState();

        // Render main button
        RenderButton(ctx);

        ctx->PopState();

//            // Render dropdown list if open
//            if (dropdownOpen) {
//                std::cout << "UCDropdown::Render - calling RenderDropdownList()" << std::endl;
//                RenderDropdownList();
//            } else {
//                std::cout << "UCDropdown::Render - dropdown is closed, not rendering list" << std::endl;
//            }
    }

    Rect2Di UltraCanvasDropdown::CalculatePopupPosition() {
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

    void UltraCanvasDropdown::RenderPopupContent() {
        auto ctx = GetRenderContext();
        if (ctx && dropdownOpen && !items.empty()) {
            if (needCalculateDimensions) {
                CalculateDropdownDimensions();
            }
            ctx->PushState();

            Rect2Di listRect = CalculatePopupPosition();


            // Draw shadow
            if (style.hasShadow) {
                ctx->DrawShadow(listRect, style.shadowColor, style.shadowOffset);
            }

            // Draw list background
            std::cout << "RenderDropdownList UltraCanvas::DrawFilledRect" << std::endl;
            ctx->DrawFilledRectangle(listRect, style.listBackgroundColor, style.listBorderColor, 1.0f);

            // Render visible items
            int visibleItems = std::min((int)items.size(), style.maxVisibleItems);
            int startIndex = scrollOffset;
            int endIndex = std::min(startIndex + visibleItems, (int)items.size());

            for (int i = startIndex; i < endIndex; i++) {
                RenderDropdownItem(i, listRect, i - startIndex, ctx);
            }

            // Draw scrollbar if needed
            if (needScrollbar) {
                RenderScrollbar(listRect, ctx);
            }
            ctx->PopState();
        }
    }

    bool UltraCanvasDropdown::OnEvent(const UCEvent &event) {
        if (!IsActive() || !IsVisible()) return false;
        if (event.type != UCEventType::MouseMove) {
            std::cout << "*** UltraCanvasDropdown::OnEvent() called, type: " << (int)event.type << " ***" << std::endl;
        }

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
                break;
            case UCEventType::MouseUp:
                return HandleMouseUp(event);
                break;
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
                break;
            case UCEventType::MouseLeave:
                HandleMouseLeave(event);
                break;
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);
                break;
            case UCEventType::FocusLost:
                if (dropdownOpen) {
                    CloseDropdown();
                }
                break;
        }
        return false;
    }

    void UltraCanvasDropdown::CalculateDropdownDimensions() {
        if (!window) return;
        auto ctx = window->GetRenderContext();
        int visibleItems = std::min((int)items.size(), style.maxVisibleItems);
        maxDropdownHeight = visibleItems * (int)style.itemHeight;
        dropdownHeight = std::min(maxDropdownHeight, (int)items.size() * (int)style.itemHeight);
        needScrollbar = (int)items.size() > style.maxVisibleItems;
        dropdownWidth = properties.width_size;
        for(auto it = items.begin(); it < items.end(); it++) {
            if (!it->separator) {
                Point2Di textSize = ctx->MeasureText(it->text);
                dropdownWidth = std::max(dropdownWidth, std::min(textSize.x + scrollbarWidth + 12, style.maxItemWidth));
            }
        }
        needCalculateDimensions = false;
    }

    void UltraCanvasDropdown::RenderButton(IRenderContext *ctx) {
        Rect2Di buttonRect = GetBounds();

        // Determine button state and colors
        Color bgColor = style.normalColor;
        Color textColor = style.normalTextColor;
        Color borderColor = style.borderColor;

        if (!IsEnabled()) {
            bgColor = style.disabledColor;
            textColor = style.disabledTextColor;
        } else if (buttonPressed || dropdownOpen) {
            bgColor = style.pressedColor;
        } else if (IsHovered() || IsFocused()) {
            bgColor = style.hoverColor;
            if (IsFocused()) {
                borderColor = style.focusBorderColor;
            }
        }

        // Draw shadow
        if (style.hasShadow && !dropdownOpen) {
            ctx->DrawShadow(buttonRect, style.shadowColor, style.shadowOffset);
        }

        // Draw ONLY the button background (not full screen)
        ctx->DrawFilledRectangle(buttonRect, bgColor, borderColor, style.borderWidth);

        // Draw text
        std::string displayText = GetDisplayText();
        if (!displayText.empty()) {
            ctx->SetTextColor(textColor);
            ctx->SetFont(style.fontFamily, style.fontSize);

            Point2Di textSize = ctx->MeasureText(displayText);
            float fontHeight = textSize.y;
            float textX = buttonRect.x + style.paddingLeft;
            float textY = buttonRect.y + (buttonRect.height - fontHeight) / 2;

            ctx->DrawText(displayText, Point2Di(textX, textY));
        }

        // Draw dropdown arrow
        RenderDropdownArrow(buttonRect, textColor, ctx);

        // Draw focus indicator
        if (IsFocused() && !dropdownOpen) {
            Rect2Di focusRect(buttonRect.x + 1, buttonRect.y + 1,
                              buttonRect.width - 2, buttonRect.height - 2);
            ctx->DrawFilledRectangle(focusRect, Colors::Transparent, style.focusBorderColor, 1.0f);
        }
    }

    void UltraCanvasDropdown::RenderDropdownArrow(const Rect2Di &buttonRect, const Color &color, IRenderContext *ctx) {
        ctx->SetFillColor(color);

        float arrowX = buttonRect.x + buttonRect.width - (style.arrowSize + style.arrowSize);
        float arrowY = buttonRect.y + (buttonRect.height - style.arrowSize) / 2 + 2;

        // Draw triangle pointing down using lines (simpler than polygon)
        float arrowCenterX = arrowX + style.arrowSize / 2;
        float arrowBottom = arrowY + style.arrowSize / 2;

        ctx->SetStrokeColor(color);
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
        //ctx->SetClipRect(itemRect);
        std::cout << "RenderDropdownItem: item " << itemIndex << " at " << itemRect.x << "," << itemRect.y
                  << " text=" << item.text << std::endl;

        if (item.separator) {
            // Draw separator line
            float sepY = itemY + style.itemHeight / 2;
            ctx->SetStrokeColor(style.listBorderColor);
            ctx->DrawLine(Point2Di(itemRect.x + 4, sepY), Point2Di(itemRect.x + itemRect.width - 4, sepY));
            return;
        }

        // Determine item colors - simplified
        Color bgColor = Colors::White;
        Color textColor = Colors::Black;

        if (itemIndex == selectedIndex) {
            bgColor = Color(200, 220, 255, 255); // Light blue for selected
        } else if (itemIndex == hoveredIndex && item.enabled) {
            bgColor = Color(240, 240, 240, 255); // Light gray for hover
        }

        // Draw item background
        ctx->DrawFilledRectangle(itemRect, bgColor);

        // Draw text
        if (!item.text.empty()) {
            ctx->SetTextColor(textColor);
            ctx->SetFont("Arial", 12);

            Point2Di textSize = ctx->MeasureText(item.text);
            float fontHeight = textSize.y;

            float textY = itemRect.y + (style.itemHeight - fontHeight) / 2;
            ctx->DrawTextInRect(item.text, Rect2Di(itemRect.x + 8, textY, itemRect.width - 8, style.itemHeight));

            std::cout << "RenderDropdownItem: drew text '" << item.text << "' at "
                      << (itemRect.x + 8) << "," << textY << std::endl;
        }
        ctx->PopState();
    }

    void UltraCanvasDropdown::RenderScrollbar(const Rect2Di &listRect, IRenderContext *ctx) {
        if (!needScrollbar) return;

        Rect2Di scrollbarRect(listRect.x + listRect.width - scrollbarWidth - 1,
                              listRect.y + 1, scrollbarWidth, listRect.height - 2);

        // Scrollbar background
        ctx->DrawFilledRectangle(scrollbarRect, Color(240, 240, 240, 255));

        // Calculate thumb size and position
        int totalItems = (int)items.size();
        int visibleItems = style.maxVisibleItems;

        if (totalItems > visibleItems) {
            float thumbHeight = (float)visibleItems / totalItems * scrollbarRect.height;
            thumbHeight = std::max(thumbHeight, 20.0f);

            float thumbY = scrollbarRect.y +
                           ((float)scrollOffset / (totalItems - visibleItems)) *
                           (scrollbarRect.height - thumbHeight);

            Rect2Di thumbRect(scrollbarRect.x + 2, thumbY, scrollbarWidth - 4, thumbHeight);
            ctx->DrawFilledRectangle(thumbRect, Color(160, 160, 160, 255));
        }
    }

    std::string UltraCanvasDropdown::GetDisplayText() const {
        if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
            return items[selectedIndex].text;
        }
        return "";
    }

    void UltraCanvasDropdown::EnsureItemVisible(int index) {
        if (index < 0 || index >= (int)items.size()) return;

        int visibleItems = style.maxVisibleItems;

        // If item is above visible area, scroll up to show it
        if (index < scrollOffset) {
            scrollOffset = index;
        }
            // If item is below visible area, scroll down to show it
        else if (index >= scrollOffset + visibleItems) {
            scrollOffset = index - visibleItems + 1;
        }

        // Clamp scroll offset to valid range
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

        std::cout << "UCDropdown::HandleMouseDown x=" << event.x << " y=" << event.y
                  << " dropdownOpen=" << dropdownOpen << std::endl;

        if (buttonRect.Contains(event.x, event.y)) {
            // Clicked on button
            std::cout << "UCDropdown::HandleMouseDown - clicked on button" << std::endl;
            buttonPressed = true;
            SetFocus(true);

            if (dropdownOpen) {
                std::cout << "UCDropdown::HandleMouseDown - dropdown was open, closing" << std::endl;
                CloseDropdown();
            } else {
                std::cout << "UCDropdown::HandleMouseDown - dropdown was closed, opening" << std::endl;
                OpenDropdown();
            }
            return true;
        } else if (dropdownOpen) {
            // Clicked somewhere else while dropdown is open
            std::cout << "UCDropdown::HandleMouseDown - clicked outside while open" << std::endl;
            int itemIndex = GetItemAtPosition(event.windowX, event.windowY);
            std::cout << "UCDropdown::HandleMouseDown - item index=" << itemIndex << std::endl;

            if (itemIndex >= 0 && items[itemIndex].enabled && !items[itemIndex].separator) {
                std::cout << "UCDropdown::HandleMouseDown - selecting item " << itemIndex << std::endl;
                SetSelectedIndex(itemIndex);
                CloseDropdown();
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
        if (dropdownOpen) {
            int newHovered = GetItemAtPosition(event.windowX, event.windowY);

            if (newHovered != hoveredIndex) {
                hoveredIndex = newHovered;
                RequestRedraw();
                if (hoveredIndex >= 0 && onItemHovered) {
                    onItemHovered(hoveredIndex, items[hoveredIndex]);
                    return true;
                }
            }
        }
        return false;
    }

    void UltraCanvasDropdown::HandleMouseLeave(const UCEvent &event) {
        if (dropdownOpen) {
            hoveredIndex = -1;
        }
    }

    void UltraCanvasDropdown::HandleKeyDown(const UCEvent &event) {
        if (!IsFocused()) return;

        switch (event.virtualKey) {
            case UCKeys::Return:
            case UCKeys::Space:
                if (dropdownOpen) {
                    if (hoveredIndex >= 0 && hoveredIndex < (int)items.size() &&
                        items[hoveredIndex].enabled && !items[hoveredIndex].separator) {
                        SetSelectedIndex(hoveredIndex);
                    }
                    CloseDropdown();
                } else {
                    OpenDropdown();
                }
                break;

            case UCKeys::Escape:
                if (dropdownOpen) {
                    CloseDropdown();
                }
                break;

            case UCKeys::Up:
                if (dropdownOpen) {
                    NavigateItem(-1);
                } else {
                    NavigateSelection(-1);
                }
                break;

            case UCKeys::Down:
                if (dropdownOpen) {
                    NavigateItem(1);
                } else {
                    NavigateSelection(1);
                }
                break;

            case UCKeys::Home:
                if (dropdownOpen) {
                    hoveredIndex = FindFirstEnabledItem();
                    EnsureItemVisible(hoveredIndex);
                }
                break;

            case UCKeys::End:
                if (dropdownOpen) {
                    hoveredIndex = FindLastEnabledItem();
                    EnsureItemVisible(hoveredIndex);
                }
                break;
        }
    }

    bool UltraCanvasDropdown::HandleMouseWheel(const UCEvent &event) {
        if (dropdownOpen && needScrollbar) {
            // Determine scroll direction (negative wheelDelta = scroll down)
            int scrollDirection = (event.wheelDelta > 0) ? -1 : 1;
            int scrollAmount = 3; // Scroll 3 items per wheel notch

            int newScrollOffset = scrollOffset + (scrollDirection * scrollAmount);
            int maxScroll = std::max(0, (int)items.size() - style.maxVisibleItems);

            scrollOffset = std::max(0, std::min(newScrollOffset, maxScroll));
            RequestRedraw();
            return true;
        }
        return false;
    }

    void UltraCanvasDropdown::NavigateItem(int direction) {
        if (!dropdownOpen || items.empty()) return;

        int newIndex = hoveredIndex;

        do {
            newIndex += direction;
            if (newIndex < 0) {
                newIndex = (int)items.size() - 1;
            } else if (newIndex >= (int)items.size()) {
                newIndex = 0;
            }
        } while (!items[newIndex].enabled || items[newIndex].separator);

        hoveredIndex = newIndex;
        EnsureItemVisible(hoveredIndex);
    }

    void UltraCanvasDropdown::NavigateSelection(int direction) {
        if (items.empty()) return;

        int newIndex = selectedIndex;

        do {
            newIndex += direction;
            if (newIndex < 0) {
                newIndex = (int)items.size() - 1;
            } else if (newIndex >= (int)items.size()) {
                newIndex = 0;
            }
        } while (!items[newIndex].enabled || items[newIndex].separator);

        SetSelectedIndex(newIndex);
    }

    int UltraCanvasDropdown::FindFirstEnabledItem() const {
        for (int i = 0; i < (int)items.size(); i++) {
            if (items[i].enabled && !items[i].separator) {
                return i;
            }
        }
        return -1;
    }

    int UltraCanvasDropdown::FindLastEnabledItem() const {
        for (int i = (int)items.size() - 1; i >= 0; i--) {
            if (items[i].enabled && !items[i].separator) {
                return i;
            }
        }
        return -1;
    }
}