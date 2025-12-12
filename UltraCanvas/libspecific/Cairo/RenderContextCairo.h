// libspecific/Cairo/RenderContextCairo.h
// Cairo support implementation for UltraCanvas Framework
// Version: 1.0.2
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
//

#pragma once

// ===== CORE INCLUDES =====
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasEvent.h"

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <cstring>
#include <condition_variable>

namespace UltraCanvas {

    class PaintPatternCairo : public IPaintPattern {
    private:
        cairo_pattern_t *pattern = nullptr;
    public:
        PaintPatternCairo() = delete;
        explicit PaintPatternCairo(cairo_pattern_t *pat) {
            pattern = pat;
        };
        ~PaintPatternCairo() override {
            if (pattern) {
                cairo_pattern_destroy(pattern);
            }
        };
        void* GetHandle() override { return pattern; };
    };

    struct TextSurfaceEntry {
        cairo_surface_t* surface;
        int width = 0;
        int height = 0;

        TextSurfaceEntry(cairo_surface_t* surf, int w, int h) : surface(surf), width(w), height(h) {};

        size_t GetDataSize() {
            return width*height*4+sizeof(TextSurfaceEntry);
        }

        ~TextSurfaceEntry() {
            cairo_surface_destroy(surface);
        }
    };

    struct TextDimensionsEntry {
        int width = 0;
        int height = 0;

        TextDimensionsEntry(int w, int h) : width(w), height(h) {};

        size_t GetDataSize() {
            return sizeof(TextDimensionsEntry);
        }
    };

    class RenderContextCairo : public IRenderContext {
    private:
        std::mutex cairoMutex;
        cairo_t *targetContext;
        cairo_surface_t* targetSurface;
        cairo_t *cairo;
        cairo_surface_t* stagingSurface;
        int surfaceWidth;
        int surfaceHeight;

        PangoContext *pangoContext;

        // State management
        std::vector<RenderState> stateStack;
        RenderState currentState;

        void ApplySource(const Color& sourceColor, std::shared_ptr<IPaintPattern> sourcePattern = nullptr);
        void ApplyTextSource() {
            ApplySource(currentState.textSourceColor, currentState.textSourcePattern);
        }

        void ApplyFillSource() {
            ApplySource(currentState.fillSourceColor, currentState.fillSourcePattern);
        }

        void ApplyStrokeSource() {
            ApplySource(currentState.fillSourceColor, currentState.fillSourcePattern);
        }

//        void ApplyStrokeStyle(const DrawingStyle &style);
//        void ApplyGradientFill(const Gradient& gradient);

        PangoFontDescription *CreatePangoFont(const FontStyle &style);
        PangoLayout * CreatePangoLayout(PangoFontDescription *desc, int w=0, int h=0);

        // Add a flag to track if we're being destroyed
        bool destroying = false;
        bool enableDoubleBuffering = false;
        bool CreateStagingSurface();
        void SwitchToSurface(cairo_surface_t* s);

        std::string GenerateTextCacheKey(const std::string& text, int rectWidth, int rectHeight);
        std::shared_ptr<TextSurfaceEntry> MakeTextSurface(const std::string& text, int rectWidth, int rectHeight);
        std::shared_ptr<TextSurfaceEntry> GetTextSurface(const std::string& text, int rectWidth, int rectHeight);
        std::shared_ptr<TextDimensionsEntry> MeasureTextDimensions(const std::string& text, int rectWidth, int rectHeight);

    public:
        RenderContextCairo(cairo_surface_t *surf, int width, int height, bool enableDoubleBuffering);

        ~RenderContextCairo() override;

        void SetTargetSurface(cairo_surface_t* surf, int w, int h);
        bool ResizeStagingSurface(int w, int h);

        // ===== INHERITED FROM IRenderContext =====
        // State management
        void PushState() override;
        void PopState() override;
        void ResetState() override;

        // Transformation
        void Translate(float x, float y) override;
        void Rotate(float angle) override;
        void Scale(float sx, float sy) override;
        void SetTransform(float a, float b, float c, float d, float e, float f) override;
        void Transform(float a, float b, float c, float d, float e, float f) override;
        void ResetTransform() override;

        // Clipping
//        void SetClipRect(float x, float y, float w, float h) override;
        void ClearClipRect() override;
        void ClipRect(float x, float y, float w, float h) override;
        void ClipRoundedRectangle(
                float x, float y, float width, float height,
                float borderTopLeftRadius, float borderTopRightRadius,
                float borderBottomRightRadius, float borderBottomLeftRadius) override;
        void ClipPath() override;

        // Style management
        //void SetDrawingStyle(const DrawingStyle &style) override;
        void SetTextStyle(const TextStyle &style) override;
        const TextStyle &GetTextStyle() const override;
        void SetStrokeWidth(float width) override;
        //void SetLineWidth(float width) override;
        void SetLineCap(LineCap cap) override;
        void SetLineJoin(LineJoin join) override;
        void SetMiterLimit(float limit)  override;
        void SetLineDash(const UCDashPattern& pattern) override;

        // === Text Methods ===
        void SetFontFace(const std::string& family, FontWeight fw, FontSlant fs) override;
        void SetFontSize(float size) override;
        void SetFontWeight(FontWeight fw) override;
        void SetFontSlant(FontSlant fs) override;
        void SetTextAlignment(TextAlignment align) override;
        void SetTextVerticalAlignment(TextVerticalAlignement align) override;
        void SetTextIsMarkup(bool isMarkup) override;
        void SetTextLineHeight(float height) override;
        void SetTextWrap(TextWrap wrap) override;

