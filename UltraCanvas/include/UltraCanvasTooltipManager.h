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
        // Appearance
        Color backgroundColor = Color(255, 255, 225, 240);  // Light yellow with transparency
        Color borderColor = Color(118, 118, 118, 255);      // Gray border
        Color textColor = Colors::Black;
        Color shadowColor = Color(0, 0, 0, 64);

        // Typography
        std::string fontFamily = "Sans";
        float fontSize = 11.0f;

        // Layout
        int paddingLeft = 6;
        int paddingRight = 6;
        int paddingTop = 4;
        int paddingBottom = 4;
        int maxWidth = 450;
        int borderWidth = 1;
        float cornerRadius = 3.0f;

        // Shadow
        bool hasShadow = true;
        Point2Di shadowOffset = Point2Di(2, 2);

        // Behavior
        unsigned int showDelay = 300;        // milliseconds to wait before showing
        unsigned int hideDelay = 200;        // milliseconds to wait before hiding
        int offsetX = 10;              // Offset from cursor
        int offsetY = 10;
        bool followCursor = false;     // Whether tooltip follows mouse movement

        TooltipStyle() = default;

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
