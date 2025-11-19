// include/UltraCanvasRenderContext.h
// Cross-platform rendering interface with improved context management
// Version: 2.2.0
// Last Modified: 2025-07-11
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasImage.h"
#include <thread>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <mutex>
#include <stack>

namespace UltraCanvas {
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
        Solid,
        Gradient
    };

    enum class StrokeStyle {
        Solid,
        Dashed,
        Gradient,
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

    // pattern interface mainly to automatically destory patters
    class IPaintPattern {
    public:
        virtual ~IPaintPattern() = default;
        virtual void* GetHandle() = 0;
    };

    enum class TextVerticalAlignement {
        Top,
        Middle,
        Bottom,
        Baseline
    };

    enum class TextWrap {
        WrapNone,
        WrapWord,
        WrapChar,
        WrapWordChar
    };

    enum class FontWeight {
        Normal,
        Light,
        Bold,
        ExtraBold
    };

    enum class FontSlant {
        Normal,
        Italic,
        Oblique
    };

    struct FontStyle {
        std::string fontFamily = "Arial";
        float fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;
        FontSlant fontSlant = FontSlant::Normal;
    };

    struct TextStyle {
        TextAlignment alignment = TextAlignment::Left;
        TextVerticalAlignement verticalAlignement = TextVerticalAlignement::Baseline;
        Color textColor = Colors::Black;
        float lineHeight = 1.2f;
        float letterSpacing = 0.0f;
        float wordSpacing = 0.0f;

        // Text effects
        TextWrap wrap = TextWrap::WrapWordChar;
        bool isMarkup = false;
        Color outlineColor = Colors::Black;
        float outlineWidth = 1.0f;
    };

// ===== RENDERING STATE =====
    struct RenderState {
        FontStyle fontStyle;
        TextStyle textStyle;
        Point2Df translation;
        float rotation = 0.0f;
        Point2Df scale = Point2Df(1.0f, 1.0f);
        float globalAlpha = 1.0f;

        std::shared_ptr<IPaintPattern> fillSourcePattern = nullptr;
        std::shared_ptr<IPaintPattern> strokeSourcePattern = nullptr;
        std::shared_ptr<IPaintPattern> textSourcePattern = nullptr;
//        std::shared_ptr<IPaintPattern> currentSourcePattern = nullptr;
        Color fillSourceColor = Colors::Transparent;
        Color strokeSourceColor = Colors::Transparent;
        Color textSourceColor = Colors::Transparent;
//        Color currentSourceColor = Colors::Transparent;
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
        virtual void SetTransform(float a, float b, float c, float d, float e, float f) = 0; // set matrix to
        virtual void Transform(float a, float b, float c, float d, float e, float f) = 0; // adjust current matrix by this one
        virtual void ResetTransform() = 0;

        // ===== CLIPPING =====
//        virtual void SetClipRect(float x, float y, float w, float h) = 0;
        virtual void ClearClipRect() = 0;
        virtual void ClipRect(float x, float y, float w, float h) = 0;
        virtual void ClipPath() = 0;
        virtual void ClipRoundedRectangle(
                float x, float y, float width, float height,
                float borderTopLeftRadius, float borderTopRightRadius,
                float borderBottomRightRadius, float borderBottomLeftRadius) = 0;
//        virtual Rect2Df GetClipRect() const = 0;

        // ===== BASIC SHAPES =====
        virtual void DrawLine(float x, float y, float x1, float y1) = 0;
        virtual void DrawRectangle(float x, float y, float w, float h) = 0;
        virtual void FillRectangle(float x, float y, float w, float h) = 0;
        virtual void DrawRoundedRectangle(float x, float y, float w, float h, float radius) = 0;
        virtual void FillRoundedRectangle(float x, float y, float w, float h, float radius) = 0;
        virtual void DrawRoundedRectangleWidthBorders(float x, float y, float width, float height,
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
                                              const UCDashPattern& borderBottomPattern) = 0;
        virtual void DrawCircle(float x, float y, float radius) = 0;
        virtual void FillCircle(float x, float y, float radius) = 0;
        virtual void DrawEllipse(float x, float y, float w, float h) = 0;
        virtual void FillEllipse(float x, float y, float w, float h) = 0;
        virtual void DrawArc(float x, float y, float radius, float startAngle, float endAngle) = 0;
        virtual void FillArc(float x, float y, float radius, float startAngle, float endAngle) = 0;

        virtual void DrawBezierCurve(const Point2Df& start, const Point2Df& cp1, const Point2Df& cp2, const Point2Df& end) = 0;
        virtual void DrawLinePath(const std::vector<Point2Df>& points, bool closePath) = 0;
        virtual void FillLinePath(const std::vector<Point2Df>& points) = 0;

        // PATH functions
        virtual void ClearPath() = 0;
        virtual void ClosePath() = 0;
        virtual void MoveTo(float x, float y) = 0;
        virtual void RelMoveTo(float x, float y) = 0;
        virtual void LineTo(float x, float y) = 0;
        virtual void RelLineTo(float x, float y) = 0;
        virtual void QuadraticCurveTo(float cpx, float cpy, float x, float y) = 0;
        virtual void BezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) = 0;
        virtual void RelBezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) = 0;
        virtual void Arc(float cx, float cy, float radius, float startAngle, float endAngle) = 0;
        virtual void ArcTo(float x1, float y1, float x2, float y2, float radius) = 0;
        virtual void Circle(float x, float y, float radius) = 0;
        virtual void Ellipse(float cx, float cy, float rx, float ry, float rotation, float startAngle, float endAngle) = 0;
        virtual void Rect(float x, float y, float width, float height) = 0;
        virtual void RoundedRect(float x, float y, float width, float height, float radius) = 0;

        virtual void FillPathPreserve() = 0;
        virtual void StrokePathPreserve() = 0;
        virtual void GetPathExtents(float &x, float &y, float &width, float &height) = 0;

        // === Gradient Methods ===
        virtual std::shared_ptr<IPaintPattern> CreateLinearGradientPattern(float x1, float y1, float x2, float y2,
                                                                           const std::vector<GradientStop>& stops) = 0;
        virtual std::shared_ptr<IPaintPattern> CreateRadialGradientPattern(float cx1, float cy1, float r1,
                                                                           float cx2, float cy2, float r2,
                                                                           const std::vector<GradientStop>& stops) = 0;
        virtual void SetFillPaint(std::shared_ptr<IPaintPattern> pattern) = 0;
        virtual void SetFillPaint(const Color& color) = 0;
        virtual void SetStrokePaint(std::shared_ptr<IPaintPattern> pattern) = 0;
        virtual void SetStrokePaint(const Color& color) = 0;
        virtual void SetTextPaint(std::shared_ptr<IPaintPattern> pattern) = 0;
        virtual void SetTextPaint(const Color& color) = 0;
        virtual void Fill() = 0;
        virtual void Stroke() = 0;


        // ===== STYLE MANAGEMENT =====
