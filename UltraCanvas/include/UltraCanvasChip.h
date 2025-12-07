// include/UltraCanvasChip.h
// Modern chip component for input, choice, filter, and action interactions
// Version: 1.0.0
// Last Modified: 2025-01-01
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasImageElement.h"
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <optional>

namespace UltraCanvas {

// ===== CHIP TYPE DEFINITIONS =====
enum class ChipType {
    Input,      // For user input with optional removal
    Choice,     // Single selection from a set of options
    Filter,     // Toggle filtering options
    Action,     // Trigger actions or commands
    Assist      // AI/assistant suggestions
};

enum class ChipBorderStyle {
    NoBorder,       // No border
    Outline,    // Outlined border
    Filled,     // Filled background
    Elevated,   // Shadow/elevation effect
    Flat        // Flat design
};

enum class ChipSize {
    Small,      // Compact size for dense layouts
    Medium,     // Standard size
    Large       // Large size for accessibility
};

enum class ChipState {
    Normal,     // Default state
    Hovered,    // Mouse hover
    Pressed,    // Being pressed
    Selected,   // Selected state
    Disabled,   // Disabled state
    Loading     // Loading/processing state
};

// ===== CHIP ICON SYSTEM =====
struct ChipIcon {
    std::string iconPath;
    std::string iconText;           // Unicode icon or text
    bool isThumbnail = false;
    bool isAvatar = false;
    Color iconColor = Colors::Black;
    float iconSize = 16.0f;
    
    ChipIcon() = default;
    ChipIcon(const std::string& path) : iconPath(path) {}
    ChipIcon(const std::string& text, bool isText) : iconText(text) {}
    
    static ChipIcon FromPath(const std::string& path) {
        return ChipIcon(path);
    }
    
    static ChipIcon FromText(const std::string& text) {
        return ChipIcon(text, true);
    }
    
    static ChipIcon Avatar(const std::string& path) {
        ChipIcon icon(path);
        icon.isAvatar = true;
        icon.isThumbnail = true;
        return icon;
    }
    
    bool IsEmpty() const {
        return iconPath.empty() && iconText.empty();
    }
};

// ===== CHIP STYLING =====
struct ChipStyle {
    // Colors
    Color backgroundColor = Color(240, 240, 240);
    Color backgroundHoverColor = Color(230, 230, 230);
    Color backgroundSelectedColor = Color(25, 118, 210);
    Color backgroundDisabledColor = Color(250, 250, 250);
    
    Color borderColor = Color(200, 200, 200);
    Color borderHoverColor = Color(180, 180, 180);
    Color borderSelectedColor = Color(25, 118, 210);
    Color borderDisabledColor = Color(220, 220, 220);
    
    Color textColor = Color(60, 60, 60);
    Color textHoverColor = Color(40, 40, 40);
    Color textSelectedColor = Color(255, 255, 255);
    Color textDisabledColor = Color(180, 180, 180);
    
    Color iconColor = Color(100, 100, 100);
    Color iconSelectedColor = Color(255, 255, 255);
    Color removeIconColor = Color(120, 120, 120);
    Color removeIconHoverColor = Color(200, 50, 50);
    
    // Typography
    std::string fontFamily = "Sans";
    float fontSize = 13.0f;
    FontWeight fontWeight = FontWeight::Medium;
    
    // Dimensions
    float height = 32.0f;
    float paddingHorizontal = 12.0f;
    float paddingVertical = 6.0f;
    float borderRadius = 16.0f;
    float borderWidth = 1.0f;
    
    // Spacing
    float iconSpacing = 6.0f;
    float removeIconSpacing = 4.0f;
    float removeIconSize = 16.0f;
    
    // Effects
    bool showShadow = false;
    Color shadowColor = Color(0, 0, 0, 50);
    Point2D shadowOffset = Point2D(0, 2);
    float shadowBlur = 4.0f;
    
    // Animation
    float animationDuration = 0.15f;
    bool enableHoverAnimation = true;
    bool enableRippleEffect = true;
    
    static ChipStyle Default() { return ChipStyle(); }
    
    static ChipStyle Material() {
        ChipStyle style;
        style.backgroundColor = Color(240, 240, 240);
        style.backgroundSelectedColor = Color(25, 118, 210);
        style.borderRadius = 16.0f;
        style.enableRippleEffect = true;
        return style;
    }
    
    static ChipStyle Outlined() {
        ChipStyle style;
        style.backgroundColor = Colors::Transparent;
        style.backgroundSelectedColor = Color(25, 118, 210, 30);
        style.borderColor = Color(25, 118, 210);
        style.borderSelectedColor = Color(25, 118, 210);
        style.textColor = Color(25, 118, 210);
        style.textSelectedColor = Color(25, 118, 210);
        return style;
    }
    
