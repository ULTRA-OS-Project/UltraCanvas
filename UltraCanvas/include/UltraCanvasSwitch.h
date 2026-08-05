// UltraCanvasSwitch.h
// Toggle switch: pill-shaped track with a circular thumb that snaps between sides.
// Supports horizontal/vertical orientation, optional thumb icons, and optional ON/OFF state labels
// (inside the track, outside it, or one label on each side of the track).
// Version: 1.3.0
// Last Modified: 2026-08-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasLabeledToggleBase.h"
#include "UltraCanvasImage.h"
#include <memory>
#include <string>

namespace UltraCanvas {

// ===== SWITCH ENUMS =====
    enum class SwitchOrientation {
        Horizontal,
        Vertical
    };

    // NOTE: avoid the value name `None` here — X11 defines it as a macro (0L).
    enum class SwitchStateLabelPosition {
        Hidden,        // No ON/OFF text
        InsideTrack,   // Drawn inside the track on the side opposite the thumb
        OutsideTrack,  // Drawn next to the track, between the track and the main label
        BothSides      // Both texts are always visible, one on each side of the track:
                       //   horizontal -> offText left, onText right
                       //   vertical   -> onText above, offText below (ON = top)
                       // The text matching the current state is highlighted.
    };

    enum class SwitchThumbIconStyle {
        Plain,         // Plain circle thumb (default — matches original behavior)
        Check,         // Built-in check stroke when ON, plain circle when OFF
        CustomImage    // Use thumbIconOn / thumbIconOff (UCImage); falls back to plain circle if null
    };

// ===== SWITCH VISUAL STYLE =====
    struct SwitchVisualStyle {
        LabeledToggleVisualStyle base;

        // Track
        Color trackOffColor = Colors::LightGray;
        Color trackOnColor = Color(76, 175, 80, 255);   // Material green-ish
        Color trackBorderColor = Colors::ButtonShadow;
        Color trackDisabledColor = Color(220, 220, 220, 255);

        // Thumb
        Color thumbColor = Color(255, 255, 255, 255);
        Color thumbBorderColor = Colors::ButtonShadow;
        Color thumbDisabledColor = Color(240, 240, 240, 255);

        // Layout
        SwitchOrientation orientation = SwitchOrientation::Horizontal;
        float trackWidth = 36.0f;            // long axis (height when vertical)
        float trackHeight = 18.0f;           // short axis (width when vertical)
        float trackCornerRadius = -1.0f;     // <0 means pill (radius = short-axis / 2)
        float thumbInset = 2.0f;             // Gap between thumb edge and track inside edge
        float borderWidth = 1.0f;

        // Thumb icon
        SwitchThumbIconStyle thumbIconStyle = SwitchThumbIconStyle::Plain;
        UCImagePtr thumbIconOn;
        UCImagePtr thumbIconOff;
        bool thumbIconUseAsMask = false;     // Tint icon with thumbIcon*Color when true
        Color thumbIconOnColor  = Color(76, 175, 80, 255);
        Color thumbIconOffColor = Color(120, 120, 120, 255);
        float thumbIconStrokeWidth = 2.0f;   // For Check style
        float thumbIconInset = 3.0f;         // Padding inside the thumb circle

        // State labels (ON / OFF)
        SwitchStateLabelPosition stateLabelPosition = SwitchStateLabelPosition::Hidden;
        std::string onText  = "ON";
        std::string offText = "OFF";
        Color onTextColor  = Colors::White;
        Color offTextColor = Color(120, 120, 120, 255);
        std::string stateLabelFontFamily = "Arial";
        float stateLabelFontSize = 10.0f;
        FontWeight stateLabelFontWeight = FontWeight::Bold;
        float stateLabelTrackPadding = 4.0f;     // Gap between thumb and inside-track label
        float stateLabelOutsidePadding = 6.0f;   // Gap between track and outside-track label

        // Side labels (SwitchStateLabelPosition::BothSides only).
        // Both texts stay visible; the one matching the current state uses the
        // active color/weight, the other the inactive pair.
        std::string sideLabelFontFamily = "Arial";
        float sideLabelFontSize = 12.0f;
        FontWeight sideLabelActiveWeight = FontWeight::Normal;
        FontWeight sideLabelInactiveWeight = FontWeight::Normal;
        Color sideLabelActiveColor = Colors::TextDefault;
        Color sideLabelInactiveColor = Color(150, 150, 150, 255);
        Color sideLabelDisabledColor = Colors::TextDisabled;
        float sideLabelPadding = 8.0f;           // Gap between track and each side label
        bool sideLabelClickSelects = true;       // Click a side label to select that state
                                                 // (click on the track always toggles)
    };

// ===== TOGGLE SWITCH =====
    class UltraCanvasSwitch : public UltraCanvasLabeledToggleBase {
    private:
        SwitchVisualStyle visualStyle;

        // Last mouse position (element-local), used by BothSides to turn a click
        // on a side label into a state selection instead of a plain toggle.
        Point2Di lastPointer{0, 0};
        bool lastPointerValid = false;

