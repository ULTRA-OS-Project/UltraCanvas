// UltraCanvasSlider.h
// Interactive slider control with multiple styles and value display options
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <functional>
#include <cmath>

namespace UltraCanvas {

// ===== SLIDER DEFINITIONS =====
enum class SliderStyle {
    Horizontal,     // Classic horizontal bar
    Vertical,       // Classic vertical bar
    Circular,       // Circular/knob style
    Progress,       // Progress bar style
    Range          // Range slider with two handles
};

enum class SliderValueDisplay {
    NoDisplay,          // No value display
    Number,        // Numeric value
    Percentage,    // Percentage display
    Tooltip,       // Show on hover
    Always         // Always visible
};

enum class SliderOrientation {
    Horizontal, Vertical
};

// ===== SLIDER COMPONENT =====
class UltraCanvasSlider : public UltraCanvasElement {
private:
    // ===== STANDARD PROPERTIES (REQUIRED) =====
    StandardProperties properties;
    
    // ===== SLIDER PROPERTIES =====
    float minValue = 0.0f;
    float maxValue = 100.0f;
    float currentValue = 0.0f;
    float step = 1.0f;
    SliderStyle sliderStyle = SliderStyle::Horizontal;
    SliderValueDisplay valueDisplay = SliderValueDisplay::NoDisplay;
    SliderOrientation orientation = SliderOrientation::Horizontal;
    
    // ===== APPEARANCE =====
    Color trackColor = Color(200, 200, 200);
    Color fillColor = Color(0, 120, 215);
    Color handleColor = Color(255, 255, 255);
    Color handleBorderColor = Color(100, 100, 100);
    Color textColor = Colors::Black;
    float trackHeight = 6.0f;
    float handleSize = 16.0f;
    
    // ===== STATE =====
    bool isDragging = false;
    Point2D dragOffset;
    bool showTooltip = false;
    std::string valueFormat = "%.1f";
    
public:
    // ===== CONSTRUCTOR (REQUIRED PATTERN) =====
    UltraCanvasSlider(const std::string& identifier = "Slider", 
                     long id = 0, long x = 0, long y = 0, long w = 200, long h = 30)
        : UltraCanvasElement(identifier, id, x, y, w, h) {
        
        // Initialize standard properties
        properties = StandardProperties(identifier, id, x, y, w, h);
        properties.MousePtr = MousePointer::Hand;
        properties.MouseCtrl = MouseControls::Object2D;
    }

    // ===== VALUE MANAGEMENT =====
    void SetRange(float min, float max) {
        minValue = min;
        maxValue = max;
        currentValue = std::max(min, std::min(max, currentValue));
    }
    
    void SetValue(float value) {
        float newValue = std::max(minValue, std::min(maxValue, value));
        if (step > 0) {
            newValue = std::round(newValue / step) * step;
        }
        
        if (newValue != currentValue) {
            currentValue = newValue;
            if (onValueChanged) onValueChanged(currentValue);
        }
    }
    
    float GetValue() const { return currentValue; }
    float GetMinValue() const { return minValue; }
    float GetMaxValue() const { return maxValue; }
    
    void SetStep(float stepValue) { step = stepValue; }
    float GetStep() const { return step; }
    
    // ===== STYLE MANAGEMENT =====
    void SetSliderStyle(SliderStyle style) {
        sliderStyle = style;
        
        // Adjust default orientation based on style
        if (style == SliderStyle::Vertical) {
            orientation = SliderOrientation::Vertical;
        } else if (style == SliderStyle::Horizontal) {
            orientation = SliderOrientation::Horizontal;
        }
    }
    
    SliderStyle GetSliderStyle() const { return sliderStyle; }
    
    void SetValueDisplay(SliderValueDisplay mode) { valueDisplay = mode; }
    SliderValueDisplay GetValueDisplay() const { return valueDisplay; }
    
    void SetOrientation(SliderOrientation orient) { 
        orientation = orient;
        if (orient == SliderOrientation::Vertical && sliderStyle == SliderStyle::Horizontal) {
            sliderStyle = SliderStyle::Vertical;
        }
    }
    
    void SetColors(const Color& track, const Color& fill, const Color& handle) {
        trackColor = track;
        fillColor = fill;
        handleColor = handle;
    }
    
    void SetTrackHeight(float height) { trackHeight = height; }
    void SetHandleSize(float size) { handleSize = size; }
    void SetValueFormat(const std::string& format) { valueFormat = format; }
    