    static ChipStyle Flat() {
        ChipStyle style;
        style.backgroundColor = Color(245, 245, 245);
        style.borderWidth = 0.0f;
        style.borderRadius = 8.0f;
        return style;
    }
    
    static ChipStyle Elevated() {
        ChipStyle style;
        style.backgroundColor = Colors::White;
        style.showShadow = true;
        style.borderWidth = 0.0f;
        return style;
    }
};

// ===== CHIP DATA STRUCTURE =====
struct ChipData {
    std::string text;
    std::string value;              // Optional value different from display text
    ChipIcon leadingIcon;
    ChipIcon trailingIcon;
    bool selected = false;
    bool enabled = true;
    bool removable = false;
    std::string tooltip;
    void* userData = nullptr;       // Custom user data
    
    ChipData() = default;
    ChipData(const std::string& chipText) : text(chipText), value(chipText) {}
    ChipData(const std::string& chipText, const std::string& chipValue) : text(chipText), value(chipValue) {}
    
    bool IsEmpty() const { return text.empty(); }
};

// ===== MAIN CHIP COMPONENT =====
class UltraCanvasChip : public UltraCanvasUIElement {
private:
    // ===== STANDARD PROPERTIES (REQUIRED) =====
    StandardProperties properties;
    
    // ===== CHIP PROPERTIES =====
    ChipData chipData;
    ChipType chipType = ChipType::Action;
    ChipBorderStyle borderStyle = ChipBorderStyle::Filled;
    ChipSize chipSize = ChipSize::Medium;
    ChipState currentState = ChipState::Normal;
    ChipStyle style;
    
    // ===== INTERACTION STATE =====
    bool isPressed = false;
    bool showRemoveIcon = false;
    std::chrono::steady_clock::time_point lastStateChange;
    
    // ===== LAYOUT CACHE =====
    mutable Rect2D textBounds;
    mutable Rect2D iconBounds;
    mutable Rect2D removeIconBounds;
    mutable bool layoutDirty = true;
    
public:
    // ===== CONSTRUCTOR (REQUIRED PATTERN) =====
    UltraCanvasChip(const std::string& identifier = "Chip", 
                   long id = 0, long x = 0, long y = 0, long w = 100, long h = 32)
        : UltraCanvasUIElement(identifier, id, x, y, w, h) {
        
        // Initialize standard properties
        properties = StandardProperties(identifier, id, x, y, w, h);
        mousePtr = MousePointer::Hand;

        // Initialize default style
        style = ChipStyle::Default();
        
        // Initialize timing
        lastStateChange = std::chrono::steady_clock::now();
    }
    

    // ===== CHIP CONTENT MANAGEMENT =====
    void SetText(const std::string& text) {
        chipData.text = text;
        if (chipData.value.empty()) {
            chipData.value = text;
        }
        InvalidateLayout();
    }
    
    const std::string& GetText() const { return chipData.text; }
    
    void SetValue(const std::string& value) {
        chipData.value = value;
    }
    
    const std::string& GetValue() const { return chipData.value; }
    
    void SetData(const ChipData& data) {
        chipData = data;
        InvalidateLayout();
    }
    
    const ChipData& GetData() const { return chipData; }
    
    // ===== ICON MANAGEMENT =====
    void SetLeadingIcon(const ChipIcon& icon) {
        chipData.leadingIcon = icon;
        InvalidateLayout();
    }
    
    void SetTrailingIcon(const ChipIcon& icon) {
        chipData.trailingIcon = icon;
        InvalidateLayout();
    }
    
    void SetIcon(const ChipIcon& icon) {
        SetLeadingIcon(icon);
    }
    
    const ChipIcon& GetLeadingIcon() const { return chipData.leadingIcon; }
    const ChipIcon& GetTrailingIcon() const { return chipData.trailingIcon; }
    
    void ClearIcons() {
        chipData.leadingIcon = ChipIcon();
        chipData.trailingIcon = ChipIcon();
        InvalidateLayout();
    }
    
    // ===== CHIP TYPE AND STYLE =====
    void SetType(ChipType type) {
        chipType = type;
        
        // Adjust default behavior based on type
        switch (type) {
            case ChipType::Input:
                SetRemovable(true);
                break;
            case ChipType::Choice:
            case ChipType::Filter:
                // These support selection
                break;
            case ChipType::Action:
            case ChipType::Assist:
                SetRemovable(false);
                break;
        }
    }
    
    ChipType GetType() const { return chipType; }
    
