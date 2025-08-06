// UltraCanvasColorPicker.h
// Interactive color picker component with multiple selection modes and formats
// Version: 1.0.0
// Last Modified: 2024-12-30
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include <functional>
#include <string>
#include <vector>
#include <cmath>

namespace UltraCanvas {

// ===== COLOR FORMAT DEFINITIONS =====
enum class ColorFormat {
    RGB,        // Red, Green, Blue (0-255)
    HSV,        // Hue, Saturation, Value (H: 0-360, S/V: 0-100)
    HSL,        // Hue, Saturation, Lightness (H: 0-360, S/L: 0-100)
    HEX,        // Hexadecimal (#RRGGBB or #AARRGGBB)
    CMYK        // Cyan, Magenta, Yellow, Key (0-100)
};

enum class ColorPickerMode {
    Compact,        // Small color swatch with popup
    Inline,         // Full picker always visible
    Popup,          // Button that opens popup dialog
    Palette,        // Predefined color palette
    Gradient,       // Gradient color wheel
    Sliders,        // RGB/HSV sliders only
    Custom          // Custom layout
};

enum class ColorWheelType {
    HSV_Circle,     // Traditional HSV color wheel
    HSL_Circle,     // HSL color wheel
    RGB_Triangle,   // RGB triangle
    Square_SV,      // Square saturation/value picker
    Linear_Hue      // Linear hue bar
};

// ===== COLOR UTILITY STRUCTURES =====
struct HSVColor {
    float h, s, v, a;  // Hue: 0-360, Saturation/Value: 0-1, Alpha: 0-1
    
    HSVColor(float hue = 0.0f, float sat = 1.0f, float val = 1.0f, float alpha = 1.0f)
        : h(hue), s(sat), v(val), a(alpha) {}
    
    Color ToRGB() const;
    static HSVColor FromRGB(const Color& rgb);
    std::string ToString() const;
};

struct HSLColor {
    float h, s, l, a;  // Hue: 0-360, Saturation/Lightness: 0-1, Alpha: 0-1
    
    HSLColor(float hue = 0.0f, float sat = 1.0f, float light = 0.5f, float alpha = 1.0f)
        : h(hue), s(sat), l(light), a(alpha) {}
    
    Color ToRGB() const;
    static HSLColor FromRGB(const Color& rgb);
    std::string ToString() const;
};

struct CMYKColor {
    float c, m, y, k, a;  // Cyan, Magenta, Yellow, Key (Black): 0-1, Alpha: 0-1
    
    CMYKColor(float cyan = 0.0f, float magenta = 0.0f, float yellow = 0.0f, float key = 0.0f, float alpha = 1.0f)
        : c(cyan), m(magenta), y(yellow), k(key), a(alpha) {}
    
    Color ToRGB() const;
    static CMYKColor FromRGB(const Color& rgb);
    std::string ToString() const;
};

// ===== COLOR PICKER STYLING =====
struct ColorPickerStyle {
    // Overall appearance
    ColorPickerMode mode = ColorPickerMode::Inline;
    ColorWheelType wheelType = ColorWheelType::HSV_Circle;
    
    // Dimensions
    float wheelSize = 200.0f;
    float sliderWidth = 20.0f;
    float sliderLength = 150.0f;
    float swatchSize = 24.0f;
    float spacing = 8.0f;
    
    // Colors
    Color backgroundColor = Colors::WindowBackground;
    Color borderColor = Colors::Gray;
    Color textColor = Colors::Black;
    Color sliderTrackColor = Colors::LightGray;
    Color sliderHandleColor = Colors::White;
    Color gridColor = Color(200, 200, 200);
    
    // Features
    bool showAlpha = true;
    bool showPreview = true;
    bool showInputFields = true;
    bool showPalette = true;
    bool showEyedropper = false;
    bool allowCustomColors = true;
    
    // Input formats
    std::vector<ColorFormat> supportedFormats = {ColorFormat::RGB, ColorFormat::HSV, ColorFormat::HEX};
    ColorFormat defaultFormat = ColorFormat::RGB;
    
    static ColorPickerStyle Compact();
    static ColorPickerStyle Inline();
    static ColorPickerStyle Popup();
    static ColorPickerStyle Palette();
    static ColorPickerStyle Professional();
};

// ===== PREDEFINED COLOR PALETTES =====
class ColorPalette {
public:
    std::string name;
    std::vector<Color> colors;
    
    ColorPalette(const std::string& paletteName, const std::vector<Color>& paletteColors)
        : name(paletteName), colors(paletteColors) {}
    
    static ColorPalette Basic();
    static ColorPalette Web();
    static ColorPalette Material();
    static ColorPalette Pastel();
    static ColorPalette Grayscale();
    static ColorPalette Rainbow();
    static ColorPalette Custom(const std::string& name, const std::vector<Color>& colors);
};

// ===== MAIN COLOR PICKER COMPONENT =====
class UltraCanvasColorPicker : public UltraCanvasElement {
private:
    // ===== STANDARD PROPERTIES (REQUIRED) =====
    StandardProperties properties;
    
    // ===== COLOR STATE =====
    Color currentColor = Colors::Red;
    HSVColor currentHSV;
    ColorFormat currentFormat = ColorFormat::RGB;
    
    // ===== STYLE AND BEHAVIOR =====
    ColorPickerStyle style;
    std::vector<ColorPalette> palettes;
    int selectedPaletteIndex = 0;
    
    // ===== UI STATE =====
    bool isPopupOpen = false;
    bool isDraggingWheel = false;
    bool isDraggingSlider = false;
    int activeSlider = -1;  // 0=hue, 1=saturation, 2=value/lightness, 3=alpha
    Point2D wheelCenter;
    float wheelRadius;
    
