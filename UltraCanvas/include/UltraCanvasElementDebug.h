// include/UltraCanvasElementDebug.h
// Debug rendering functions for UltraCanvas elements
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasContainer.h"
#include <string>
#include <sstream>
#include <iomanip>

namespace UltraCanvas {

// ===== DEBUG RENDERING CONFIGURATION =====
    struct DebugRenderSettings {
        bool showBorders = true;
        bool showCoordinates = true;
        bool showElementID = true;
        bool showTransformation = true;
        bool showBounds = true;
        bool showZIndex = true;
        bool showVisibilityState = true;
        bool showActiveState = true;

        // Visual styling
        Color borderColor = Color(255, 0, 0, 180);        // Semi-transparent red
        Color textColor = Color(255, 255, 255, 255);      // White text
        Color textBackgroundColor = Color(0, 0, 0, 200);  // Semi-transparent black
        float borderWidth = 2.0f;
        float textSize = 12.0f;
        std::string fontFamily = "Arial";

        // Layout settings
        float textPadding = 4.0f;
        float cornerRadius = 3.0f;
        bool multilineText = true;

        // Advanced debug options
        bool showAbsolutePosition = true;
        bool showRelativePosition = true;
        bool showParentInfo = false;
        bool showChildCount = false;
    };

// ===== GLOBAL DEBUG SETTINGS =====
    class UltraCanvasDebugRenderer {
    private:
        static DebugRenderSettings globalSettings;
        static bool debugEnabled;

    public:
        static void SetDebugEnabled(bool enabled) { debugEnabled = enabled; }
        static bool IsDebugEnabled() { return debugEnabled; }

        static void SetGlobalSettings(const DebugRenderSettings& settings) {
            globalSettings = settings;
        }

        static const DebugRenderSettings& GetGlobalSettings() {
            return globalSettings;
        }

        // Quick preset configurations
        static void SetMinimalDebug() {
            globalSettings.showBorders = true;
            globalSettings.showCoordinates = false;
            globalSettings.showElementID = true;
            globalSettings.showTransformation = false;
            globalSettings.showBounds = false;
            globalSettings.borderColor = Color(255, 0, 0, 128);
        }

        static void SetFullDebug() {
            globalSettings.showBorders = true;
            globalSettings.showCoordinates = true;
            globalSettings.showElementID = true;
            globalSettings.showTransformation = true;
            globalSettings.showBounds = true;
            globalSettings.showZIndex = true;
            globalSettings.showVisibilityState = true;
            globalSettings.showActiveState = true;
        }

        static void SetProductionSafe() {
            globalSettings.showBorders = false;
            globalSettings.showCoordinates = false;
            globalSettings.showElementID = false;
            globalSettings.showTransformation = false;
            globalSettings.showBounds = false;
            debugEnabled = false;
        }
    };

// ===== ELEMENT DEBUG EXTENSION =====
    class UltraCanvasUIElementDebugExtension {
    public:
        // Main debug rendering function for any UltraCanvasUIElement
        static std::string RenderDebugInfo(UltraCanvasUIElement* element,
                                           const DebugRenderSettings& settings = UltraCanvasDebugRenderer::GetGlobalSettings()) {
            if (!element || !UltraCanvasDebugRenderer::IsDebugEnabled()) return "";
            auto ctx = element->GetRenderContext();
            ctx->PushState();

            Rect2Di bounds = element->GetBounds();
            Point2Di absolutePos = element->GetPositionInWindow();

            // Draw debug border
            if (settings.showBorders) {
                DrawDebugBorder(bounds, settings, ctx);
            }

            // Prepare debug text
            std::string debugText = GenerateDebugText(element, settings, ctx);

            if (!debugText.empty()) {
                std::cout << debugText << std::endl;
                DrawDebugText(debugText, bounds, absolutePos, settings,ctx);
            }
            ctx->PopState();
            return debugText;
        }