        // Measured extents of the two side labels (BothSides mode).
        struct SideLabelMetrics {
            float offWidth = 0.0f;
            float onWidth = 0.0f;
            float height = 0.0f;
        };

        Color GetCurrentTrackColor() const;
        Color GetCurrentThumbColor() const;

        // Computes the track sub-rect inside indicatorRect (which may include
        // outside-track or side label space).
        Rect2Df GetTrackRect() const;

        // Conservative width estimate for an outside-track label (in long-axis pixels).
        float EstimateStateLabelExtent() const;

        // Measures both side labels at both weights, so the track keeps its
        // position when the active/inactive weights differ.
        SideLabelMetrics MeasureSideLabels() const;

        void DrawThumbIcon(IRenderContext* ctx, const Point2Df& thumbCenter, float thumbRadius);
        void DrawStateLabelInsideTrack(IRenderContext* ctx, const Rect2Df& track);
        void DrawStateLabelOutsideTrack(IRenderContext* ctx, const Rect2Df& track);
        void DrawSideLabels(IRenderContext* ctx, const Rect2Df& track);

    protected:
        void DrawIndicator(IRenderContext* ctx) override;
        Size2Df GetIndicatorSize() const override;
        void OnActivate() override;
        const LabeledToggleVisualStyle& GetBaseVisualStyle() const override { return visualStyle.base; }
        void DrawFocusRingShape(IRenderContext* ctx) override;

    public:
        // ===== CONSTRUCTORS =====
        UltraCanvasSwitch(const std::string& identifier,
                          float x, float y, float w, float h,
                          const std::string& labelText = "");

        UltraCanvasSwitch(const std::string& identifier,
                          float w, float h,
                          const std::string& labelText = "")
            : UltraCanvasSwitch(identifier, -1, -1, w, h, labelText) {}

        UltraCanvasSwitch(const std::string& identifier, const std::string& labelText)
            : UltraCanvasSwitch(identifier, -1, -1, -1, -1, labelText) {}

        explicit UltraCanvasSwitch(const std::string& labelText = "")
            : UltraCanvasSwitch("", -1, -1, -1, -1, labelText) {}

        ~UltraCanvasSwitch() override = default;

        // Indeterminate is invalid for switches; clamp to Unchecked.
        void SetCheckState(CheckedState state) override;

        // Tracks the pointer position before the base class turns it into an activation.
        bool OnEvent(const UCEvent& event) override;

        // ===== APPEARANCE =====
        void SetVisualStyle(const SwitchVisualStyle& s) { visualStyle = s; layoutDirty = true; InvalidateLayout(); RequestRedraw(); }
        SwitchVisualStyle& GetVisualStyle() { return visualStyle; }
        const SwitchVisualStyle& GetVisualStyle() const { return visualStyle; }

        void SetTrackSize(float width, float height) {
            visualStyle.trackWidth = width;
            visualStyle.trackHeight = height;
            layoutDirty = true;
            InvalidateLayout();
            RequestRedraw();
        }

        void SetOrientation(SwitchOrientation o) {
            visualStyle.orientation = o;
            layoutDirty = true;
            InvalidateLayout();
            RequestRedraw();
        }

        void SetThumbIcons(UCImagePtr onIcon, UCImagePtr offIcon, bool useAsMask = false) {
            visualStyle.thumbIconStyle = SwitchThumbIconStyle::CustomImage;
            visualStyle.thumbIconOn = std::move(onIcon);
            visualStyle.thumbIconOff = std::move(offIcon);
            visualStyle.thumbIconUseAsMask = useAsMask;
        }

        void SetStateLabels(const std::string& onText, const std::string& offText,
                            SwitchStateLabelPosition position) {
            visualStyle.onText = onText;
            visualStyle.offText = offText;
            visualStyle.stateLabelPosition = position;
            layoutDirty = true;
            InvalidateLayout();
            RequestRedraw();
        }

        // Two always-visible texts with the track between them: offLabel goes to the
        // OFF side of the track (left when horizontal, bottom when vertical), onLabel
        // to the ON side. Whichever matches the current state is highlighted.
        void SetSideLabels(const std::string& offLabel, const std::string& onLabel) {
            visualStyle.offText = offLabel;
            visualStyle.onText = onLabel;
            visualStyle.stateLabelPosition = SwitchStateLabelPosition::BothSides;
            layoutDirty = true;
            InvalidateLayout();
            RequestRedraw();
        }

        // ===== FACTORY =====
        static std::shared_ptr<UltraCanvasSwitch> Create(
                const std::string& identifier,
                float x, float y,
                const std::string& text = "",
                bool checked = false);

        // Switch flanked by two texts (offLabel — track — onLabel), no main label.
        static std::shared_ptr<UltraCanvasSwitch> CreateWithSideLabels(
                const std::string& identifier,
                float x, float y,
                const std::string& offLabel,
                const std::string& onLabel,
                bool checked = false);
    };

} // namespace UltraCanvas
