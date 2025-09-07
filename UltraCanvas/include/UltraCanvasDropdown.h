// UltraCanvasDropdown.h
// Interactive dropdown/combobox component with styling options
// Version: 1.2.4
// Last Modified: 2025-01-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderInterface.h"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <algorithm>

namespace UltraCanvas {

// ===== DROPDOWN ITEM DATA =====
    struct DropdownItem {
        std::string text;
        std::string value;
        std::string iconPath;
        bool enabled = true;
        bool separator = false;
        void* userData = nullptr;

        DropdownItem() = default;
        DropdownItem(const std::string& itemText) : text(itemText), value(itemText) {}
        DropdownItem(const std::string& itemText, const std::string& itemValue)
                : text(itemText), value(itemValue) {}
    };

// ===== DROPDOWN STYLING =====
    struct DropdownStyle {
        // Button appearance
        Color normalColor = Colors::White;
        Color hoverColor = Color(240, 245, 255, 255);
        Color pressedColor = Color(225, 235, 255, 255);
        Color disabledColor = Color(245, 245, 245, 255);
        Color borderColor = Color(180, 180, 180, 255);
        Color focusBorderColor = Color(100, 150, 255, 255);

        // Text colors
        Color normalTextColor = Colors::Black;
        Color disabledTextColor = Color(128, 128, 128, 255);

        // List appearance
        Color listBackgroundColor = Colors::White;
        Color listBorderColor = Color(180, 180, 180, 255);
        Color itemHoverColor = Color(240, 245, 255, 255);
        Color itemSelectedColor = Color(225, 235, 255, 255);

        // Dimensions
        float borderWidth = 1.0f;
        float cornerRadius = 2.0f;
        float paddingLeft = 8.0f;
        float paddingRight = 20.0f;
        float itemHeight = 20.0f;
        int maxVisibleItems = 8;
        float arrowSize = 8.0f;

        // Shadow
        bool hasShadow = true;
        Color shadowColor = Color(0, 0, 0, 80);
        Point2Di shadowOffset = Point2Di(2, 2);

        // Font
        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
    };

// ===== DROPDOWN COMPONENT =====
    class UltraCanvasDropdown : public UltraCanvasElement {
    public:
        // ===== CALLBACKS =====
        std::function<void(int, const DropdownItem&)> onSelectionChanged;
        std::function<void(int, const DropdownItem&)> onItemHovered;
        std::function<void()> onDropdownOpened;
        std::function<void()> onDropdownClosed;

    private:
        std::vector<DropdownItem> items;
        int selectedIndex = -1;
        int hoveredIndex = -1;
        bool dropdownOpen = false;
        bool buttonPressed = false;
        int scrollOffset = 0;

        DropdownStyle style;
        int dropdownHeight = 0;
        int maxDropdownHeight = 0;
        bool needsScrollbar = false;

    public:
        UltraCanvasDropdown(const std::string& identifier, long id, long x, long y, long w, long h = 24)
                : UltraCanvasElement(identifier, id, x, y, w, h) {
            CalculateDropdownHeight();
        }

        virtual ~UltraCanvasDropdown() = default;

        // ===== ITEM MANAGEMENT =====
        void AddItem(const std::string& text) {
            items.emplace_back(text);
            CalculateDropdownHeight();
        }

        void AddItem(const std::string& text, const std::string& value) {
            items.emplace_back(text, value);
            CalculateDropdownHeight();
        }

        void AddItem(const DropdownItem& item) {
            items.push_back(item);
            CalculateDropdownHeight();
        }

        void AddSeparator() {
            DropdownItem separator;
            separator.separator = true;
            separator.enabled = false;
            items.push_back(separator);
            CalculateDropdownHeight();
        }

        void ClearItems() {
            items.clear();
            selectedIndex = -1;
            hoveredIndex = -1;
            scrollOffset = 0;
            CalculateDropdownHeight();
        }

        void RemoveItem(int index) {
            if (index >= 0 && index < (int)items.size()) {
                items.erase(items.begin() + index);
                if (selectedIndex == index) {
                    selectedIndex = -1;
                } else if (selectedIndex > index) {
                    selectedIndex--;
                }
                CalculateDropdownHeight();
            }
        }

