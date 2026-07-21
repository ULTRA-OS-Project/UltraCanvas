// include/UltraCanvasColorPicker.h
// Comprehensive colour picker widget with HSV colour wheel (ring or bar
// layout), SV square, foreground/background swatches, hex input, mode selector
// (HSV/HSL/RGB as either a tab bar or a dropdown) and editable channel sliders
// (thin or thick style, optional +/- spinners, optionally collapsible) with an
// alpha channel. Hovering a swatch floods the whole widget surface with that
// colour so it can be judged on a large area, and a screen colour-picker
// ("eyedropper") button switches the pointer to an eyedropper cursor and
// samples a pixel from the window into the foreground (left/Select mouse) or
// background (right/Adjust mouse) colour — the button used on the icon selects
// the target swatch, which live-previews the pixel under the pointer as the
// mouse moves.
// Version: 1.2.3
// Last Modified: 2026-07-21
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

// ===== MODE SELECTOR PRESENTATION =====
// The HSV/HSL/RGB model can be switched either with a clickable tab bar or
// with a dropdown button; the two presentations are mutually exclusive.
    enum class ColorPickerModeSelector {
        TabBar,     // row of HSV | HSL | RGB tabs (default)
        Dropdown    // compact dropdown button
    };

// ===== CHANNEL SLIDER PRESENTATION =====
    enum class ColorPickerSliderStyle {
        Thin,       // thin track with the handle overhanging it (default)
        Thick       // thick bar that fully encloses the control point
    };

