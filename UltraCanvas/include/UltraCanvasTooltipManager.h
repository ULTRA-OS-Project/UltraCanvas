// include/UltraCanvasTooltipManager.h
// Updated tooltip system compatible with unified UltraCanvas architecture
// Version: 2.1.1
// Last Modified: 2026-04-27
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasTooltipTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasTimer.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>

namespace UltraCanvas {
    // TooltipStyle and TooltipContent live in UltraCanvasTooltipTypes.h

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

        // Global state
        static bool enabled;

    public:
        // ===== CORE FUNCTIONALITY =====

        // Show a plain-text tooltip (the text may contain Pango markup for
        // inline styling, e.g. "<b>bold</b>")
        static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string &text, const Point2Di &position, const TooltipStyle& newStyle);

        static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string& text, const Point2Di& position) {
            TooltipStyle style;
            UpdateAndShowTooltip(win, text, position, style);
        }

        // Show a structured tooltip (title, label/value rows, bullets, …).
        // The explicit-style overload wins over content.styleOverride.
        static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const TooltipContent& content, const Point2Di& position, const TooltipStyle& newStyle);

        static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const TooltipContent& content, const Point2Di& position) {
            UpdateAndShowTooltip(win, content, position,
                                 content.styleOverride ? *content.styleOverride : TooltipStyle());
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
        static void UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const TooltipContent& content, const Point2Di& position, const TooltipStyle& newStyle);
        static void UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const TooltipContent& content, const Point2Di& position) {
            UpdateAndShowTooltipImmediately(win, content, position,
                                            content.styleOverride ? *content.styleOverride : TooltipStyle());
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