        // ===== SELECTION MANAGEMENT =====
        void SetSelectedIndex(int index) {
            if (index >= -1 && index < (int)items.size()) {
                if (selectedIndex != index) {
                    selectedIndex = index;

                    if (index >= 0) {
                        EnsureItemVisible(index);
                    }

                    if (onSelectionChanged && index >= 0) {
                        onSelectionChanged(index, items[index]);
                    }
                }
            }
        }

        int GetSelectedIndex() const {
            return selectedIndex;
        }

        const DropdownItem* GetSelectedItem() const {
            if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
                return &items[selectedIndex];
            }
            return nullptr;
        }

        Rect2Di GetActualBounds() override {
            Rect2Di buttonRect = GetBounds();

            // CRITICAL: If dropdown is open, also check dropdown list area
            if (dropdownOpen) {
                return Rect2Di(buttonRect.x, buttonRect.y + buttonRect.height,
                               buttonRect.width, dropdownHeight);
            } else {
                return buttonRect;
            }
        }

        bool Contains(float px, float py) override {
            return GetActualBounds().Contains(px, py);
        }

// Also override the Point2Di version for consistency
        bool Contains(const Point2Di& point) override {
            return Contains(point.x, point.y);
        }

        // ===== DROPDOWN STATE =====
        void OpenDropdown() {
            if (!dropdownOpen && !items.empty()) {
                dropdownOpen = true;
                hoveredIndex = selectedIndex;
                if (onDropdownOpened) {
                    onDropdownOpened();
                }
                AddThisPopupElementToWindow();
            }
        }