//        virtual void SetDrawingStyle(const DrawingStyle& style) = 0;
        virtual void SetAlpha(float alpha) = 0;
        virtual float GetAlpha() const = 0;
//        virtual const DrawingStyle& GetDrawingStyle() const = 0;

        // === Style Methods ===
        virtual void SetStrokeWidth(float width) = 0;
        virtual void SetLineCap(LineCap cap) = 0;
        virtual void SetLineJoin(LineJoin join) = 0;
        virtual void SetMiterLimit(float limit) = 0;
        virtual void SetLineDash(const UCDashPattern& pattern) = 0;

        // === Text Methods ===
        virtual void SetFontFace(const std::string& family, FontWeight fw, FontSlant fs) = 0;
        virtual void SetFontSize(float size) = 0;
        virtual void SetFontWeight(FontWeight fw) = 0;
        virtual void SetFontSlant(FontSlant fs) = 0;
        virtual void SetTextLineHeight(float height) = 0;
        virtual void SetTextWrap(TextWrap wrap) = 0;

        void SetFontStyle(const FontStyle& style) {
            SetFontFace(style.fontFamily, style.fontWeight, style.fontSlant);
            SetFontSize(style.fontSize);
        }

        virtual const TextStyle& GetTextStyle() const = 0;
        virtual void SetTextStyle(const TextStyle& style) = 0;
        virtual void SetTextAlignment(TextAlignment align) = 0;
        virtual void SetTextIsMarkup(bool isMarkup) = 0;

        virtual void FillText(const std::string& text, float x, float y) = 0;
        virtual void StrokeText(const std::string& text, float x, float y) = 0;

        // === Transform Methods ===

        // ===== TEXT RENDERING =====
        virtual void DrawText(const std::string& text, float x, float y) = 0;
        virtual void DrawTextInRect(const std::string& text, float x, float y, float w, float h) = 0;
        virtual bool GetTextLineDimensions(const std::string& text, int& w, int& h) = 0;
        virtual bool GetTextDimensions(const std::string &text, int rectWidth, int rectHeight, int& retWidth, int &retHeight) = 0;

        int GetTextLineWidth(const std::string& text) {
            int w, h;
            GetTextLineDimensions(text, w, h);
            return w;
        };
        int GetTextLineHeight(const std::string& text) {
            int w, h;
            GetTextLineDimensions(text, w, h);
            return h;
        };

        virtual int GetTextIndexForXY(const std::string &text, int x, int y, int w = 0, int h = 0) = 0;

        // ===== IMAGE RENDERING =====
        virtual void DrawImage(const std::string& imagePath, float x, float y) = 0;
        virtual void DrawImage(const std::string &imagePath, float x, float y, float w, float h, ImageFitMode fitMode) = 0;
        virtual void DrawPartOfImage(const std::string& imagePath, const Rect2Df& srcRect, const Rect2Df& destRect) = 0;
        virtual void DrawImage(std::shared_ptr<UCImage> image, float x, float y) = 0;
        virtual void DrawImage(std::shared_ptr<UCImage> image, float x, float y, float w, float h, ImageFitMode fitMode) = 0;
        virtual void DrawPartOfImage(std::shared_ptr<UCImage> image, const Rect2Df& srcRect, const Rect2Df& destRect) = 0;

