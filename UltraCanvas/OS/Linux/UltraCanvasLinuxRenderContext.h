// UltraCanvasX11RenderContext.h
// Linux/X11 platform support implementation for UltraCanvas Framework
// Version: 1.0.2
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
//

#ifndef INC_3_ULTRACANVASX11RENDERCONTEXT_H
#define INC_3_ULTRACANVASX11RENDERCONTEXT_H

#include "../../include/UltraCanvasRenderInterface.h"

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"
#include "../../include/UltraCanvasRenderInterface.h"

// ===== LINUX PLATFORM INCLUDES =====
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

// ===== STANDARD INCLUDES =====
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace UltraCanvas {
// ===== LINUX RENDER CONTEXT =====
    class LinuxRenderContext : public IRenderContext {
    private:
        cairo_t* cairo;
        PangoContext* pangoContext;
        PangoFontMap* fontMap;

        // State management
        std::vector<RenderState> stateStack;
        RenderState currentState;

        // Internal helper methods
        void ApplyDrawingStyle(const DrawingStyle& style);
        void ApplyTextStyle(const TextStyle& style);
        PangoFontDescription* CreatePangoFont(const TextStyle& style);

        // Add a flag to track if we're being destroyed
        bool destroying = false;
        bool contextValid;

    public:
        LinuxRenderContext(cairo_t* cairoContext);
        virtual ~LinuxRenderContext();

        // ===== INHERITED FROM IRenderContext =====

        // State management
        virtual void PushState() override;
        virtual void PopState() override;
        virtual void ResetState() override;

        // Transformation
        virtual void Translate(float x, float y) override;
        virtual void Rotate(float angle) override;
        virtual void Scale(float sx, float sy) override;
        virtual void SetTransform(float a, float b, float c, float d, float e, float f) override;
        virtual void ResetTransform() override;

        // Clipping
        virtual void SetClipRect(const Rect2D& rect) override;
        virtual void ClearClipRect() override;
        virtual void IntersectClipRect(const Rect2D& rect) override;

        // Style management
        virtual void SetDrawingStyle(const DrawingStyle& style) override;
        virtual const DrawingStyle& GetDrawingStyle() const override;
        virtual void SetTextStyle(const TextStyle& style) override;
        virtual const TextStyle& GetTextStyle() const override;
        virtual void SetGlobalAlpha(float alpha) override;
        virtual float GetGlobalAlpha() const override;

        // Basic drawing
        virtual void DrawLine(const Point2D& start, const Point2D& end) override;
        virtual void DrawRectangle(const Rect2D& rect) override;
        virtual void FillRectangle(const Rect2D& rect) override;
        virtual void DrawRoundedRectangle(const Rect2D& rect, float radius) override;
        virtual void FillRoundedRectangle(const Rect2D& rect, float radius) override;
        virtual void DrawCircle(const Point2D& center, float radius) override;
        virtual void FillCircle(const Point2D& center, float radius) override;
        virtual void DrawEllipse(const Rect2D& rect) override;
        virtual void FillEllipse(const Rect2D& rect) override;
        virtual void DrawArc(const Point2D& center, float radius, float startAngle, float endAngle) override;
        virtual void FillArc(const Point2D& center, float radius, float startAngle, float endAngle) override;
        virtual void DrawBezier(const Point2D& start, const Point2D& cp1, const Point2D& cp2, const Point2D& end) override;
        virtual void DrawPath(const std::vector<Point2D>& points, bool closePath = false) override;
        virtual void FillPath(const std::vector<Point2D>& points) override;

        // Text rendering
        virtual void DrawText(const std::string& text, const Point2D& position) override;
        virtual void DrawTextInRect(const std::string& text, const Rect2D& rect) override;
        virtual Point2D MeasureText(const std::string& text) override;

        // Image rendering
        virtual void DrawImage(const std::string& imagePath, const Point2D& position) override;
        virtual void DrawImage(const std::string& imagePath, const Rect2D& destRect) override;
        virtual void DrawImage(const std::string& imagePath, const Rect2D& srcRect, const Rect2D& destRect) override;

        // ===== ENHANCED IMAGE RENDERING METHODS =====
        virtual bool IsImageFormatSupported(const std::string& filePath);
        virtual Point2D GetImageDimensions(const std::string& imagePath);

        void DrawImageWithFilter(const std::string& imagePath, const Rect2D& destRect,
                                 cairo_filter_t filter = CAIRO_FILTER_BILINEAR);
        void DrawImageTiled(const std::string& imagePath, const Rect2D& fillRect);

        // ===== IMAGE CACHE MANAGEMENT =====
        void ClearImageCache();
        void SetImageCacheSize(size_t maxSizeBytes);
        size_t GetImageCacheMemoryUsage();

        // ===== CONTEXT MANAGEMENT =====
        void InvalidateContext();
        void UpdateContext(cairo_t* newCairoContext);
        bool ValidateContext() const;

        // Pixel operations
        virtual void SetPixel(const Point2D& point, const Color& color) override;
        virtual Color GetPixel(const Point2D& point) override;
        virtual void Clear(const Color& color) override;

        // Utility functions
        virtual void Flush() override;
        virtual void* GetNativeContext() override;

        // ===== CAIRO-SPECIFIC METHODS =====
        void SetCairoColor(const Color& color);
        cairo_t* GetCairo() const { return cairo; }
        PangoContext* GetPangoContext() const { return pangoContext; }

        // ===== OVERRIDE THE VIRTUAL CONVENIENCE METHODS =====
        void SetFont(const std::string& fontFamily, float fontSize) override {
            std::cout << "LinuxRenderContext::SetFont called with: " << fontFamily << ", " << fontSize << std::endl;
            TextStyle style = GetTextStyle();
            style.fontFamily = fontFamily;
            style.fontSize = fontSize;
            SetTextStyle(style);
            std::cout << "LinuxRenderContext::SetFont completed. New fontSize: " << GetTextStyle().fontSize << std::endl;
        }

        void SetTextColor(const Color& color) override {
            std::cout << "LinuxRenderContext::SetTextColor called with: R=" << (int)color.r
                      << " G=" << (int)color.g << " B=" << (int)color.b << " A=" << (int)color.a << std::endl;
            TextStyle style = GetTextStyle();
            style.textColor = color;
            SetTextStyle(style);
            std::cout << "LinuxRenderContext::SetTextColor completed. New color: R=" << (int)GetTextStyle().textColor.r << std::endl;
        }

        void SetTextAlign(TextAlign align) override {
            std::cout << "LinuxRenderContext::SetTextAlign called with: " << (int)align << std::endl;
            TextStyle style = GetTextStyle();
            style.alignment = align;
            SetTextStyle(style);
            std::cout << "LinuxRenderContext::SetTextAlign completed." << std::endl;
        }
    };

    // ===== CAIRO FILTER CONSTANTS =====
    // These provide easier access to Cairo filter types
    namespace CairoFilters {
        constexpr cairo_filter_t Fast = CAIRO_FILTER_FAST;
        constexpr cairo_filter_t Good = CAIRO_FILTER_GOOD;
        constexpr cairo_filter_t Best = CAIRO_FILTER_BEST;
        constexpr cairo_filter_t Nearest = CAIRO_FILTER_NEAREST;
        constexpr cairo_filter_t Bilinear = CAIRO_FILTER_BILINEAR;
        constexpr cairo_filter_t Gaussian = CAIRO_FILTER_GAUSSIAN;
    }

    // ===== CONVENIENCE FUNCTIONS FOR IMAGE RENDERING =====

    // Load image and get dimensions without rendering
    inline Point2D GetImageDimensions(const std::string& imagePath) {
        // This would typically use the current render context
        IRenderContext* ctx = GetRenderContext();
        if (auto linuxCtx = dynamic_cast<LinuxRenderContext*>(ctx)) {
            return linuxCtx->GetImageDimensions(imagePath);
        }
        return Point2D(0, 0);
    }

    // Check if image format is supported
    inline bool IsImageFormatSupported(const std::string& imagePath) {
        IRenderContext* ctx = GetRenderContext();
        if (auto linuxCtx = dynamic_cast<LinuxRenderContext*>(ctx)) {
            return linuxCtx->IsImageFormatSupported(imagePath);
        }
        return false;
    }

    // Draw image with high-quality filtering
    inline void DrawImageHighQuality(const std::string& imagePath, const Rect2D& destRect) {
        IRenderContext* ctx = GetRenderContext();
        if (auto linuxCtx = dynamic_cast<LinuxRenderContext*>(ctx)) {
            linuxCtx->DrawImageWithFilter(imagePath, destRect, CAIRO_FILTER_BEST);
        }
    }

    // Draw image with pixelated (nearest neighbor) filtering
    inline void DrawImagePixelated(const std::string& imagePath, const Rect2D& destRect) {
        IRenderContext* ctx = GetRenderContext();
        if (auto linuxCtx = dynamic_cast<LinuxRenderContext*>(ctx)) {
            linuxCtx->DrawImageWithFilter(imagePath, destRect, CAIRO_FILTER_NEAREST);
        }
    }

    // Draw repeating background image
    inline void DrawImageBackground(const std::string& imagePath, const Rect2D& area) {
        IRenderContext* ctx = GetRenderContext();
        if (auto linuxCtx = dynamic_cast<LinuxRenderContext*>(ctx)) {
            linuxCtx->DrawImageTiled(imagePath, area);
        }
    }
} // namespace UltraCanvas
#endif //INC_3_ULTRACANVASX11RENDERCONTEXT_H
