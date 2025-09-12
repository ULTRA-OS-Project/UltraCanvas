// UltraCanvasTooltipManager.h
// Updated tooltip system compatible with unified UltraCanvas architecture
// Version: 2.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasBaseWindow.h"
#include <string>
#include <chrono>
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
        std::string fontFamily = "Arial";
        float fontSize = 11.0f;
        FontWeight fontWeight = FontWeight::Normal;

        // Layout
        int paddingLeft = 6;
        int paddingRight = 6;
        int paddingTop = 4;
        int paddingBottom = 4;
        int maxWidth = 300;
        int borderWidth = 1;
        float cornerRadius = 3.0f;

        // Shadow
        bool hasShadow = true;
        Point2Di shadowOffset = Point2Di(2, 2);
        float shadowBlur = 3.0f;

        // Behavior
        float showDelay = 0.8f;        // Seconds to wait before showing
        float hideDelay = 0.5f;        // Seconds to wait before hiding
        int offsetX = 10;              // Offset from cursor
        int offsetY = 10;
        bool followCursor = false;     // Whether tooltip follows mouse movement

        TooltipStyle() = default;
    };

// ===== TOOLTIP MANAGER CLASS =====
    class UltraCanvasTooltipManager {
    private:
        // State tracking
        static UltraCanvasBaseWindow* targetWindow;
        static std::string currentText;
        static Point2Di tooltipPosition;
//        static Point2Di cursorPosition;
        static bool visible;
        static bool pendingShow;
        static bool pendingHide;

        // Timing
        static std::chrono::steady_clock::time_point hoverStartTime;
        static std::chrono::steady_clock::time_point hideStartTime;
        static float showDelay;
        static float hideDelay;

        // Style and layout
        static TooltipStyle style;
        static Point2Di tooltipSize;
        static std::vector<std::string> wrappedLines;

        // Global state
        static bool enabled;

    public:
        // ===== CORE FUNCTIONALITY =====

        // Update tooltip state - call this every frame
        static void Update();

        // Show tooltip for an element
        static void UpdateAndShowTooltip(UltraCanvasWindow* win, const std::string &text, const Point2Di &position, const TooltipStyle& newStyle);

        static void UpdateAndShowTooltip(UltraCanvasWindow* win, const std::string& text, const Point2Di& position) {
            TooltipStyle style;
            UpdateAndShowTooltip(win, text, position, style);
        }

        // Hide current tooltip
        static void HideTooltip();
        static void HideTooltipImmediately();

        // Force immediate show/hide
        static void UpdateAndShowTooltipImmediately(UltraCanvasWindow* win, const std::string &text, const Point2Di &position, const TooltipStyle& newStyle);
        static void UpdateAndShowTooltipImmediately(UltraCanvasWindow* win, const std::string &text, const Point2Di &position) {
            TooltipStyle style;
            UpdateAndShowTooltipImmediately(win, text, position, style);
        }


        // ===== RENDERING =====

        // Render tooltip - call this during window rendering
        static void Render(const UltraCanvasBaseWindow* win);

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

//        static UltraCanvasElement* GetTooltipSource() {
//            return tooltipSource;
//        }
        void SetStyle(const TooltipStyle &newStyle);

        static const std::string& GetCurrentText() {
            return currentText;
        }

        static Point2Di GetTooltipPosition() {
            return tooltipPosition;
        }

        static Point2Di GetTooltipSize() {
            return tooltipSize;
        }

        static void UpdateTooltipPosition(const Point2Di& position);

    private:
        static Rect2Di screenBounds;

        // ===== INTERNAL HELPER METHODS =====

        static void CalculateTooltipLayout();

        static std::vector<std::string> WrapText(const std::string& text, float maxWidth);
        static std::vector<std::string> SplitWords(const std::string& text);

        static void DrawShadow(IRenderContext* ctx);
        static void DrawBackground(IRenderContext* ctx);
        static void DrawBorder(IRenderContext* ctx);
        static void DrawText(IRenderContext* ctx);
    };
} // namespace UltraCanvas