    // ===== LAYOUT CACHE =====
    Rect2D wheelRect, previewRect, paletteRect, inputRect;
    std::vector<Rect2D> sliderRects;
    std::vector<Rect2D> paletteSwatches;
    bool layoutDirty = true;
    
public:
    // ===== CONSTRUCTOR (REQUIRED PATTERN) =====
    UltraCanvasColorPicker(const std::string& identifier = "ColorPicker", 
                          long id = 0, long x = 0, long y = 0, long w = 300, long h = 250)
        : UltraCanvasElement(identifier, id, x, y, w, h) {
        
        // Initialize standard properties
        properties = StandardProperties(identifier, id, x, y, w, h);
        properties.MousePtr = MousePointer::Default;
        properties.MouseCtrl = MouseControls::Input;
        
        // Initialize style
        style = ColorPickerStyle::Inline();
        
        // Initialize color state
        currentHSV = HSVColor::FromRGB(currentColor);
        
        // Initialize palettes
        palettes.push_back(ColorPalette::Basic());
        palettes.push_back(ColorPalette::Web());
        palettes.push_back(ColorPalette::Material());
    }
    

    // ===== COLOR MANAGEMENT =====
    void SetColor(const Color& color) {
        if (currentColor == color) return;
        
        currentColor = color;
        currentHSV = HSVColor::FromRGB(color);
        
        if (onColorChanged) onColorChanged(color);
        if (onColorChangedRaw) onColorChangedRaw(color.ToARGB());
    }
    
    void SetColor(uint32_t argb) {
        SetColor(Color::FromARGB(argb));
    }
    
    void SetColorHSV(const HSVColor& hsv) {
        currentHSV = hsv;
        SetColor(hsv.ToRGB());
    }
    
    const Color& GetColor() const { return currentColor; }
    uint32_t GetColorARGB() const { return currentColor.ToARGB(); }
    const HSVColor& GetColorHSV() const { return currentHSV; }
    HSLColor GetColorHSL() const { return HSLColor::FromRGB(currentColor); }
    CMYKColor GetColorCMYK() const { return CMYKColor::FromRGB(currentColor); }
    
    std::string GetColorString(ColorFormat format = ColorFormat::RGB) const {
        switch (format) {
            case ColorFormat::RGB:
                return GetColorRGBString();
            case ColorFormat::HSV:
                return currentHSV.ToString();
            case ColorFormat::HSL:
                return GetColorHSL().ToString();
            case ColorFormat::HEX:
                return GetColorHexString();
            case ColorFormat::CMYK:
                return GetColorCMYK().ToString();
            default:
                return GetColorRGBString();
        }
    }
    
    std::string GetColorRGBString() const {
        return "rgb(" + std::to_string(currentColor.r) + ", " + 
               std::to_string(currentColor.g) + ", " + 
               std::to_string(currentColor.b) + ")";
    }
    
    std::string GetColorHexString(bool includeAlpha = false) const {
        char buffer[16];
        if (includeAlpha) {
            snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", 
                    currentColor.a, currentColor.r, currentColor.g, currentColor.b);
        } else {
            snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", 
                    currentColor.r, currentColor.g, currentColor.b);
        }
        return std::string(buffer);
    }
    
    // ===== STYLE MANAGEMENT =====
    void SetStyle(const ColorPickerStyle& newStyle) {
        style = newStyle;
        layoutDirty = true;
        
        // Adjust size based on mode
        if (style.mode == ColorPickerMode::Compact) {
            properties.width_size = static_cast<long>(style.swatchSize + 4);
            properties.height_size = static_cast<long>(style.swatchSize + 4);
        }
    }
    
    const ColorPickerStyle& GetStyle() const { return style; }
    
    void SetMode(ColorPickerMode mode) {
        style.mode = mode;
        layoutDirty = true;
    }
    
    void SetColorFormat(ColorFormat format) {
        currentFormat = format;
    }
    
    ColorFormat GetColorFormat() const { return currentFormat; }
    
    // ===== ALPHA CHANNEL =====
    void SetAlphaEnabled(bool enabled) {
        style.showAlpha = enabled;
        layoutDirty = true;
    }
    
    bool IsAlphaEnabled() const { return style.showAlpha; }
    
    void SetAlpha(float alpha) {
        currentColor.a = static_cast<uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
        currentHSV.a = alpha;
        
        if (onColorChanged) onColorChanged(currentColor);
        if (onColorChangedRaw) onColorChangedRaw(currentColor.ToARGB());
    }
    
    float GetAlpha() const { return currentColor.a / 255.0f; }
    
    // ===== PALETTE MANAGEMENT =====
    void AddPalette(const ColorPalette& palette) {
        palettes.push_back(palette);
        layoutDirty = true;
    }
    
    void SetSelectedPalette(int index) {
        if (index >= 0 && index < static_cast<int>(palettes.size())) {
            selectedPaletteIndex = index;
            layoutDirty = true;
        }
    }
    
    const std::vector<ColorPalette>& GetPalettes() const { return palettes; }
    const ColorPalette& GetSelectedPalette() const { return palettes[selectedPaletteIndex]; }
    
    // ===== POPUP MANAGEMENT =====
    void ShowPopup() {
        if (style.mode == ColorPickerMode::Popup || style.mode == ColorPickerMode::Compact) {
            isPopupOpen = true;
            if (onPopupOpened) onPopupOpened();
        }
    }
    
    void HidePopup() {
        isPopupOpen = false;
        isDraggingWheel = false;
        isDraggingSlider = false;
        if (onPopupClosed) onPopupClosed();
    }
    
    bool IsPopupOpen() const { return isPopupOpen; }
    
    // ===== RENDERING (REQUIRED OVERRIDE) =====
    void Render() override {
        if (!IsVisible()) return;
        
        ULTRACANVAS_RENDER_SCOPE();
        
        // Update layout if needed
        if (layoutDirty) {
            CalculateLayout();
            layoutDirty = false;
        }
        
        // Render based on mode
        switch (style.mode) {
            case ColorPickerMode::Compact:
                RenderCompactMode();
                break;
            case ColorPickerMode::Inline:
                RenderInlineMode();
                break;
            case ColorPickerMode::Popup:
                RenderPopupMode();
                break;
            case ColorPickerMode::Palette:
                RenderPaletteMode();
                break;
            default:
                RenderInlineMode();
                break;
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
                
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
                
            case UCEventType::MouseLeave:
                if (!isDraggingWheel && !isDraggingSlider) {
                    // Can hide popup if click outside
                }
                break;
        }
    }
    
    // ===== EVENT CALLBACKS =====
    std::function<void(const Color&)> onColorChanged;
    std::function<void(uint32_t)> onColorChangedRaw;
    std::function<void(const Color&)> onColorSelected;
    std::function<void()> onPopupOpened;
    std::function<void()> onPopupClosed;
    std::function<void(ColorFormat)> onFormatChanged;