        // Render debug info for element hierarchy
//        static void RenderDebugHierarchy(UltraCanvasUIElement* rootElement,
//                                         const DebugRenderSettings& settings = UltraCanvasDebugRenderer::GetGlobalSettings()) {
//            if (!rootElement || !UltraCanvasDebugRenderer::IsDebugEnabled()) return;
//
//            // Render this element
//            RenderDebugInfo(rootElement, settings);
//
//            // Render all children recursively
//            for (auto* child : rootElement->GetChildren()) {
//                RenderDebugHierarchy(child, settings);
//            }
//        }

        // Quick debug render with default settings
//        static void QuickDebug(UltraCanvasUIElement* element) {
//            DebugRenderSettings quickSettings;
//            quickSettings.showBorders = true;
//            quickSettings.showElementID = true;
//            quickSettings.showCoordinates = true;
//            quickSettings.borderColor = Color(0, 255, 0, 200); // Green border
//            RenderDebugInfo(element, quickSettings);
//        }

        static void DrawDebugBorder(const Rect2Di& bounds, const DebugRenderSettings& settings, IRenderContext* ctx) {
            ctx->PushState();

            ctx->SetStrokeColor(settings.borderColor);
            ctx->SetStrokeWidth(settings.borderWidth);

            // Draw main border rectangle
            ctx->DrawRectangle(bounds);

            // Draw corner markers for better visibility
            float markerSize = 8.0f;
            Color markerColor = Color(settings.borderColor.r, settings.borderColor.g,
                                      settings.borderColor.b, settings.borderColor.a + 75);
            ctx->SetStrokeColor(markerColor);
            ctx->SetStrokeWidth(1.0f);

            // Top-left corner
            ctx->DrawLine(Point2Di(bounds.x, bounds.y),
                     Point2Di(bounds.x + markerSize, bounds.y));
            ctx->DrawLine(Point2Di(bounds.x, bounds.y),
                     Point2Di(bounds.x, bounds.y + markerSize));

            // Top-right corner
            ctx->DrawLine(Point2Di(bounds.x + bounds.width, bounds.y),
                     Point2Di(bounds.x + bounds.width - markerSize, bounds.y));
            ctx->DrawLine(Point2Di(bounds.x + bounds.width, bounds.y),
                     Point2Di(bounds.x + bounds.width, bounds.y + markerSize));

            // Bottom-left corner
            ctx->DrawLine(Point2Di(bounds.x, bounds.y + bounds.height),
                     Point2Di(bounds.x + markerSize, bounds.y + bounds.height));
            ctx->DrawLine(Point2Di(bounds.x, bounds.y + bounds.height),
                     Point2Di(bounds.x, bounds.y + bounds.height - markerSize));

            // Bottom-right corner
            ctx->DrawLine(Point2Di(bounds.x + bounds.width, bounds.y + bounds.height),
                     Point2Di(bounds.x + bounds.width - markerSize, bounds.y + bounds.height));
            ctx->DrawLine(Point2Di(bounds.x + bounds.width, bounds.y + bounds.height),
                     Point2Di(bounds.x + bounds.width, bounds.y + bounds.height - markerSize));

            ctx->PopState();
        }

