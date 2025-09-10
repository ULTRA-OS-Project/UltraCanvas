// UltraCanvasAdvancedControls.h
// Advanced form controls: enhanced sliders, radio buttons, switches, and toggle groups
// Version: 1.0.0
// Last Modified: 2025-01-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasSlider.h"  // Extends existing slider
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>

namespace UltraCanvas {

// ===== ADVANCED STYLING SYSTEM =====
struct AdvancedControlStyle {
    // Colors
    Color primaryColor = Color(25, 118, 210);
    Color secondaryColor = Color(156, 39, 176);
    Color backgroundColor = Color(240, 240, 240);
    Color borderColor = Color(200, 200, 200);
    Color textColor = Color(60, 60, 60);
    Color disabledColor = Color(200, 200, 200);
    Color focusColor = Color(25, 118, 210, 100);
    
    // State Colors
    Color normalColor = backgroundColor;
    Color hoverColor = Color(230, 230, 230);
    Color pressedColor = Color(220, 220, 220);
    Color selectedColor = primaryColor;
    Color disabledBackgroundColor = Color(250, 250, 250);
    
    // Typography
    std::string fontFamily = "Arial";
    float fontSize = 13.0f;
    FontWeight fontWeight = FontWeight::Normal;
    
    // Dimensions
    float borderWidth = 1.0f;
    float borderRadius = 4.0f;
    float padding = 8.0f;
    float spacing = 6.0f;
    
    // Effects
    bool showShadow = false;
    Color shadowColor = Color(0, 0, 0, 50);
    Point2D shadowOffset = Point2D(0, 1);
    float shadowBlur = 2.0f;
    
    // Animation
    float animationDuration = 0.2f;
    bool enableAnimations = true;
    bool enableRippleEffect = false;
    
    static AdvancedControlStyle Default() { return AdvancedControlStyle(); }
    static AdvancedControlStyle Material();
    static AdvancedControlStyle iOS();
    static AdvancedControlStyle Flat();
    static AdvancedControlStyle Dark();
};

// ===== ENHANCED SLIDER COMPONENT =====
class UltraCanvasAdvancedSlider : public UltraCanvasSlider {
private:
    // Extended properties
    HandleShape handleShape = HandleShape::Circle;
    LabelPosition labelPosition = LabelPosition::None;
    LabelPosition valuePosition = LabelPosition::None;
    std::string labelText;
    bool showValueAsPercent = false;
    bool showTicks = false;
    std::vector<float> tickMarks;
    std::vector<std::string> tickLabels;
    AdvancedControlStyle controlStyle;
    
    // Advanced features
    bool snapToTicks = false;
    float stepSize = 0.0f;
    std::vector<std::pair<float, Color>> colorSegments;
    bool showTrail = false;
    Color trailColor = Color(25, 118, 210, 100);
    
public:
    UltraCanvasAdvancedSlider(const std::string& identifier = "AdvancedSlider", 
                             long id = 0, long x = 0, long y = 0, long w = 200, long h = 32)
        : UltraCanvasSlider(identifier, id, x, y, w, h) {
        controlStyle = AdvancedControlStyle::Default();
    }
    
    // ===== HANDLE STYLING =====
    void SetHandleShape(HandleShape shape) {
        handleShape = shape;
    }
    
    HandleShape GetHandleShape() const { return handleShape; }
    
    void SetHandleSize(float size) {
        SetHandleSize(size);  // Delegate to base class
    }
    
    // ===== LABEL MANAGEMENT =====
    void SetLabel(const std::string& text, LabelPosition position = LabelPosition::Top) {
        labelText = text;
        labelPosition = position;
    }
    
    const std::string& GetLabel() const { return labelText; }
    
    void SetLabelPosition(LabelPosition position) {
        labelPosition = position;
    }
    
    LabelPosition GetLabelPosition() const { return labelPosition; }
    
    // ===== VALUE DISPLAY =====
    void ShowValue(LabelPosition position, bool asPercent = false) {
        valuePosition = position;
        showValueAsPercent = asPercent;
    }
    
    void HideValue() {
        valuePosition = LabelPosition::None;
    }
    
    // ===== TICK MARKS =====
    void SetTickMarks(const std::vector<float>& ticks) {
        tickMarks = ticks;
        showTicks = !ticks.empty();
    }
    
    void SetTickLabels(const std::vector<std::string>& labels) {
        tickLabels = labels;
    }
    
    void SetSnapToTicks(bool snap) {
        snapToTicks = snap;
    }
    
    void SetStepSize(float step) {
        stepSize = step;
    }
    
    // ===== COLOR SEGMENTS =====
    void SetColorSegments(const std::vector<std::pair<float, Color>>& segments) {
        colorSegments = segments;
    }
    
    void SetTrail(bool show, const Color& color = Color(25, 118, 210, 100)) {
        showTrail = show;
        trailColor = color;
    }
    
    // ===== STYLE MANAGEMENT =====
    void SetControlStyle(const AdvancedControlStyle& style) {
        controlStyle = style;
    }
    
    const AdvancedControlStyle& GetControlStyle() const { return controlStyle; }
    