private:
    // ===== LAYOUT CALCULATION =====
    void CalculateLayout() {
        Rect2D bounds = GetBounds();
        
        switch (style.mode) {
            case ColorPickerMode::Compact:
                CalculateCompactLayout(bounds);
                break;
            case ColorPickerMode::Inline:
                CalculateInlineLayout(bounds);
                break;
            case ColorPickerMode::Popup:
                CalculatePopupLayout(bounds);
                break;
            case ColorPickerMode::Palette:
                CalculatePaletteLayout(bounds);
                break;
        }
    }
    
    void CalculateCompactLayout(const Rect2D& bounds) {
        // Just a color swatch that opens popup when clicked
        previewRect = bounds;
    }
    
    void CalculateInlineLayout(const Rect2D& bounds) {
        float currentY = bounds.y + style.spacing;
        float currentX = bounds.x + style.spacing;
        
        // Color wheel/square
        wheelRect = Rect2D(currentX, currentY, style.wheelSize, style.wheelSize);
        wheelCenter = Point2D(wheelRect.x + wheelRect.width / 2, wheelRect.y + wheelRect.height / 2);
        wheelRadius = std::min(wheelRect.width, wheelRect.height) / 2 - 5;
        
        currentX += style.wheelSize + style.spacing;
        
        // Sliders (Hue, Saturation, Value, Alpha)
        sliderRects.clear();
        int sliderCount = style.showAlpha ? 4 : 3;
        float sliderHeight = (style.wheelSize - style.spacing * (sliderCount - 1)) / sliderCount;
        
        for (int i = 0; i < sliderCount; i++) {
            sliderRects.emplace_back(currentX, currentY, style.sliderLength, sliderHeight);
            currentY += sliderHeight + style.spacing;
        }
        
        // Preview area
        currentY = bounds.y + style.spacing;
        currentX += style.sliderLength + style.spacing;
        previewRect = Rect2D(currentX, currentY, 60, 40);
        
        // Palette area
        if (style.showPalette) {
            currentY += 50;
            paletteRect = Rect2D(bounds.x + style.spacing, currentY, 
                               bounds.width - style.spacing * 2, 60);
            CalculatePaletteSwatches();
        }
    }
    
    void CalculatePopupLayout(const Rect2D& bounds) {
        // Button that opens popup
        previewRect = bounds;
    }
    
    void CalculatePaletteLayout(const Rect2D& bounds) {
        paletteRect = bounds;
        CalculatePaletteSwatches();
    }
    
    void CalculatePaletteSwatches() {
        paletteSwatches.clear();
        if (selectedPaletteIndex >= palettes.size()) return;
        
        const auto& palette = palettes[selectedPaletteIndex];
        int swatchesPerRow = static_cast<int>(paletteRect.width / (style.swatchSize + 2));
        
        for (size_t i = 0; i < palette.colors.size(); i++) {
            int row = static_cast<int>(i / swatchesPerRow);
            int col = static_cast<int>(i % swatchesPerRow);
            
            float x = paletteRect.x + col * (style.swatchSize + 2);
            float y = paletteRect.y + row * (style.swatchSize + 2);
            
            paletteSwatches.emplace_back(x, y, style.swatchSize, style.swatchSize);
        }
    }
    
    // ===== RENDERING METHODS =====
    void RenderCompactMode() {
        // Draw color swatch
        SetFillColor(currentColor);
        UltraCanvas::DrawFilledRect(previewRect, currentColor, style.borderColor, 1.0f);
        
        // Draw popup if open
        if (isPopupOpen) {
            RenderColorPickerPopup();
        }
    }
    
    void RenderInlineMode() {
        // Draw background
        SetFillColor(style.backgroundColor);
        UltraCanvas::DrawFilledRect(GetBounds(), style.backgroundColor, style.borderColor, 1.0f);
        
        // Draw color wheel/square
        RenderColorWheel();
        
        // Draw sliders
        RenderColorSliders();
        
        // Draw preview
        RenderColorPreview();
        
        // Draw palette
        if (style.showPalette) {
            RenderColorPalette();
        }
        
        // Draw input fields
        if (style.showInputFields) {
            RenderInputFields();
        }
    }
    
    void RenderPopupMode() {
        // Draw button
        Color buttonColor = IsHovered() ? style.sliderHandleColor : currentColor;
        UltraCanvas::DrawFilledRect(previewRect, buttonColor, style.borderColor, 1.0f);
        
        // Draw popup if open
        if (isPopupOpen) {
            RenderColorPickerPopup();
        }
    }
    
    void RenderPaletteMode() {
        RenderColorPalette();
    }
    
    void RenderColorWheel() {
        if (wheelRect.width <= 0) return;
        
        // Draw color wheel based on type
        switch (style.wheelType) {
            case ColorWheelType::HSV_Circle:
                RenderHSVWheel();
                break;
            case ColorWheelType::Square_SV:
                RenderSquareSVPicker();
                break;
            default:
                RenderHSVWheel();
                break;
        }
        
        // Draw selection indicator
        RenderWheelSelectionIndicator();
    }
    
    void RenderHSVWheel() {
        // Draw hue wheel (simplified - in real implementation would use pixel manipulation)
        for (int i = 0; i < 360; i += 5) {
            float angle = i * M_PI / 180.0f;
            HSVColor hueColor(i, 1.0f, 1.0f, 1.0f);
            
            SetStrokeColor(hueColor.ToRGB());
            SetStrokeWidth(2.0f);
            
            float x1 = wheelCenter.x + (wheelRadius - 10) * cos(angle);
            float y1 = wheelCenter.y + (wheelRadius - 10) * sin(angle);
            float x2 = wheelCenter.x + wheelRadius * cos(angle);
            float y2 = wheelCenter.y + wheelRadius * sin(angle);
            
            DrawLine(Point2D(x1, y1), Point2D(x2, y2));
        }
        
        // Draw saturation/value square in center (simplified)
        float squareSize = wheelRadius * 1.2f;
        Rect2D svSquare(wheelCenter.x - squareSize/2, wheelCenter.y - squareSize/2, 
                       squareSize, squareSize);
        
        // Draw gradient from white to current hue color
        HSVColor baseHue(currentHSV.h, 1.0f, 1.0f, 1.0f);
        DrawGradientRect(svSquare, Colors::White, baseHue.ToRGB(), false);
        
        // Draw black overlay for value
        Color blackOverlay(0, 0, 0, static_cast<uint8_t>((1.0f - currentHSV.v) * 255));
        SetFillColor(blackOverlay);
        DrawRect(svSquare);
    }
    
    void RenderSquareSVPicker() {
        // Simplified square saturation/value picker
        DrawGradientRect(wheelRect, Colors::White, 
                        HSVColor(currentHSV.h, 1.0f, 1.0f).ToRGB(), true);
    }
    
    void RenderWheelSelectionIndicator() {
        // Draw selection crosshair
        Point2D selectionPos = CalculateSelectionPosition();
        
        SetStrokeColor(Colors::White);
        SetStrokeWidth(2.0f);
        
        // Draw crosshair
        DrawLine(Point2D(selectionPos.x - 5, selectionPos.y), 
                Point2D(selectionPos.x + 5, selectionPos.y));
        DrawLine(Point2D(selectionPos.x, selectionPos.y - 5), 
                Point2D(selectionPos.x, selectionPos.y + 5));
        
        // Draw circle
        DrawCircle(selectionPos, 3.0f);
    }
    
    Point2D CalculateSelectionPosition() {
        // Calculate position based on current HSV values
        if (style.wheelType == ColorWheelType::HSV_Circle) {
            // For HSV wheel, selection is in the center square
            float squareSize = wheelRadius * 1.2f;
            float x = wheelCenter.x - squareSize/2 + currentHSV.s * squareSize;
            float y = wheelCenter.y - squareSize/2 + (1.0f - currentHSV.v) * squareSize;
            return Point2D(x, y);
        } else {
            // For square picker
            float x = wheelRect.x + currentHSV.s * wheelRect.width;
            float y = wheelRect.y + (1.0f - currentHSV.v) * wheelRect.height;
            return Point2D(x, y);
        }
    }
    
    void RenderColorSliders() {
        if (sliderRects.empty()) return;
        
        // Hue slider
        if (sliderRects.size() > 0) {
            RenderHueSlider(sliderRects[0]);
        }
        
        // Saturation slider
        if (sliderRects.size() > 1) {
            RenderSaturationSlider(sliderRects[1]);
        }
        
        // Value slider
        if (sliderRects.size() > 2) {
            RenderValueSlider(sliderRects[2]);
        }
        
        // Alpha slider
        if (sliderRects.size() > 3 && style.showAlpha) {
            RenderAlphaSlider(sliderRects[3]);
        }
    }
    
    void RenderHueSlider(const Rect2D& rect) {
        // Draw hue gradient
        for (int i = 0; i < static_cast<int>(rect.width); i++) {
            float hue = (i / rect.width) * 360.0f;
            HSVColor hueColor(hue, 1.0f, 1.0f);
            
            SetStrokeColor(hueColor.ToRGB());
            SetStrokeWidth(1.0f);
            DrawLine(Point2D(rect.x + i, rect.y), 
                    Point2D(rect.x + i, rect.y + rect.height));
        }
        
        // Draw handle
        float handleX = rect.x + (currentHSV.h / 360.0f) * rect.width;
        RenderSliderHandle(Point2D(handleX, rect.y + rect.height / 2));
    }
    
    void RenderSaturationSlider(const Rect2D& rect) {
        HSVColor lowSat(currentHSV.h, 0.0f, currentHSV.v);
        HSVColor highSat(currentHSV.h, 1.0f, currentHSV.v);
        
        DrawGradientRect(rect, lowSat.ToRGB(), highSat.ToRGB(), true);
        
        float handleX = rect.x + currentHSV.s * rect.width;
        RenderSliderHandle(Point2D(handleX, rect.y + rect.height / 2));
    }
    
    void RenderValueSlider(const Rect2D& rect) {
        HSVColor lowValue(currentHSV.h, currentHSV.s, 0.0f);
        HSVColor highValue(currentHSV.h, currentHSV.s, 1.0f);
        
        DrawGradientRect(rect, lowValue.ToRGB(), highValue.ToRGB(), true);
        
        float handleX = rect.x + currentHSV.v * rect.width;
        RenderSliderHandle(Point2D(handleX, rect.y + rect.height / 2));
    }
    
    void RenderAlphaSlider(const Rect2D& rect) {
        // Draw checkerboard background for transparency
        RenderTransparencyBackground(rect);
        
        // Draw alpha gradient
        Color transparent = currentColor.WithAlpha(0);
        Color opaque = currentColor.WithAlpha(255);
        DrawGradientRect(rect, transparent, opaque, true);
        
        float handleX = rect.x + GetAlpha() * rect.width;
        RenderSliderHandle(Point2D(handleX, rect.y + rect.height / 2));
    }
    
    void RenderSliderHandle(const Point2D& position) {
        SetFillColor(style.sliderHandleColor);
        SetStrokeColor(Colors::Gray);
        SetStrokeWidth(1.0f);
        
        DrawCircle(position, 6.0f);
    }
    
    void RenderColorPreview() {
        if (previewRect.width <= 0) return;
        
        // Draw transparency background
        RenderTransparencyBackground(previewRect);
        
        // Draw current color
        SetFillColor(currentColor);
        UltraCanvas::DrawFilledRect(previewRect, currentColor, style.borderColor, 1.0f);
        
        // Draw label
        SetTextColor(style.textColor);
        DrawText("Preview", Point2D(previewRect.x, previewRect.y + previewRect.height + 5));
    }
    
    void RenderColorPalette() {
        if (selectedPaletteIndex >= palettes.size()) return;
        
        const auto& palette = palettes[selectedPaletteIndex];
        
        for (size_t i = 0; i < palette.colors.size() && i < paletteSwatches.size(); i++) {
            const Color& color = palette.colors[i];
            const Rect2D& swatch = paletteSwatches[i];
            
            // Draw transparency background for colors with alpha
            if (color.a < 255) {
                RenderTransparencyBackground(swatch);
            }
            
            // Draw color swatch
            SetFillColor(color);
            UltraCanvas::DrawFilledRect(swatch, color, style.borderColor, 1.0f);
            
            // Highlight if this is the current color
            if (color == currentColor) {
                SetStrokeColor(Colors::Black);
                SetStrokeWidth(2.0f);
                DrawRect(swatch);
            }
        }
    }
    
    void RenderInputFields() {
        // Would render text input fields for RGB/HSV/HEX values
        // Simplified for this example
    }
    
    void RenderColorPickerPopup() {
        // Render full color picker in popup window
        // Would need to create a popup window/dialog
        // Simplified for this example
    }
    
    void RenderTransparencyBackground(const Rect2D& rect) {
        // Draw checkerboard pattern for transparency visualization
        int checkSize = 8;
        Color light = Color(240, 240, 240);
        Color dark = Color(200, 200, 200);
        
        for (int y = 0; y < static_cast<int>(rect.height); y += checkSize) {
            for (int x = 0; x < static_cast<int>(rect.width); x += checkSize) {
                bool isLight = ((x / checkSize) + (y / checkSize)) % 2 == 0;
                SetFillColor(isLight ? light : dark);
                
                Rect2D checkRect(rect.x + x, rect.y + y, checkSize, checkSize);
                DrawRect(checkRect);
            }
        }
    }
    
    // ===== EVENT HANDLERS =====
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        Point2D clickPos(event.x, event.y);
        
        // Check if clicking on wheel/square
        if (wheelRect.Contains(clickPos)) {
            isDraggingWheel = true;
            UpdateColorFromWheelPosition(clickPos);
            return;
        }
        
        // Check if clicking on sliders
        for (size_t i = 0; i < sliderRects.size(); i++) {
            if (sliderRects[i].Contains(clickPos)) {
                isDraggingSlider = true;
                activeSlider = static_cast<int>(i);
                UpdateColorFromSliderPosition(clickPos, activeSlider);
                return;
            }
        }
        
        // Check if clicking on palette
        for (size_t i = 0; i < paletteSwatches.size(); i++) {
            if (paletteSwatches[i].Contains(clickPos)) {
                if (selectedPaletteIndex < palettes.size() && i < palettes[selectedPaletteIndex].colors.size()) {
                    SetColor(palettes[selectedPaletteIndex].colors[i]);
                    if (onColorSelected) onColorSelected(currentColor);
                }
                return;
            }
        }
        
        // Check if clicking on preview (for compact/popup modes)
        if (previewRect.Contains(clickPos)) {
            if (style.mode == ColorPickerMode::Compact || style.mode == ColorPickerMode::Popup) {
                if (isPopupOpen) {
                    HidePopup();
                } else {
                    ShowPopup();
                }
            }
        }
    }
    
    void HandleMouseMove(const UCEvent& event) {
        Point2D movePos(event.x, event.y);
        
        if (isDraggingWheel) {
            UpdateColorFromWheelPosition(movePos);
        } else if (isDraggingSlider && activeSlider >= 0) {
            UpdateColorFromSliderPosition(movePos, activeSlider);
        }
    }
    
    void HandleMouseUp(const UCEvent& event) {
        isDraggingWheel = false;
        isDraggingSlider = false;
        activeSlider = -1;
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (!IsFocused()) return;
        
        switch (event.virtualKey) {
            case UCKeys::Escape:
                if (isPopupOpen) HidePopup();
                break;
            case UCKeys::Return:
                if (onColorSelected) onColorSelected(currentColor);
                break;
        }
    }
    
    void UpdateColorFromWheelPosition(const Point2D& position) {
        if (style.wheelType == ColorWheelType::HSV_Circle) {
            // Update saturation and value from center square position
            float squareSize = wheelRadius * 1.2f;
            float relativeX = (position.x - (wheelCenter.x - squareSize/2)) / squareSize;
            float relativeY = (position.y - (wheelCenter.y - squareSize/2)) / squareSize;
            
            currentHSV.s = std::clamp(relativeX, 0.0f, 1.0f);
            currentHSV.v = std::clamp(1.0f - relativeY, 0.0f, 1.0f);
        } else {
            // Square picker
            float relativeX = (position.x - wheelRect.x) / wheelRect.width;
            float relativeY = (position.y - wheelRect.y) / wheelRect.height;
            
            currentHSV.s = std::clamp(relativeX, 0.0f, 1.0f);
            currentHSV.v = std::clamp(1.0f - relativeY, 0.0f, 1.0f);
        }
        
        SetColor(currentHSV.ToRGB());
    }
    
    void UpdateColorFromSliderPosition(const Point2D& position, int sliderIndex) {
        if (sliderIndex >= static_cast<int>(sliderRects.size())) return;
        
        const Rect2D& slider = sliderRects[sliderIndex];
        float relative = std::clamp((position.x - slider.x) / slider.width, 0.0f, 1.0f);
        
        switch (sliderIndex) {
            case 0: // Hue
                currentHSV.h = relative * 360.0f;
                break;
            case 1: // Saturation
                currentHSV.s = relative;
                break;
            case 2: // Value
                currentHSV.v = relative;
                break;
            case 3: // Alpha
                if (style.showAlpha) {
                    SetAlpha(relative);
                    return; // SetAlpha calls SetColor, so return early
                }
                break;
        }
        
        SetColor(currentHSV.ToRGB());
    }
};

