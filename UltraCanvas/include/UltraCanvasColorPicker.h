// include/UltraCanvasColorPicker.h
// Comprehensive colour picker widget with HSV colour wheel, SV square,
// preview swatches, hex input, mode selector (HSV/HSL/RGB) and editable
// channel sliders with an alpha channel.
// Version: 1.0.0
// Last Modified: 2026-06-21
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"
#include <string>
#include <functional>
#include <memory>
#include <array>
#include <cmath>
#include <algorithm>

namespace UltraCanvas {

// ===== COLOUR MODEL USED BY THE CHANNEL EDITORS =====
    enum class ColorPickerModel {
        HSV,
        HSL,
        RGB
    };

// ===== STYLING =====
    struct ColorPickerStyle {
        // Surface
        Color backgroundColor = Color(43, 43, 43, 255);
        Color panelColor = Color(60, 60, 60, 255);
        Color borderColor = Color(80, 80, 80, 255);
        Color textColor = Color(220, 220, 220, 255);
        Color mutedTextColor = Color(160, 160, 160, 255);

        // Field / control chrome
        Color fieldColor = Color(30, 30, 30, 255);
        Color fieldBorderColor = Color(90, 90, 90, 255);
        Color accentColor = Color(230, 60, 60, 255);     // selection / active tab
        Color markerColor = Colors::White;
        Color markerOutline = Color(20, 20, 20, 255);

        // Metrics
        float padding = 10.0f;
        float ringThickness = 26.0f;     // hue ring thickness
        float cornerRadius = 4.0f;
        float rowHeight = 22.0f;
        float rowGap = 6.0f;
        float tabHeight = 24.0f;
        float swatchSize = 36.0f;

        // Fonts
        std::string fontFamily = "Sans";
        float fontSize = 13.0f;
    };

// ===== COLOUR SPACE CONVERSION HELPERS =====
// HSV->RGB is provided by the global UltraCanvas::HSV() helper. The remaining
// conversions are defined here so the picker (and callers) can round-trip
// between RGB, HSV and HSL without losing hue information on greys.

    inline void RGBToHSV(const Color& c, float& h, float& s, float& v) {
        float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
        float maxC = std::max({r, g, b});
        float minC = std::min({r, g, b});
        float delta = maxC - minC;

        v = maxC;
        s = (maxC <= 0.0f) ? 0.0f : (delta / maxC);

        if (delta <= 1e-6f) {
            h = 0.0f; // achromatic; hue undefined, keep 0 by convention
            return;
        }
        if (maxC == r) {
            h = 60.0f * std::fmod((g - b) / delta, 6.0f);
        } else if (maxC == g) {
            h = 60.0f * (((b - r) / delta) + 2.0f);
        } else {
            h = 60.0f * (((r - g) / delta) + 4.0f);
        }
        if (h < 0.0f) h += 360.0f;
    }

    inline void RGBToHSL(const Color& c, float& h, float& s, float& l) {
        float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
        float maxC = std::max({r, g, b});
        float minC = std::min({r, g, b});
        float delta = maxC - minC;

        l = (maxC + minC) * 0.5f;

        if (delta <= 1e-6f) {
            h = 0.0f;
            s = 0.0f;
            return;
        }
        s = delta / (1.0f - std::fabs(2.0f * l - 1.0f));

        if (maxC == r) {
            h = 60.0f * std::fmod((g - b) / delta, 6.0f);
        } else if (maxC == g) {
            h = 60.0f * (((b - r) / delta) + 2.0f);
        } else {
            h = 60.0f * (((r - g) / delta) + 4.0f);
        }
        if (h < 0.0f) h += 360.0f;
    }

    inline Color HSLToRGB(float h, float s, float l, uint8_t a = 255) {
        float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
        float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
        float m = l - c * 0.5f;

        float r = 0, g = 0, b = 0;
        if (h < 60)       { r = c; g = x; b = 0; }
        else if (h < 120) { r = x; g = c; b = 0; }
        else if (h < 180) { r = 0; g = c; b = x; }
        else if (h < 240) { r = 0; g = x; b = c; }
        else if (h < 300) { r = x; g = 0; b = c; }
        else              { r = c; g = 0; b = x; }

        return Color(
            static_cast<uint8_t>(std::lround((r + m) * 255.0f)),
            static_cast<uint8_t>(std::lround((g + m) * 255.0f)),
            static_cast<uint8_t>(std::lround((b + m) * 255.0f)),
            a);
    }

// ===== MAIN WIDGET =====
    class UltraCanvasColorPicker : public UltraCanvasUIElement {
    public:
        // ===== CONSTRUCTORS (standard UltraCanvas pattern) =====
        UltraCanvasColorPicker(const std::string& identifier, float x, float y, float w, float h);

        UltraCanvasColorPicker(const std::string& identifier, float w, float h)
            : UltraCanvasColorPicker(identifier, -1, -1, w, h) {}

        explicit UltraCanvasColorPicker(const std::string& identifier)
            : UltraCanvasColorPicker(identifier, -1, -1, 290, 470) {}

        // ===== COLOUR ACCESS =====
        Color GetColor() const;
        void SetColor(const Color& color, bool notify = true);

        uint8_t GetAlpha() const { return alpha; }
        void SetAlpha(uint8_t a, bool notify = true);

        // HSV accessors (canonical internal representation)
        float GetHue() const { return hue; }
        float GetSaturation() const { return sat; }
        float GetValue() const { return val; }
        void SetHSV(float h, float s, float v, bool notify = true);

