// UltraCanvasRenderContext.h - Enhanced Version
// Cross-platform rendering interface with improved context management
// Version: 2.2.0
// Last Modified: 2025-07-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <thread>
#include <unordered_map>
#include <iostream>
#include <mutex>
#include <stack>

namespace UltraCanvas {

// Forward declarations
class UltraCanvasWindow;
class UltraCanvasBaseWindow;
// ===== GRADIENT STRUCTURES =====
    struct GradientStop {
        float position;    // 0.0 to 1.0
        Color color;

        GradientStop(float pos = 0.0f, const Color& col = Colors::Black)
                : position(pos), color(col) {}
    };

    enum class GradientType {
        Linear,
        Radial,
        Conic
    };

    struct Gradient {
        GradientType type;
        Point2Df startPoint;
        Point2Df endPoint;
        float radius1, radius2;  // For radial gradients
        std::vector<GradientStop> stops;

        Gradient(GradientType gradType = GradientType::Linear) : type(gradType) {
            radius1 = radius2 = 0;
        }
    };

// ===== DRAWING STYLES =====
    enum class FillMode {
        NoneFill,
        Solid,
        Gradient,
        Pattern,
        Texture
    };

    enum class StrokeStyle {
        Solid,
        Dashed,
        Dotted,
        DashDot,
        Custom
    };

    enum class LineCap {
        Butt,
        Round,
        Square
    };

    enum class LineJoin {
        Miter,
        Round,
        Bevel
    };

    struct DrawingStyle {
        // Fill properties
        FillMode fillMode = FillMode::Solid;
        Color fillColor = Colors::White;
        Gradient fillGradient;
        std::string patternPath;

        // Stroke properties
        bool hasStroke = false;
        Color strokeColor = Colors::Black;
        float strokeWidth = 1.0f;
        StrokeStyle strokeStyle = StrokeStyle::Solid;
        LineCap lineCap = LineCap::Butt;
        LineJoin lineJoin = LineJoin::Miter;
        std::vector<float> dashPattern;

        // Shadow properties
        bool hasShadow = false;
        Color shadowColor = Color(0, 0, 0, 128);
        Point2Df shadowOffset = Point2Df(2, 2);
        float shadowBlur = 2.0f;

        // Alpha blending
        float globalAlpha = 1.0f;
    };

// ===== TEXT RENDERING STRUCTURES =====
    enum class TextAlign {
        Left,
        Center,
        Right,
        Justify
    };

    enum class TextBaseline {
        Top,
        Middle,
        Bottom,
        Baseline
    };

    enum class FontWeight {
        Normal,
        Bold,
        Light,
        ExtraBold
    };

    enum class FontStyle {
        Normal,
        Italic,
        Oblique
    };

    struct TextStyle {
        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;
        FontStyle fontStyle = FontStyle::Normal;
        Color textColor = Colors::Black;
        TextAlign alignment = TextAlign::Left;
        TextBaseline baseline = TextBaseline::Baseline;
        float lineHeight = 1.2f;
        float letterSpacing = 0.0f;
        float wordSpacing = 0.0f;

        // Text effects
        bool hasUnderline = false;
        bool hasStrikethrough = false;
        bool hasOutline = false;
        Color outlineColor = Colors::Black;
        float outlineWidth = 1.0f;
    };

// ===== RENDERING STATE =====
    struct RenderState {
        DrawingStyle style;
        TextStyle textStyle;
        Rect2Df clipRect;
        Point2Df translation;
        float rotation = 0.0f;
        Point2Df scale = Point2Df(1.0f, 1.0f);
        float globalAlpha = 1.0f;

        RenderState() {
            clipRect = Rect2Df(0, 0, 10000, 10000); // Large default clip
        }
    };

    class IPixelBuffer {
    public:
        virtual ~IPixelBuffer() = default;
        virtual bool IsValid() const  = 0;
        virtual size_t GetSizeInBytes() const  = 0;
        virtual uint32_t* GetPixelData() = 0;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
    };