// ===== COLOR CONVERSION IMPLEMENTATIONS =====
inline Color HSVColor::ToRGB() const {
    float c = v * s;
    float x = c * (1 - abs(fmod(h / 60.0f, 2) - 1));
    float m = v - c;
    
    float r1, g1, b1;
    
    if (h >= 0 && h < 60) {
        r1 = c; g1 = x; b1 = 0;
    } else if (h >= 60 && h < 120) {
        r1 = x; g1 = c; b1 = 0;
    } else if (h >= 120 && h < 180) {
        r1 = 0; g1 = c; b1 = x;
    } else if (h >= 180 && h < 240) {
        r1 = 0; g1 = x; b1 = c;
    } else if (h >= 240 && h < 300) {
        r1 = x; g1 = 0; b1 = c;
    } else {
        r1 = c; g1 = 0; b1 = x;
    }
    
    return Color(
        static_cast<uint8_t>((r1 + m) * 255),
        static_cast<uint8_t>((g1 + m) * 255),
        static_cast<uint8_t>((b1 + m) * 255),
        static_cast<uint8_t>(a * 255)
    );
}

inline HSVColor HSVColor::FromRGB(const Color& rgb) {
    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;
    float alpha = rgb.a / 255.0f;
    
    float max = std::max({r, g, b});
    float min = std::min({r, g, b});
    float delta = max - min;
    
    float h = 0;
    if (delta != 0) {
        if (max == r) {
            h = 60 * fmod((g - b) / delta, 6);
        } else if (max == g) {
            h = 60 * ((b - r) / delta + 2);
        } else {
            h = 60 * ((r - g) / delta + 4);
        }
    }
    if (h < 0) h += 360;
    
    float s = (max == 0) ? 0 : delta / max;
    float v = max;
    
    return HSVColor(h, s, v, alpha);
}

