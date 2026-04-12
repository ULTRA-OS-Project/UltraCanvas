// libspecific/Cairo/RenderContextCairo.h
// Cairo support implementation for UltraCanvas Framework
// Version: 1.0.5 - Rect-based DrawPixmap/DrawMask
// Last Modified: 2026-04-11
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

        static std::vector<RenderContextCairo*> g_Instances;

        // Add a flag to track if we're being destroyed
        bool destroying = false;
        bool enableDoubleBuffering = false;
        bool CreateStagingSurface();
        void SwitchToSurface(cairo_surface_t* s);

        std::string GenerateTextCacheKey(const std::string& text, const Size2Di &sz);

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
        void Translate(double x, double y) override;
        void Rotate(double angle) override;
        void Scale(double sx, double sy) override;
        void SetTransform(double a, double b, double c, double d, double e, double f) override;
        void Transform(double a, double b, double c, double d, double e, double f) override;
        void ResetTransform() override;

        // Clipping
//        void SetClipRect(float x, float y, float w, float h) override;
        void ClearClipRect() override;
        void ClipRect(const Rect2Df& rect) override;
        void ClipRoundedRectangle(
                const Rect2Df& rect,
                double borderTopLeftRadius, double borderTopRightRadius,
                double borderBottomRightRadius, double borderBottomLeftRadius) override;
        void ClipPath() override;

        // Style management
        //void SetDrawingStyle(const DrawingStyle &style) override;
        void SetTextStyle(const TextStyle &style) override;
        const TextStyle &GetTextStyle() const override;
        void SetStrokeWidth(double width) override;
        //void SetLineWidth(float width) override;
        void SetLineCap(LineCap cap) override;
        void SetLineJoin(LineJoin join) override;
        void SetMiterLimit(double limit)  override;
        void SetLineDash(const UCDashPattern& pattern) override;

        // === Text Methods ===
        void SetFontFace(const std::string& family, FontWeight fw, FontSlant fs) override;
        void SetFontFamily(const std::string& family) override;
        void SetFontSize(double size) override;
        void SetFontWeight(FontWeight fw) override;
        void SetFontSlant(FontSlant fs) override;
        void SetTextAlignment(TextAlignment align) override;
        void SetTextVerticalAlignment(VerticalAlignment align) override;
        void SetTextIsMarkup(bool isMarkup) override;
        void SetTextLineHeight(double height) override;
        void SetTextWrap(TextWrap wrap) override;

        void SetAlpha(double alpha) override;
        double GetAlpha() const override;
        std::shared_ptr<IPaintPattern> CreateRadialGradientPattern(double cx1, double cy1, double r1,
                                                                   double cx2, double cy2, double r2,
                                                                   const std::vector<GradientStop>& stops) override;
        std::shared_ptr<IPaintPattern> CreateLinearGradientPattern(double x1, double y1, double x2, double y2,
                                                                   const std::vector<GradientStop>& stops) override;
        void SetFillPaint(std::shared_ptr<IPaintPattern> pattern) override;
        void SetFillPaint(const Color& color) override;
        void SetStrokePaint(std::shared_ptr<IPaintPattern> pattern) override;
        void SetStrokePaint(const Color& color) override;
        void SetTextPaint(std::shared_ptr<IPaintPattern> pattern) override;
        void SetTextPaint(const Color& color) override;
        void SetCurrentPaint(const Color& color) override;
        void SetCurrentPaint(std::shared_ptr<IPaintPattern> pattern) override;

        // Basic drawing
        void DrawLine(const Point2Df& from, const Point2Df& to) override;
        void DrawRectangle(const Rect2Df& rect) override;
        void FillRectangle(const Rect2Df& rect) override;
        void DrawRoundedRectangle(const Rect2Df & rect, double radius) override;
        void FillRoundedRectangle(const Rect2Df & rect, double radius) override;
        void DrawRoundedRectangleWidthBorders(const Rect2Df & rect,
                                              bool fill,
                                              double borderLeftWidth, double borderRightWidth,
                                              double borderTopWidth, double borderBottomWidth,
                                              const Color& borderLeftColor, const Color& borderRightColor,
                                              const Color& borderTopColor, const Color& borderBottomColor,
                                              double borderTopLeftRadius, double borderTopRightRadius,
                                              double borderBottomRightRadius, double borderBottomLeftRadius,
                                              const UCDashPattern& borderLeftPattern,
                                              const UCDashPattern& borderRightPattern,
                                              const UCDashPattern& borderTopPattern,
                                              const UCDashPattern& borderBottomPattern) override;
        void DrawCircle(const Point2Df& center, double radius) override;
        void FillCircle(const Point2Df& center, double radius) override;
        void DrawEllipse(const Rect2Df& rect) override;
        void FillEllipse(const Rect2Df& rect) override;
        void DrawArc(double x, double y, double radius, double startAngle, double endAngle) override;
        void FillArc(double x, double y, double radius, double startAngle, double endAngle) override;
        void DrawBezierCurve(const Point2Df &start, const Point2Df &cp1, const Point2Df &cp2, const Point2Df &end) override;
        void DrawLinePath(const std::vector<Point2Df> &points, bool closePath) override;
        void FillLinePath(const std::vector<Point2Df> &points) override;

        // path functions
        void ClearPath() override;
        void ClosePath() override;
        void MoveTo(double x, double y) override;
        void RelMoveTo(double x, double y) override;
        void LineTo(double x, double y) override;
        void RelLineTo(double x, double y) override;
        void QuadraticCurveTo(double cpx, double cpy, double x, double y) override;
        void BezierCurveTo(double cp1x, double cp1y, double cp2x, double cp2y, double x, double y) override;
        void RelBezierCurveTo(double cp1x, double cp1y, double cp2x, double cp2y, double x, double y) override;
        void Arc(double cx, double cy, double radius, double startAngle, double endAngle) override;
        void ArcTo(double x1, double y1, double x2, double y2, double radius) override;
        void Ellipse(double cx, double cy, double rx, double ry, double rotation) override;
        void Rect(double x, double y, double width, double height) override;
        void RoundedRect(double x, double y, double width, double height, double radius) override;
        void Circle(double x, double y, double radius) override;

        Rect2Df GetPathExtents() override;
        void StrokePathPreserve() override;
        void FillPathPreserve() override;
        void FillText(const std::string& text, double x, double y) override;
        void StrokeText(const std::string& text, double x, double y) override;
        void Fill() override;
        void Stroke() override;

        // Text rendering
        std::unique_ptr<ITextLayout> CreateTextLayout(const std::string& text, bool isMarkup) override;
        std::shared_ptr<ITextLayout> GetOrCreateTextLayout(const std::string& text, const Size2Di& sz, bool isMarkup) override;

        void DrawTextLayout(ITextLayout &layout, const Point2Df &pos) override;
        void DrawText(const std::string &text, const Point2Df &pos) override;
        void DrawTextInRect(const std::string &text, const Rect2Df &rect) override;
        Size2Di GetTextDimensions(const std::string &text, const Size2Di& explicitSize) override;
        int GetTextIndexForXY(const std::string &text, int x, int y, int w = 0, int h = 0) override;

        // Image rendering
        void DrawPartOfPixmap(UCPixmap & pixmap, const Rect2Df &srcRect, const Rect2Df &destRect) override;
        void DrawPixmap(UCPixmap& pixmap, const Rect2Df& rect, ImageFitMode fitMode) override;
        void DrawMask(const Color& drawColor, UCPixmap& mask, const Rect2Df& rect, ImageFitMode fitMode) override;

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

        // ===== CONFIGURABLE TEXT RENDERING OPTIONS =====
        static void SetTextAntialias(cairo_antialias_t mode);
        static cairo_antialias_t GetTextAntialias();
        static void SetTextHintStyle(cairo_hint_style_t style);
        static cairo_hint_style_t GetTextHintStyle();
        static void SetTextHintMetrics(cairo_hint_metrics_t metrics);
        static cairo_hint_metrics_t GetTextHintMetrics();
        void ApplyPangoFontOptions();

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