    class UltraCanvasPixelBuffer : public IPixelBuffer {
    private:
        std::vector<uint32_t> pixels;
        int width = 0, height = 0;
    public:
        UltraCanvasPixelBuffer() = default;
        UltraCanvasPixelBuffer(int w, int h) { Init(w, h); };

        void Init(int w, int h, bool clear = 0) {
            pixels.resize(w * h);
            width = w;
            height = h;
            if (clear) {
                pixels.clear();
            }
        };

        void Clear() {
            pixels.clear();
        };

        bool IsValid() const override { return !pixels.empty() && width > 0 && height > 0; }

        size_t GetSizeInBytes() const override { return pixels.size() * 4; }

        uint32_t GetPixel(int x, int y) const {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                return pixels[y * width + x];
            }
            return 0;
        }

        void SetPixel(int x, int y, uint32_t pixel) {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                pixels[y * width + x] = pixel;
            }
        }

        uint32_t *GetPixelData() override  { return pixels.data(); }
        int GetWidth() const  override { return width; }
        int GetHeight() const override  { return height; }
    };

    // ===== DOUBLE BUFFER INTERFACE =====
    class IDoubleBuffer {
    public:
        virtual ~IDoubleBuffer() = default;

        // Initialize double buffer with window surface
        virtual bool Initialize(int width, int height, void* windowSurface) = 0;

        // Resize buffer when window resizes
        virtual bool Resize(int newWidth, int newHeight) = 0;

        // Get staging surface for rendering
        virtual void* GetStagingContext() = 0;
        virtual void* GetStagingSurface() = 0;

        // Copy staging surface to window surface
        virtual void SwapBuffers() = 0;

        // Clean up resources
        virtual void Cleanup() = 0;

        // Get buffer dimensions
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;

        // Check if buffer is valid
        virtual bool IsValid() const = 0;
    };

