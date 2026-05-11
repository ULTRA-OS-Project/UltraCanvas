// UltraCanvasSwitch.h
// Toggle switch: pill-shaped track with a circular thumb that snaps between sides.
// Supports horizontal/vertical orientation, optional thumb icons, and optional ON/OFF state labels.
// Version: 1.1.0
// Last Modified: 2026-05-07
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
        OutsideTrack   // Drawn next to the track, between the track and the main label
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
    };

// ===== TOGGLE SWITCH =====
    class UltraCanvasSwitch : public UltraCanvasLabeledToggleBase {
    private:
        SwitchVisualStyle visualStyle;

        Color GetCurrentTrackColor() const;
        Color GetCurrentThumbColor() const;

        // Computes the track sub-rect inside indicatorRect (which may include
        // outside-track label space along the long axis).
        Rect2Df GetTrackRect() const;

        // Conservative width estimate for an outside-track label (in long-axis pixels).
        float EstimateStateLabelExtent() const;

        void DrawThumbIcon(IRenderContext* ctx, const Point2Df& thumbCenter, float thumbRadius);
        void DrawStateLabelInsideTrack(IRenderContext* ctx, const Rect2Df& track);
        void DrawStateLabelOutsideTrack(IRenderContext* ctx, const Rect2Df& track);

    protected:
        void DrawIndicator(IRenderContext* ctx) override;
        Size2Df GetIndicatorSize() const override;
        void OnActivate() override { Toggle(); }
        const LabeledToggleVisualStyle& GetBaseVisualStyle() const override { return visualStyle.base; }
        void DrawFocusRingShape(IRenderContext* ctx) override;

    public:
        UltraCanvasSwitch(const std::string& identifier = "",
                          long x = 0, long y = 0, long w = 200, long h = 30,
                          const std::string& labelText = "");
        ~UltraCanvasSwitch() override = default;

        // Indeterminate is invalid for switches; clamp to Unchecked.
        void SetCheckState(CheckedState state) override;

        // ===== APPEARANCE =====
        void SetVisualStyle(const SwitchVisualStyle& s) { visualStyle = s; layoutDirty = true; }
        SwitchVisualStyle& GetVisualStyle() { return visualStyle; }
        const SwitchVisualStyle& GetVisualStyle() const { return visualStyle; }

        void SetTrackSize(float width, float height) {
            visualStyle.trackWidth = width;
            visualStyle.trackHeight = height;
            layoutDirty = true;
        }

        void SetOrientation(SwitchOrientation o) {
            visualStyle.orientation = o;
            layoutDirty = true;
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
        }

        // ===== FACTORY =====
        static std::shared_ptr<UltraCanvasSwitch> Create(
                const std::string& identifier,
                long x, long y,
                const std::string& text = "",
                bool checked = false);
    };

} // namespace UltraCanvas
