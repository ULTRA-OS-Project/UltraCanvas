// include/UltraCanvasButton3Sections.h
// Multi-section button component with configurable left/center/right sections
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasButton.h"
#include <functional>
#include <string>
#include <memory>

namespace UltraCanvas {

// ===== SECTION DEFINITIONS =====
enum class SectionType {
    Text,           // Plain text content
    Number,         // Numeric value with formatting
    Icon,           // Icon/image file
    Empty,          // Empty section
    Custom          // Custom content via callback
};

enum class SectionAlignment {
    Left, Center, Right, Stretch
};

struct ButtonSection {
    SectionType type = SectionType::Empty;
    std::string content;                    // Text, number as string, or icon path
    Color backgroundColor = Colors::Transparent;
    Color textColor = Colors::Black;
    Color borderColor = Colors::Transparent;
    SectionAlignment alignment = SectionAlignment::Center;
    float width = 0.0f;                     // 0 = auto, >0 = fixed width
    float padding = 4.0f;
    bool clickable = false;
    std::function<void()> onClick;          // Per-section click handler
    
    // Static factory methods
    static ButtonSection Text(const std::string& text, const Color& color = Colors::Black);
    static ButtonSection Number(float value, const std::string& format = "%.0f", const Color& color = Colors::Black);
    static ButtonSection Icon(const std::string& iconPath, float size = 16.0f);
    static ButtonSection Empty(float width = 30.0f);
    static ButtonSection Custom(std::function<void(const Rect2Di&)> renderCallback);
    
private:
    std::function<void(const Rect2Di&)> customRenderer;
    
public:
    void SetCustomRenderer(std::function<void(const Rect2Di&)> renderer) { customRenderer = renderer; }
    const std::function<void(const Rect2Di&)>& GetCustomRenderer() const { return customRenderer; }
};

// ===== BUTTON STYLE DEFINITIONS =====
enum class Button3SectionsStyle {
    Standard,       // Regular appearance with borders
    Flat,           // No borders, clean look
    Segmented,      // Clear section separators
    Card,           // Card-like appearance with shadow
    Toolbar,        // Toolbar button style
    Dashboard       // Dashboard widget style
};

struct Button3SectionsAppearance {
    Button3SectionsStyle style = Button3SectionsStyle::Standard;
    
    // Overall button appearance
    Color backgroundColor = Colors::ButtonFace;
    Color borderColor = Colors::Gray;
    Color hoverColor = Colors::ButtonHighlight;
    Color pressedColor = Colors::ButtonShadow;
    Color disabledColor = Colors::LightGray;
    
    // Section separators
    bool showSeparators = true;
    Color separatorColor = Colors::Gray;
    float separatorWidth = 1.0f;
    
    // Dimensions
    float cornerRadius = 4.0f;
    float borderWidth = 1.0f;
    float sectionSpacing = 2.0f;
    
    // Effects
    bool hasShadow = false;
    Color shadowColor = Color(0, 0, 0, 100);
    Point2Df shadowOffset = Point2Df(2, 2);
    
    static Button3SectionsAppearance Default();
    static Button3SectionsAppearance Flat();
    static Button3SectionsAppearance Segmented();
    static Button3SectionsAppearance Card();
    static Button3SectionsAppearance Toolbar();
    static Button3SectionsAppearance Dashboard();
};

// ===== MAIN COMPONENT CLASS =====
class UltraCanvasButton3Sections : public UltraCanvasElement {
private:
    // ===== STANDARD PROPERTIES (REQUIRED) =====
    StandardProperties properties;
    
    // ===== SECTION DATA =====
    ButtonSection leftSection;
    ButtonSection centerSection;
    ButtonSection rightSection;
    
    // ===== APPEARANCE =====
    Button3SectionsAppearance appearance;
    
    // ===== STATE =====
    bool isPressed = false;
    int hoveredSection = -1;  // -1=none, 0=left, 1=center, 2=right
    int pressedSection = -1;
    