    void SetBorderStyle(ChipBorderStyle borderStyleType) {
        borderStyle = borderStyleType;
        
        // Apply appropriate style
        switch (borderStyleType) {
            case ChipBorderStyle::NoBorder:
                style.borderWidth = 0.0f;
                break;
            case ChipBorderStyle::Outline:
                style = ChipStyle::Outlined();
                break;
            case ChipBorderStyle::Filled:
                style = ChipStyle::Default();
                break;
            case ChipBorderStyle::Elevated:
                style = ChipStyle::Elevated();
                break;
            case ChipBorderStyle::Flat:
                style = ChipStyle::Flat();
                break;
        }
    }
    
    ChipBorderStyle GetBorderStyle() const { return borderStyle; }
    
    void SetChipStyle(const ChipStyle& chipStyle) {
        style = chipStyle;
        InvalidateLayout();
    }
    
    const ChipStyle& GetChipStyle() const { return style; }
    
    void SetSize(ChipSize size) {
        chipSize = size;
        
        // Adjust style based on size
        switch (size) {
            case ChipSize::Small:
                style.height = 24.0f;
                style.fontSize = 11.0f;
                style.paddingHorizontal = 8.0f;
                style.borderRadius = 12.0f;
                break;
            case ChipSize::Medium:
                style.height = 32.0f;
                style.fontSize = 13.0f;
                style.paddingHorizontal = 12.0f;
                style.borderRadius = 16.0f;
                break;
            case ChipSize::Large:
                style.height = 40.0f;
                style.fontSize = 15.0f;
                style.paddingHorizontal = 16.0f;
                style.borderRadius = 20.0f;
                break;
        }
        
        // Update component height
        properties.height_size = static_cast<long>(style.height);
        InvalidateLayout();
    }
    
    ChipSize GetSize() const { return chipSize; }
    
    // ===== SELECTION AND STATE =====
    void SetSelected(bool selected) {
        if (chipData.selected != selected) {
            chipData.selected = selected;
            UpdateState();
            if (onSelectionChanged) onSelectionChanged(selected);
        }
    }
    
    bool IsSelected() const { return chipData.selected; }
    
    void SetEnabled(bool enabled) override {
        UltraCanvasUIElement::SetEnabled(enabled);
        chipData.enabled = enabled;
        UpdateState();
    }
    
    bool IsEnabled() const { return chipData.enabled; }
    
    void SetRemovable(bool removable) {
        bool wasRemovable = showRemoveIcon;
        showRemoveIcon = removable;
        chipData.removable = removable;
        
        if (wasRemovable != removable) {
            InvalidateLayout();
        }
    }
    
    bool IsRemovable() const { return showRemoveIcon; }
    
    ChipState GetState() const { return currentState; }
    
    // ===== AUTO-SIZING =====
    void AutoResize() {
        CalculateLayout();
        
        float requiredWidth = CalculateRequiredWidth();
        
        properties.width_size = static_cast<long>(requiredWidth);
        properties.height_size = static_cast<long>(style.height);
        
        InvalidateLayout();
    }
    
    Point2D GetPreferredSize() const {
        float width = CalculateRequiredWidth();
        return Point2D(width, style.height);
    }
    
    // ===== INTERACTION =====
    void Click() {
        if (!chipData.enabled) return;
        
        if (chipType == ChipType::Choice || chipType == ChipType::Filter) {
            SetSelected(!chipData.selected);
        }
        
        if (onClick) onClick();
    }
    
    void Remove() {
        if (!showRemoveIcon || !chipData.enabled) return;
        
        if (onRemove) onRemove();
    }
    
    // ===== TOOLTIP =====
    void SetTooltip(const std::string& tooltipText) {
        chipData.tooltip = tooltipText;
    }
    
    const std::string& GetTooltip() const { return chipData.tooltip; }
    
    // ===== USER DATA =====
    void SetUserData(void* userData) {
        chipData.userData = userData;
    }
    
    void* GetUserData() const { return chipData.userData; }
    
    // ===== RENDERING (REQUIRED OVERRIDE) =====
    void Render(IRenderContext* ctx) override {
        if (!IsVisible()) return;
        
        ctx->PushState();
        
        // Update layout if needed
        if (layoutDirty) {
            CalculateLayout();
        }
        
        // Get current colors based on state
        Color bgColor = GetBackgroundColor();
        Color borderColor = GetBorderColor();
        Color textColor = GetTextColor();
        
        Rect2D bounds = GetBounds();
        
        // Draw shadow if enabled
        if (style.showShadow && currentState != ChipState::Pressed) {
            DrawShadow(bounds);
        }
        
        // Draw background
        if (style.borderRadius > 0) {
            ctx->DrawRoundedRectangle(bounds, style.borderRadius, bgColor, borderColor, style.borderWidth);
        } else {
            UltraCanvas::DrawFilledRect(bounds, bgColor, borderColor, style.borderWidth);
        }
        
        // Draw leading icon
        if (!chipData.leadingIcon.IsEmpty()) {
            DrawIcon(chipData.leadingIcon, iconBounds);
        }
        
        // Draw text
        if (!chipData.text.empty()) {
            ctx->PaintWidthColortextColor);
            ctx->SetFont(style.fontFamily, style.fontSize);
            ctx->DrawText(chipData.text, Point2D(textBounds.x, textBounds.y + textBounds.height * 0.7f));
        }
        
