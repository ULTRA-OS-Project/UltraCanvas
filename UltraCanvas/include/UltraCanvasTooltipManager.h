// UltraCanvasTooltipManager.h
// Updated tooltip system compatible with unified UltraCanvas architecture
// Version: 2.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
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
        Point2D shadowOffset = Point2D(2, 2);
        float shadowBlur = 3.0f;

        // Behavior
        float showDelay = 0.8f;        // Seconds to wait before showing
        float hideDelay = 0.1f;        // Seconds to wait before hiding
        int offsetX = 10;              // Offset from cursor
        int offsetY = 10;
        bool followCursor = false;     // Whether tooltip follows mouse movement

        TooltipStyle() = default;
    };

// ===== TOOLTIP MANAGER CLASS =====
    class UltraCanvasTooltipManager {
    private:
        // State tracking
        static UltraCanvasElement* hoveredElement;
        static UltraCanvasElement* tooltipSource;
        static std::string currentText;
        static Point2D tooltipPosition;
        static Point2D cursorPosition;
        static bool visible;
        static bool pendingShow;

        // Timing
        static std::chrono::steady_clock::time_point hoverStartTime;
        static std::chrono::steady_clock::time_point hideStartTime;
        static float showDelay;
        static float hideDelay;

        // Style and layout
        static TooltipStyle style;
        static Point2D tooltipSize;
        static std::vector<std::string> wrappedLines;

        // Global state
        static bool enabled;
        static bool debugMode;

    public:
        // ===== CORE FUNCTIONALITY =====

        // Update tooltip state - call this every frame
        static void Update(float deltaTime);

        // Show tooltip for an element
        static void ShowTooltip(UltraCanvasElement* element, const std::string& text,
                                const Point2D& position = Point2D(-1, -1));

        // Hide current tooltip
        static void HideTooltip();

        // Force immediate show/hide
        static void ShowImmediately(UltraCanvasElement* element, const std::string& text,
                                    const Point2D& position = Point2D(-1, -1));

        static void HideImmediately();

        // ===== RENDERING =====

        // Render tooltip - call this during window rendering
        static void Render();

        // ===== CONFIGURATION =====

        static void SetStyle(const TooltipStyle& newStyle);

        static const TooltipStyle& GetStyle() {
            return style;
        }

        static void SetEnabled(bool enable) {
            enabled = enable;
            if (!enabled) {
                HideImmediately();
            }
        }

        static bool IsEnabled() {
            return enabled;
        }

        static void SetDebugMode(bool debug) {
            debugMode = debug;
        }

        // ===== STATE QUERIES =====

        static bool IsVisible() {
            return visible;
        }

        static bool IsPending() {
            return pendingShow;
        }

        static UltraCanvasElement* GetTooltipSource() {
            return tooltipSource;
        }

        static const std::string& GetCurrentText() {
            return currentText;
        }

        static Point2D GetTooltipPosition() {
            return tooltipPosition;
        }

        static Point2D GetTooltipSize() {
            return tooltipSize;
        }

        // ===== MOUSE TRACKING =====

        // Call this from mouse move events to update cursor position
        static void UpdateCursorPosition(const Point2D& position);

        // Handle element hover events
        static void OnElementHover(UltraCanvasElement* element, const std::string& tooltipText,
                                   const Point2D& mousePosition);

        static void OnElementLeave(UltraCanvasElement* element);

        // ===== UTILITY FUNCTIONS =====

        // Get screen bounds for tooltip positioning
        static void SetScreenBounds(const Rect2D& bounds) {
            screenBounds = bounds;
        }

        static Rect2D GetScreenBounds() {
            return screenBounds;
        }

    private:
        static Rect2D screenBounds;

        // ===== INTERNAL HELPER METHODS =====

        static void CalculateTooltipLayout();

        static void UpdateTooltipPosition();

        static std::vector<std::string> WrapText(const std::string& text, float maxWidth);

        static std::vector<std::string> SplitWords(const std::string& text);

        static void DrawShadow();

        static void DrawBackground();

        static void DrawBorder();

        static void DrawText();
    };
} // namespace UltraCanvas