    // ===== LAYOUT CACHE =====
    Rect2Di leftRect, centerRect, rightRect;
    bool layoutDirty = true;
    
public:
    // ===== CONSTRUCTOR (REQUIRED PATTERN) =====
    UltraCanvasButton3Sections(const std::string& identifier = "Button3Sections", 
                              long id = 0, long x = 0, long y = 0, long w = 150, long h = 30)
        : UltraCanvasElement(identifier, id, x, y, w, h) {
        
        // Initialize standard properties
        properties = StandardProperties(identifier, id, x, y, w, h);
        properties.MousePtr = MousePointer::Hand;
        properties.MouseCtrl = MouseControls::Button;
        
        // Initialize default sections
        leftSection = ButtonSection::Empty();
        centerSection = ButtonSection::Text("Button");
        rightSection = ButtonSection::Empty();
        
        // Initialize appearance
        appearance = Button3SectionsAppearance::Default();
    }
    

    // ===== SECTION MANAGEMENT =====
    void SetSections(const ButtonSection& left, const ButtonSection& center, const ButtonSection& right) {
        leftSection = left;
        centerSection = center;
        rightSection = right;
        layoutDirty = true;
    }
    
    void SetLeftSection(const ButtonSection& section) {
        leftSection = section;
        layoutDirty = true;
    }
    
    void SetCenterSection(const ButtonSection& section) {
        centerSection = section;
        layoutDirty = true;
    }
    
    void SetRightSection(const ButtonSection& section) {
        rightSection = section;
        layoutDirty = true;
    }
    
    const ButtonSection& GetLeftSection() const { return leftSection; }
    const ButtonSection& GetCenterSection() const { return centerSection; }
    const ButtonSection& GetRightSection() const { return rightSection; }
    
    // ===== APPEARANCE MANAGEMENT =====
    void SetAppearance(const Button3SectionsAppearance& newAppearance) {
        appearance = newAppearance;
        layoutDirty = true;
    }
    
    const Button3SectionsAppearance& GetAppearance() const {
        return appearance;
    }
    
    void SetStyle(Button3SectionsStyle style) {
        switch (style) {
            case Button3SectionsStyle::Standard:
                appearance = Button3SectionsAppearance::Default();
                break;
            case Button3SectionsStyle::Flat:
                appearance = Button3SectionsAppearance::Flat();
                break;
            case Button3SectionsStyle::Segmented:
                appearance = Button3SectionsAppearance::Segmented();
                break;
            case Button3SectionsStyle::Card:
                appearance = Button3SectionsAppearance::Card();
                break;
            case Button3SectionsStyle::Toolbar:
                appearance = Button3SectionsAppearance::Toolbar();
                break;
            case Button3SectionsStyle::Dashboard:
                appearance = Button3SectionsAppearance::Dashboard();
                break;
        }
        layoutDirty = true;
    }
    
    // ===== CONVENIENT TEXT SETTERS =====
    void SetText(const std::string& text) {
        centerSection = ButtonSection::Text(text, centerSection.textColor);
        layoutDirty = true;
    }
    
    void SetLeftText(const std::string& text) {
        leftSection = ButtonSection::Text(text, leftSection.textColor);
        layoutDirty = true;
    }
    
    void SetRightText(const std::string& text) {
        rightSection = ButtonSection::Text(text, rightSection.textColor);
        layoutDirty = true;
    }
    
    void SetLeftIcon(const std::string& iconPath) {
        leftSection = ButtonSection::Icon(iconPath);
        layoutDirty = true;
    }
    
    void SetRightIcon(const std::string& iconPath) {
        rightSection = ButtonSection::Icon(iconPath);
        layoutDirty = true;
    }
    
    // ===== RENDERING (REQUIRED OVERRIDE) =====
    void Render() override {

        if (!IsVisible()) return;
        auto ctx = GetRenderContext();
        ctx->PushState();
        
        // Update layout if needed
        if (layoutDirty) {
            CalculateLayout();
            layoutDirty = false;
        }
        
        // Draw button background and border
        DrawButtonBackground(ctx);
        
        // Draw sections
        DrawSection(leftSection, leftRect, 0);
        DrawSection(centerSection, centerRect, 1);
        DrawSection(rightSection, rightRect, 2);
        
        // Draw section separators
        if (appearance.showSeparators) {
            DrawSectionSeparators();
        }
        
        // Draw shadow if enabled
        if (appearance.hasShadow && !isPressed) {
            DrawButtonShadow();
        }
    }
    
    // ===== EVENT HANDLING (REQUIRED OVERRIDE) =====
    bool OnEvent(const UCEvent& event) override {
        if (!IsActive() || !IsVisible()) return false;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
                
            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;
                
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
                
            case UCEventType::MouseLeave:
                hoveredSection = -1;
                break;
                
            case UCEventType::KeyDown:
                if (IsFocused() && (event.virtualKey == UCKeys::Return || event.virtualKey == UCKeys::Space)) {
                    TriggerClick();
                }
                break;
        }
        return false;
    }
    