        // Draw trailing icon
        if (!chipData.trailingIcon.IsEmpty()) {
            DrawIcon(chipData.trailingIcon, Rect2D(0, 0, 0, 0)); // Would need proper bounds
        }
        
        // Draw remove icon
        if (showRemoveIcon && chipData.enabled) {
            DrawRemoveIcon();
        }
        
        // Draw ripple effect if pressed
        if (style.enableRippleEffect && currentState == ChipState::Pressed) {
            DrawRippleEffect(bounds);
        }
    }
    
    // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
    bool OnEvent(const UCEvent& event) override {
        if (IsDisabled() || !IsVisible() || !chipData.enabled) return false;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;
                
            case UCEventType::MouseEnter:
                SetState(ChipState::Hovered);
                if (onHoverEnter) onHoverEnter();
                break;
                
            case UCEventType::MouseLeave:
                SetState(ChipState::Normal);
                isPressed = false;
                if (onHoverLeave) onHoverLeave();
                break;
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
        }
        return false;
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void()> onClick;
    std::function<void()> onRemove;
    std::function<void(bool)> onSelectionChanged;
    std::function<void()> onHoverEnter;
    std::function<void()> onHoverLeave;
    std::function<void(ChipState)> onStateChanged;

private:
    // ===== LAYOUT CALCULATION =====
    void CalculateLayout() const {
        if (!layoutDirty) return;
        
        Rect2D bounds = GetBounds();
        float currentX = bounds.x + style.paddingHorizontal;
        float centerY = bounds.y + bounds.height / 2.0f;
        
        // Leading icon bounds
        if (!chipData.leadingIcon.IsEmpty()) {
            float iconSize = chipData.leadingIcon.iconSize;
            iconBounds = Rect2D(currentX, centerY - iconSize / 2.0f, iconSize, iconSize);
            currentX += iconSize + style.iconSpacing;
        }
        
        // Text bounds
        if (!chipData.text.empty()) {
            float textWidth = GetTextWidth(chipData.text);
            float textHeight = style.fontSize;
            textBounds = Rect2D(currentX, centerY - textHeight / 2.0f, textWidth, textHeight);
            currentX += textWidth;
        }
        
        // Remove icon bounds
        if (showRemoveIcon) {
            currentX += style.removeIconSpacing;
            float iconSize = style.removeIconSize;
            removeIconBounds = Rect2D(currentX, centerY - iconSize / 2.0f, iconSize, iconSize);
        }
        
        layoutDirty = false;
    }
    
    void InvalidateLayout() {
        layoutDirty = true;
    }
    
    float CalculateRequiredWidth() const {
        float width = style.paddingHorizontal * 2;
        
        // Leading icon
        if (!chipData.leadingIcon.IsEmpty()) {
            width += chipData.leadingIcon.iconSize + style.iconSpacing;
        }
        
        // Text
        if (!chipData.text.empty()) {
            width += GetTextWidth(chipData.text);
        }
        
        // Remove icon
        if (showRemoveIcon) {
            width += style.removeIconSpacing + style.removeIconSize;
        }
        
        return width;
    }
    
    // ===== STATE MANAGEMENT =====
    void SetState(ChipState newState) {
        if (currentState != newState) {
            currentState = newState;
            lastStateChange = std::chrono::steady_clock::now();
            if (onStateChanged) onStateChanged(newState);
        }
    }
    
    void UpdateState() {
        if (!chipData.enabled) {
            SetState(ChipState::Disabled);
        } else if (chipData.selected) {
            SetState(ChipState::Selected);
        } else if (isPressed) {
            SetState(ChipState::Pressed);
        } else {
            SetState(ChipState::Normal);
        }
    }
    
    // ===== COLOR CALCULATION =====
    Color GetBackgroundColor() const {
        switch (currentState) {
            case ChipState::Hovered:
                return chipData.selected ? style.backgroundSelectedColor : style.backgroundHoverColor;
            case ChipState::Pressed:
                return chipData.selected ? style.backgroundSelectedColor.WithAlpha(200) : style.backgroundHoverColor;
            case ChipState::Selected:
                return style.backgroundSelectedColor;
            case ChipState::Disabled:
                return style.backgroundDisabledColor;
            default:
                return chipData.selected ? style.backgroundSelectedColor : style.backgroundColor;
        }
    }
    