    // ===== ENHANCED RENDERING =====
    void Render() override {
        if (!IsVisible()) return;
        
        ctx->PushState();
        
        // Render label
        if (!labelText.empty() && labelPosition != LabelPosition::None) {
            RenderLabel();
        }
        
        // Get slider area (excluding label space)
        Rect2D sliderArea = GetSliderArea();
        
        // Render track with segments or standard
        if (!colorSegments.empty()) {
            RenderSegmentedTrack(sliderArea);
        } else {
            RenderStandardTrack(sliderArea);
        }
        
        // Render trail if enabled
        if (showTrail) {
            RenderTrail(sliderArea);
        }
        
        // Render tick marks
        if (showTicks && !tickMarks.empty()) {
            RenderTickMarks(sliderArea);
        }
        
        // Render custom handle
        RenderCustomHandle(sliderArea);
        
        // Render value display
        if (valuePosition != LabelPosition::None) {
            RenderValueDisplay();
        }
    }

private:
    Rect2D GetSliderArea() const {
        Rect2D area = GetBounds();
        
        // Adjust for label space
        if (!labelText.empty() && labelPosition != LabelPosition::None) {
            switch (labelPosition) {
                case LabelPosition::Top:
                    area.y += 20;
                    area.height -= 20;
                    break;
                case LabelPosition::Bottom:
                    area.height -= 20;
                    break;
                case LabelPosition::Left:
                    area.x += 60;
                    area.width -= 60;
                    break;
                case LabelPosition::Right:
                    area.width -= 60;
                    break;
                default:
                    break;
            }
        }
        
        return area;
    }
    
    void RenderLabel() {
        ctx->SetTextColor(controlStyle.textColor);
        ctx->SetFont(controlStyle.fontFamily, controlStyle.fontSize);
        
        Point2D labelPos = CalculateLabelPosition();
        ctx->DrawText(labelText, labelPos);
    }
    
    void RenderSegmentedTrack(const Rect2D& area) {
        float trackHeight = 6.0f;
        float trackY = area.y + (area.height - trackHeight) / 2.0f;
        
        for (size_t i = 0; i < colorSegments.size(); ++i) {
            float startValue = (i == 0) ? GetMinValue() : colorSegments[i-1].first;
            float endValue = colorSegments[i].first;
            
            float startX = area.x + (startValue - GetMinValue()) / (GetMaxValue() - GetMinValue()) * area.width;
            float endX = area.x + (endValue - GetMinValue()) / (GetMaxValue() - GetMinValue()) * area.width;
            
            Rect2D segment(startX, trackY, endX - startX, trackHeight);
            ctx->SetFillColor(colorSegments[i].second);
            ctx->DrawRectangle(segment);
        }
    }
    
    void RenderStandardTrack(const Rect2D& area) {
        float trackHeight = 6.0f;
        float trackY = area.y + (area.height - trackHeight) / 2.0f;
        Rect2D track(area.x, trackY, area.width, trackHeight);
        
        ctx->SetFillColor(controlStyle.backgroundColor);
        if (controlStyle.borderRadius > 0) {
            // Would need rounded rectangle function
            ctx->DrawRectangle(track);
        } else {
            ctx->DrawRectangle(track);
        }
        
        ctx->SetStrokeColor(controlStyle.borderColor);
        ctx->SetStrokeWidth(controlStyle.borderWidth);
        ctx->DrawRectangle(track);
    }
    
    void RenderTrail(const Rect2D& area) {
        float progress = (GetValue() - GetMinValue()) / (GetMaxValue() - GetMinValue());
        float trackHeight = 6.0f;
        float trackY = area.y + (area.height - trackHeight) / 2.0f;
        
        Rect2D trail(area.x, trackY, area.width * progress, trackHeight);
        ctx->SetFillColor(trailColor);
        ctx->DrawRectangle(trail);
    }
    
    void RenderTickMarks(const Rect2D& area) {
        ctx->SetStrokeColor(controlStyle.borderColor);
        ctx->SetStrokeWidth(1.0f);
        
        for (size_t i = 0; i < tickMarks.size(); ++i) {
            float tickValue = tickMarks[i];
            float tickX = area.x + (tickValue - GetMinValue()) / (GetMaxValue() - GetMinValue()) * area.width;
            
            // Draw tick mark
            ctx->DrawLine(Point2D(tickX, area.y + area.height - 8), Point2D(tickX, area.y + area.height));
            
            // Draw tick label if available
            if (i < tickLabels.size()) {
                ctx->SetTextColor(controlStyle.textColor);
                ctx->SetFont(controlStyle.fontFamily, controlStyle.fontSize * 0.8f);
                ctx->DrawText(tickLabels[i], Point2D(tickX - 10, area.y + area.height + 12));
            }
        }
    }
    