    // ===== CLICK HANDLING =====
    void TriggerClick() {
        if (onClick) onClick();
    }
    
    void TriggerSectionClick(int sectionIndex) {
        switch (sectionIndex) {
            case 0:
                if (leftSection.onClick) leftSection.onClick();
                break;
            case 1:
                if (centerSection.onClick) centerSection.onClick();
                break;
            case 2:
                if (rightSection.onClick) rightSection.onClick();
                break;
        }
        
        if (onSectionClick) onSectionClick(sectionIndex);
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void()> onClick;
    std::function<void(int)> onSectionClick;  // Section index: 0=left, 1=center, 2=right
    std::function<void(int)> onSectionHovered;
    std::function<void()> onPressed;
    std::function<void()> onReleased;

private:
    // ===== LAYOUT CALCULATION =====
    void CalculateLayout() {
        Rect2Di bounds = GetBounds();
        float totalWidth = bounds.width - appearance.borderWidth * 2;
        float sectionHeight = bounds.height - appearance.borderWidth * 2;
        
        // Calculate section widths
        float leftWidth = CalculateSectionWidth(leftSection, totalWidth);
        float rightWidth = CalculateSectionWidth(rightSection, totalWidth);
        float centerWidth = totalWidth - leftWidth - rightWidth;
        
        // Adjust if center section would be too small
        if (centerWidth < 20.0f && centerSection.type != SectionType::Empty) {
            float reduction = (20.0f - centerWidth) / 2.0f;
            leftWidth = std::max(0.0f, leftWidth - reduction);
            rightWidth = std::max(0.0f, rightWidth - reduction);
            centerWidth = totalWidth - leftWidth - rightWidth;
        }
        
        // Set section rectangles
        float currentX = bounds.x + appearance.borderWidth;
        float currentY = bounds.y + appearance.borderWidth;
        
        leftRect = Rect2Di(currentX, currentY, leftWidth, sectionHeight);
        currentX += leftWidth;
        
        centerRect = Rect2Di(currentX, currentY, centerWidth, sectionHeight);
        currentX += centerWidth;
        
        rightRect = Rect2Di(currentX, currentY, rightWidth, sectionHeight);
    }
    
    float CalculateSectionWidth(const ButtonSection& section, float totalWidth) {
        if (section.type == SectionType::Empty) return 0.0f;
        if (section.width > 0.0f) return section.width;
        
        // Auto-calculate based on content
        switch (section.type) {
            case SectionType::Icon:
                return 24.0f + section.padding * 2; // Standard icon size
            case SectionType::Text:
            case SectionType::Number:
                return GetTextWidth(section.content) + section.padding * 2;
            default:
                return totalWidth * 0.33f; // Default to 1/3 width
        }
    }
    
    // ===== RENDERING METHODS =====
    void DrawButtonBackground(IRenderContext* ctx) {
        Rect2Di bounds = GetBounds();
        
        // Determine background color based on state
        Color bgColor = appearance.backgroundColor;
        if (!IsEnabled()) {
            bgColor = appearance.disabledColor;
        } else if (isPressed) {
            bgColor = appearance.pressedColor;
        } else if (hoveredSection >= 0) {
            bgColor = appearance.hoverColor;
        }
        
        // Draw background
        if (appearance.cornerRadius > 0.0f) {
            ctx->DrawRoundedRectangle(bounds, appearance.cornerRadius, bgColor, appearance.borderColor, appearance.borderWidth);
        } else {
            ctx->DrawFilledRectangle(bounds, bgColor, appearance.borderColor, appearance.borderWidth);
        }
    }
    
    void DrawSection(const ButtonSection& section, const Rect2Di& rect, int sectionIndex) {
        if (section.type == SectionType::Empty || rect.width <= 0) return;
        
        // Draw section background if specified
        if (section.backgroundColor.a > 0) {
            ctx->SetFillColor(section.backgroundColor);
            ctx->DrawRectangle(rect);
        }
        
        // Draw section content
        switch (section.type) {
            case SectionType::Text:
            case SectionType::Number:
                DrawSectionText(section, rect, sectionIndex);
                break;
                
            case SectionType::Icon:
                DrawSectionIcon(section, rect, sectionIndex);
                break;
                
            case SectionType::Custom:
                if (section.GetCustomRenderer()) {
                    section.GetCustomRenderer()(rect);
                }
                break;
        }
        
        // Draw section border if specified
        if (section.borderColor.a > 0) {
            ctx->SetStrokeColor(section.borderColor);
            ctx->SetStrokeWidth(1.0f);
            ctx->DrawRectangle(rect);
        }
    }
    
    void DrawSectionText(const ButtonSection& section, const Rect2Di& rect, int sectionIndex) {
        if (section.content.empty()) return;
        
        // Calculate text position based on alignment
        Point2Df textPos = CalculateTextPosition(section, rect);
        
        // Set text color (dimmed if button is disabled)
        Color textColor = section.textColor;
        if (!IsEnabled()) {
            textColor = Color(textColor.r, textColor.g, textColor.b, textColor.a / 2);
        }
        
        ctx->SetTextColor(textColor);
        ctx->DrawText(section.content, textPos);
    }
    
    void DrawSectionIcon(const ButtonSection& section, const Rect2Di& rect, int sectionIndex) {
        if (section.content.empty()) return;
        
        // Calculate icon position (centered)
        float iconSize = std::min(rect.width, rect.height) - section.padding * 2;
        Point2Di iconPos(
            rect.x + (rect.width - iconSize) / 2,
            rect.y + (rect.height - iconSize) / 2
        );
        
        // Draw icon with opacity based on enabled state
        float opacity = IsEnabled() ? 1.0f : 0.5f;
        SetGlobalAlpha(opacity);
        ctx->DrawImage(section.content, Rect2Di(iconPos.x, iconPos.y, iconSize, iconSize));
        SetGlobalAlpha(1.0f);
    }
    
    Point2Df CalculateTextPosition(const ButtonSection& section, const Rect2Di& rect) {
        float textWidth = GetTextWidth(section.content);
        float textHeight = GetTextHeight(section.content);
        
        float x, y;
        
        // Horizontal alignment
        switch (section.alignment) {
            case SectionAlignment::Left:
                x = rect.x + section.padding;
                break;
            case SectionAlignment::Right:
                x = rect.x + rect.width - textWidth - section.padding;
                break;
            case SectionAlignment::Center:
            default:
                x = rect.x + (rect.width - textWidth) / 2;
                break;
        }
        
        // Vertical centering
        y = rect.y + (rect.height + textHeight) / 2;
        
        return Point2Df(x, y);
    }
    
    void DrawSectionSeparators() {
        if (leftRect.width > 0 && centerRect.width > 0) {
            // Left-center separator
            float x = leftRect.x + leftRect.width;
            ctx->SetStrokeColor(appearance.separatorColor);
            ctx->SetStrokeWidth(appearance.separatorWidth);
            ctx->DrawLine(x, GetY() + 2, x, GetY() + GetHeight() - 2);
        }
        
        if (centerRect.width > 0 && rightRect.width > 0) {
            // Center-right separator
            float x = centerRect.x + centerRect.width;
            ctx->SetStrokeColor(appearance.separatorColor);
            ctx->SetStrokeWidth(appearance.separatorWidth);
            ctx->DrawLine(x, GetY() + 2, x, GetY() + GetHeight() - 2);
        }
    }
    
    void DrawButtonShadow() {
        Rect2Di shadowRect = GetBounds();
        shadowRect.x += appearance.shadowOffset.x;
        shadowRect.y += appearance.shadowOffset.y;
        
        ctx->SetFillColor(appearance.shadowColor);
        if (appearance.cornerRadius > 0.0f) {
            ctx->DrawRoundedRectangle(shadowRect, appearance.cornerRadius);
        } else {
            ctx->DrawRectangle(shadowRect);
        }
    }
    
    // ===== EVENT HANDLERS =====
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        SetFocus(true);
        isPressed = true;
        pressedSection = GetSectionAtPoint(event.x, event.y);
        
        if (onPressed) onPressed();
    }
    