        // The colour shown in the "previous" swatch; defaults to the initial colour.
        Color GetPreviousColor() const { return previousColor; }
        void SetPreviousColor(const Color& color) { previousColor = color; RequestRedraw(); }
        // Commit the current colour as the new baseline "previous" swatch.
        void CommitColor() { previousColor = GetColor(); RequestRedraw(); }

        // ===== MODE / LAYOUT =====
        ColorPickerModel GetModel() const { return model; }
        void SetModel(ColorPickerModel m);

        // Compact mode hides the colour wheel and preview swatches, leaving only
        // the channel sliders (useful as an inline editor inside a toolbar/panel).
        void SetShowColorWheel(bool show);
        bool GetShowColorWheel() const { return showColorWheel; }

        void SetShowAlpha(bool show);
        bool GetShowAlpha() const { return showAlpha; }

        const ColorPickerStyle& GetStyle() const { return style; }
        void SetStyle(const ColorPickerStyle& s) { style = s; layoutValid = false; RequestRedraw(); }

        // ===== CALLBACKS =====
        std::function<void(const Color&)> onColorChanged;    // final value (drag end / commit)
        std::function<void(const Color&)> onColorChanging;   // continuous, during interaction

        // ===== UIElement OVERRIDES =====
        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
        bool OnEvent(const UCEvent& event) override;
        bool AcceptsFocus() const override { return true; }

    private:
        // ----- Canonical colour state (HSV is the source of truth) -----
        float hue = 0.0f;        // 0..360
        float sat = 1.0f;        // 0..1
        float val = 1.0f;        // 0..1
        uint8_t alpha = 255;
        Color previousColor = Colors::Red;

        ColorPickerModel model = ColorPickerModel::HSV;
        ColorPickerStyle style;
        bool showColorWheel = true;
        bool showAlpha = true;

        // ----- Interaction state -----
        enum class DragTarget { NoneTarget, HueRing, SVSquare, Channel0, Channel1, Channel2, Alpha };
        DragTarget dragTarget = DragTarget::NoneTarget;

        enum class EditField { NoEdit, Hex, Channel0, Channel1, Channel2, Alpha };
        EditField editField = EditField::NoEdit;
        std::string editBuffer;

        bool dropdownOpen = false;

        // ----- Cached layout (recomputed on resize) -----
        bool layoutValid = false;
        float cachedW = -1, cachedH = -1;
        Rect2Df wheelRect;          // bounding box of the hue ring
        Point2Df wheelCenter;
        float ringOuter = 0, ringInner = 0;
        Rect2Df svRect;             // saturation/value square
        Rect2Df currentSwatchRect;
        Rect2Df previousSwatchRect;
        Rect2Df swapArrowRect;
        Rect2Df modeButtonRect;
        Rect2Df hexLabelRect;
        Rect2Df hexFieldRect;
        Rect2Df tabRect;
        std::array<Rect2Df, 3> tabRects;
        // Four channel rows: label, slider track, value box (index 3 = alpha)
        std::array<Rect2Df, 4> rowLabelRects;
        std::array<Rect2Df, 4> rowSliderRects;
        std::array<Rect2Df, 4> rowValueRects;
        int channelCount = 3;       // 3 colour channels (+ alpha handled separately)

        // ----- Layout -----
        void RecalculateLayout();

        // ----- Rendering helpers -----
        void RenderHueRing(IRenderContext* ctx);
        void RenderSVSquare(IRenderContext* ctx);
        void RenderSwatches(IRenderContext* ctx);
        void RenderModeButton(IRenderContext* ctx);
        void RenderHexField(IRenderContext* ctx);
        void RenderTabs(IRenderContext* ctx);
        void RenderChannelRow(IRenderContext* ctx, int row);
        void RenderAlphaRow(IRenderContext* ctx);
        void RenderDropdownPopup(IRenderContext* ctx);
        void DrawCheckerboard(IRenderContext* ctx, const Rect2Df& rect, float cell);

        // ----- Event helpers -----
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);

        void ApplyDrag(const Point2Df& p, bool finished);
        void UpdateHueFromPoint(const Point2Df& p);
        void UpdateSVFromPoint(const Point2Df& p);

        // ----- Channel model glue -----
        // Returns the labels, current values and ranges for the active model.
        void GetChannelInfo(int row, std::string& label, float& value,
                            float& minV, float& maxV) const;
        void SetChannelValue(int row, float value);          // from slider/edit
        std::string FormatChannel(int row) const;            // display text
        std::shared_ptr<IPaintPattern> MakeChannelGradient(IRenderContext* ctx, int row) const;

        // ----- Editing -----
        void BeginEdit(EditField field);
        void CommitEdit();
        void CancelEdit();
        std::string CurrentFieldText(EditField field) const;

        // ----- Notification -----
        void Changed(bool finished);
        void SetFont(IRenderContext* ctx, bool muted = false) const;
    };

// ===== FACTORY FUNCTIONS =====
    inline std::shared_ptr<UltraCanvasColorPicker> CreateColorPicker(
            const std::string& identifier, float x, float y, float w, float h) {
        return std::make_shared<UltraCanvasColorPicker>(identifier, x, y, w, h);
    }

    inline std::shared_ptr<UltraCanvasColorPicker> CreateColorPicker(
            const std::string& identifier, const Color& initial,
            float x = -1, float y = -1, float w = 290, float h = 470) {
        auto picker = std::make_shared<UltraCanvasColorPicker>(identifier, x, y, w, h);
        picker->SetColor(initial, false);
        picker->SetPreviousColor(initial);
        return picker;
    }

} // namespace UltraCanvas