    void RenderCustomHandle(const Rect2D& area) {
        float progress = (GetValue() - GetMinValue()) / (GetMaxValue() - GetMinValue());
        float handleX = area.x + progress * area.width;
        float handleY = area.y + area.height / 2.0f;
        float handleSize = GetHandleSize();
        
        Point2D handleCenter(handleX, handleY);
        
        // Handle shadow
        if (controlStyle.showShadow) {
            Point2D shadowCenter = handleCenter + controlStyle.shadowOffset;
            ctx->SetFillColor(controlStyle.shadowColor);
            ctx->DrawCircle(shadowCenter, handleSize / 2.0f + 1);
        }
        
        // Handle background
        Color handleColor = IsHovered() ? controlStyle.hoverColor : controlStyle.normalColor;
        if (IsFocused()) {
            ctx->SetFillColor(controlStyle.focusColor);
            ctx->DrawCircle(handleCenter, handleSize / 2.0f + 2);
        }
        
        ctx->SetFillColor(handleColor);
        
        switch (handleShape) {
            case HandleShape::Circle:
                ctx->DrawCircle(handleCenter, handleSize / 2.0f);
                break;
            case HandleShape::Square: {
                Rect2D handleRect(handleX - handleSize/2, handleY - handleSize/2, handleSize, handleSize);
                ctx->DrawRectangle(handleRect);
                break;
            }
            case HandleShape::Diamond: {
                // Would need diamond drawing function
                ctx->DrawCircle(handleCenter, handleSize / 2.0f);
                break;
            }
            case HandleShape::Rectangle: {
                Rect2D handleRect(handleX - handleSize/3, handleY - handleSize/2, handleSize*2/3, handleSize);
                ctx->DrawRectangle(handleRect);
                break;
            }
            default:
                ctx->DrawCircle(handleCenter, handleSize / 2.0f);
                break;
        }
        
        // Handle border
        ctx->SetStrokeColor(controlStyle.borderColor);
        ctx->SetStrokeWidth(controlStyle.borderWidth);
        ctx->DrawCircle(handleCenter, handleSize / 2.0f);
    }
    
    void RenderValueDisplay() {
        std::string valueText;
        if (showValueAsPercent) {
            float percent = (GetValue() - GetMinValue()) / (GetMaxValue() - GetMinValue()) * 100.0f;
            valueText = std::to_string(static_cast<int>(percent)) + "%";
        } else {
            valueText = std::to_string(GetValue());
        }
        
        Point2D valuePos = CalculateValuePosition();
        
        ctx->SetTextColor(controlStyle.textColor);
        ctx->SetFont(controlStyle.fontFamily, controlStyle.fontSize * 0.9f);
        ctx->DrawText(valueText, valuePos);
    }
    
    Point2D CalculateLabelPosition() const {
        Rect2D bounds = GetBounds();
        
        switch (labelPosition) {
            case LabelPosition::Top:
                return Point2D(bounds.x, bounds.y + 15);
            case LabelPosition::Bottom:
                return Point2D(bounds.x, bounds.y + bounds.height + 15);
            case LabelPosition::Left:
                return Point2D(bounds.x - 55, bounds.y + bounds.height / 2 + 5);
            case LabelPosition::Right:
                return Point2D(bounds.x + bounds.width + 5, bounds.y + bounds.height / 2 + 5);
            default:
                return Point2D(bounds.x, bounds.y);
        }
    }
    
    Point2D CalculateValuePosition() const {
        Rect2D bounds = GetBounds();
        
        switch (valuePosition) {
            case LabelPosition::Top:
                return Point2D(bounds.x + bounds.width - 30, bounds.y + 15);
            case LabelPosition::Bottom:
                return Point2D(bounds.x + bounds.width - 30, bounds.y + bounds.height + 15);
            case LabelPosition::Left:
                return Point2D(bounds.x - 35, bounds.y + bounds.height / 2 + 5);
            case LabelPosition::Right:
                return Point2D(bounds.x + bounds.width + 25, bounds.y + bounds.height / 2 + 5);
            default:
                return Point2D(bounds.x, bounds.y);
        }
    }
};

// ===== RADIO BUTTON COMPONENT =====
class UltraCanvasRadioButton : public UltraCanvasElement {
private:
    StandardProperties properties;
    
    // Radio button properties
    std::string labelText;
    RadioStyle radioStyle = RadioStyle::Classic;
    int groupID = 0;
    bool selected = false;
    AdvancedControlStyle controlStyle;
    
    // Layout
    LabelPosition labelPosition = LabelPosition::Right;
    float radioSize = 16.0f;
    float labelSpacing = 6.0f;
    
public:
    UltraCanvasRadioButton(const std::string& identifier = "RadioButton", 
                          long id = 0, long x = 0, long y = 0, long w = 120, long h = 24)
        : UltraCanvasElement(identifier, id, x, y, w, h) {
        
        properties = StandardProperties(identifier, id, x, y, w, h);
        properties.MousePtr = MousePointer::Hand;
        properties.MouseCtrl = MouseControls::Button;
        
        controlStyle = AdvancedControlStyle::Default();
    }
    

    // ===== RADIO BUTTON MANAGEMENT =====
    void SetText(const std::string& text) {
        labelText = text;
    }
    
    const std::string& GetText() const { return labelText; }
    
    void SetStyle(RadioStyle style) {
        radioStyle = style;
    }
    
    RadioStyle GetStyle() const { return radioStyle; }
    
    void SetGroupID(int id) {
        groupID = id;
    }
    
    int GetGroupID() const { return groupID; }
    
    void SetSelected(bool isSelected) {
        if (selected != isSelected) {
            selected = isSelected;
            
            // Deselect other radio buttons in the same group
            if (selected && groupID != 0) {
                DeselectGroupSiblings();
            }
            
            if (onSelectionChanged) onSelectionChanged(selected);
        }
    }
    
    bool IsSelected() const { return selected; }
    
    void SetRadioSize(float size) {
        radioSize = size;
    }
    