    void HandleMouseUp(const UCEvent& event) {
        if (isPressed) {
            isPressed = false;
            
            if (Contains(event.x, event.y)) {
                int clickedSection = GetSectionAtPoint(event.x, event.y);
                
                // Trigger appropriate click handlers
                if (clickedSection >= 0) {
                    TriggerSectionClick(clickedSection);
                }
                TriggerClick();
            }
            
            if (onReleased) onReleased();
        }
        
        pressedSection = -1;
    }
    
    void HandleMouseMove(const UCEvent& event) {
        int newHoveredSection = Contains(event.x, event.y) ? 
                               GetSectionAtPoint(event.x, event.y) : -1;
        
        if (newHoveredSection != hoveredSection) {
            hoveredSection = newHoveredSection;
            if (onSectionHovered) onSectionHovered(hoveredSection);
        }
    }
    
    int GetSectionAtPoint(int x, int y) {
        if (leftRect.Contains(x, y) && leftSection.type != SectionType::Empty) return 0;
        if (centerRect.Contains(x, y) && centerSection.type != SectionType::Empty) return 1;
        if (rightRect.Contains(x, y) && rightSection.type != SectionType::Empty) return 2;
        return -1;
    }
};

// ===== STATIC FACTORY METHODS FOR BUTTON SECTION =====
inline ButtonSection ButtonSection::Text(const std::string& text, const Color& color) {
    ButtonSection section;
    section.type = SectionType::Text;
    section.content = text;
    section.textColor = color;
    return section;
}