//        virtual bool IsImageFormatSupported(const std::string& filePath) = 0;
//        virtual bool GetImageDimensions(const std::string& imagePath, int& w, int& h) = 0;

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
            SetStrokePaint(col);
            DrawLine(start_x, start_y, end_x, end_y);
        }

        void DrawLine(int start_x, int start_y, int end_x, int end_y, const Color &col) {
            SetStrokePaint(col);
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

        void DrawEllipse(int x, int y, int w, int h) {
            DrawEllipse(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
        }
        void DrawEllipse(const Rect2Df& rect) {
            DrawEllipse(rect.x, rect.y, rect.width, rect.height);
        }

        void DrawEllipse(const Rect2Di& rect) {
            DrawEllipse(rect.x, rect.y, rect.width, rect.height);
        }


        void FillEllipse(int x, int y, int w, int h) {
            FillEllipse(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
        }

        void FillEllipse(const Rect2Df& rect) {
            FillEllipse(rect.x, rect.y, rect.width, rect.height);
        }

        void FillEllipse(const Rect2Di& rect) {
            FillEllipse(rect.x, rect.y, rect.width, rect.height);
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

        void DrawImage(const std::string& imagePath, const Point2Df& position) {
            DrawImage(imagePath, position.x, position.y);
        }
        void DrawImage(const std::string& imagePath, const Point2Di& position) {
            DrawImage(imagePath, position.x, position.y);
        }
        void DrawImage(std::shared_ptr<UCImage> image, const Rect2Di& rect, ImageFitMode fitMode) {
            DrawImage(image, rect.x, rect.y, rect.width, rect.height, fitMode);
        }

        void DrawImage(std::shared_ptr<UCImage> image, const Rect2Df& rect, ImageFitMode fitMode) {
            DrawImage(image, rect.x, rect.y, rect.width, rect.height, fitMode);
        }

//        void SetClipRect(int x, int y, int w, int h) {
//            SetClipRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
//        }
//        void SetClipRect(const Rect2Df& rect) {
//            SetClipRect(rect.x, rect.y, rect.width, rect.height);
//        }
//        void SetClipRect(const Rect2Di& rect) {
//            SetClipRect(rect.x, rect.y, rect.width, rect.height);
//        }

        void ClipRect(int x, int y, int w, int h) {
            ClipRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
        }
        void ClipRect(const Rect2Df& rect) {
            ClipRect(rect.x, rect.y, rect.width, rect.height);
        }
        void ClipRect(const Rect2Di& rect) {
            ClipRect(rect.x, rect.y, rect.width, rect.height);
        }

        Point2Di GetTextDimension(const std::string& text) {
            Point2Di p = {0, 0};
            GetTextLineDimensions(text, p.x, p.y);
            return p;
        }

        Point2Df CalculateCenteredTextPosition(const std::string& text, const Rect2Df& bounds) {
            int txt_w, txt_h;
            GetTextLineDimensions(text, txt_w, txt_h);
            return Point2Df(
                    bounds.x + (bounds.width - static_cast<float>(txt_w)) / 2,     // Center horizontally
                    bounds.y + (bounds.height - static_cast<float>(txt_h)) / 2   // Center vertically (baseline adjusted)
            );
        }

        // ===== ALTERNATIVE: USE DRAWTEXT WITH RECTANGLE =====
        void DrawTextInRect(const std::string& text, const Rect2Di& bounds) {
            DrawTextInRect(text, static_cast<float>(bounds.x), static_cast<float>(bounds.y), static_cast<float>(bounds.width), static_cast<float>(bounds.height));
        }

        void DrawTextInRect(const std::string& text, const Rect2Df& bounds) {
            DrawTextInRect(text, bounds.x, bounds.y, bounds.width, bounds.height);
        }

        // Draw filled rectangle with border
        void DrawFilledRectangle(const Rect2Df& rect, const Color& fillColor,
                        float borderWidth = 1.0f, const Color& borderColor = Colors::Transparent, float borderRadius = 0.0f) {

            if (fillColor.a == 0 && borderColor.a == 0) return;

            PushState();
            if (borderRadius > 0) {
                RoundedRect(rect.x, rect.y, rect.width, rect.height, borderRadius);
            } else {
                Rect(rect.x, rect.y, rect.width, rect.height);
            }
            if (fillColor.a > 0) {
                SetFillPaint(fillColor);
                FillPathPreserve();
            }
            if (borderWidth > 0 && borderColor.a > 0) {
                SetStrokePaint(borderColor);
                SetStrokeWidth(borderWidth);
                StrokePathPreserve();
            }
            ClearPath();
            PopState();
        }

        void DrawFilledRectangle(const Rect2Di& rect, const Color& fillColor,
                                float borderWidth = 0.0f,
                                const Color& borderColor = Colors::Transparent,
                                float borderRadius = 0.0f) {
            DrawFilledRectangle(Rect2Df(rect.x, rect.y, rect.width, rect.height), fillColor, borderWidth, borderColor, borderRadius);
        }

        void DrawFilledCircle(const Point2Df& center, float radius, const Color& fillColor, const Color& borderColor = Colors::Transparent, float borderWidth = 1.0f) {
            PushState();

            Circle(center.x, center.y, radius);
            if (fillColor.a > 0) {
                SetFillPaint(fillColor);
                FillPathPreserve();
            }
            if (borderWidth > 0) {
                SetStrokeWidth(borderWidth);
                SetStrokePaint(borderColor);
                StrokePathPreserve();
            }
            ClearPath();
            PopState();
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
                GetTextLineDimensions(text, txt_w, txt_h);
                DrawFilledRectangle(Rect2Df(position.x, position.y, txt_w, txt_h), backgroundColor);
            }

            SetTextPaint(textColor);
            DrawText(text, position);
            PopState();
        }
    };
} // namespace UltraCanvas