    void SetLabelPosition(LabelPosition position) {
        labelPosition = position;
    }
    
    void SetControlStyle(const AdvancedControlStyle& style) {
        controlStyle = style;
    }
    
    // ===== RENDERING =====
    void Render() override {
        if (!IsVisible()) return;
        
        ctx->PushState();
        
        // Calculate positions
        Rect2D radioArea = GetRadioArea();
        Point2D labelPos = GetLabelPosition();
        
        // Render radio button
        RenderRadioButton(radioArea);
        
        // Render label
        if (!labelText.empty()) {
            ctx->SetTextColor(IsEnabled() ? controlStyle.textColor : controlStyle.disabledColor);
            ctx->SetFont(controlStyle.fontFamily, controlStyle.fontSize);
            ctx->DrawText(labelText, labelPos);
        }
        
        // Render focus indicator
        if (IsFocused()) {
            RenderFocusIndicator(radioArea);
        }
    }
    
    bool OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible() || !IsEnabled()) return false;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                if (Contains(event.x, event.y)) {
                    SetFocus(true);
                    if (!selected) {
                        SetSelected(true);
                        if (onSelected) onSelected();
                    }
                }
                break;
                
            case UCEventType::KeyDown:
                if (IsFocused() && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    if (!selected) {
                        SetSelected(true);
                        if (onSelected) onSelected();
                    }
                }
                break;
        }
        return false;
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void()> onSelected;
    std::function<void(bool)> onSelectionChanged;

private:

    Rect2D GetRadioArea() const {
        Rect2D bounds = GetBounds();
        
        switch (labelPosition) {
            case LabelPosition::Right:
            case LabelPosition::None:
                return Rect2D(bounds.x, bounds.y + (bounds.height - radioSize) / 2, radioSize, radioSize);
            case LabelPosition::Left:
                return Rect2D(bounds.x + bounds.width - radioSize, bounds.y + (bounds.height - radioSize) / 2, radioSize, radioSize);
            case LabelPosition::Top:
                return Rect2D(bounds.x + (bounds.width - radioSize) / 2, bounds.y + bounds.height - radioSize, radioSize, radioSize);
            case LabelPosition::Bottom:
                return Rect2D(bounds.x + (bounds.width - radioSize) / 2, bounds.y, radioSize, radioSize);
            default:
                return Rect2D(bounds.x, bounds.y, radioSize, radioSize);
        }
    }
    
    Point2D GetLabelPosition() const {
        Rect2D bounds = GetBounds();
        Rect2D radioArea = GetRadioArea();
        
        switch (labelPosition) {
            case LabelPosition::Right:
                return Point2D(radioArea.x + radioArea.width + labelSpacing, bounds.y + bounds.height / 2 + 4);
            case LabelPosition::Left:
                return Point2D(bounds.x, bounds.y + bounds.height / 2 + 4);
            case LabelPosition::Top:
                return Point2D(bounds.x + (bounds.width - GetTextWidth(labelText)) / 2, bounds.y + 15);
            case LabelPosition::Bottom:
                return Point2D(bounds.x + (bounds.width - GetTextWidth(labelText)) / 2, bounds.y + bounds.height);
            default:
                return Point2D(radioArea.x + radioArea.width + labelSpacing, bounds.y + bounds.height / 2 + 4);
        }
    }
    
    void RenderRadioButton(const Rect2D& area) {
        Color backgroundColor = GetBackgroundColor();
        Color borderColor = GetBorderColor();
        
        switch (radioStyle) {
            case RadioStyle::Classic:
                RenderClassicRadio(area, backgroundColor, borderColor);
                break;
            case RadioStyle::Square:
                RenderSquareRadio(area, backgroundColor, borderColor);
                break;
            case RadioStyle::Toggle:
                RenderToggleRadio(area, backgroundColor, borderColor);
                break;
            case RadioStyle::Card:
                RenderCardRadio(area, backgroundColor, borderColor);
                break;
            case RadioStyle::Button:
                RenderButtonRadio(area, backgroundColor, borderColor);
                break;
            default:
                RenderClassicRadio(area, backgroundColor, borderColor);
                break;
        }
    }
    
    void RenderClassicRadio(const Rect2D& area, const Color& bgColor, const Color& borderColor) {
        Point2D center(area.x + area.width / 2, area.y + area.height / 2);
        float radius = area.width / 2;
        
        // Outer circle
        ctx->SetFillColor(bgColor);
        ctx->DrawCircle(center, radius);
        
        ctx->SetStrokeColor(borderColor);
        ctx->SetStrokeWidth(controlStyle.borderWidth);
        ctx->DrawCircle(center, radius);
        
        // Inner dot if selected
        if (selected) {
           ctx->SetFillColor(controlStyle.selectedColor);
            ctx->DrawCircle(center, radius * 0.5f);
        }
    }
    
    void RenderSquareRadio(const Rect2D& area, const Color& bgColor, const Color& borderColor) {
       ctx->SetFillColor(bgColor);
        ctx->DrawRectangle(area);
        
        ctx->SetStrokeColor(borderColor);
        ctx->SetStrokeWidth(controlStyle.borderWidth);
        ctx->DrawRectangle(area);
        
        if (selected) {
            Rect2D innerRect(area.x + 3, area.y + 3, area.width - 6, area.height - 6);
           ctx->SetFillColor(controlStyle.selectedColor);
            ctx->DrawRectangle(innerRect);
        }
    }
    
    void RenderToggleRadio(const Rect2D& area, const Color& bgColor, const Color& borderColor) {
        // Toggle-style radio (like a small switch)
        float toggleWidth = area.width * 1.5f;
        float toggleHeight = area.height * 0.8f;
        Rect2D toggleArea(area.x, area.y + (area.height - toggleHeight) / 2, toggleWidth, toggleHeight);
        
        Color toggleBg = selected ? controlStyle.selectedColor : bgColor;
       ctx->SetFillColor(toggleBg);
        ctx->DrawRectangle(toggleArea);  // Would be rounded in full implementation
        
        // Handle
        float handleSize = toggleHeight * 0.8f;
        float handleX = selected ? (toggleArea.x + toggleArea.width - handleSize - 2) : (toggleArea.x + 2);
        Point2D handleCenter(handleX + handleSize / 2, toggleArea.y + toggleArea.height / 2);
        
       ctx->SetFillColor(Colors::White);
        ctx->DrawCircle(handleCenter, handleSize / 2);
    }
    
    void RenderCardRadio(const Rect2D& area, const Color& bgColor, const Color& borderColor) {
        // Render as a card that can be selected
        Rect2D cardArea = GetBounds();
        
        Color cardBg = selected ? controlStyle.selectedColor.WithAlpha(50) : bgColor;
        Color cardBorder = selected ? controlStyle.selectedColor : borderColor;
        
       ctx->SetFillColor(cardBg);
        ctx->DrawRectangle(cardArea);
        
        ctx->SetStrokeColor(cardBorder);
        ctx->SetStrokeWidth(selected ? 2.0f : controlStyle.borderWidth);
        ctx->DrawRectangle(cardArea);
    }
    
    void RenderButtonRadio(const Rect2D& area, const Color& bgColor, const Color& borderColor) {
        // Render as a button that appears pressed when selected
        Rect2D buttonArea = GetBounds();
        
        Color buttonBg = selected ? controlStyle.selectedColor : bgColor;
        Color textColor = selected ? Colors::White : controlStyle.textColor;
        
       ctx->SetFillColor(buttonBg);
        ctx->DrawRectangle(buttonArea);
        
        ctx->SetStrokeColor(borderColor);
        ctx->SetStrokeWidth(controlStyle.borderWidth);
        ctx->DrawRectangle(buttonArea);
    }
    
    void RenderFocusIndicator(const Rect2D& area) {
        ctx->SetStrokeColor(controlStyle.focusColor);
        SetStrokeWidth(2.0f);
        
        Rect2D focusRect(area.x - 2, area.y - 2, area.width + 4, area.height + 4);
        ctx->DrawRectangle(focusRect);
    }
    
    Color GetBackgroundColor() const {
        if (!IsEnabled()) return controlStyle.disabledBackgroundColor;
        if (IsPressed()) return controlStyle.pressedColor;
        if (IsHovered()) return controlStyle.hoverColor;
        return controlStyle.normalColor;
    }
    
    Color GetBorderColor() const {
        if (!IsEnabled()) return controlStyle.disabledColor;
        if (selected) return controlStyle.selectedColor;
        return controlStyle.borderColor;
    }
    
    void DeselectGroupSiblings() {
        // This would require access to parent container or radio group manager
        // Implementation would depend on the group management system
    }
};