inline std::string HSVColor::ToString() const {
    return "hsv(" + std::to_string(static_cast<int>(h)) + ", " + 
           std::to_string(static_cast<int>(s * 100)) + "%, " + 
           std::to_string(static_cast<int>(v * 100)) + "%)";
}

// HSL Color conversions
inline Color HSLColor::ToRGB() const {
    float c = (1 - abs(2 * l - 1)) * s;
    float x = c * (1 - abs(fmod(h / 60.0f, 2) - 1));
    float m = l - c / 2;
    
    float r1, g1, b1;
    
    if (h >= 0 && h < 60) {
        r1 = c; g1 = x; b1 = 0;
    } else if (h >= 60 && h < 120) {
        r1 = x; g1 = c; b1 = 0;
    } else if (h >= 120 && h < 180) {
        r1 = 0; g1 = c; b1 = x;
    } else if (h >= 180 && h < 240) {
        r1 = 0; g1 = x; b1 = c;
    } else if (h >= 240 && h < 300) {
        r1 = x; g1 = 0; b1 = c;
    } else {
        r1 = c; g1 = 0; b1 = x;
    }
    
    return Color(
        static_cast<uint8_t>((r1 + m) * 255),
        static_cast<uint8_t>((g1 + m) * 255),
        static_cast<uint8_t>((b1 + m) * 255),
        static_cast<uint8_t>(a * 255)
    );
}