        static std::string GenerateDebugText(UltraCanvasUIElement* element, const DebugRenderSettings& settings, IRenderContext* ctx) {
            std::ostringstream debugText;

            if (settings.showElementID) {
                debugText << "ID: '" << element->GetIdentifier() << "'";
                if (element->GetIdentifierID() != 0) {
                    debugText << " (" << element->GetIdentifierID() << ")";
                }
                if (settings.multilineText) debugText << "\n";
                else debugText << " | ";
            }

            if (settings.showCoordinates) {
                if (settings.showRelativePosition) {
                    debugText << "Pos: (" << element->GetX() << ", " << element->GetY() << ")";
                    if (settings.multilineText) debugText << "\n";
                    else debugText << " | ";
                }

                if (settings.showAbsolutePosition) {
                    Point2Di absPos = element->GetPositionInWindow();
                    debugText << "Abs: (" << std::fixed << std::setprecision(1)
                              << absPos.x << ", " << absPos.y << ")";
                    if (settings.multilineText) debugText << "\n";
                    else debugText << " | ";
                }
            }

            if (settings.showBounds) {
                debugText << "Size: " << element->GetWidth() << "x" << element->GetHeight();
                if (settings.multilineText) debugText << "\n";
                else debugText << " | ";
            }

            if (settings.showZIndex) {
                debugText << "Z: " << element->GetZIndex();
                if (settings.multilineText) debugText << "\n";
                else debugText << " | ";
            }

            if (settings.showVisibilityState) {
                debugText << "V:" << (element->IsVisible() ? "T" : "F");
                if (settings.showActiveState) {
                    debugText << " A:" << (element->IsActive() ? "T" : "F");
                }
                if (settings.multilineText) debugText << "\n";
                else debugText << " | ";
            }

            if (settings.showParentInfo && element->GetParentContainer()) {
                debugText << "Parent: '" << element->GetParentContainer()->GetIdentifier() << "'";
                if (settings.multilineText) debugText << "\n";
                else debugText << " | ";
            }

//            if (settings.showChildCount) {
//                debugText << "Children: " << element->GetChildren().size();
//                if (settings.multilineText) debugText << "\n";
//                else debugText << " | ";
//            }

            // Clean up trailing separators
            std::string result = debugText.str();
            if (!settings.multilineText && result.length() >= 3) {
                if (result.substr(result.length() - 3) == " | ") {
                    result = result.substr(0, result.length() - 3);
                }
            } else if (settings.multilineText && !result.empty() && result.back() == '\n') {
                result.pop_back();
            }

            return result;
        }

        static void DrawDebugText(const std::string& text, const Rect2Di& bounds,
                                  const Point2Di& absolutePos, const DebugRenderSettings& settings, IRenderContext* ctx) {
            if (text.empty()) return;

            ctx->PushState();

            // Set up text rendering
            ctx->SetFont(settings.fontFamily, settings.textSize);

            // Measure text to calculate background size
            Point2Di textSize = ctx->MeasureText(text);

            // Calculate text position (top-left corner of element, with padding)
            Point2Di textPos = Point2Di(bounds.x + settings.textPadding,
                                      bounds.y - textSize.y - settings.textPadding);

            // Ensure text stays within screen bounds
            if (textPos.y < 0) {
                textPos.y = bounds.y + bounds.height + settings.textPadding;
            }

            // Draw text background
            if (settings.textBackgroundColor.a > 0) {
                Rect2Di backgroundRect(
                        textPos.x - settings.textPadding,
                        textPos.y - settings.textPadding,
                        textSize.x + (settings.textPadding * 2),
                        textSize.y + (settings.textPadding * 2)
                );

                ctx->DrawFilledRectangle(backgroundRect, settings.textBackgroundColor,
                               Colors::Transparent, 0, settings.cornerRadius);
            }

            // Draw the text
            ctx->SetTextColor(settings.textColor);
            ctx->DrawText(text, textPos);
            ctx->PopState();
        }
    };

// ===== CONVENIENCE FUNCTIONS =====

// Global function for easy debugging
    inline void DrawElementDebug(UltraCanvasUIElement* element) {
        UltraCanvasUIElementDebugExtension::RenderDebugInfo(element);
    }

// Enable/disable global debugging
    inline void EnableElementDebugging(bool enabled = true) {
        UltraCanvasDebugRenderer::SetDebugEnabled(enabled);
    }

// Quick debug all elements in a container
    inline void DebugAllElements(const std::vector<UltraCanvasUIElement*>& elements) {
        for (auto* element : elements) {
            if (element) {
                UltraCanvasUIElementDebugExtension::RenderDebugInfo(element);
            }
        }
    }

// ===== ELEMENT EXTENSION METHODS =====

// Extension to add debug rendering to any UltraCanvasUIElement
// This can be called directly on element instances
    inline void RenderElementDebugOverlay(UltraCanvasUIElement* element) {
        UltraCanvasUIElementDebugExtension::RenderDebugInfo(element);
    }

// Render debug info with custom settings
    inline void RenderElementDebugOverlay(UltraCanvasUIElement* element, const DebugRenderSettings& settings) {
        UltraCanvasUIElementDebugExtension::RenderDebugInfo(element, settings);
    }

} // namespace UltraCanvas