// ===== UNIFIED RENDERING INTERFACE =====
    class IRenderContext {
    public:
        virtual ~IRenderContext() = default;

        // ===== STATE MANAGEMENT =====
        virtual void PushState() = 0;
        virtual void PopState() = 0;
        virtual void ResetState() = 0;

        // ===== TRANSFORMATION =====
        virtual void Translate(float x, float y) = 0;
        virtual void Rotate(float angle) = 0;
        virtual void Scale(float sx, float sy) = 0;
        virtual void SetTransform(float a, float b, float c, float d, float e, float f) = 0;
        virtual void ResetTransform() = 0;

        // ===== CLIPPING =====
        virtual void SetClipRect(float x, float y, float w, float h) = 0;
        virtual void ClearClipRect() = 0;
        virtual void IntersectClipRect(float x, float y, float w, float h) = 0;
//        virtual Rect2Df GetClipRect() const = 0;

        // ===== STYLE MANAGEMENT =====
        virtual void SetDrawingStyle(const DrawingStyle& style) = 0;
        virtual void SetTextStyle(const TextStyle& style) = 0;
        virtual void SetGlobalAlpha(float alpha) = 0;
        virtual float GetGlobalAlpha() const = 0;
        virtual const DrawingStyle& GetDrawingStyle() const = 0;
        virtual const TextStyle& GetTextStyle() const = 0;

        // ===== BASIC SHAPES =====
        virtual void DrawLine(float x, float y, float x1, float y1) = 0;
        virtual void DrawRectangle(float x, float y, float w, float h) = 0;
        virtual void FillRectangle(float x, float y, float w, float h) = 0;
        virtual void DrawRoundedRectangle(float x, float y, float w, float h, float radius) = 0;
        virtual void FillRoundedRectangle(float x, float y, float w, float h, float radius) = 0;
        virtual void DrawCircle(float x, float y, float radius) = 0;
        virtual void FillCircle(float x, float y, float radius) = 0;
        virtual void DrawEllipse(float x, float y, float w, float h) = 0;
        virtual void FillEllipse(float x, float y, float w, float h) = 0;
        virtual void DrawArc(float x, float y, float radius, float startAngle, float endAngle) = 0;
        virtual void FillArc(float x, float y, float radius, float startAngle, float endAngle) = 0;
        virtual void DrawBezier(const Point2Df& start, const Point2Df& cp1, const Point2Df& cp2, const Point2Df& end) = 0;
        virtual void DrawPath(const std::vector<Point2Df>& points, bool closePath = false) = 0;
        virtual void FillPath(const std::vector<Point2Df>& points) = 0;

//        virtual void ClosePath() = 0;
//        virtual void FillPath() = 0;
//        virtual void StrokePath() = 0;

        // ===== TEXT RENDERING =====
        virtual void DrawText(const std::string& text, float x, float y) = 0;
        virtual void DrawTextInRect(const std::string& text, float x, float y, float w, float h) = 0;
        virtual bool MeasureText(const std::string& text, int& w, int& h) = 0;
        int GetTextWidth(const std::string& text) {
            int w, h;
            MeasureText(text, w, h);
            return w;
        };
        int GetTextHeight(const std::string& text) {
            int w, h;
            MeasureText(text, w, h);
            return h;
        };

        // ===== IMAGE RENDERING =====
        virtual void DrawImage(const std::string& imagePath, float x, float y) = 0;
        virtual void DrawImage(const std::string& imagePath, float x, float y, float w, float h) = 0;
        virtual void DrawImage(const std::string& imagePath, const Rect2Df& srcRect, const Rect2Df& destRect) = 0;
        virtual bool IsImageFormatSupported(const std::string& filePath) = 0;
        virtual bool GetImageDimensions(const std::string& imagePath, int& w, int& h) = 0;

        // ===== PIXEL OPERATIONS =====
//        virtual void SetPixel(const Point2Df& point, const Color& color) = 0;
//        virtual Color GetPixel(const Point2Df& point) = 0;
        virtual void Clear(const Color& color) = 0;
        virtual bool PaintPixelBuffer(int x, int y, int width, int height, uint32_t* pixels) = 0;
        bool PaintPixelBuffer(int x, int y, IPixelBuffer& pxBuf) {
            return PaintPixelBuffer(x, y, pxBuf.GetWidth(), pxBuf.GetHeight(), pxBuf.GetPixelData());
        };
        virtual IPixelBuffer* SavePixelRegion(const Rect2Di& region) = 0;
        virtual bool RestorePixelRegion(const Rect2Di& region, IPixelBuffer* buf) = 0;
//        virtual bool SaveRegionAsImage(const Rect2Di& region, const std::string& filename) = 0;


        // ===== UTILITY FUNCTIONS =====
        virtual void Flush() = 0;
        virtual void* GetNativeContext() = 0;

        virtual void SetFillColor(const Color& color) {
            DrawingStyle style = GetDrawingStyle();
            style.fillColor = color;
            SetDrawingStyle(style);
        }

        virtual void SetStrokeColor(const Color& color) {
            DrawingStyle style = GetDrawingStyle();
            style.strokeColor = color;
            style.hasStroke = true;
            SetDrawingStyle(style);
        }

        virtual void SetFillGradient(const Color& startColor, const Color& endColor,
                             const Point2Df& startPoint, const Point2Df& endPoint)  = 0;

        void SetFillGradient(const Color& startColor, const Color& endColor,
                                     float start_x, float start_y, float end_x, float end_y)  {
            SetFillGradient(startColor, endColor,
                            Point2Df(start_x, start_y),
                            Point2Df(end_x, end_y));
        }

        void SetFillGradient(const Color& startColor, const Color& endColor,
                                     int start_x, int start_y, int end_x, int end_y) {
            SetFillGradient(startColor, endColor,
                            static_cast<float>(start_x), static_cast<float>(start_y),
                            static_cast<float>(end_x), static_cast<float>(end_y));
        }

        /**
        * Set gradient fill using integer coordinates
        */
        void SetFillGradient(const Color& startColor, const Color& endColor,
                             const Point2Di& startPoint, const Point2Di& endPoint) {
            SetFillGradient(startColor, endColor,
                        Point2Df(static_cast<float>(startPoint.x), static_cast<float>(startPoint.y)),
                        Point2Df(static_cast<float>(endPoint.x), static_cast<float>(endPoint.y)));
        }

/**
 * Set horizontal gradient fill across a rectangle
 */
        void SetHorizontalGradientFill(const Color& leftColor, const Color& rightColor,
                                       const Rect2Df& bounds) {
            SetFillGradient(leftColor, rightColor,
                            Point2Df(bounds.x, bounds.y + bounds.height / 2.0f),
                            Point2Df(bounds.x + bounds.width, bounds.y + bounds.height / 2.0f));
        }

/**
 * Set vertical gradient fill across a rectangle
 */
        void SetVerticalGradientFill(const Color& topColor, const Color& bottomColor,
                                     const Rect2Df& bounds) {
            SetFillGradient(topColor, bottomColor,
                            Point2Df(bounds.x + bounds.width / 2.0f, bounds.y),
                            Point2Df(bounds.x + bounds.width / 2.0f, bounds.y + bounds.height));
        }

        virtual void SetStrokeWidth(float width) {
            DrawingStyle style = GetDrawingStyle();
            style.strokeWidth = width;
            style.hasStroke = (width > 0);
            SetDrawingStyle(style);
        }

        virtual void SetFont(const std::string& fontFamily, float fontSize) {
            TextStyle style = GetTextStyle();
            style.fontFamily = fontFamily;
            style.fontSize = fontSize;
            SetTextStyle(style);
        }

        virtual void SetTextColor(const Color& color) {
            TextStyle style = GetTextStyle();
            style.textColor = color;
            SetTextStyle(style);
        }

        virtual void SetTextAlign(TextAlign align) {
            TextStyle style = GetTextStyle();
            style.alignment = align;
            SetTextStyle(style);
        }

        void DrawLine(const Point2Df& start, const Point2Df& end) {
            DrawLine(start.x, start.y, end.x, end.y);
        }

        void DrawLine(const Point2Di& start, const Point2Di& end) {
            DrawLine(static_cast<float>(start.x), static_cast<float>(start.y), static_cast<float>(end.x), static_cast<float>(end.y));
        }

        void DrawLine(const Point2Df& start, const Point2Df& end, const Color &col) {
            DrawLine(start.x, start.y, end.x, end.y);
        }

        void DrawLine(float start_x, float start_y, float end_x, float end_y, const Color &col) {
            SetStrokeColor(col);
            DrawLine(start_x, start_y, end_x, end_y);
        }

        void DrawLine(int start_x, int start_y, int end_x, int end_y, const Color &col) {
            SetStrokeColor(col);
            DrawLine(static_cast<float>(start_x), static_cast<float>(start_y), static_cast<float>(end_x), static_cast<float>(end_y));
        }

        void DrawRectangle(int x, int y, int w, int h) {
            DrawRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
        }
        void DrawRectangle(const Rect2Df& rect) {
            DrawRectangle(rect.x, rect.y, rect.width, rect.height);
        }

        void DrawRectangle(const Rect2Di& rect) {
            DrawRectangle(rect.x, rect.y, rect.width, rect.height);
        }


        void FillRectangle(int x, int y, int w, int h) {
            FillRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
        }

        void FillRectangle(const Rect2Df& rect) {
            FillRectangle(rect.x, rect.y, rect.width, rect.height);
        }

        void FillRectangle(const Rect2Di& rect) {
            FillRectangle(rect.x, rect.y, rect.width, rect.height);
        }


        void DrawRoundedRectangle(int x, int y, int w, int h, float radius) {
            DrawRoundedRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), radius);
        }

        void DrawRoundedRectangle(const Rect2Df& rect, float radius) {
            DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
        }

        void DrawRoundedRectangle(const Rect2Di& rect, float radius) {
            DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
        }

        void FillRoundedRectangle(int x, int y, int w, int h, float radius) {
            FillRoundedRectangle(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h), radius);
        }

        void FillRoundedRectangle(const Rect2Df& rect, float radius) {
            FillRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
        }

        void FillRoundedRectangle(const Rect2Di& rect, float radius) {
            FillRoundedRectangle(rect.x, rect.y, rect.width, rect.height, radius);
        }

        void DrawCircle(int x, int y, float radius) {
            DrawCircle(static_cast<float>(x), static_cast<float>(y), radius);
        }
        void DrawCircle(const Point2Df& center, float radius) {
            DrawCircle(center.x, center.y, radius);
        }
        void DrawCircle(const Point2Di& center, float radius) {
            DrawCircle(center.x, center.y, radius);
        }

        void FillCircle(int x, int y, float radius) {
            FillCircle(static_cast<float>(x), static_cast<float>(y), radius);
        }
        void FillCircle(const Point2Df& center, float radius) {
            FillCircle(center.x, center.y, radius);
        }
        void FillCircle(const Point2Di& center, float radius) {
            FillCircle(center.x, center.y, radius);
        }

        void DrawText(const std::string& text, int x, int y) {
            DrawText(text, static_cast<float>(x), static_cast<float>(y));
        }
        void DrawText(const std::string& text, const Point2Df& position) {
            DrawText(text, position.x, position.y);
        }
        void DrawText(const std::string& text, const Point2Di& position) {
            DrawText(text, position.x, position.y);
        }

        void DrawImage(const std::string& imagePath, int x, int y) {
            DrawImage(imagePath, static_cast<float>(x), static_cast<float>(y));
        }
        void DrawImage(const std::string& imagePath, const Point2Df& position) {
            DrawImage(imagePath, position.x, position.y);
        }
        void DrawImage(const std::string& imagePath, const Point2Di& position) {
            DrawImage(imagePath, position.x, position.y);
        }

        void DrawImage(const std::string& imagePath, int x, int y, int w, int h) {
            DrawImage(imagePath, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
        }
        void DrawImage(const std::string& imagePath, const Rect2Df& position) {
            DrawImage(imagePath, position.x, position.y, position.width, position.height);
        }
        void DrawImage(const std::string& imagePath, const Rect2Di& position) {
            DrawImage(imagePath, position.x, position.y, position.width, position.height);;
        }

        void SetClipRect(int x, int y, int w, int h) {
            SetClipRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
        }
        void SetClipRect(const Rect2Df& rect) {
            SetClipRect(rect.x, rect.y, rect.width, rect.height);
        }
        void SetClipRect(const Rect2Di& rect) {
            SetClipRect(rect.x, rect.y, rect.width, rect.height);
        }

        void IntersectClipRect(int x, int y, int w, int h) {
            IntersectClipRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
        }
        void IntersectClipRect(const Rect2Df& rect) {
            IntersectClipRect(rect.x, rect.y, rect.width, rect.height);
        }
        void IntersectClipRect(const Rect2Di& rect) {
            IntersectClipRect(rect.x, rect.y, rect.width, rect.height);
        }

        Point2Di MeasureText(const std::string& text) {
            Point2Di p = {0, 0};
            MeasureText(text, p.x, p.y);
            return p;
        }

        Point2Df CalculateCenteredTextPosition(const std::string& text, const Rect2Df& bounds) {
            int txt_w, txt_h;
            MeasureText(text, txt_w, txt_h);
            return Point2Df(
                    bounds.x + (bounds.width - static_cast<float>(txt_w)) / 2,     // Center horizontally
                    bounds.y + (bounds.height - static_cast<float>(txt_h)) / 2   // Center vertically (baseline adjusted)
            );
        }

        // ===== ALTERNATIVE: USE DRAWTEXT WITH RECTANGLE =====
        //void DrawCenteredText(const std::string& text, const Rect2Df& bounds) {
        //    // Set text style for centering
        //    // Draw text in the center of the rectangle
        //    Point2Df center(bounds.x + bounds.width / 2, bounds.y + bounds.height / 2);
        //    DrawText(text, center);
        //}

        void DrawTextInRect(const std::string& text, const Rect2Di& bounds) {
            DrawTextInRect(text, static_cast<float>(bounds.x), static_cast<float>(bounds.y), static_cast<float>(bounds.width), static_cast<float>(bounds.height));
        }

        void DrawTextInRect(const std::string& text, const Rect2Df& bounds) {
            DrawTextInRect(text, bounds.x, bounds.y, bounds.width, bounds.height);
        }

        // Draw filled rectangle with border
        void DrawFilledRectangle(const Rect2Df& rect, const Color& fillColor,
                                        const Color& borderColor = Colors::Transparent, float borderWidth = 1.0f, float borderRadius = 0.0f) {
            PushState();
            // Draw fill background
            if (fillColor.a > 0) {
                SetFillColor(fillColor);
                FillRectangle(rect);
            }

            // Draw border
            if (borderColor.a > 0 && borderWidth > 0) {
                SetStrokeColor(borderColor);
                SetStrokeWidth(borderWidth);
                DrawRectangle(rect);  // <-- FIXED: Use DrawRectangle for stroke only
            }
            PopState();
        }

        void DrawFilledRectangle(const Rect2Di& rect, const Color& fillColor,
                                        const Color& borderColor = Colors::Transparent, float borderWidth = 1.0f, float borderRadius = 0.0f) {
            DrawFilledRectangle(Rect2Df(rect.x, rect.y, rect.width, rect.height), fillColor, borderColor, borderWidth, borderRadius);
        }

        void DrawFilledCircle(const Point2Df& center, float radius, const Color& fillColor) {
            if (fillColor.a > 0) {
                PushState();
                SetFillColor(fillColor);
                DrawingStyle style = GetDrawingStyle();
                style.hasStroke = false;
                SetDrawingStyle(style);
                DrawCircle(center, radius);
                PopState();
            }
        }

        void DrawFilledCircle(const Point2Di& center, float radius, const Color& fillColor) {
            DrawFilledCircle(Point2Df(center.x, center.y), radius, fillColor);
        }

// Draw text with background
        void DrawTextWithBackground(const std::string& text, const Point2Df& position,
                                           const Color& textColor, const Color& backgroundColor = Colors::Transparent) {
            PushState();
            if (backgroundColor.a > 0) {
                int txt_w, txt_h;
                MeasureText(text, txt_w, txt_h);
                DrawFilledRectangle(Rect2Df(position.x, position.y, txt_w, txt_h), backgroundColor);
            }

            SetTextColor(textColor);
            DrawText(text, position);
            PopState();
        }

// Draw gradient rectangle
        void DrawGradientRect(const Rect2Df& rect, const Color& startColor, const Color& endColor,
                                     bool horizontal = true) {
            PushState();

            DrawingStyle style = GetDrawingStyle();
            style.fillMode = FillMode::Gradient;
            style.fillGradient.type = GradientType::Linear;
            style.fillGradient.startPoint = Point2Df(rect.x, rect.y);
            style.fillGradient.endPoint = horizontal ?
                                          Point2Df(rect.x + rect.width, rect.y) :
                                          Point2Df(rect.x, rect.y + rect.height);
            style.fillGradient.stops.clear();
            style.fillGradient.stops.emplace_back(0.0f, startColor);
            style.fillGradient.stops.emplace_back(1.0f, endColor);
            style.hasStroke = false;

            SetDrawingStyle(style);
            DrawRectangle(rect);

            PopState();
        }

// Draw shadow
        void DrawShadow(const Rect2Df& rect, const Color& shadowColor = Color(0, 0, 0, 64),
                               const Point2Df& offset = Point2Df(2, 2)) {
            PushState();

            Rect2Df shadowRect(rect.x + offset.x, rect.y + offset.y, rect.width, rect.height);
            SetFillColor(shadowColor);
            DrawingStyle style = GetDrawingStyle();
            style.hasStroke = false;
            SetDrawingStyle(style);
            DrawRectangle(shadowRect);

            PopState();
        }

        void DrawShadow(const Rect2Di& rect, const Color& shadowColor = Color(0, 0, 0, 64),
                               const Point2Di& offset = Point2Di(2, 2)) {
            DrawShadow(Rect2Df(rect.x, rect.y, rect.width, rect.height),
                       shadowColor, Point2Df (offset.x, offset.y));
        }
    };
} // namespace UltraCanvas