inline HSLColor HSLColor::FromRGB(const Color& rgb) {
    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;
    float alpha = rgb.a / 255.0f;
    
    float max = std::max({r, g, b});
    float min = std::min({r, g, b});
    float delta = max - min;
    
    float h = 0;
    if (delta != 0) {
        if (max == r) {
            h = 60 * fmod((g - b) / delta, 6);
        } else if (max == g) {
            h = 60 * ((b - r) / delta + 2);
        } else {
            h = 60 * ((r - g) / delta + 4);
        }
    }
    if (h < 0) h += 360;
    
    float l = (max + min) / 2;
    float s = (delta == 0) ? 0 : delta / (1 - abs(2 * l - 1));
    
    return HSLColor(h, s, l, alpha);
}

inline std::string HSLColor::ToString() const {
    return "hsl(" + std::to_string(static_cast<int>(h)) + ", " + 
           std::to_string(static_cast<int>(s * 100)) + "%, " + 
           std::to_string(static_cast<int>(l * 100)) + "%)";
}

// CMYK Color conversions
inline Color CMYKColor::ToRGB() const {
    float r = (1 - c) * (1 - k);
    float g = (1 - m) * (1 - k);
    float b = (1 - y) * (1 - k);
    
    return Color(
        static_cast<uint8_t>(r * 255),
        static_cast<uint8_t>(g * 255),
        static_cast<uint8_t>(b * 255),
        static_cast<uint8_t>(a * 255)
    );
}