    Color GetBorderColor() const {
        switch (currentState) {
            case ChipState::Hovered:
                return chipData.selected ? style.borderSelectedColor : style.borderHoverColor;
            case ChipState::Selected:
                return style.borderSelectedColor;
            case ChipState::Disabled:
                return style.borderDisabledColor;
            default:
                return chipData.selected ? style.borderSelectedColor : style.borderColor;
        }
    }
    
    Color GetTextColor() const {
        switch (currentState) {
            case ChipState::Hovered:
                return chipData.selected ? style.textSelectedColor : style.textHoverColor;
            case ChipState::Selected:
                return style.textSelectedColor;
            case ChipState::Disabled:
                return style.textDisabledColor;
            default:
                return chipData.selected ? style.textSelectedColor : style.textColor;
        }
    }
    
    // ===== DRAWING HELPERS =====
    void DrawIcon(const ChipIcon& icon, const Rect2D& bounds) {
        if (icon.IsEmpty()) return;
        
        if (!icon.iconPath.empty()) {
            // Draw image icon
            ctx->DrawImage(icon.iconPath, bounds);
        } else if (!icon.iconText.empty()) {
            // Draw text icon
            ctx->PaintWidthColorchipData.selected ? style.iconSelectedColor : style.iconColor);
            ctx->SetFont(style.fontFamily, icon.iconSize);
            ctx->DrawText(icon.iconText, Point2D(bounds.x, bounds.y + bounds.height * 0.7f));
        }
    }
    
    void DrawRemoveIcon() {
        if (removeIconBounds.width <= 0) return;
        
        Color iconColor = (currentState == ChipState::Hovered && IsRemoveIconHovered()) 
                         ? style.removeIconHoverColor 
                         : style.removeIconColor;
        
        ctx->PaintWidthColoriconColor);
        ctx->SetStrokeWidth(2.0f);
        
        // Draw X icon
        float centerX = removeIconBounds.x + removeIconBounds.width / 2.0f;
        float centerY = removeIconBounds.y + removeIconBounds.height / 2.0f;
        float size = removeIconBounds.width * 0.3f;
        
        ctx->DrawLine(Point2D(centerX - size, centerY - size), Point2D(centerX + size, centerY + size));
        ctx->DrawLine(Point2D(centerX - size, centerY + size), Point2D(centerX + size, centerY - size));
    }
    
    void DrawShadow(const Rect2D& bounds) {
        Rect2D shadowRect = bounds;
        shadowRect.x += style.shadowOffset.x;
        shadowRect.y += style.shadowOffset.y;
        
        // Simplified shadow - would need proper shadow rendering
        ctx->PaintWidthColorstyle.shadowColor);
        if (style.borderRadius > 0) {
            ctx->DrawRoundedRectangle(shadowRect, style.borderRadius, style.shadowColor, Colors::Transparent, 0);
        } else {
            ctx->DrawRectangle(shadowRect);
        }
    }
    
    void DrawRippleEffect(const Rect2D& bounds) {
        // Simplified ripple effect
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStateChange);
        
        if (elapsed.count() < 300) {
            float progress = elapsed.count() / 300.0f;
            float radius = progress * bounds.width / 2.0f;
            
            Color rippleColor = style.textColor.WithAlpha(static_cast<uint8_t>(50 * (1.0f - progress)));
            ctx->PaintWidthColorrippleColor);
            
            Point2D center(bounds.x + bounds.width / 2.0f, bounds.y + bounds.height / 2.0f);
            ctx->DrawCircle(center, radius);
        }
    }
    
    void DrawRoundedRectangle(const Rect2D& rect, float radius, const Color& fillColor, const Color& borderColor, float borderWidth) {
        ctx->PaintWidthColorfillColor);
        ctx->DrawRectangle(rect); // Simplified - would need proper rounded rectangle
        
        if (borderWidth > 0.0f) {
            ctx->PaintWidthColorborderColor);
            ctx->SetStrokeWidth(borderWidth);
            ctx->DrawRectangle(rect);
        }
    }
    
    // ===== EVENT HANDLERS =====
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        isPressed = true;
        SetState(ChipState::Pressed);
        
        // Check if clicking remove icon
        if (showRemoveIcon && IsRemoveIconHovered(Point2D(event.x, event.y))) {
            // Don't trigger main click for remove icon
            return;
        }
        
        SetFocus(true);
    }
    
    void HandleMouseUp(const UCEvent& event) {
        if (!isPressed) return;
        
        isPressed = false;
        
        if (Contains(event.x, event.y)) {
            // Check if clicking remove icon
            if (showRemoveIcon && IsRemoveIconHovered(Point2D(event.x, event.y))) {
                Remove();
            } else {
                Click();
            }
            
            SetState(ChipState::Hovered);
        } else {
            SetState(ChipState::Normal);
        }
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return;
        
        switch (event.virtualKey) {
            case UCKeys::Space:
            case UCKeys::Return:
                Click();
                break;
            case UCKeys::Delete:
            case UCKeys::Backspace:
                if (showRemoveIcon) {
                    Remove();
                }
                break;
        }
    }
    
    // ===== UTILITY METHODS =====
    bool IsRemoveIconHovered(const Point2D& mousePos = Point2D(-1, -1)) const {
        if (!showRemoveIcon || removeIconBounds.width <= 0) return false;
        
        if (mousePos.x < 0 || mousePos.y < 0) {
            // Use current hover state
            return currentState == ChipState::Hovered;
        }
        
        // Convert to local coordinates
        Point2D localPos(mousePos.x - GetX(), mousePos.y - GetY());
        return removeIconBounds.Contains(localPos);
    }
};