inline ButtonSection ButtonSection::Number(float value, const std::string& format, const Color& color) {
    ButtonSection section;
    section.type = SectionType::Number;
    
    // Format the number according to the format string
    char buffer[32];
    snprintf(buffer, sizeof(buffer), format.c_str(), value);
    section.content = std::string(buffer);
    section.textColor = color;
    
    return section;
}

inline ButtonSection ButtonSection::Icon(const std::string& iconPath, float size) {
    ButtonSection section;
    section.type = SectionType::Icon;
    section.content = iconPath;
    section.width = size + 8; // Add padding
    return section;
}

inline ButtonSection ButtonSection::Empty(float width) {
    ButtonSection section;
    section.type = SectionType::Empty;
    section.width = width;
    return section;
}

inline ButtonSection ButtonSection::Custom(std::function<void(const Rect2Di&)> renderCallback) {
    ButtonSection section;
    section.type = SectionType::Custom;
    section.SetCustomRenderer(renderCallback);
    return section;
}

// ===== STATIC FACTORY METHODS FOR APPEARANCE =====
inline Button3SectionsAppearance Button3SectionsAppearance::Default() {
    return Button3SectionsAppearance(); // Use default constructor values
}

inline Button3SectionsAppearance Button3SectionsAppearance::Flat() {
    Button3SectionsAppearance appearance;
    appearance.style = Button3SectionsStyle::Flat;
    appearance.borderWidth = 0.0f;
    appearance.showSeparators = false;
    appearance.backgroundColor = Colors::Transparent;
    appearance.hoverColor = Color(200, 200, 200, 100);
    return appearance;
}

inline Button3SectionsAppearance Button3SectionsAppearance::Segmented() {
    Button3SectionsAppearance appearance;
    appearance.style = Button3SectionsStyle::Segmented;
    appearance.showSeparators = true;
    appearance.separatorWidth = 2.0f;
    appearance.separatorColor = Colors::DarkGray;
    appearance.cornerRadius = 6.0f;
    return appearance;
}

inline Button3SectionsAppearance Button3SectionsAppearance::Card() {
    Button3SectionsAppearance appearance;
    appearance.style = Button3SectionsStyle::Card;
    appearance.cornerRadius = 8.0f;
    appearance.hasShadow = true;
    appearance.backgroundColor = Colors::White;
    appearance.borderColor = Color(200, 200, 200);
    return appearance;
}

inline Button3SectionsAppearance Button3SectionsAppearance::Toolbar() {
    Button3SectionsAppearance appearance;
    appearance.style = Button3SectionsStyle::Toolbar;
    appearance.borderWidth = 0.0f;
    appearance.showSeparators = false;
    appearance.backgroundColor = Colors::Transparent;
    appearance.hoverColor = Color(100, 100, 100, 50);
    return appearance;
}