// ===== SWITCH COMPONENT =====
class UltraCanvasSwitch : public UltraCanvasElement {
private:
    StandardProperties properties;
    
    // Switch properties
    std::string labelText;
    SwitchStyle switchStyle = SwitchStyle::Toggle;
    bool switchState = false;
    AdvancedControlStyle controlStyle;
    
    // Layout
    LabelPosition labelPosition = LabelPosition::Right;
    float switchWidth = 44.0f;
    float switchHeight = 24.0f;
    float labelSpacing = 8.0f;
    
    // Animation
    float animationProgress = 0.0f;
    std::chrono::steady_clock::time_point lastUpdate;
    bool animating = false;
    
public:
    UltraCanvasSwitch(const std::string& identifier = "Switch", 
                     long id = 0, long x = 0, long y = 0, long w = 80, long h = 32)
        : UltraCanvasElement(identifier, id, x, y, w, h) {
        
        properties = StandardProperties(identifier, id, x, y, w, h);
        properties.MousePtr = MousePointer::Hand;
        properties.MouseCtrl = MouseControls::Button;
        
        controlStyle = AdvancedControlStyle::Default();
        lastUpdate = std::chrono::steady_clock::now();
    }
    

    // ===== SWITCH MANAGEMENT =====
    void SetText(const std::string& text) {
        labelText = text;
    }
    
    const std::string& GetText() const { return labelText; }
    
    void SetStyle(SwitchStyle style) {
        switchStyle = style;
    }
    
    SwitchStyle GetStyle() const { return switchStyle; }
    
    void SetState(bool on) {
        if (switchState != on) {
            switchState = on;
            
            if (controlStyle.enableAnimations) {
                StartAnimation();
            } else {
                animationProgress = on ? 1.0f : 0.0f;
            }
            
            if (onToggle) onToggle(switchState);
        }
    }
    
    bool GetState() const { return switchState; }
    