// ===== CHIP COLLECTION COMPONENT =====
class UltraCanvasChipGroup : public UltraCanvasUIElement {
private:
    StandardProperties properties;
    std::vector<std::shared_ptr<UltraCanvasChip>> chips;
    ChipType groupType = ChipType::Choice;
    bool allowMultipleSelection = false;
    bool allowEmptySelection = true;
    float chipSpacing = 8.0f;
    float lineSpacing = 8.0f;
    bool wrapChips = true;
    
public:
    UltraCanvasChipGroup(const std::string& identifier = "ChipGroup", 
                        long id = 0, long x = 0, long y = 0, long w = 300, long h = 100)
        : UltraCanvasUIElement(identifier, id, x, y, w, h) {
        
        properties = StandardProperties(identifier, id, x, y, w, h);
    }

    // Group Management
    void AddChip(std::shared_ptr<UltraCanvasChip> chip) {
        chips.push_back(chip);
        AddChild(chip.get());
        chip->onClick = [this, chip]() { HandleChipClick(chip); };
        PerformLayout();
    }
    
    void AddChip(const ChipData& data) {
        auto chip = CreateChip("chip_" + std::to_string(chips.size()), chips.size(), 0, 0, 0, 0);
        chip->SetData(data);
        chip->SetType(groupType);
        AddChip(chip);
    }
    
    void AddChip(const std::string& text) {
        AddChip(ChipData(text));
    }
    
    void RemoveChip(std::shared_ptr<UltraCanvasChip> chip) {
        auto it = std::find(chips.begin(), chips.end(), chip);
        if (it != chips.end()) {
            RemoveChild(chip.get());
            chips.erase(it);
            PerformLayout();
        }
    }
    
    void ClearChips() {
        for (auto& chip : chips) {
            RemoveChild(chip.get());
        }
        chips.clear();
        PerformLayout();
    }
    
    // Selection Management
    void SetSelectedChips(const std::vector<std::string>& values) {
        for (auto& chip : chips) {
            bool shouldSelect = std::find(values.begin(), values.end(), chip->GetValue()) != values.end();
            chip->SetSelected(shouldSelect);
        }
    }
    
    std::vector<std::string> GetSelectedValues() const {
        std::vector<std::string> values;
        for (const auto& chip : chips) {
            if (chip->IsSelected()) {
                values.push_back(chip->GetValue());
            }
        }
        return values;
    }
    
    std::vector<std::shared_ptr<UltraCanvasChip>> GetSelectedChips() const {
        std::vector<std::shared_ptr<UltraCanvasChip>> selected;
        for (const auto& chip : chips) {
            if (chip->IsSelected()) {
                selected.push_back(chip);
            }
        }
        return selected;
    }
    
    // Group Properties
    void SetGroupType(ChipType type) {
        groupType = type;
        for (auto& chip : chips) {
            chip->SetType(type);
        }
        
        // Adjust selection behavior
        switch (type) {
            case ChipType::Choice:
                allowMultipleSelection = false;
                break;
            case ChipType::Filter:
                allowMultipleSelection = true;
                break;
            default:
                break;
        }
    }
    
    void SetMultipleSelection(bool allow) {
        allowMultipleSelection = allow;
    }
    
    void SetChipSpacing(float spacing) {
        chipSpacing = spacing;
        PerformLayout();
    }
    
    void SetWrapChips(bool wrap) {
        wrapChips = wrap;
        PerformLayout();
    }
    
    void Render(IRenderContext* ctx) override {
        // Chips render themselves
    }
    
    bool OnEvent(const UCEvent& event) override {
        // Events handled by individual chips
    }
    