inline CMYKColor CMYKColor::FromRGB(const Color& rgb) {
    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;
    float alpha = rgb.a / 255.0f;
    
    float k = 1 - std::max({r, g, b});
    
    float c = (k == 1) ? 0 : (1 - r - k) / (1 - k);
    float m = (k == 1) ? 0 : (1 - g - k) / (1 - k);
    float y = (k == 1) ? 0 : (1 - b - k) / (1 - k);
    
    return CMYKColor(c, m, y, k, alpha);
}

inline std::string CMYKColor::ToString() const {
    return "cmyk(" + std::to_string(static_cast<int>(c * 100)) + "%, " + 
           std::to_string(static_cast<int>(m * 100)) + "%, " + 
           std::to_string(static_cast<int>(y * 100)) + "%, " + 
           std::to_string(static_cast<int>(k * 100)) + "%)";
}

// ===== PREDEFINED PALETTES =====
inline ColorPalette ColorPalette::Basic() {
    return ColorPalette("Basic", {
        Colors::Red, Colors::Green, Colors::Blue, Colors::Yellow,
        Colors::Cyan, Colors::Magenta, Colors::White, Colors::Black,
        Colors::Gray, Colors::LightGray, Colors::DarkGray
    });
}

inline ColorPalette ColorPalette::Web() {
    return ColorPalette("Web Safe", {
        Color(255, 0, 0), Color(0, 255, 0), Color(0, 0, 255),
        Color(255, 255, 0), Color(255, 0, 255), Color(0, 255, 255),
        Color(128, 0, 0), Color(0, 128, 0), Color(0, 0, 128),
        Color(128, 128, 0), Color(128, 0, 128), Color(0, 128, 128)
    });
}

inline ColorPalette ColorPalette::Material() {
    return ColorPalette("Material Design", {
        Color(244, 67, 54),   // Red
        Color(233, 30, 99),   // Pink
        Color(156, 39, 176),  // Purple
        Color(103, 58, 183),  // Deep Purple
        Color(63, 81, 181),   // Indigo
        Color(33, 150, 243),  // Blue
        Color(3, 169, 244),   // Light Blue
        Color(0, 188, 212),   // Cyan
        Color(0, 150, 136),   // Teal
        Color(76, 175, 80),   // Green
        Color(139, 195, 74),  // Light Green
        Color(205, 220, 57)   // Lime
    });
}

inline ColorPalette ColorPalette::Pastel() {
    return ColorPalette("Pastel", {
        Color(255, 179, 186), Color(255, 223, 186), Color(255, 255, 186),
        Color(186, 255, 201), Color(186, 225, 255), Color(186, 186, 255),
        Color(225, 186, 255), Color(255, 186, 255), Color(255, 186, 225),
        Color(220, 220, 220), Color(245, 245, 245), Color(255, 255, 255)
    });
}

inline ColorPalette ColorPalette::Grayscale() {
    return ColorPalette("Grayscale", {
        Color(0, 0, 0), Color(32, 32, 32), Color(64, 64, 64), Color(96, 96, 96),
        Color(128, 128, 128), Color(160, 160, 160), Color(192, 192, 192),
        Color(224, 224, 224), Color(240, 240, 240), Color(255, 255, 255)
    });
}

inline ColorPalette ColorPalette::Rainbow() {
    return ColorPalette("Rainbow", {
        Color(255, 0, 0),     // Red
        Color(255, 127, 0),   // Orange
        Color(255, 255, 0),   // Yellow
        Color(127, 255, 0),   // Spring Green
        Color(0, 255, 0),     // Green
        Color(0, 255, 127),   // Turquoise
        Color(0, 255, 255),   // Cyan
        Color(0, 127, 255),   // Ocean
        Color(0, 0, 255),     // Blue
        Color(127, 0, 255),   // Violet
        Color(255, 0, 255),   // Magenta
        Color(255, 0, 127)    // Rose
    });
}

inline ColorPalette ColorPalette::Custom(const std::string& name, const std::vector<Color>& colors) {
    return ColorPalette(name, colors);
}

// ===== COLOR PICKER STYLE IMPLEMENTATIONS =====
inline ColorPickerStyle ColorPickerStyle::Compact() {
    ColorPickerStyle style;
    style.mode = ColorPickerMode::Compact;
    style.swatchSize = 24.0f;
    style.showAlpha = false;
    style.showPreview = true;
    style.showInputFields = false;
    style.showPalette = false;
    return style;
}

inline ColorPickerStyle ColorPickerStyle::Inline() {
    ColorPickerStyle style;
    style.mode = ColorPickerMode::Inline;
    style.wheelSize = 200.0f;
    style.showAlpha = true;
    style.showPreview = true;
    style.showInputFields = true;
    style.showPalette = true;
    return style;
}