    void Toggle() {
        SetState(!switchState);
    }
    
    void SetSwitchSize(float width, float height) {
        switchWidth = width;
        switchHeight = height;
    }
    
    void SetLabelPosition(LabelPosition position) {
        labelPosition = position;
    }
    
    void SetControlStyle(const AdvancedControlStyle& style) {
        controlStyle = style;
    }
    
    // ===== RENDERING =====
    void Render() override {
        if (!IsVisible()) return;
        
        ctx->PushState();
        
        // Update animation
        if (animating) {
            UpdateAnimation();
        }
        
        // Calculate positions
        Rect2D switchArea = GetSwitchArea();
        Point2D labelPos = GetLabelPosition();
        
        // Render switch
        RenderSwitch(switchArea);
        
        // Render label
        if (!labelText.empty()) {
            ctx->SetTextColor(IsEnabled() ? controlStyle.textColor : controlStyle.disabledColor);
            ctx->SetFont(controlStyle.fontFamily, controlStyle.fontSize);
            ctx->DrawText(labelText, labelPos);
        }
        
        // Render focus indicator
        if (IsFocused()) {
            RenderFocusIndicator(switchArea);
        }
    }
    
    bool OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible() || !IsEnabled()) return false;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                if (Contains(event.x, event.y)) {
                    SetFocus(true);
                    Toggle();
                }
                break;
                
            case UCEventType::KeyDown:
                if (IsFocused() && (event.virtualKey == UCKeys::Space || event.virtualKey == UCKeys::Return)) {
                    Toggle();
                }
                break;
        }
        return false;
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void(bool)> onToggle;