        void CloseDropdown() {
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

        bool IsDropdownOpen() const {
            return dropdownOpen;
        }

        // ===== STYLING =====
        void SetStyle(const DropdownStyle& newStyle) {
            style = newStyle;
            CalculateDropdownHeight();
        }

        const DropdownStyle& GetStyle() const {
            return style;
        }

        // ===== ACCESSORS =====
        const std::vector<DropdownItem>& GetItems() const {
            return items;
        }

        int GetItemCount() const {
            return (int)items.size();
        }

        const DropdownItem* GetItem(int index) const {
            if (index >= 0 && index < (int)items.size()) {
                return &items[index];
            }
            return nullptr;
        }

        // ===== RENDERING =====
        void Render() override {
            if (!IsVisible()) return;

            std::cout << "UCDropdown::Render - dropdownOpen=" << dropdownOpen << " items=" << items.size() << std::endl;

            ULTRACANVAS_RENDER_SCOPE();

            // Render main button
            RenderButton();

//            // Render dropdown list if open
//            if (dropdownOpen) {
//                std::cout << "UCDropdown::Render - calling RenderDropdownList()" << std::endl;
//                RenderDropdownList();
//            } else {
//                std::cout << "UCDropdown::Render - dropdown is closed, not rendering list" << std::endl;
//            }
        }

        void RenderPopupContent() override {
            if (dropdownOpen && !items.empty()) {
                Point2Di globalPos = parentContainer->GetPositionInWindow();

                // Translate to dropdown position
                //ResetTransform();
                Translate(globalPos.x, globalPos.y);

                Rect2Di buttonRect = GetBounds();
                Rect2Di listRect(buttonRect.x, buttonRect.y + buttonRect.height,
                                 buttonRect.width, dropdownHeight);

                // Draw shadow
                if (style.hasShadow) {
                    DrawShadow(listRect, style.shadowColor, style.shadowOffset);
                }

                // Draw list background
                std::cout << "RenderDropdownList UltraCanvas::DrawFilledRect" << std::endl;
                DrawFilledRectangle(listRect, style.listBackgroundColor, style.listBorderColor, 1.0f);

                // Render visible items
                int visibleItems = std::min((int)items.size(), style.maxVisibleItems);
                int startIndex = scrollOffset;
                int endIndex = std::min(startIndex + visibleItems, (int)items.size());

                for (int i = startIndex; i < endIndex; i++) {
                    RenderDropdownItem(i, listRect, i - startIndex);
                }

                // Draw scrollbar if needed
                if (needsScrollbar) {
                    RenderScrollbar(listRect);
                }
            }
        }

        // ===== EVENT HANDLING =====
        bool OnEvent(const UCEvent& event) override {
            if (!IsActive() || !IsVisible()) return false;
            if (event.type != UCEventType::MouseMove) {
                std::cout << "*** UltraCanvasDropdown::OnEvent() called, type: " << (int)event.type << " ***" << std::endl;
            }

            switch (event.type) {
                case UCEventType::MouseDown:
                    return HandleMouseDown(event);
                    break;
                case UCEventType::MouseUp:
                    HandleMouseUp(event);
                    break;
                case UCEventType::MouseMove:
                    HandleMouseMove(event);
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

    private:
        void CalculateDropdownHeight() {
            int visibleItems = std::min((int)items.size(), style.maxVisibleItems);
            maxDropdownHeight = visibleItems * (int)style.itemHeight;
            dropdownHeight = std::min(maxDropdownHeight, (int)items.size() * (int)style.itemHeight);
            needsScrollbar = (int)items.size() > style.maxVisibleItems;
        }

        void RenderButton() {
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
                DrawShadow(buttonRect, style.shadowColor, style.shadowOffset);
            }

            // Draw ONLY the button background (not full screen)
            DrawFilledRectangle(buttonRect, bgColor, borderColor, style.borderWidth);

            // Draw text
            std::string displayText = GetDisplayText();
            if (!displayText.empty()) {
                SetTextColor(textColor);
                SetFont(style.fontFamily, style.fontSize);

                Point2Di textSize = MeasureText(displayText);
                float fontHeight = textSize.y;
                float textX = buttonRect.x + style.paddingLeft;
                float textY = buttonRect.y + (buttonRect.height - fontHeight) / 2;

                DrawText(displayText, Point2Di(textX, textY));
            }

            // Draw dropdown arrow
            RenderDropdownArrow(buttonRect, textColor);

            // Draw focus indicator
            if (IsFocused() && !dropdownOpen) {
                Rect2Di focusRect(buttonRect.x + 1, buttonRect.y + 1,
                                 buttonRect.width - 2, buttonRect.height - 2);
                DrawFilledRectangle(focusRect, Colors::Transparent, style.focusBorderColor, 1.0f);
            }
        }

        void RenderDropdownArrow(const Rect2Di& buttonRect, const Color& color) {
            SetFillColor(color);

            float arrowX = buttonRect.x + buttonRect.width - (style.arrowSize + style.arrowSize);
            float arrowY = buttonRect.y + (buttonRect.height - style.arrowSize) / 2 + 2;

            // Draw triangle pointing down using lines (simpler than polygon)
            float arrowCenterX = arrowX + style.arrowSize / 2;
            float arrowBottom = arrowY + style.arrowSize / 2;

            SetStrokeColor(color);
            SetStrokeWidth(1.0f);

            // Draw down arrow using lines
            DrawLine(arrowX, arrowY, arrowCenterX, arrowBottom);
            DrawLine(arrowCenterX, arrowBottom, arrowX + style.arrowSize, arrowY);
        }

        void RenderDropdownList() {
            if (items.empty()) return;

            ULTRACANVAS_RENDER_SCOPE();

            Rect2Di buttonRect = GetBounds();
            Rect2Di listRect(buttonRect.x, buttonRect.y + buttonRect.height,
                             buttonRect.width, dropdownHeight);

            // Draw shadow
            if (style.hasShadow) {
                DrawShadow(listRect, style.shadowColor, style.shadowOffset);
            }

            // Draw list background
            std::cout << "RenderDropdownList UltraCanvas::DrawFilledRect" << std::endl;
            DrawFilledRectangle(listRect, style.listBackgroundColor, style.listBorderColor, 1.0f);

            // Render visible items
            int visibleItems = std::min((int)items.size(), style.maxVisibleItems);
            int startIndex = scrollOffset;
            int endIndex = std::min(startIndex + visibleItems, (int)items.size());

            for (int i = startIndex; i < endIndex; i++) {
                RenderDropdownItem(i, listRect, i - startIndex);
            }

            // Draw scrollbar if needed
            if (needsScrollbar) {
                RenderScrollbar(listRect);
            }

            ClearClipRect();
        }

        void RenderDropdownItem(int itemIndex, const Rect2Di& listRect, int visualIndex) {
            const DropdownItem& item = items[itemIndex];

            float itemY = listRect.y + 1 + visualIndex * style.itemHeight;
            Rect2Di itemRect(listRect.x + 1, itemY, listRect.width - 2, style.itemHeight);

            std::cout << "RenderDropdownItem: item " << itemIndex << " at " << itemRect.x << "," << itemRect.y
                      << " text=" << item.text << std::endl;

            if (item.separator) {
                // Draw separator line
                float sepY = itemY + style.itemHeight / 2;
                SetStrokeColor(style.listBorderColor);
                DrawLine(Point2Di(itemRect.x + 4, sepY), Point2Di(itemRect.x + itemRect.width - 4, sepY));
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
            DrawFilledRectangle(itemRect, bgColor);

            // Draw text
            if (!item.text.empty()) {
                SetTextColor(textColor);
                SetFont("Arial", 12);

                Point2Di textSize = MeasureText(item.text);
                float fontHeight = textSize.y;

                float textY = itemRect.y + (style.itemHeight - fontHeight) / 2;
                DrawText(item.text, Point2Di(itemRect.x + 8, textY));

                std::cout << "RenderDropdownItem: drew text '" << item.text << "' at "
                          << (itemRect.x + 8) << "," << textY << std::endl;
            }
        }

        void RenderScrollbar(const Rect2Di& listRect) {
            if (!needsScrollbar) return;

            int scrollbarWidth = 12;
            Rect2Di scrollbarRect(listRect.x + listRect.width - scrollbarWidth - 1,
                                 listRect.y + 1, scrollbarWidth, listRect.height - 2);

            // Scrollbar background
            DrawFilledRectangle(scrollbarRect, Color(240, 240, 240, 255));

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
                DrawFilledRectangle(thumbRect, Color(160, 160, 160, 255));
            }
        }

        std::string GetDisplayText() const {
            if (selectedIndex >= 0 && selectedIndex < (int)items.size()) {
                return items[selectedIndex].text;
            }
            return "";
        }

        // FIXED: Complete EnsureItemVisible implementation
        void EnsureItemVisible(int index) {
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

        int GetItemAtPosition(int x, int y) const {
            if (!dropdownOpen) return -1;

            Rect2Di buttonRect = GetBounds();
            Rect2Di listRect(buttonRect.x, buttonRect.y + buttonRect.height,
                             buttonRect.width, dropdownHeight);

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

        // ===== EVENT HANDLERS =====
        bool HandleMouseDown(const UCEvent& event) {
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
                int itemIndex = GetItemAtPosition(event.x, event.y);
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

        void HandleMouseUp(const UCEvent& event) {
            buttonPressed = false;
        }

        void HandleMouseMove(const UCEvent& event) {
            if (dropdownOpen) {
                int newHovered = GetItemAtPosition(event.x, event.y);

                if (newHovered != hoveredIndex) {
                    hoveredIndex = newHovered;

                    if (hoveredIndex >= 0 && onItemHovered) {
                        onItemHovered(hoveredIndex, items[hoveredIndex]);
                    }
                }
            }
        }

        void HandleMouseLeave(const UCEvent& event) {
            if (dropdownOpen) {
                hoveredIndex = -1;
            }
        }

        void HandleKeyDown(const UCEvent& event) {
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

        bool HandleMouseWheel(const UCEvent& event) {
            if (dropdownOpen && needsScrollbar) {
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

        void NavigateItem(int direction) {
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

        void NavigateSelection(int direction) {
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

        int FindFirstEnabledItem() const {
            for (int i = 0; i < (int)items.size(); i++) {
                if (items[i].enabled && !items[i].separator) {
                    return i;
                }
            }
            return -1;
        }

        int FindLastEnabledItem() const {
            for (int i = (int)items.size() - 1; i >= 0; i--) {
                if (items[i].enabled && !items[i].separator) {
                    return i;
                }
            }
            return -1;
        }
    };

// ===== DROPDOWN BUILDER =====
    class DropdownBuilder {
    private:
        std::shared_ptr<UltraCanvasDropdown> dropdown;

    public:
        DropdownBuilder(const std::string& identifier, long x, long y, long w = 150, long h = 24) {
            dropdown = std::make_shared<UltraCanvasDropdown>(identifier, 0, x, y, w, h);
        }

        DropdownBuilder& AddItem(const std::string& text) {
            dropdown->AddItem(text);
            return *this;
        }

        DropdownBuilder& AddItem(const std::string& text, const std::string& value) {
            dropdown->AddItem(text, value);
            return *this;
        }

        DropdownBuilder& AddItems(const std::vector<std::string>& items) {
            for (const std::string& item : items) {
                dropdown->AddItem(item);
            }
            return *this;
        }

        DropdownBuilder& AddSeparator() {
            dropdown->AddSeparator();
            return *this;
        }

        DropdownBuilder& SetStyle(const DropdownStyle& style) {
            dropdown->SetStyle(style);
            return *this;
        }

        DropdownBuilder& SetSelectedIndex(int index) {
            dropdown->SetSelectedIndex(index);
            return *this;
        }

        DropdownBuilder& OnSelectionChanged(std::function<void(int, const DropdownItem&)> callback) {
            dropdown->onSelectionChanged = callback;
            return *this;
        }

        DropdownBuilder& OnDropdownOpened(std::function<void()> callback) {
            dropdown->onDropdownOpened = callback;
            return *this;
        }

        DropdownBuilder& OnDropdownClosed(std::function<void()> callback) {
            dropdown->onDropdownClosed = callback;
            return *this;
        }

        std::shared_ptr<UltraCanvasDropdown> Build() {
            return dropdown;
        }
    };

// ===== PREDEFINED DROPDOWN STYLES =====
    namespace DropdownStyles {
        inline DropdownStyle Default() {
            return DropdownStyle(); // Uses default values
        }

        inline DropdownStyle Flat() {
            DropdownStyle style;
            style.normalColor = Colors::White;
            style.hoverColor = Color(240, 240, 240, 255);
            style.pressedColor = Color(230, 230, 230, 255);
            style.borderColor = Color(200, 200, 200, 255);
            style.cornerRadius = 0.0f;
            style.hasShadow = false;
            return style;
        }

        inline DropdownStyle Modern() {
            DropdownStyle style;
            style.normalColor = Colors::White;
            style.hoverColor = Color(245, 245, 245, 255);
            style.pressedColor = Color(235, 235, 235, 255);
            style.borderColor = Color(180, 180, 180, 255);
            style.focusBorderColor = Color(100, 150, 255, 255);
            style.cornerRadius = 4.0f;
            style.paddingLeft = 12.0f;
            style.paddingRight = 30.0f;
            style.fontSize = 13.0f;
            return style;
        }

        inline DropdownStyle Dark() {
            DropdownStyle style;
            style.normalColor = Color(45, 45, 45, 255);
            style.hoverColor = Color(55, 55, 55, 255);
            style.pressedColor = Color(35, 35, 35, 255);
            style.disabledColor = Color(60, 60, 60, 255);
            style.borderColor = Color(80, 80, 80, 255);
            style.focusBorderColor = Color(100, 150, 255, 255);
            style.normalTextColor = Colors::White;
            style.disabledTextColor = Color(128, 128, 128, 255);
            style.listBackgroundColor = Color(40, 40, 40, 255);
            style.listBorderColor = Color(80, 80, 80, 255);
            style.itemHoverColor = Color(60, 60, 60, 255);
            style.itemSelectedColor = Color(50, 50, 50, 255);
            return style;
        }
    }

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasDropdown> CreateDropdown(
            const std::string& identifier, long id, long x, long y, long w, long h = 24) {
        return std::make_shared<UltraCanvasDropdown>(identifier, id, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasDropdown> CreateAutoDropdown(
            const std::string& identifier, long x, long y, const std::vector<std::string>& items) {
        auto dropdown = std::make_shared<UltraCanvasDropdown>(identifier, 0, x, y, 150, 24);

        for (const std::string& item : items) {
            dropdown->AddItem(item);
        }

        return dropdown;
    }

} // namespace UltraCanvas