    // ===== RENDERING (REQUIRED OVERRIDE) =====
    void Render() override {
        if (!IsVisible()) return;
        
        ULTRACANVAS_RENDER_SCOPE();
        
        switch (sliderStyle) {
            case SliderStyle::Horizontal:
            case SliderStyle::Vertical:
                RenderLinearSlider();
                break;
            case SliderStyle::Circular:
                RenderCircularSlider();
                break;
            case SliderStyle::Progress:
                RenderProgressSlider();
                break;
            case SliderStyle::Range:
                RenderRangeSlider();
                break;
        }
        
        // Render value display
        if (valueDisplay != SliderValueDisplay::NoDisplay) {
            RenderValueDisplay();
        }
        
        // Render tooltip if hovering
        if (showTooltip && valueDisplay == SliderValueDisplay::Tooltip) {
            RenderTooltip();
        }
    }
    
    // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
    void OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
                
            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;
                
            case UCEventType::MouseEnter:
                SetHovered(true);
                showTooltip = true;
                break;
                
            case UCEventType::MouseLeave:
                SetHovered(false);
                showTooltip = false;
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
        }
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void(float)> onValueChanged;
    std::function<void(float)> onValueChanging; // Called during drag
    std::function<void()> onDragStart;
    std::function<void()> onDragEnd;

private:
    // ===== RENDERING METHODS =====
    void RenderLinearSlider() {
        Rect2D bounds = GetBounds();
        bool isVertical = (orientation == SliderOrientation::Vertical);
        
        // Calculate track rectangle
        Rect2D trackRect;
        if (isVertical) {
            trackRect = Rect2D(
                bounds.x + (bounds.width - trackHeight) / 2,
                bounds.y + handleSize / 2,
                trackHeight,
                bounds.height - handleSize
            );
        } else {
            trackRect = Rect2D(
                bounds.x + handleSize / 2,
                bounds.y + (bounds.height - trackHeight) / 2,
                bounds.width - handleSize,
                trackHeight
            );
        }
        
        // Draw track
        SetFillColor(trackColor);
        DrawRoundedRect(trackRect, trackHeight / 2);
        
        // Calculate fill rectangle
        float valueRatio = (currentValue - minValue) / (maxValue - minValue);
        Rect2D fillRect = trackRect;
        
        if (isVertical) {
            float fillHeight = trackRect.height * valueRatio;
            fillRect.y = trackRect.y + trackRect.height - fillHeight;
            fillRect.height = fillHeight;
        } else {
            fillRect.width = trackRect.width * valueRatio;
        }
        
        // Draw fill
        SetFillColor(fillColor);
        DrawRoundedRect(fillRect, trackHeight / 2);
        
        // Calculate handle position
        Point2D handlePos = CalculateHandlePosition();
        
        // Draw handle
        SetFillColor(handleColor);
        SetStrokeColor(handleBorderColor);
        SetStrokeWidth(1.0f);
        DrawFilledCircle(handlePos, handleSize / 2, handleColor, handleBorderColor, 1.0f);
    }
    