private:
    Rect2D GetSwitchArea() const {
        Rect2D bounds = GetBounds();
        
        switch (labelPosition) {
            case LabelPosition::Right:
            case LabelPosition::None:
                return Rect2D(bounds.x, bounds.y + (bounds.height - switchHeight) / 2, switchWidth, switchHeight);
            case LabelPosition::Left:
                return Rect2D(bounds.x + bounds.width - switchWidth, bounds.y + (bounds.height - switchHeight) / 2, switchWidth, switchHeight);
            case LabelPosition::Top:
                return Rect2D(bounds.x + (bounds.width - switchWidth) / 2, bounds.y + bounds.height - switchHeight, switchWidth, switchHeight);
            case LabelPosition::Bottom:
                return Rect2D(bounds.x + (bounds.width - switchWidth) / 2, bounds.y, switchWidth, switchHeight);
            default:
                return Rect2D(bounds.x, bounds.y, switchWidth, switchHeight);
        }
    }
    
    Point2D GetLabelPosition() const {
        Rect2D bounds = GetBounds();
        Rect2D switchArea = GetSwitchArea();
        
        switch (labelPosition) {
            case LabelPosition::Right:
                return Point2D(switchArea.x + switchArea.width + labelSpacing, bounds.y + bounds.height / 2 + 4);
            case LabelPosition::Left:
                return Point2D(bounds.x, bounds.y + bounds.height / 2 + 4);
            case LabelPosition::Top:
                return Point2D(bounds.x + (bounds.width - GetTextWidth(labelText)) / 2, bounds.y + 15);
            case LabelPosition::Bottom:
                return Point2D(bounds.x + (bounds.width - GetTextWidth(labelText)) / 2, bounds.y + bounds.height);
            default:
                return Point2D(switchArea.x + switchArea.width + labelSpacing, bounds.y + bounds.height / 2 + 4);
        }
    }
    
    void RenderSwitch(const Rect2D& area) {
        switch (switchStyle) {
            case SwitchStyle::Toggle:
                RenderToggleSwitch(area);
                break;
            case SwitchStyle::iOS:
                RenderIOSSwitch(area);
                break;
            case SwitchStyle::Material:
                RenderMaterialSwitch(area);
                break;
            case SwitchStyle::Checkbox:
                RenderCheckboxSwitch(area);
                break;
            case SwitchStyle::Button:
                RenderButtonSwitch(area);
                break;
            default:
                RenderToggleSwitch(area);
                break;
        }
    }
    
    void RenderToggleSwitch(const Rect2D& area) {
        // Background track
        Color trackColor = switchState ? controlStyle.selectedColor : controlStyle.backgroundColor;
       ctx->SetFillColor(trackColor);
        ctx->DrawRectangle(area);  // Would be rounded in full implementation
        
        ctx->SetStrokeColor(controlStyle.borderColor);
        SetStrokeWidth(controlStyle.borderWidth);
        ctx->DrawRectangle(area);
        
        // Thumb/handle
        float thumbSize = area.height * 0.8f;
        float thumbTravel = area.width - thumbSize - 4;
        float thumbX = area.x + 2 + (thumbTravel * animationProgress);
        float thumbY = area.y + (area.height - thumbSize) / 2;
        
        Point2D thumbCenter(thumbX + thumbSize / 2, thumbY + thumbSize / 2);
        
       ctx->SetFillColor(Colors::White);
        ctx->DrawCircle(thumbCenter, thumbSize / 2);
        
        ctx->SetStrokeColor(controlStyle.borderColor);
        SetStrokeWidth(1.0f);
        ctx->DrawCircle(thumbCenter, thumbSize / 2);
    }
    
    void RenderIOSSwitch(const Rect2D& area) {
        // iOS-style switch with more rounded appearance
        Color trackColor = switchState ? Color(52, 199, 89) : Color(229, 229, 234);
       ctx->SetFillColor(trackColor);
        ctx->DrawRectangle(area);  // Would be fully rounded in complete implementation
        
        // Thumb
        float thumbSize = area.height - 4;
        float thumbTravel = area.width - thumbSize - 4;
        float thumbX = area.x + 2 + (thumbTravel * animationProgress);
        float thumbY = area.y + 2;
        
        Point2D thumbCenter(thumbX + thumbSize / 2, thumbY + thumbSize / 2);
        
        // Thumb shadow
        Point2D shadowCenter(thumbCenter.x, thumbCenter.y + 1);
       ctx->SetFillColor(Color(0, 0, 0, 30));
        ctx->DrawCircle(shadowCenter, thumbSize / 2);
        
       ctx->SetFillColor(Colors::White);
        ctx->DrawCircle(thumbCenter, thumbSize / 2);
    }
    
    void RenderMaterialSwitch(const Rect2D& area) {
        // Material Design switch
        Color trackColor = switchState ? controlStyle.selectedColor.WithAlpha(128) : Color(0, 0, 0, 38);
       ctx->SetFillColor(trackColor);
        ctx->DrawRectangle(area);
        
        // Thumb
        float thumbSize = switchState ? area.height : area.height * 0.7f;
        float thumbTravel = area.width - thumbSize - 4;
        float thumbX = area.x + 2 + (thumbTravel * animationProgress);
        float thumbY = area.y + (area.height - thumbSize) / 2;
        
        Point2D thumbCenter(thumbX + thumbSize / 2, thumbY + thumbSize / 2);
        
        Color thumbColor = switchState ? controlStyle.selectedColor : Color(250, 250, 250);
       ctx->SetFillColor(thumbColor);
        ctx->DrawCircle(thumbCenter, thumbSize / 2);
        
        // Ripple effect when pressed
        if (IsPressed()) {
           ctx->SetFillColor(Color(158, 158, 158, 100));
            ctx->DrawCircle(thumbCenter, thumbSize);
        }
    }
    
    void RenderCheckboxSwitch(const Rect2D& area) {
        // Checkbox-style toggle
        Color bgColor = switchState ? controlStyle.selectedColor : controlStyle.backgroundColor;
       ctx->SetFillColor(bgColor);
        ctx->DrawRectangle(area);
        
        ctx->SetStrokeColor(controlStyle.borderColor);
        SetStrokeWidth(controlStyle.borderWidth);
        ctx->DrawRectangle(area);
        
        if (switchState) {
            // Draw checkmark
            ctx->SetStrokeColor(Colors::White);
            SetStrokeWidth(2.0f);
            
            Point2D p1(area.x + area.width * 0.3f, area.y + area.height * 0.5f);
            Point2D p2(area.x + area.width * 0.45f, area.y + area.height * 0.7f);
            Point2D p3(area.x + area.width * 0.7f, area.y + area.height * 0.3f);
            
            ctx->DrawLine(p1, p2);
            ctx->DrawLine(p2, p3);
        }
    }
    
    void RenderButtonSwitch(const Rect2D& area) {
        // Button-style toggle
        Rect2D buttonArea = GetBounds();
        
        Color buttonBg = switchState ? controlStyle.selectedColor : controlStyle.backgroundColor;
        Color textColor = switchState ? Colors::White : controlStyle.textColor;
        
       ctx->SetFillColor(buttonBg);
        ctx->DrawRectangle(buttonArea);
        
        ctx->SetStrokeColor(controlStyle.borderColor);
        SetStrokeWidth(controlStyle.borderWidth);
        ctx->DrawRectangle(buttonArea);
        
        // Show ON/OFF text
        std::string stateText = switchState ? "ON" : "OFF";
        ctx->SetTextColor(textColor);
        ctx->SetFont(controlStyle.fontFamily, controlStyle.fontSize * 0.8f);
        
        Point2D textPos(buttonArea.x + (buttonArea.width - GetTextWidth(stateText)) / 2, 
                       buttonArea.y + buttonArea.height / 2 + 4);
        DrawText(stateText, textPos);
    }
    
    void RenderFocusIndicator(const Rect2D& area) {
        ctx->SetStrokeColor(controlStyle.focusColor);
        SetStrokeWidth(2.0f);
        
        Rect2D focusRect(area.x - 2, area.y - 2, area.width + 4, area.height + 4);
        ctx->DrawRectangle(focusRect);
    }
    
    void StartAnimation() {
        animating = true;
        lastUpdate = std::chrono::steady_clock::now();
    }
    
    void UpdateAnimation() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);
        
        float deltaTime = elapsed.count() / 1000.0f;
        float animationSpeed = 1.0f / controlStyle.animationDuration;
        
        float targetProgress = switchState ? 1.0f : 0.0f;
        float progressDelta = animationSpeed * deltaTime;
        
        if (animationProgress < targetProgress) {
            animationProgress = std::min(targetProgress, animationProgress + progressDelta);
        } else if (animationProgress > targetProgress) {
            animationProgress = std::max(targetProgress, animationProgress - progressDelta);
        }
        
        if (std::abs(animationProgress - targetProgress) < 0.01f) {
            animationProgress = targetProgress;
            animating = false;
        }
        
        lastUpdate = now;
    }
};