// ===== HUE + SV AREA PRESENTATION =====
    enum class ColorPickerWheelStyle {
        Ring,       // circular hue ring with the SV square inscribed (default)
        Bar         // maximised SV rectangle with a hue bar underneath
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
        float ringThickness = 26.0f;     // hue ring thickness (Ring style)
        float hueBarHeight = 18.0f;      // hue bar height (Bar style)
        float cornerRadius = 4.0f;
        float rowHeight = 22.0f;
        float rowGap = 6.0f;
        float tabHeight = 24.0f;
        float swatchSize = 36.0f;

        // Uniform scale applied to every metric and font above (0.6 = 60%
        // sized picker). Lets the same widget be embedded at reduced size.
        float uiScale = 1.0f;

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

        ~UltraCanvasColorPicker() override;

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

        // ----- Foreground / background aliases -----
        // The "current" swatch is the foreground colour (the value being edited);
        // the "previous" swatch is the background colour. These aliases make the
        // swatch semantics explicit for callers that think in fore/background terms.
        Color GetForegroundColor() const { return GetColor(); }
        void  SetForegroundColor(const Color& c, bool notify = true) { SetColor(c, notify); }
        Color GetBackgroundColor() const { return previousColor; }
        void  SetBackgroundColor(const Color& c) { SetPreviousColor(c); }

        // ===== MODE / LAYOUT =====
        ColorPickerModel GetModel() const { return model; }
        void SetModel(ColorPickerModel m);

        // How the HSV/HSL/RGB model selector is presented: a clickable tab bar
        // OR a dropdown button — never both at the same time.
        ColorPickerModeSelector GetModeSelector() const { return modeSelector; }
        void SetModeSelector(ColorPickerModeSelector s);

        // Channel slider presentation: thin track with an overhanging handle,
        // or a thick bar that encloses the control point (Slider-demo style).
        ColorPickerSliderStyle GetSliderStyle() const { return sliderStyle; }
        void SetSliderStyle(ColorPickerSliderStyle s);

        // Hue/SV presentation: circular ring with the inscribed SV square, or a
        // maximised SV rectangle with the full hue range as a bar underneath.
        ColorPickerWheelStyle GetWheelStyle() const { return wheelStyle; }
        void SetWheelStyle(ColorPickerWheelStyle s);

        // Show < and > step arrows inside each numeric value field.
        bool GetShowValueSpinners() const { return showValueSpinners; }
        void SetShowValueSpinners(bool show);

        // When collapsible, the channel sliders hide behind a disclosure row
        // (dropdown icon); `expanded` selects the initial state.
        bool GetSlidersCollapsible() const { return slidersCollapsible; }
        void SetSlidersCollapsible(bool collapsible, bool expanded = true);
        bool GetSlidersExpanded() const { return slidersExpanded; }
        void SetSlidersExpanded(bool expanded);

        // Compact mode hides the colour wheel and preview swatches, leaving only
        // the channel sliders (useful as an inline editor inside a toolbar/panel).
        void SetShowColorWheel(bool show);
        bool GetShowColorWheel() const { return showColorWheel; }

        void SetShowAlpha(bool show);
        bool GetShowAlpha() const { return showAlpha; }

        const ColorPickerStyle& GetStyle() const { return style; }
        void SetStyle(const ColorPickerStyle& s) { style = s; layoutValid = false; RequestRedraw(); }

        // Convenience: scale the whole picker UI (metrics + fonts). 0.6 gives a
        // 60% sized picker; pair it with a proportionally smaller widget size.
        float GetUIScale() const { return style.uiScale; }
        void SetUIScale(float scale);

        // ===== CALLBACKS =====
        std::function<void(const Color&)> onColorChanged;    // final value (drag end / commit)
        std::function<void(const Color&)> onColorChanging;   // continuous, during interaction

        // Screen colour-picker ("eyedropper") request. Fired when the eyedropper
        // button next to the background swatch is clicked. When set, the host
        // takes over: it performs platform screen sampling and writes the pixel
        // back via SetForegroundColor / SetBackgroundColor. When NOT set, the
        // picker runs its built-in mode: the pointer becomes an eyedropper
        // cursor and the button used here selects the target swatch — left
        // (Select) button targets the foreground colour, right (Adjust) button
        // the background colour. That target swatch live-previews the pixel
        // under the pointer as the mouse moves, and the next click commits the
        // sampled pixel into it; Escape cancels.
        std::function<void(bool foreground)> onScreenColorPick;

        // Built-in eyedropper mode control (also usable programmatically).
        // `foreground` selects which swatch the pick targets — the tile that
        // live-previews while moving and receives the sampled colour on click.
        // It mirrors the mouse button used on the eyedropper icon: left/Select
        // -> foreground, right/Adjust -> background.
        bool IsScreenPickActive() const { return screenPickActive; }
        void StartScreenPick(bool foreground = true);
        void CancelScreenPick() { EndScreenPick(); }

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
        ColorPickerModeSelector modeSelector = ColorPickerModeSelector::TabBar;
        ColorPickerSliderStyle sliderStyle = ColorPickerSliderStyle::Thin;
        ColorPickerWheelStyle wheelStyle = ColorPickerWheelStyle::Ring;
        ColorPickerStyle style;
        bool showColorWheel = true;
        bool showAlpha = true;
        bool showValueSpinners = false;
        bool slidersCollapsible = false;
        bool slidersExpanded = true;

        // ----- Interaction state -----
        enum class DragTarget { NoneTarget, HueRing, HueBar, SVSquare,
                                Channel0, Channel1, Channel2, Alpha, TextDrag };
        DragTarget dragTarget = DragTarget::NoneTarget;

        enum class EditField { NoEdit, Hex, Channel0, Channel1, Channel2, Alpha };
        EditField editField = EditField::NoEdit;
        std::string editBuffer;
        // Caret index and selection anchor into editBuffer; the selection is
        // [min(caret,anchor), max(caret,anchor)) — empty when they are equal.
        size_t editCaret = 0;
        size_t editAnchor = 0;

        bool dropdownOpen = false;

        // Built-in eyedropper: true while waiting for the sampling click. The
        // window event filter is installed once (lazily) and stays installed,
        // guarded by screenPickActive — removing it from inside its own
        // invocation would destroy the running lambda.
        bool screenPickActive = false;
        bool screenPickFilterInstalled = false;
        UCMouseCursor screenPickCursor = UCMouseCursor::Cross;

        // Live preview while the eyedropper is armed: as the pointer moves, the
        // pixel under it is sampled and shown in the target swatch tile so the
        // user sees the colour before committing it with a click. The value is
        // only a preview (no HSV/callback commit) and is discarded on cancel.
        // screenPickForeground records which swatch is the target (chosen by the
        // mouse button used on the eyedropper icon): true = foreground swatch,
        // false = background swatch.
        bool screenPickForeground = true;
        bool screenPickPreviewValid = false;
        Color screenPickPreview;

        // Which swatch the pointer is hovering; drives the full-surface colour
        // preview (the whole widget background is flooded with that colour).
        enum class HoverSwatch { NoneSwatch, Foreground, Background };
        HoverSwatch hoverSwatch = HoverSwatch::NoneSwatch;

        // ----- Cached layout (recomputed on resize) -----
        bool layoutValid = false;
        float cachedW = -1, cachedH = -1;
        Rect2Df wheelRect;          // bounding box of the hue ring (Ring style)
        Point2Df wheelCenter;
        float ringOuter = 0, ringInner = 0;
        Rect2Df svRect;             // saturation/value square / rectangle
        Rect2Df hueBarRect;         // hue bar (Bar style)
        Rect2Df currentSwatchRect;
        Rect2Df previousSwatchRect;
        Rect2Df swapArrowRect;
        Rect2Df screenPickRect;     // eyedropper (screen colour picker) button
        Rect2Df modeButtonRect;     // dropdown selector (Dropdown mode only)
        Rect2Df hexLabelRect;
        Rect2Df hexFieldRect;
        Rect2Df tabRect;            // tab bar (TabBar mode only)
        std::array<Rect2Df, 3> tabRects;
        Rect2Df slidersHeaderRect;  // disclosure row (collapsible mode only)
        // Four channel rows: label, slider track, value box (index 3 = alpha)
        std::array<Rect2Df, 4> rowLabelRects;
        std::array<Rect2Df, 4> rowSliderRects;
        std::array<Rect2Df, 4> rowValueRects;
        int channelCount = 3;       // 3 colour channels (+ alpha handled separately)

        // ----- Layout -----
        void RecalculateLayout();
        float Scaled(float v) const { return v * std::max(0.1f, style.uiScale); }
        bool SlidersVisible() const { return !slidersCollapsible || slidersExpanded; }

        // ----- Rendering helpers -----
        void RenderHueRing(IRenderContext* ctx);
        void RenderHueBar(IRenderContext* ctx);
        void RenderSVSquare(IRenderContext* ctx);
        void RenderSwatches(IRenderContext* ctx);
        void RenderModeButton(IRenderContext* ctx);
        void RenderScreenPickButton(IRenderContext* ctx);
        void RenderHexField(IRenderContext* ctx);
        void RenderTabs(IRenderContext* ctx);
        void RenderSlidersHeader(IRenderContext* ctx);
        void RenderChannelRow(IRenderContext* ctx, int row);
        void RenderAlphaRow(IRenderContext* ctx);
        void RenderDropdownPopup(IRenderContext* ctx);
        void RenderSliderTrackAndHandle(IRenderContext* ctx, int row, float t,
                                        const std::shared_ptr<IPaintPattern>& gradient,
                                        bool checkerUnder);
        void RenderValueBox(IRenderContext* ctx, int row, bool editing,
                            const std::string& text);
        void DrawCheckerboard(IRenderContext* ctx, const Rect2Df& rect, float cell);

        // ----- Event helpers -----
        bool HandleMouseDown(const UCEvent& event);
        bool HandleMouseMove(const UCEvent& event);
        bool HandleMouseUp(const UCEvent& event);
        bool HandleKeyDown(const UCEvent& event);

        void ApplyDrag(const Point2Df& p, bool finished);
        void UpdateHueFromPoint(const Point2Df& p);

        // ----- Hover tooltips -----
        // Explain the model choices (HSV/HSL/RGB) and the channel / hex labels
        // (H, S, V, A, L, R, G, B, Hex) when the pointer rests over them.
        void UpdateHoverTooltip(const UCEvent& event);
        std::string ModelTooltip(ColorPickerModel m) const;
        std::string ChannelTooltip(const std::string& label) const;
        void UpdateHueFromBar(const Point2Df& p);
        void UpdateSVFromPoint(const Point2Df& p);
        void UpdateSwatchHover(const Point2Df& p);           // full-surface preview

        // Value-field spinner zones (< and > arrows). Row 0..3 (3 = alpha).
        Rect2Df SpinnerDownRect(int row) const;
        Rect2Df SpinnerUpRect(int row) const;
        void StepChannel(int row, float direction);          // direction = +1/-1

        // ----- Built-in eyedropper -----
        std::string ScreenPickFilterId() const;
        void EndScreenPick(bool restoreCursor = true);
        void HandleScreenPickClick(const UCEvent& event);
        // Sample the pixel under the pointer into the live preview swatch.
        void UpdateScreenPickPreview(const UCEvent& event);

        // ----- Channel model glue -----
        // Returns the labels, current values and ranges for the active model.
        void GetChannelInfo(int row, std::string& label, float& value,
                            float& minV, float& maxV) const;
        void SetChannelValue(int row, float value);          // from slider/edit
        std::string FormatChannel(int row) const;            // display text
        std::shared_ptr<IPaintPattern> MakeChannelGradient(IRenderContext* ctx, int row) const;

        // ----- Editing -----
        // clickAt places the caret at that point; without it the whole text is
        // selected (keyboard/Tab entry).
        void BeginEdit(EditField field, const Point2Df* clickAt = nullptr);
        void CommitEdit();
        void CancelEdit();
        std::string CurrentFieldText(EditField field) const;

        // ----- Inline text editor (caret / selection / clipboard) -----
        bool HasEditSelection() const { return editCaret != editAnchor; }
        size_t EditSelMin() const { return std::min(editCaret, editAnchor); }
        size_t EditSelMax() const { return std::max(editCaret, editAnchor); }
        void EditSelectAll();
        void EditDeleteSelection();
        void EditInsert(const std::string& s);
        void EditCopy();
        void EditCut();
        void EditPaste();
        bool EditAcceptsChar(char c) const;          // per-field character filter
        void EditMoveToNextField(bool backwards);    // Tab / Shift+Tab
        Rect2Df EditFieldRect(EditField field) const;
        Rect2Df EditTextRect(EditField field) const; // inner text area
        bool EditTextCentered(EditField field) const;
        float EditTextWidth(IRenderContext* ctx, const std::string& s) const;
        float EditTextStartX(IRenderContext* ctx) const;
        size_t CaretIndexFromPoint(const Point2Df& p);
        void RenderEditableText(IRenderContext* ctx); // selection + text + caret

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
