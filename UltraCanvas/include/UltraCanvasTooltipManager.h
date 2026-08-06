// include/UltraCanvasTooltipManager.h
// Updated tooltip system compatible with unified UltraCanvas architecture
// Version: 2.1.1
// Last Modified: 2026-04-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTimer.h"
#include <string>
#include <functional>
#include <memory>

namespace UltraCanvas {

// ===== TOOLTIP CONFIGURATION =====
    struct TooltipStyle {
        // Appearance (defaults follow the modern dark tooltip look of
        // current desktop environments; see Light() for a light variant)
        Color backgroundColor = Color(45, 45, 48, 245);     // Dark neutral, slightly translucent
        Color borderColor = Color(255, 255, 255, 36);       // Subtle light hairline
        Color textColor = Color(242, 242, 242, 255);
        Color shadowColor = Color(0, 0, 0, 90);

        // Typography
        std::string fontFamily = "Sans";
        float fontSize = 11.0f;

        // Layout
        int paddingLeft = 10;
        int paddingRight = 10;
        int paddingTop = 7;
        int paddingBottom = 7;
        int maxWidth = 450;
        int borderWidth = 1;
        float cornerRadius = 6.0f;

        // Shadow: soft drop shadow below the tooltip. shadowBlur is the
        // spread in pixels; 0 gives the legacy hard-edged shadow.
        bool hasShadow = true;
        Point2Di shadowOffset = Point2Di(0, 3);
        int shadowBlur = 10;

        // Behavior
        unsigned int showDelay = 300;        // milliseconds to wait before showing
        unsigned int hideDelay = 200;        // milliseconds to wait before hiding
        int offsetX = 10;              // Offset from cursor
        int offsetY = 10;
        bool followCursor = false;     // Whether tooltip follows mouse movement

        TooltipStyle() = default;

        // Preset: the default dark theme, spelled out for readability
        static TooltipStyle Dark() {
            return TooltipStyle();
        }

        // Preset: light theme for apps where a dark tooltip feels too heavy
        static TooltipStyle Light() {
            TooltipStyle s;
            s.backgroundColor = Color(255, 255, 255, 250);
            s.borderColor = Color(0, 0, 0, 28);
            s.textColor = Color(36, 41, 47, 255);
            s.shadowColor = Color(0, 0, 0, 60);
            return s;
        }

        bool operator==(const TooltipStyle& other) const {
            return backgroundColor == other.backgroundColor
                && borderColor == other.borderColor
                && textColor == other.textColor
                && shadowColor == other.shadowColor
                && fontFamily == other.fontFamily
                && fontSize == other.fontSize
                && paddingLeft == other.paddingLeft
                && paddingRight == other.paddingRight
                && paddingTop == other.paddingTop
                && paddingBottom == other.paddingBottom
                && maxWidth == other.maxWidth
                && borderWidth == other.borderWidth
                && cornerRadius == other.cornerRadius
                && hasShadow == other.hasShadow
                && shadowOffset == other.shadowOffset
                && shadowBlur == other.shadowBlur
                && showDelay == other.showDelay
                && hideDelay == other.hideDelay
                && offsetX == other.offsetX
                && offsetY == other.offsetY
                && followCursor == other.followCursor;
        }

        bool operator!=(const TooltipStyle& other) const {
            return !(*this == other);
        }
    };

// ===== TOOLTIP MANAGER CLASS =====
    class UltraCanvasTooltipManager {
    private:
        // State tracking
        static UltraCanvasWindowBase* targetWindow;
        static std::unique_ptr<IRenderContext> renderCtx;
        static std::string currentText;
        static Rect2Di tooltipRect;
//        static Point2Di cursorPosition;
        static bool visible;
        static bool pendingShow;
        static bool pendingHide;
        static bool needsRedraw;

        // Timing (via Application timer API)
        static TimerId showTimerId;
        static TimerId hideTimerId;

        // Style and layout
        static TooltipStyle style;
        static std::unique_ptr<ITextLayout> textLayout;

        // Global state
        static bool enabled;

    public:
        // ===== CORE FUNCTIONALITY =====

        // Show tooltip for an element
        static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string &text, const Point2Di &position, const TooltipStyle& newStyle);

        static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string& text, const Point2Di& position) {
            TooltipStyle style;
            UpdateAndShowTooltip(win, text, position, style);
        }

        // Hide current tooltip
        static void HideTooltip();
        static void HideTooltipImmediately();

        // Force immediate show/hide
        static void UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const std::string &text, const Point2Di &position, const TooltipStyle& newStyle);
        static void UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const std::string &text, const Point2Di &position) {
            TooltipStyle style;
            UpdateAndShowTooltipImmediately(win, text, position, style);
        }


        // ===== RENDERING =====

        // Render tooltip - call this during window rendering
        static IRenderContext* Render(UltraCanvasWindowBase* win);

        // ===== CONFIGURATION =====

        static void SetEnabled(bool enable) {
            enabled = enable;
            if (!enabled) {
                HideTooltipImmediately();
            }
        }

        static bool IsEnabled() {
            return enabled;
        }

        static bool IsVisible() {
            return visible;
        }

        static bool IsPending() {
            return pendingShow;
        }

//        static UltraCanvasUIElement* GetTooltipSource() {
//            return tooltipSource;
//        }
        void SetStyle(const TooltipStyle &newStyle);

        static const std::string& GetCurrentText() {
            return currentText;
        }

        static Point2Di GetTooltipPosition() {
            return tooltipRect.TopLeft();
        }

        // Top-left corner of the rendered surface (tooltip position minus the
        // soft-shadow margins). Use this when compositing the Render() result.
        static Point2Di GetCompositePosition();

        static Size2Di GetTooltipSize() {
            return tooltipRect.Size();
        }

        static void UpdateTooltipPosition(const Point2Di& position);

    private:
        static Rect2Di screenBounds;

        // ===== INTERNAL HELPER METHODS =====

        static void CancelShowTimer();
        static void CancelHideTimer();

        static void CalculateTooltipLayout();
    };
} // namespace UltraCanvas