// ===== RADIO GROUP MANAGER =====
class UltraCanvasRadioGroup : public UltraCanvasElement {
private:
    StandardProperties properties;
    std::vector<std::shared_ptr<UltraCanvasRadioButton>> radioButtons;
    std::string groupLabel;
    int selectedIndex = -1;
    bool allowNoneSelected = false;
    
public:
    UltraCanvasRadioGroup(const std::string& identifier = "RadioGroup", 
                         long id = 0, long x = 0, long y = 0, long w = 200, long h = 100)
        : UltraCanvasElement(identifier, id, x, y, w, h) {
        
        properties = StandardProperties(identifier, id, x, y, w, h);
    }
    

    void AddRadioButton(std::shared_ptr<UltraCanvasRadioButton> button) {
        radioButtons.push_back(button);
        AddChild(button.get());
        
        button->SetGroupID(GetID());
        button->onSelected = [this, button]() {
            SelectButton(button);
        };
        
        ArrangeButtons();
    }
    
    void AddRadioButton(const std::string& text) {
        auto button = CreateRadioButton("radio_" + std::to_string(radioButtons.size()), 
                                       radioButtons.size(), 0, 0, 150, 24);
        button->SetText(text);
        AddRadioButton(button);
    }
    
    void SetSelectedIndex(int index) {
        if (index >= 0 && index < static_cast<int>(radioButtons.size())) {
            if (selectedIndex >= 0) {
                radioButtons[selectedIndex]->SetSelected(false);
            }
            
            selectedIndex = index;
            radioButtons[index]->SetSelected(true);
            
            if (onSelectionChanged) onSelectionChanged(index);
        } else if (allowNoneSelected && index == -1) {
            if (selectedIndex >= 0) {
                radioButtons[selectedIndex]->SetSelected(false);
            }
            selectedIndex = -1;
            
            if (onSelectionChanged) onSelectionChanged(-1);
        }
    }
    
    int GetSelectedIndex() const { return selectedIndex; }
    
    std::string GetSelectedValue() const {
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(radioButtons.size())) {
            return radioButtons[selectedIndex]->GetText();
        }
        return "";
    }
    
    void SetGroupLabel(const std::string& label) {
        groupLabel = label;
    }
    
    void SetAllowNoneSelected(bool allow) {
        allowNoneSelected = allow;
    }
    
    void Render() override {
        if (!IsVisible()) return;
        
        ctx->PushState();
        
        // Render group label if present
        if (!groupLabel.empty()) {
            SetTextColor(Colors::Black);
            SetFont("Arial", 14.0f);
            DrawText(groupLabel, Point2D(GetX(), GetY() + 15));
        }
        
        // Children (radio buttons) render themselves
    }
    
    bool OnEvent(const UCEvent& event) override {
        // Events handled by individual radio buttons
        return false;
    }
    
    std::function<void(int)> onSelectionChanged;

private:

    void SelectButton(std::shared_ptr<UltraCanvasRadioButton> button) {
        for (size_t i = 0; i < radioButtons.size(); ++i) {
            if (radioButtons[i] == button) {
                SetSelectedIndex(static_cast<int>(i));
                break;
            }
        }
    }
    
    void ArrangeButtons() {
        float currentY = GetY() + (!groupLabel.empty() ? 25 : 5);
        
        for (auto& button : radioButtons) {
            button->SetPosition(GetX() + 10, static_cast<long>(currentY));
            currentY += 30;
        }
        
        // Update container height
        float totalHeight = currentY - GetY() + 5;
        properties.height_size = static_cast<long>(totalHeight);
    }
};

// ===== STYLE IMPLEMENTATIONS =====
inline AdvancedControlStyle AdvancedControlStyle::Material() {
    AdvancedControlStyle style;
    style.primaryColor = Color(25, 118, 210);
    style.secondaryColor = Color(156, 39, 176);
    style.borderRadius = 4.0f;
    style.enableRippleEffect = true;
    style.animationDuration = 0.3f;
    return style;
}

inline AdvancedControlStyle AdvancedControlStyle::iOS() {
    AdvancedControlStyle style;
    style.primaryColor = Color(52, 199, 89);
    style.borderRadius = 8.0f;
    style.showShadow = true;
    style.animationDuration = 0.25f;
    return style;
}

inline AdvancedControlStyle AdvancedControlStyle::Flat() {
    AdvancedControlStyle style;
    style.borderWidth = 0.0f;
    style.showShadow = false;
    style.backgroundColor = Color(245, 245, 245);
    style.enableAnimations = false;
    return style;
}

inline AdvancedControlStyle AdvancedControlStyle::Dark() {
    AdvancedControlStyle style;
    style.backgroundColor = Color(50, 50, 50);
    style.borderColor = Color(100, 100, 100);
    style.textColor = Color(220, 220, 220);
    style.primaryColor = Color(100, 150, 255);
    return style;
}

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasAdvancedSlider> CreateAdvancedSlider(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasAdvancedSlider>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasRadioButton> CreateRadioButton(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasRadioButton>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasSwitch> CreateSwitch(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasSwitch>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasRadioGroup> CreateRadioGroup(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasRadioGroup>(
        id, identifier, id, x, y, w, h);
}

} // namespace UltraCanvas