    std::function<void(std::shared_ptr<UltraCanvasChip>)> onChipClicked;
    std::function<void(const std::vector<std::string>&)> onSelectionChanged;

private:
    void PerformLayout() {
        if (chips.empty()) return;
        
        float currentX = static_cast<float>(GetX());
        float currentY = static_cast<float>(GetY());
        float maxHeight = 0.0f;
        
        for (auto& chip : chips) {
            chip->AutoResize();
            Point2D chipSize = chip->GetPreferredSize();
            
            // Check if we need to wrap
            if (wrapChips && currentX + chipSize.x > GetX() + GetWidth() && currentX > GetX()) {
                currentX = static_cast<float>(GetX());
                currentY += maxHeight + lineSpacing;
                maxHeight = 0.0f;
            }
            
            chip->SetPosition(static_cast<long>(currentX), static_cast<long>(currentY));
            currentX += chipSize.x + chipSpacing;
            maxHeight = std::max(maxHeight, chipSize.y);
        }
    }
    
    void HandleChipClick(std::shared_ptr<UltraCanvasChip> clickedChip) {
        if (groupType == ChipType::Choice && !allowMultipleSelection) {
            // Single selection - deselect all others
            for (auto& chip : chips) {
                if (chip != clickedChip) {
                    chip->SetSelected(false);
                }
            }
        }
        
        if (onChipClicked) onChipClicked(clickedChip);
        if (onSelectionChanged) onSelectionChanged(GetSelectedValues());
    }
};

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasChip> CreateChip(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasUIElementFactory::CreateWithID<UltraCanvasChip>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasChip> CreateChipWithText(
    const std::string& identifier, long id, long x, long y, const std::string& text) {
    auto chip = CreateChip(identifier, id, x, y, 0, 0);
    chip->SetText(text);
    chip->AutoResize();
    return chip;
}

inline std::shared_ptr<UltraCanvasChip> CreateInputChip(
    const std::string& identifier, long id, long x, long y, const std::string& text) {
    auto chip = CreateChipWithText(identifier, id, x, y, text);
    chip->SetType(ChipType::Input);
    chip->SetRemovable(true);
    return chip;
}

inline std::shared_ptr<UltraCanvasChip> CreateChoiceChip(
    const std::string& identifier, long id, long x, long y, const std::string& text) {
    auto chip = CreateChipWithText(identifier, id, x, y, text);
    chip->SetType(ChipType::Choice);
    return chip;
}

inline std::shared_ptr<UltraCanvasChip> CreateFilterChip(
    const std::string& identifier, long id, long x, long y, const std::string& text) {
    auto chip = CreateChipWithText(identifier, id, x, y, text);
    chip->SetType(ChipType::Filter);
    return chip;
}

inline std::shared_ptr<UltraCanvasChip> CreateActionChip(
    const std::string& identifier, long id, long x, long y, const std::string& text) {
    auto chip = CreateChipWithText(identifier, id, x, y, text);
    chip->SetType(ChipType::Action);
    return chip;
}

inline std::shared_ptr<UltraCanvasChipGroup> CreateChipGroup(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasUIElementFactory::CreateWithID<UltraCanvasChipGroup>(
        id, identifier, id, x, y, w, h);
}

// ===== BUILDER PATTERN =====
class ChipBuilder {
private:
    std::string identifier = "Chip";
    long id = 0;
    long x = 0, y = 0, w = 0, h = 0;
    ChipData data;
    ChipType type = ChipType::Action;
    ChipBorderStyle borderStyle = ChipBorderStyle::Filled;
    ChipSize size = ChipSize::Medium;
    ChipStyle style = ChipStyle::Default();
    bool autoResize = true;
    
public:
    ChipBuilder& SetIdentifier(const std::string& chipId) { identifier = chipId; return *this; }
    ChipBuilder& SetID(long chipId) { id = chipId; return *this; }
    ChipBuilder& SetPosition(long px, long py) { x = px; y = py; return *this; }
    ChipBuilder& SetSize(long width, long height) { w = width; h = height; autoResize = false; return *this; }
    ChipBuilder& SetText(const std::string& text) { data.text = text; data.value = text; return *this; }
    ChipBuilder& SetValue(const std::string& value) { data.value = value; return *this; }
    ChipBuilder& SetLeadingIcon(const ChipIcon& icon) { data.leadingIcon = icon; return *this; }
    ChipBuilder& SetTrailingIcon(const ChipIcon& icon) { data.trailingIcon = icon; return *this; }
    ChipBuilder& SetType(ChipType chipType) { type = chipType; return *this; }
    ChipBuilder& SetBorderStyle(ChipBorderStyle border) { borderStyle = border; return *this; }
    ChipBuilder& SetSize(ChipSize chipSize) { size = chipSize; return *this; }
    ChipBuilder& SetStyle(const ChipStyle& chipStyle) { style = chipStyle; return *this; }
    ChipBuilder& SetSelected(bool selected) { data.selected = selected; return *this; }
    ChipBuilder& SetRemovable(bool removable) { data.removable = removable; return *this; }
    ChipBuilder& SetTooltip(const std::string& tooltip) { data.tooltip = tooltip; return *this; }
    ChipBuilder& SetUserData(void* userData) { data.userData = userData; return *this; }
    
    std::shared_ptr<UltraCanvasChip> Build() {
        auto chip = CreateChip(identifier, id, x, y, w, h);
        
        chip->SetData(data);
        chip->SetType(type);
        chip->SetBorderStyle(borderStyle);
        chip->SetSize(size);
        chip->SetChipStyle(style);
        
        if (autoResize) {
            chip->AutoResize();
        }
        
        return chip;
    }
};

class ChipGroupBuilder {
private:
    std::string identifier = "ChipGroup";
    long id = 0;
    long x = 0, y = 0, w = 300, h = 100;
    ChipType groupType = ChipType::Choice;
    bool allowMultiple = false;
    std::vector<ChipData> chips;
    float spacing = 8.0f;
    bool wrap = true;
    
public:
    ChipGroupBuilder& SetIdentifier(const std::string& groupId) { identifier = groupId; return *this; }
    ChipGroupBuilder& SetID(long groupId) { id = groupId; return *this; }
    ChipGroupBuilder& SetPosition(long px, long py) { x = px; y = py; return *this; }
    ChipGroupBuilder& SetSize(long width, long height) { w = width; h = height; return *this; }
    ChipGroupBuilder& SetType(ChipType type) { groupType = type; return *this; }
    ChipGroupBuilder& SetMultipleSelection(bool allow) { allowMultiple = allow; return *this; }
    ChipGroupBuilder& SetSpacing(float chipSpacing) { spacing = chipSpacing; return *this; }
    ChipGroupBuilder& SetWrap(bool wrapChips) { wrap = wrapChips; return *this; }
    ChipGroupBuilder& AddChip(const ChipData& chip) { chips.push_back(chip); return *this; }
    ChipGroupBuilder& AddChip(const std::string& text) { chips.emplace_back(text); return *this; }
    
    std::shared_ptr<UltraCanvasChipGroup> Build() {
        auto group = CreateChipGroup(identifier, id, x, y, w, h);
        
        group->SetGroupType(groupType);
        group->SetMultipleSelection(allowMultiple);
        group->SetChipSpacing(spacing);
        group->SetWrapChips(wrap);
        
        for (const auto& chipData : chips) {
            group->AddChip(chipData);
        }
        
        return group;
    }
};

// ===== PREDEFINED CHIP STYLES =====
namespace ChipStyles {
    inline ChipStyle Material() { return ChipStyle::Material(); }
    inline ChipStyle Outlined() { return ChipStyle::Outlined(); }
    inline ChipStyle Flat() { return ChipStyle::Flat(); }
    inline ChipStyle Elevated() { return ChipStyle::Elevated(); }
    
    inline ChipStyle Primary() {
        ChipStyle style = ChipStyle::Material();
        style.backgroundColor = Color(25, 118, 210);
        style.backgroundSelectedColor = Color(21, 101, 192);
        style.textColor = Colors::White;
        style.textSelectedColor = Colors::White;
        return style;
    }
    
    inline ChipStyle Secondary() {
        ChipStyle style = ChipStyle::Material();
        style.backgroundColor = Color(156, 39, 176);
        style.backgroundSelectedColor = Color(142, 36, 170);
        style.textColor = Colors::White;
        style.textSelectedColor = Colors::White;
        return style;
    }
    
    inline ChipStyle Success() {
        ChipStyle style = ChipStyle::Material();
        style.backgroundColor = Color(76, 175, 80);
        style.backgroundSelectedColor = Color(69, 160, 73);
        style.textColor = Colors::White;
        style.textSelectedColor = Colors::White;
        return style;
    }
    
    inline ChipStyle Warning() {
        ChipStyle style = ChipStyle::Material();
        style.backgroundColor = Color(255, 152, 0);
        style.backgroundSelectedColor = Color(245, 124, 0);
        style.textColor = Colors::White;
        style.textSelectedColor = Colors::White;
        return style;
    }
    
    inline ChipStyle Error() {
        ChipStyle style = ChipStyle::Material();
        style.backgroundColor = Color(244, 67, 54);
        style.backgroundSelectedColor = Color(229, 57, 53);
        style.textColor = Colors::White;
        style.textSelectedColor = Colors::White;
        return style;
    }
}

} // namespace UltraCanvas