inline ColorPickerStyle ColorPickerStyle::Popup() {
    ColorPickerStyle style;
    style.mode = ColorPickerMode::Popup;
    style.wheelSize = 180.0f;
    style.showAlpha = true;
    style.showPreview = true;
    style.showInputFields = false;
    style.showPalette = true;
    return style;
}

inline ColorPickerStyle ColorPickerStyle::Palette() {
    ColorPickerStyle style;
    style.mode = ColorPickerMode::Palette;
    style.swatchSize = 32.0f;
    style.showAlpha = false;
    style.showPreview = false;
    style.showInputFields = false;
    style.showPalette = true;
    return style;
}

inline ColorPickerStyle ColorPickerStyle::Professional() {
    ColorPickerStyle style;
    style.mode = ColorPickerMode::Inline;
    style.wheelSize = 250.0f;
    style.sliderLength = 200.0f;
    style.showAlpha = true;
    style.showPreview = true;
    style.showInputFields = true;
    style.showPalette = true;
    style.showEyedropper = true;
    style.supportedFormats = {ColorFormat::RGB, ColorFormat::HSV, ColorFormat::HSL, ColorFormat::HEX, ColorFormat::CMYK};
    return style;
}

// ===== FACTORY FUNCTIONS =====
inline std::shared_ptr<UltraCanvasColorPicker> CreateColorPicker(
    const std::string& identifier, long id, long x, long y, long w, long h) {
    return UltraCanvasElementFactory::CreateWithID<UltraCanvasColorPicker>(
        id, identifier, id, x, y, w, h);
}

inline std::shared_ptr<UltraCanvasColorPicker> CreateCompactColorPicker(
    const std::string& identifier, long id, long x, long y, const Color& initialColor = Colors::Red) {
    auto picker = CreateColorPicker(identifier, id, x, y, 28, 28);
    picker->SetStyle(ColorPickerStyle::Compact());
    picker->SetColor(initialColor);
    return picker;
}

inline std::shared_ptr<UltraCanvasColorPicker> CreateInlineColorPicker(
    const std::string& identifier, long id, long x, long y, const Color& initialColor = Colors::Red) {
    auto picker = CreateColorPicker(identifier, id, x, y, 400, 300);
    picker->SetStyle(ColorPickerStyle::Inline());
    picker->SetColor(initialColor);
    return picker;
}

inline std::shared_ptr<UltraCanvasColorPicker> CreatePaletteColorPicker(
    const std::string& identifier, long id, long x, long y, long w, long h, 
    const ColorPalette& palette) {
    auto picker = CreateColorPicker(identifier, id, x, y, w, h);
    picker->SetStyle(ColorPickerStyle::Palette());
    picker->AddPalette(palette);
    return picker;
}

// ===== BUILDER PATTERN =====
class ColorPickerBuilder {
private:
    std::string identifier = "ColorPicker";
    long id = 0;
    long x = 0, y = 0, w = 300, h = 250;
    Color initialColor = Colors::Red;
    ColorPickerStyle style = ColorPickerStyle::Inline();
    std::vector<ColorPalette> palettes;
    std::function<void(const Color&)> colorHandler;
    std::function<void(uint32_t)> rawColorHandler;
    
public:
    ColorPickerBuilder& SetIdentifier(const std::string& id) { identifier = id; return *this; }
    ColorPickerBuilder& SetID(long elementId) { id = elementId; return *this; }
    ColorPickerBuilder& SetPosition(long px, long py) { x = px; y = py; return *this; }
    ColorPickerBuilder& SetSize(long width, long height) { w = width; h = height; return *this; }
    ColorPickerBuilder& SetInitialColor(const Color& color) { initialColor = color; return *this; }
    ColorPickerBuilder& SetStyle(const ColorPickerStyle& pickerStyle) { style = pickerStyle; return *this; }
    ColorPickerBuilder& SetMode(ColorPickerMode mode) { style.mode = mode; return *this; }
    ColorPickerBuilder& EnableAlpha(bool enable) { style.showAlpha = enable; return *this; }
    ColorPickerBuilder& ShowPalette(bool show) { style.showPalette = show; return *this; }
    ColorPickerBuilder& AddPalette(const ColorPalette& palette) { palettes.push_back(palette); return *this; }
    ColorPickerBuilder& OnColorChanged(std::function<void(const Color&)> handler) { colorHandler = handler; return *this; }
    ColorPickerBuilder& OnColorChangedRaw(std::function<void(uint32_t)> handler) { rawColorHandler = handler; return *this; }
    
    std::shared_ptr<UltraCanvasColorPicker> Build() {
        auto picker = CreateColorPicker(identifier, id, x, y, w, h);
        picker->SetStyle(style);
        picker->SetColor(initialColor);
        
        for (const auto& palette : palettes) {
            picker->AddPalette(palette);
        }
        
        if (colorHandler) picker->onColorChanged = colorHandler;
        if (rawColorHandler) picker->onColorChangedRaw = rawColorHandler;
        
        return picker;
    }
};

// ===== LEGACY C-STYLE API (BACKWARD COMPATIBLE) =====
extern "C" {
    void* CreateColorPickerC(int x, int y, int width, int height);
    void SetColorPickerColor(void* handle, uint32_t argb);
    uint32_t GetColorPickerColor(void* handle);
    void SetColorPickerMode(void* handle, int mode);
    void SetColorPickerAlphaEnabled(void* handle, bool enabled);
    void SetColorPickerChangeHandler(void* handle, void(*handler)(uint32_t));
    void ShowColorPickerPopup(void* handle);
    void HideColorPickerPopup(void* handle);
    void DestroyColorPicker(void* handle);
}

} // namespace UltraCanvas
    