    void RenderCircularSlider() {
        Rect2D bounds = GetBounds();
        Point2D center(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
        float radius = std::min(bounds.width, bounds.height) / 2 - handleSize / 2;
        
        // Draw track circle
        SetStrokeColor(trackColor);
        SetStrokeWidth(trackHeight);
        DrawCircle(center, radius);
        
        // Calculate value angle (0 to 2π)
        float valueRatio = (currentValue - minValue) / (maxValue - minValue);
        float angle = valueRatio * 2 * M_PI - M_PI / 2; // Start from top
        
        // Draw fill arc
        SetStrokeColor(fillColor);
        SetStrokeWidth(trackHeight);
        DrawArc(center, radius, -M_PI / 2, angle);
        
        // Calculate handle position
        Point2D handlePos(
            center.x + radius * std::cos(angle),
            center.y + radius * std::sin(angle)
        );
        
        // Draw handle
        SetFillColor(handleColor);
        SetStrokeColor(handleBorderColor);
        SetStrokeWidth(2.0f);
        DrawFilledCircle(handlePos, handleSize / 2, handleColor, handleBorderColor, 2.0f);
    }
    
    void RenderProgressSlider() {
        Rect2D bounds = GetBounds();
        
        // Draw background
        SetFillColor(trackColor);
        DrawRoundedRect(bounds, bounds.height / 2);
        
        // Calculate progress rectangle
        float valueRatio = (currentValue - minValue) / (maxValue - minValue);
        Rect2D progressRect = bounds;
        progressRect.width = bounds.width * valueRatio;
        
        // Draw progress
        SetFillColor(fillColor);
        DrawRoundedRect(progressRect, bounds.height / 2);
    }
    
    void RenderRangeSlider() {
        // For now, render as regular slider
        // Full range slider would need two handles
        RenderLinearSlider();
    }
    
    void RenderValueDisplay() {
        if (valueDisplay == SliderValueDisplay::Tooltip && !showTooltip) return;
        
        std::string valueText = FormatValue();
        Point2D textPos = CalculateValueDisplayPosition();
        
        SetFont("Arial", 10.0f);
        SetTextColor(textColor);
        DrawText(valueText, textPos);
    }
    
    void RenderTooltip() {
        if (!showTooltip) return;
        
        std::string valueText = FormatValue();
        Point2D textSize = MeasureText(valueText);
        Point2D handlePos = CalculateHandlePosition();
        
        // Position tooltip above handle
        Rect2D tooltipRect(
            handlePos.x - textSize.x / 2 - 4,
            handlePos.y - handleSize / 2 - textSize.y - 8,
            textSize.x + 8,
            textSize.y + 4
        );
        
        // Draw tooltip background
        SetFillColor(Color(50, 50, 50, 200));
        DrawRoundedRect(tooltipRect, 4);
        
        // Draw tooltip text
        SetFont("Arial", 10.0f);
        SetTextColor(Colors::White);
        DrawText(valueText, Point2D(tooltipRect.x + 4, tooltipRect.y + 2));
    }
    
    // ===== CALCULATION METHODS =====
    Point2D CalculateHandlePosition() {
        Rect2D bounds = GetBounds();
        float valueRatio = (currentValue - minValue) / (maxValue - minValue);
        
        if (sliderStyle == SliderStyle::Circular) {
            Point2D center(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
            float radius = std::min(bounds.width, bounds.height) / 2 - handleSize / 2;
            float angle = valueRatio * 2 * M_PI - M_PI / 2;
            
            return Point2D(
                center.x + radius * std::cos(angle),
                center.y + radius * std::sin(angle)
            );
        } else if (orientation == SliderOrientation::Vertical) {
            return Point2D(
                bounds.x + bounds.width / 2,
                bounds.y + bounds.height - handleSize / 2 - (bounds.height - handleSize) * valueRatio
            );
        } else {
            return Point2D(
                bounds.x + handleSize / 2 + (bounds.width - handleSize) * valueRatio,
                bounds.y + bounds.height / 2
            );
        }
    }
    
    Point2D CalculateValueDisplayPosition() {
        Rect2D bounds = GetBounds();
        
        switch (valueDisplay) {
            case SliderValueDisplay::Always:
                if (orientation == SliderOrientation::Vertical) {
                    return Point2D(bounds.x + bounds.width + 5, bounds.y + bounds.height / 2);
                } else {
                    return Point2D(bounds.x + bounds.width / 2, bounds.y - 15);
                }
                break;
            default:
                return CalculateHandlePosition();
        }
    }
    
    std::string FormatValue() {
        char buffer[32];
        
        switch (valueDisplay) {
            case SliderValueDisplay::Percentage:
                {
                    float percentage = (currentValue - minValue) / (maxValue - minValue) * 100;
                    snprintf(buffer, sizeof(buffer), "%.0f%%", percentage);
                }
                break;
            case SliderValueDisplay::Number:
            case SliderValueDisplay::Tooltip:
            case SliderValueDisplay::Always:
            default:
                snprintf(buffer, sizeof(buffer), valueFormat.c_str(), currentValue);
                break;
        }
        
        return std::string(buffer);
    }
    
    float CalculateValueFromPosition(const Point2D& position) {
        Rect2D bounds = GetBounds();
        float ratio = 0.0f;
        
        if (sliderStyle == SliderStyle::Circular) {
            Point2D center(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
            float angle = std::atan2(position.y - center.y, position.x - center.x);
            angle += M_PI / 2; // Adjust for starting at top
            if (angle < 0) angle += 2 * M_PI;
            ratio = angle / (2 * M_PI);
        } else if (orientation == SliderOrientation::Vertical) {
            ratio = 1.0f - (position.y - bounds.y - handleSize / 2) / (bounds.height - handleSize);
        } else {
            ratio = (position.x - bounds.x - handleSize / 2) / (bounds.width - handleSize);
        }
        
        ratio = std::max(0.0f, std::min(1.0f, ratio));
        return minValue + ratio * (maxValue - minValue);
    }
    
    // ===== EVENT HANDLERS =====
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        Point2D clickPos(event.x, event.y);
        Point2D handlePos = CalculateHandlePosition();
        
        // Check if clicked on handle
        float handleDistance = Distance(clickPos, handlePos);
        if (handleDistance <= handleSize / 2) {
            isDragging = true;
            dragOffset = Point2D(clickPos.x - handlePos.x, clickPos.y - handlePos.y);
            if (onDragStart) onDragStart();
        } else {
            // Click on track - jump to position
            float newValue = CalculateValueFromPosition(clickPos);
            SetValue(newValue);
        }
        
        SetFocus(true);
    }
    
    void HandleMouseMove(const UCEvent& event) {
        if (!isDragging) return;
        
        Point2D mousePos(event.x - dragOffset.x, event.y - dragOffset.y);
        float newValue = CalculateValueFromPosition(mousePos);
        
        SetValue(newValue);
        if (onValueChanging) onValueChanging(currentValue);
    }
    
    void HandleMouseUp(const UCEvent& event) {
        if (isDragging) {
            isDragging = false;
            if (onDragEnd) onDragEnd();
        }
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return;
        
        float delta = step > 0 ? step : (maxValue - minValue) / 100;
        
        switch (event.virtualKey) {
            case UCKeys::Left:
            case UCKeys::Down:
                SetValue(currentValue - delta);
                break;
            case UCKeys::Right:
            case UCKeys::Up:
                SetValue(currentValue + delta);
                break;
            case UCKeys::Home:
                SetValue(minValue);
                break;
            case UCKeys::End:
                SetValue(maxValue);
                break;
        }
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasSlider> CreateSlider(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasSlider>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasSlider> CreateHorizontalSlider(
    const std::string& identifier, long id, long x, long y, long w, long h, 
    float min = 0.0f, float max = 100.0f, float value = 0.0f) {
    auto slider = CreateSlider(identifier, id, x, y, w, h);
    slider->SetRange(min, max);
    slider->SetValue(value);
    slider->SetSliderStyle(SliderStyle::Horizontal);
    return slider;
}

inline std::shared_ptr<UltraCanvasSlider> CreateVerticalSlider(
    const std::string& identifier, long id, long x, long y, long w, long h, 
    float min = 0.0f, float max = 100.0f, float value = 0.0f) {
    auto slider = CreateSlider(identifier, id, x, y, w, h);
    slider->SetRange(min, max);
    slider->SetValue(value);
    slider->SetSliderStyle(SliderStyle::Vertical);
    return slider;
}

inline std::shared_ptr<UltraCanvasSlider> CreateCircularSlider(
    const std::string& identifier, long id, long x, long y, long size, 
    float min = 0.0f, float max = 100.0f, float value = 0.0f) {
    auto slider = CreateSlider(identifier, id, x, y, size, size);
    slider->SetRange(min, max);
    slider->SetValue(value);
    slider->SetSliderStyle(SliderStyle::Circular);
    return slider;
}

} // namespace UltraCanvas


// ===================================================================
// UltraCanvasTabs.h
// Interactive tab container with multiple styles and content management
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework

#include "UltraCanvasContainer.h"
#include <vector>

namespace UltraCanvas {

// ===== TAB DEFINITIONS =====
enum class TabStyle {
    Classic,       // Traditional rectangular tabs
    Rounded,       // Rounded corner tabs
    Underline,     // Modern underline style
    Pills,         // Pill-shaped tabs
    Minimal        // Minimal text-only tabs
};

enum class TabPosition {
    Top, Bottom, Left, Right
};

enum class TabCloseButton {
    None, Hover, Always
};

// ===== TAB DATA STRUCTURE =====
struct TabData {
    std::string title;
    std::string id;
    std::shared_ptr<UltraCanvasElement> content;
    bool enabled = true;
    bool closable = true;
    Color customColor = Colors::Transparent;
    std::string iconPath;
    
    TabData(const std::string& tabTitle, std::shared_ptr<UltraCanvasElement> tabContent = nullptr)
        : title(tabTitle), content(tabContent) {
        id = "tab_" + std::to_string(reinterpret_cast<uintptr_t>(this));
    }
};

// ===== TABS COMPONENT =====
class UltraCanvasTabs : public UltraCanvasElement {
private:
    // ===== STANDARD PROPERTIES (REQUIRED) =====
    StandardProperties properties;
    
    // ===== TAB MANAGEMENT =====
    std::vector<TabData> tabs;
    int activeTabIndex = -1;
    int hoveredTabIndex = -1;
    int closeHoverIndex = -1;
    
    // ===== APPEARANCE =====
    TabStyle tabStyle = TabStyle::Classic;
    TabPosition tabPosition = TabPosition::Top;
    TabCloseButton closeButtonMode = TabCloseButton::Hover;
    
    Color tabBackgroundColor = Color(240, 240, 240);
    Color activeTabColor = Colors::White;
    Color inactiveTabColor = Color(220, 220, 220);
    Color hoverTabColor = Color(230, 230, 230);
    Color tabBorderColor = Color(180, 180, 180);
    Color tabTextColor = Colors::Black;
    Color activeTabTextColor = Colors::Black;
    Color disabledTabTextColor = Color(150, 150, 150);
    
    float tabHeight = 30.0f;
    float tabMinWidth = 80.0f;
    float tabMaxWidth = 200.0f;
    float tabSpacing = 2.0f;
    float tabPadding = 10.0f;
    
    // ===== CONTENT AREA =====
    std::shared_ptr<UltraCanvasContainer> contentContainer;
    
    // ===== SCROLLING =====
    bool scrollableTabs = false;
    float tabScrollOffset = 0.0f;
    float maxTabScrollOffset = 0.0f;
    
public:
    // ===== CONSTRUCTOR (REQUIRED PATTERN) =====
    UltraCanvasTabs(const std::string& identifier = "Tabs", 
                   long id = 0, long x = 0, long y = 0, long w = 400, long h = 300)
        : UltraCanvasElement(identifier, id, x, y, w, h) {
        
        // Initialize standard properties
        properties = StandardProperties(identifier, id, x, y, w, h);
        properties.MousePtr = MousePointer::Hand;
        properties.MouseCtrl = MouseControls::Object2D;
        
        // Create content container
        contentContainer = std::make_shared<UltraCanvasContainer>("TabContent", id + 1000, 
                                                                 x, y + tabHeight, w, h - tabHeight);
        AddChild(contentContainer.get());
    }

    // ===== TAB MANAGEMENT =====
    int AddTab(const std::string& title, std::shared_ptr<UltraCanvasElement> content = nullptr) {
        TabData tab(title, content);
        tabs.push_back(tab);
        
        int tabIndex = static_cast<int>(tabs.size() - 1);
        
        // Set as active if first tab
        if (tabs.size() == 1) {
            SetActiveTab(tabIndex);
        }
        
        CalculateTabLayout();
        
        if (onTabAdded) onTabAdded(tabIndex, title);
        return tabIndex;
    }
    
    void RemoveTab(int index) {
        if (index < 0 || index >= static_cast<int>(tabs.size())) return;
        
        std::string removedTitle = tabs[index].title;
        
        // Remove tab content from container
        if (tabs[index].content) {
            contentContainer->RemoveChild(tabs[index].content);
        }
        
        tabs.erase(tabs.begin() + index);
        
        // Adjust active tab index
        if (activeTabIndex == index) {
            if (tabs.empty()) {
                activeTabIndex = -1;
            } else if (index >= static_cast<int>(tabs.size())) {
                SetActiveTab(static_cast<int>(tabs.size()) - 1);
            } else {
                SetActiveTab(index);
            }
        } else if (activeTabIndex > index) {
            activeTabIndex--;
        }
        
        CalculateTabLayout();
        
        if (onTabRemoved) onTabRemoved(index, removedTitle);
    }
    
    void SetActiveTab(int index) {
        if (index < 0 || index >= static_cast<int>(tabs.size()) || !tabs[index].enabled) return;
        
        int previousTab = activeTabIndex;
        activeTabIndex = index;
        
        // Hide all tab content
        for (auto& tab : tabs) {
            if (tab.content) {
                tab.content->SetVisible(false);
            }
        }
        
        // Show active tab content
        if (tabs[activeTabIndex].content) {
            tabs[activeTabIndex].content->SetVisible(true);
            
            // Ensure content is in container
            if (!contentContainer->FindChildById(tabs[activeTabIndex].content->GetIdentifier())) {
                contentContainer->AddChild(tabs[activeTabIndex].content);
            }
        }
        
        if (onTabChanged) onTabChanged(activeTabIndex, previousTab);
    }
    
    int GetActiveTab() const { return activeTabIndex; }
    
    void SetTabTitle(int index, const std::string& title) {
        if (index >= 0 && index < static_cast<int>(tabs.size())) {
            tabs[index].title = title;
            CalculateTabLayout();
        }
    }
    
    std::string GetTabTitle(int index) const {
        if (index >= 0 && index < static_cast<int>(tabs.size())) {
            return tabs[index].title;
        }
        return "";
    }
    
    void SetTabContent(int index, std::shared_ptr<UltraCanvasElement> content) {
        if (index >= 0 && index < static_cast<int>(tabs.size())) {
            // Remove old content
            if (tabs[index].content) {
                contentContainer->RemoveChild(tabs[index].content);
            }
            
            // Set new content
            tabs[index].content = content;
            
            // Add to container if this is the active tab
            if (index == activeTabIndex && content) {
                contentContainer->AddChild(content);
                content->SetVisible(true);
            }
        }
    }
    
    std::shared_ptr<UltraCanvasElement> GetTabContent(int index) const {
        if (index >= 0 && index < static_cast<int>(tabs.size())) {
            return tabs[index].content;
        }
        return nullptr;
    }
    
    void SetTabEnabled(int index, bool enabled) {
        if (index >= 0 && index < static_cast<int>(tabs.size())) {
            tabs[index].enabled = enabled;
            
            // If disabling active tab, switch to next enabled tab
            if (!enabled && index == activeTabIndex) {
                for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
                    if (tabs[i].enabled) {
                        SetActiveTab(i);
                        break;
                    }
                }
            }
        }
    }
    
    void SetTabClosable(int index, bool closable) {
        if (index >= 0 && index < static_cast<int>(tabs.size())) {
            tabs[index].closable = closable;
        }
    }
    
    int GetTabCount() const { return static_cast<int>(tabs.size()); }
    
    // ===== STYLE MANAGEMENT =====
    void SetTabStyle(TabStyle style) {
        tabStyle = style;
        CalculateTabLayout();
    }
    
    TabStyle GetTabStyle() const { return tabStyle; }
    
    void SetTabPosition(TabPosition position) {
        tabPosition = position;
        UpdateContentContainerLayout();
        CalculateTabLayout();
    }
    
    void SetTabColors(const Color& background, const Color& active, const Color& inactive, 
                     const Color& hover, const Color& border) {
        tabBackgroundColor = background;
        activeTabColor = active;
        inactiveTabColor = inactive;
        hoverTabColor = hover;
        tabBorderColor = border;
    }
    
    void SetTabDimensions(float height, float minWidth, float maxWidth, float spacing, float padding) {
        tabHeight = height;
        tabMinWidth = minWidth;
        tabMaxWidth = maxWidth;
        tabSpacing = spacing;
        tabPadding = padding;
        
        UpdateContentContainerLayout();
        CalculateTabLayout();
    }
    
    void SetCloseButtonMode(TabCloseButton mode) { closeButtonMode = mode; }
    void SetScrollableTabs(bool scrollable) { scrollableTabs = scrollable; }
    
    // ===== RENDERING (REQUIRED OVERRIDE) =====
    void Render() override {
        if (!IsVisible()) return;
        
        ULTRACANVAS_RENDER_SCOPE();
        
        // Draw tab background
        Rect2D tabArea = GetTabArea();
        SetFillColor(tabBackgroundColor);
        DrawRect(tabArea);
        
        // Draw tabs
        RenderTabs();
        
        // Draw content border
        RenderContentBorder();
        
        // Render children (content container)
        RenderChildren();
    }
    
    // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
    void OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return;
        
        // Handle tab area events
        if (IsInTabArea(Point2D(event.x, event.y))) {
            switch (event.type) {
                case UCEventType::MouseDown:
                    HandleTabMouseDown(event);
                    break;
                    
                case UCEventType::MouseMove:
                    HandleTabMouseMove(event);
                    break;
                    
                case UCEventType::MouseUp:
                    HandleTabMouseUp(event);
                    break;
                    
                case UCEventType::MouseLeave:
                    hoveredTabIndex = -1;
                    closeHoverIndex = -1;
                    break;
            }
        } else {
            // Forward to children
            UltraCanvasElement::OnEvent(event);
        }
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void(int, const std::string&)> onTabAdded;
    std::function<void(int, const std::string&)> onTabRemoved;
    std::function<void(int, int)> onTabChanged; // newIndex, oldIndex
    std::function<void(int)> onTabCloseRequested;
    std::function<void(int)> onTabDoubleClicked;

private:
    // ===== LAYOUT CALCULATIONS =====
    void UpdateContentContainerLayout() {
        if (!contentContainer) return;
        
        Rect2D bounds = GetBounds();
        Rect2D contentRect = bounds;
        
        switch (tabPosition) {
            case TabPosition::Top:
                contentRect.y += tabHeight;
                contentRect.height -= tabHeight;
                break;
            case TabPosition::Bottom:
                contentRect.height -= tabHeight;
                break;
            case TabPosition::Left:
                contentRect.x += tabHeight;
                contentRect.width -= tabHeight;
                break;
            case TabPosition::Right:
                contentRect.width -= tabHeight;
                break;
        }
        
        contentContainer->SetPosition(static_cast<long>(contentRect.x), static_cast<long>(contentRect.y));
        contentContainer->SetSize(static_cast<long>(contentRect.width), static_cast<long>(contentRect.height));
    }
    
    void CalculateTabLayout() {
        // Calculate tab positions and scrolling if needed
        float totalTabWidth = 0.0f;
        
        for (auto& tab : tabs) {
            Point2D textSize = MeasureText(tab.title);
            float tabWidth = std::max(tabMinWidth, std::min(tabMaxWidth, textSize.x + tabPadding * 2));
            totalTabWidth += tabWidth + tabSpacing;
        }
        
        Rect2D tabArea = GetTabArea();
        bool isHorizontal = (tabPosition == TabPosition::Top || tabPosition == TabPosition::Bottom);
        
        if (isHorizontal) {
            maxTabScrollOffset = std::max(0.0f, totalTabWidth - tabArea.width);
            scrollableTabs = (maxTabScrollOffset > 0);
        }
    }
    
    Rect2D GetTabArea() const {
        Rect2D bounds = GetBounds();
        
        switch (tabPosition) {
            case TabPosition::Top:
                return Rect2D(bounds.x, bounds.y, bounds.width, tabHeight);
            case TabPosition::Bottom:
                return Rect2D(bounds.x, bounds.y + bounds.height - tabHeight, bounds.width, tabHeight);
            case TabPosition::Left:
                return Rect2D(bounds.x, bounds.y, tabHeight, bounds.height);
            case TabPosition::Right:
                return Rect2D(bounds.x + bounds.width - tabHeight, bounds.y, tabHeight, bounds.height);
            default:
                return Rect2D(bounds.x, bounds.y, bounds.width, tabHeight);
        }
    }
    
    Rect2D GetTabRect(int index) const {
        if (index < 0 || index >= static_cast<int>(tabs.size())) {
            return Rect2D(0, 0, 0, 0);
        }
        
        Rect2D tabArea = GetTabArea();
        float currentX = tabArea.x - tabScrollOffset;
        
        for (int i = 0; i <= index; ++i) {
            Point2D textSize = MeasureText(tabs[i].title);
            float tabWidth = std::max(tabMinWidth, std::min(tabMaxWidth, textSize.x + tabPadding * 2));
            
            if (i == index) {
                return Rect2D(currentX, tabArea.y, tabWidth, tabArea.height);
            }
            
            currentX += tabWidth + tabSpacing;
        }
        
        return Rect2D(0, 0, 0, 0);
    }
    
    bool IsInTabArea(const Point2D& point) const {
        Rect2D tabArea = GetTabArea();
        return tabArea.Contains(point);
    }
    
    int GetTabAtPosition(const Point2D& point) const {
        for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
            Rect2D tabRect = GetTabRect(i);
            if (tabRect.Contains(point)) {
                return i;
            }
        }
        return -1;
    }
    
    // ===== RENDERING METHODS =====
    void RenderTabs() {
        for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
            RenderTab(i);
        }
    }
    
    void RenderTab(int index) {
        if (index < 0 || index >= static_cast<int>(tabs.size())) return;
        
        const TabData& tab = tabs[index];
        Rect2D tabRect = GetTabRect(index);
        
        // Skip if tab is outside visible area
        Rect2D tabArea = GetTabArea();
        if (tabRect.x + tabRect.width < tabArea.x || tabRect.x > tabArea.x + tabArea.width) {
            return;
        }
        
        // Determine tab color
        Color bgColor = inactiveTabColor;
        if (index == activeTabIndex) {
            bgColor = activeTabColor;
        } else if (index == hoveredTabIndex) {
            bgColor = hoverTabColor;
        }
        
        if (!tab.enabled) {
            bgColor = Color(bgColor.r * 0.7f, bgColor.g * 0.7f, bgColor.b * 0.7f, bgColor.a);
        }
        
        // Draw tab background based on style
        switch (tabStyle) {
            case TabStyle::Classic:
                RenderClassicTab(tabRect, bgColor, index == activeTabIndex);
                break;
            case TabStyle::Rounded:
                RenderRoundedTab(tabRect, bgColor, index == activeTabIndex);
                break;
            case TabStyle::Underline:
                RenderUnderlineTab(tabRect, bgColor, index == activeTabIndex);
                break;
            case TabStyle::Pills:
                RenderPillTab(tabRect, bgColor, index == activeTabIndex);
                break;
            case TabStyle::Minimal:
                RenderMinimalTab(tabRect, bgColor, index == activeTabIndex);
                break;
        }
        
        // Draw tab text
        Color textColor = tab.enabled ? (index == activeTabIndex ? activeTabTextColor : tabTextColor) : disabledTabTextColor;
        SetFont("Arial", 11.0f);
        SetTextColor(textColor);
        
        Point2D textPos(
            tabRect.x + tabPadding,
            tabRect.y + (tabRect.height - 11.0f) / 2
        );
        DrawText(tab.title, textPos);
        
        // Draw close button if needed
        if (tab.closable && (closeButtonMode == TabCloseButton::Always || 
                           (closeButtonMode == TabCloseButton::Hover && index == hoveredTabIndex))) {
            RenderCloseButton(tabRect, index);
        }
    }
    
    void RenderClassicTab(const Rect2D& rect, const Color& bgColor, bool isActive) {
        SetFillColor(bgColor);
        DrawRect(rect);
        
        SetStrokeColor(tabBorderColor);
        SetStrokeWidth(1.0f);
        
        // Draw borders (skip bottom for active tab)
        DrawLine(Point2D(rect.x, rect.y), Point2D(rect.x + rect.width, rect.y)); // Top
        DrawLine(Point2D(rect.x, rect.y), Point2D(rect.x, rect.y + rect.height)); // Left
        DrawLine(Point2D(rect.x + rect.width, rect.y), Point2D(rect.x + rect.width, rect.y + rect.height)); // Right
        
        if (!isActive) {
            DrawLine(Point2D(rect.x, rect.y + rect.height), Point2D(rect.x + rect.width, rect.y + rect.height)); // Bottom
        }
    }
    
    void RenderRoundedTab(const Rect2D& rect, const Color& bgColor, bool isActive) {
        SetFillColor(bgColor);
        DrawRoundedRect(rect, 8.0f);
        
        if (!isActive) {
            SetStrokeColor(tabBorderColor);
            SetStrokeWidth(1.0f);
            DrawRoundedRect(rect, 8.0f);
        }
    }
    
    void RenderUnderlineTab(const Rect2D& rect, const Color& bgColor, bool isActive) {
        if (isActive) {
            SetStrokeColor(activeTabColor);
            SetStrokeWidth(3.0f);
            DrawLine(Point2D(rect.x, rect.y + rect.height - 2), 
                    Point2D(rect.x + rect.width, rect.y + rect.height - 2));
        }
    }
    
    void RenderPillTab(const Rect2D& rect, const Color& bgColor, bool isActive) {
        if (isActive) {
            SetFillColor(bgColor);
            DrawRoundedRect(rect, rect.height / 2);
        }
    }
    
    void RenderMinimalTab(const Rect2D& rect, const Color& bgColor, bool isActive) {
        // Only text, no background
    }
    
    void RenderCloseButton(const Rect2D& tabRect, int tabIndex) {
        float buttonSize = 12.0f;
        Point2D buttonPos(
            tabRect.x + tabRect.width - tabPadding - buttonSize,
            tabRect.y + (tabRect.height - buttonSize) / 2
        );
        
        Rect2D buttonRect(buttonPos.x, buttonPos.y, buttonSize, buttonSize);
        
        // Draw button background if hovered
        if (tabIndex == closeHoverIndex) {
            SetFillColor(Color(255, 100, 100, 100));
            DrawRoundedRect(buttonRect, buttonSize / 2);
        }
        
        // Draw X
        SetStrokeColor(Color(100, 100, 100));
        SetStrokeWidth(1.5f);
        float margin = 3.0f;
        DrawLine(Point2D(buttonPos.x + margin, buttonPos.y + margin), 
                Point2D(buttonPos.x + buttonSize - margin, buttonPos.y + buttonSize - margin));
        DrawLine(Point2D(buttonPos.x + buttonSize - margin, buttonPos.y + margin), 
                Point2D(buttonPos.x + margin, buttonPos.y + buttonSize - margin));
    }
    
    void RenderContentBorder() {
        Rect2D contentArea = contentContainer->GetBounds();
        SetStrokeColor(tabBorderColor);
        SetStrokeWidth(1.0f);
        DrawRect(contentArea);
    }
    
    // ===== EVENT HANDLERS =====
    void HandleTabMouseDown(const UCEvent& event) {
        Point2D clickPos(event.x, event.y);
        int tabIndex = GetTabAtPosition(clickPos);
        
        if (tabIndex >= 0) {
            // Check if clicked on close button
            Rect2D tabRect = GetTabRect(tabIndex);
            if (tabs[tabIndex].closable && closeButtonMode != TabCloseButton::None) {
                float buttonSize = 12.0f;
                Rect2D closeRect(
                    tabRect.x + tabRect.width - tabPadding - buttonSize,
                    tabRect.y + (tabRect.height - buttonSize) / 2,
                    buttonSize, buttonSize
                );
                
                if (closeRect.Contains(clickPos)) {
                    if (onTabCloseRequested) onTabCloseRequested(tabIndex);
                    return;
                }
            }
            
            // Regular tab click
            SetActiveTab(tabIndex);
        }
    }
    
    void HandleTabMouseMove(const UCEvent& event) {
        Point2D mousePos(event.x, event.y);
        int newHoveredTab = GetTabAtPosition(mousePos);
        
        if (newHoveredTab != hoveredTabIndex) {
            hoveredTabIndex = newHoveredTab;
        }
        
        // Check close button hover
        int newCloseHover = -1;
        if (hoveredTabIndex >= 0 && tabs[hoveredTabIndex].closable) {
            Rect2D tabRect = GetTabRect(hoveredTabIndex);
            float buttonSize = 12.0f;
            Rect2D closeRect(
                tabRect.x + tabRect.width - tabPadding - buttonSize,
                tabRect.y + (tabRect.height - buttonSize) / 2,
                buttonSize, buttonSize
            );
            
            if (closeRect.Contains(mousePos)) {
                newCloseHover = hoveredTabIndex;
            }
        }
        
        if (newCloseHover != closeHoverIndex) {
            closeHoverIndex = newCloseHover;
        }
    }
    
    void HandleTabMouseUp(const UCEvent& event) {
        // Handle any mouse up logic
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasTabs> CreateTabs(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasTabs>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasTabs> CreateTabsWithStyle(
    const std::string& identifier, long id, long x, long y, long w, long h, TabStyle style) {
    auto tabs = CreateTabs(identifier, id, x, y, w, h);
    tabs->SetTabStyle(style);
    return tabs;
}

} // namespace UltraCanvas