inline Button3SectionsAppearance Button3SectionsAppearance::Dashboard() {
    Button3SectionsAppearance appearance;
    appearance.style = Button3SectionsStyle::Dashboard;
    appearance.cornerRadius = 12.0f;
    appearance.backgroundColor = Color(240, 240, 240);
    appearance.borderColor = Color(180, 180, 180);
    appearance.showSeparators = true;
    appearance.separatorColor = Color(200, 200, 200);
    return appearance;
}

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasButton3Sections> CreateButton3Sections(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasButton3Sections>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasButton3Sections> CreateButton3SectionsWithText(
    const std::string& identifier, long id, long x, long y, long w, long h,
    const std::string& leftText, const std::string& centerText, const std::string& rightText) {
    auto button = CreateButton3Sections(identifier, id, x, y, w, h);
    button->SetSections(
        ButtonSection::Text(leftText),
        ButtonSection::Text(centerText),
        ButtonSection::Text(rightText)
    );
    return button;
}

inline std::shared_ptr<UltraCanvasButton3Sections> CreateIconTextButton(
    const std::string& identifier, long id, long x, long y, long w, long h,
    const std::string& iconPath, const std::string& text) {
    auto button = CreateButton3Sections(identifier, id, x, y, w, h);
    button->SetSections(
        ButtonSection::Icon(iconPath),
        ButtonSection::Text(text),
        ButtonSection::Empty()
    );
    return button;
}

// ===== BUILDER PATTERN =====
class Button3SectionsBuilder {
private:
    std::string identifier = "Button3Sections";
    long id = 0;
    long x = 0, y = 0, w = 150, h = 30;
    ButtonSection leftSection = ButtonSection::Empty();
    ButtonSection centerSection = ButtonSection::Text("Button");
    ButtonSection rightSection = ButtonSection::Empty();
    Button3SectionsAppearance appearance = Button3SectionsAppearance::Default();
    std::function<void()> clickHandler;
    std::function<void(int)> sectionClickHandler;
    
public:
    Button3SectionsBuilder& SetIdentifier(const std::string& id) { identifier = id; return *this; }
    Button3SectionsBuilder& SetID(long elementId) { id = elementId; return *this; }
    Button3SectionsBuilder& SetPosition(long px, long py) { x = px; y = py; return *this; }
    Button3SectionsBuilder& SetSize(long width, long height) { w = width; h = height; return *this; }
    
    Button3SectionsBuilder& SetSections(const ButtonSection& left, const ButtonSection& center, const ButtonSection& right) {
        leftSection = left; centerSection = center; rightSection = right; return *this;
    }
    
    Button3SectionsBuilder& SetLeftSection(const ButtonSection& section) { leftSection = section; return *this; }
    Button3SectionsBuilder& SetCenterSection(const ButtonSection& section) { centerSection = section; return *this; }
    Button3SectionsBuilder& SetRightSection(const ButtonSection& section) { rightSection = section; return *this; }
    
    Button3SectionsBuilder& SetAppearance(const Button3SectionsAppearance& app) { appearance = app; return *this; }
    Button3SectionsBuilder& SetStyle(Button3SectionsStyle style) {
        switch (style) {
            case Button3SectionsStyle::Flat: appearance = Button3SectionsAppearance::Flat(); break;
            case Button3SectionsStyle::Segmented: appearance = Button3SectionsAppearance::Segmented(); break;
            case Button3SectionsStyle::Card: appearance = Button3SectionsAppearance::Card(); break;
            case Button3SectionsStyle::Toolbar: appearance = Button3SectionsAppearance::Toolbar(); break;
            case Button3SectionsStyle::Dashboard: appearance = Button3SectionsAppearance::Dashboard(); break;
            default: appearance = Button3SectionsAppearance::Default(); break;
        }
        return *this;
    }
    
    Button3SectionsBuilder& OnClick(std::function<void()> handler) { clickHandler = handler; return *this; }
    Button3SectionsBuilder& OnSectionClick(std::function<void(int)> handler) { sectionClickHandler = handler; return *this; }
    
    std::shared_ptr<UltraCanvasButton3Sections> Build() {
        auto button = CreateButton3Sections(identifier, id, x, y, w, h);
        button->SetSections(leftSection, centerSection, rightSection);
        button->SetAppearance(appearance);
        
        if (clickHandler) button->onClick = clickHandler;
        if (sectionClickHandler) button->onSectionClick = sectionClickHandler;
        
        return button;
    }
};

// ===== LEGACY C-STYLE API (BACKWARD COMPATIBLE) =====
extern "C" {
    void* CreateButton3SectionsC(int x, int y, int width, int height);
    void SetButton3SectionsText(void* handle, const char* leftText, const char* centerText, const char* rightText);
    void SetButton3SectionsStyle(void* handle, int style);
    void SetButton3SectionsClickHandler(void* handle, void(*handler)());
    void DestroyButton3Sections(void* handle);
}

} // namespace UltraCanvas