        void SetAlpha(float alpha) override;
        float GetAlpha() const override;
        std::shared_ptr<IPaintPattern> CreateRadialGradientPattern(float cx1, float cy1, float r1,
                                                                   float cx2, float cy2, float r2,
                                                                   const std::vector<GradientStop>& stops) override;
        std::shared_ptr<IPaintPattern> CreateLinearGradientPattern(float x1, float y1, float x2, float y2,
                                                                   const std::vector<GradientStop>& stops) override;
        void SetFillPaint(std::shared_ptr<IPaintPattern> pattern) override;
        void SetFillPaint(const Color& color) override;
        void SetStrokePaint(std::shared_ptr<IPaintPattern> pattern) override;
        void SetStrokePaint(const Color& color) override;
        void SetTextPaint(std::shared_ptr<IPaintPattern> pattern) override;
        void SetTextPaint(const Color& color) override;

        // Basic drawing
        void DrawLine(float x, float y, float x1, float y1) override;
        void DrawRectangle(float x, float y, float w, float h) override;
        void FillRectangle(float x, float y, float w, float h) override;
        void DrawRoundedRectangle(float x, float y, float w, float h, float radius) override;
        void FillRoundedRectangle(float x, float y, float w, float h, float radius) override;
        void DrawRoundedRectangleWidthBorders(float x, float y, float width, float height,
                                                     bool fill,
                                                     float borderLeftWidth, float borderRightWidth,
                                                     float borderTopWidth, float borderBottomWidth,
                                                     const Color& borderLeftColor, const Color& borderRightColor,
                                                     const Color& borderTopColor, const Color& borderBottomColor,
                                                     float borderTopLeftRadius, float borderTopRightRadius,
                                                     float borderBottomRightRadius, float borderBottomLeftRadius,
                                                     const UCDashPattern& borderLeftPattern,
                                                     const UCDashPattern& borderRightPattern,
                                                     const UCDashPattern& borderTopPattern,
                                                     const UCDashPattern& borderBottomPattern) override;
        void DrawCircle(float x, float y, float radius) override;
        void FillCircle(float x, float y, float radius) override;
        void DrawEllipse(float x, float y, float w, float h) override;
        void FillEllipse(float x, float y, float w, float h) override;
        void DrawArc(float x, float y, float radius, float startAngle, float endAngle) override;
        void FillArc(float x, float y, float radius, float startAngle, float endAngle) override;
        void DrawBezierCurve(const Point2Df &start, const Point2Df &cp1, const Point2Df &cp2, const Point2Df &end) override;
        void DrawLinePath(const std::vector<Point2Df> &points, bool closePath) override;
        void FillLinePath(const std::vector<Point2Df> &points) override;

        // path functions
        void ClearPath() override;
        void ClosePath() override;
        void MoveTo(float x, float y) override;
        void RelMoveTo(float x, float y) override;
        void LineTo(float x, float y) override;
        void RelLineTo(float x, float y) override;
        void QuadraticCurveTo(float cpx, float cpy, float x, float y) override;
        void BezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) override;
        void RelBezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) override;
        void Arc(float cx, float cy, float radius, float startAngle, float endAngle) override;
        void ArcTo(float x1, float y1, float x2, float y2, float radius) override;
        void Ellipse(float cx, float cy, float rx, float ry, float rotation, float startAngle, float endAngle) override;
        void Rect(float x, float y, float width, float height) override;
        void RoundedRect(float x, float y, float width, float height, float radius) override;
        void Circle(float x, float y, float radius) override;

        void GetPathExtents(float &x, float &y, float &width, float &height) override;
        void StrokePathPreserve() override;
        void FillPathPreserve() override;
        void FillText(const std::string& text, float x, float y) override;
        void StrokeText(const std::string& text, float x, float y) override;
        void Fill() override;
        void Stroke() override;

        // Text rendering
        void DrawText(const std::string &text, float x, float y) override;
        void DrawTextInRect(const std::string &text, float x, float y, float w, float h) override;
        bool GetTextLineDimensions(const std::string &text, int &w, int &h) override;
        bool GetTextDimensions(const std::string &text, int width, int height, int &retWidth, int &retHeight) override;
        int GetTextIndexForXY(const std::string &text, int x, int y, int w = 0, int h = 0) override;

        // Image rendering
        void DrawPartOfPixmap(UCPixmap & pixmap, const Rect2Df &srcRect, const Rect2Df &destRect) override;
        void DrawPixmap(UCPixmap& pixmap, float x, float y, float w, float h, ImageFitMode fitMode) override;

        // ===== ENHANCED IMAGE RENDERING METHODS =====
//        void DrawImageWithFilter(const std::string &imagePath, float x, float y, float w, float h,
//                                 cairo_filter_t filter = CAIRO_FILTER_BILINEAR);

        void DrawImageTiled(UCImagePtr image, float x, float y, float w, float h);

        // ===== CONTEXT MANAGEMENT =====
        void UpdateContext(cairo_t *newCairoContext);

        // Pixel operations
        void Clear(const Color &color) override;

        // Utility functions
        void SwapBuffers();

        void *GetNativeContext() override;

        // ===== CAIRO-SPECIFIC METHODS =====
        void SetCairoColor(const Color &color);

        cairo_t *GetCairo() const { return cairo; }

        PangoContext *GetPangoContext() const { return pangoContext; }

        cairo_surface_t *GetCairoSurface() const {
            return cairo ? cairo_get_target(cairo) : nullptr;
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

} // namespace UltraCanvas
