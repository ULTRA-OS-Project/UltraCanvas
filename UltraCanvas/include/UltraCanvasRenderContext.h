// include/UltraCanvasRenderContext.h
// Cross-platform rendering interface with improved context management
// Version: 2.5.0
// Last Modified: 2026-04-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasImage.h"
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <thread>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <mutex>
#include <stack>

namespace UltraCanvas {
    class ITextLayout;
// ===== GRADIENT STRUCTURES =====
    struct GradientStop {
        double position;    // 0.0 to 1.0
        Color color;

        GradientStop(double pos = 0.0f, const Color& col = Colors::Black)
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
        double radius1, radius2;  // For radial gradients
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

    enum class EllipsizeMode {
        EllipsizeNone,
        EllipsizeStart,
        EllipsizeMiddle,
        EllipsizeEnd
    };

    struct FontStyle {
        std::string fontFamily;             // Empty = use system default font
        double fontSize = 12.0f;
        FontWeight fontWeight = FontWeight::Normal;
        FontSlant fontSlant = FontSlant::Normal;
    };

    struct TextStyle {
        TextAlignment alignment = TextAlignment::Left;
        VerticalAlignment verticalAlignment = VerticalAlignment::Top;
        double lineHeight = 1.2f;
        double letterSpacing = 0.0f;
        int indent = 0;
        TextWrap wrap = TextWrap::WrapWordChar;
        bool isMarkup = false;
    };

// ===== RENDERING STATE =====
    struct RenderState {
        FontStyle fontStyle;
        TextStyle textStyle;
        Point2Df translation;
        double rotation = 0.0f;
        Point2Df scale = Point2Df(1.0f, 1.0f);
        double globalAlpha = 1.0f;

        std::shared_ptr<IPaintPattern> fillSourcePattern = nullptr;
        std::shared_ptr<IPaintPattern> strokeSourcePattern = nullptr;
        std::shared_ptr<IPaintPattern> textSourcePattern = nullptr;
//        std::shared_ptr<IPaintPattern> currentSourcePattern = nullptr;
        Color fillSourceColor = Colors::Transparent;
        Color strokeSourceColor = Colors::Transparent;
        Color textSourceColor = Colors::Transparent;
//        Color currentSourceColor = Colors::Transparent;
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
        virtual void Translate(double x, double y) = 0;
        virtual void Rotate(double angle) = 0;
        virtual void Scale(double sx, double sy) = 0;
        virtual void SetTransform(double a, double b, double c, double d, double e, double f) = 0; // set matrix to
        virtual void Transform(double a, double b, double c, double d, double e, double f) = 0; // adjust current matrix by this one
        virtual void ResetTransform() = 0;

        // ===== CLIPPING =====
        virtual void ClearClipRect() = 0;
        virtual void ClipRect(const Rect2Df& rect) = 0;
        virtual void ClipPath() = 0;
        virtual void ClipRoundedRectangle(
                const Rect2Df& rect,
                double borderTopLeftRadius, double borderTopRightRadius,
                double borderBottomRightRadius, double borderBottomLeftRadius) = 0;
//        virtual Rect2Df GetClipRect() const = 0;

        // ===== BASIC SHAPES =====
        virtual void DrawLine(const Point2Df& from, const Point2Df& to) = 0;
        virtual void DrawRectangle(const Rect2Df& rect) = 0;
        virtual void FillRectangle(const Rect2Df& rect) = 0;
        virtual void DrawRoundedRectangle(const Rect2Df & rect, double radius) = 0;
        virtual void FillRoundedRectangle(const Rect2Df & rect, double radius) = 0;
        virtual void DrawRoundedRectangleWidthBorders(const Rect2Df & rect,
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
                                                      const UCDashPattern& borderBottomPattern) = 0;
        virtual void DrawCircle(const Point2Df& center, double radius) = 0;
        virtual void FillCircle(const Point2Df& center, double radius) = 0;
        virtual void DrawEllipse(const Rect2Df& rect) = 0;
        virtual void FillEllipse(const Rect2Df& rect) = 0;
        virtual void DrawArc(double x, double y, double radius, double startAngle, double endAngle) = 0;
        virtual void FillArc(double x, double y, double radius, double startAngle, double endAngle) = 0;

        virtual void DrawBezierCurve(const Point2Df& start, const Point2Df& cp1, const Point2Df& cp2, const Point2Df& end) = 0;
        virtual void DrawLinePath(const std::vector<Point2Df>& points, bool closePath) = 0;
        virtual void FillLinePath(const std::vector<Point2Df>& points) = 0;

        // PATH functions
        virtual void ClearPath() = 0;
        virtual void ClosePath() = 0;
        virtual void MoveTo(double x, double y) = 0;
        virtual void RelMoveTo(double x, double y) = 0;
        virtual void LineTo(double x, double y) = 0;
        virtual void RelLineTo(double x, double y) = 0;
        virtual void QuadraticCurveTo(double cpx, double cpy, double x, double y) = 0;
        virtual void BezierCurveTo(double cp1x, double cp1y, double cp2x, double cp2y, double x, double y) = 0;
        virtual void RelBezierCurveTo(double cp1x, double cp1y, double cp2x, double cp2y, double x, double y) = 0;
        virtual void Arc(double cx, double cy, double radius, double startAngle, double endAngle) = 0;
        virtual void ArcTo(double x1, double y1, double x2, double y2, double radius) = 0;
        virtual void Circle(double x, double y, double radius) = 0;
        virtual void Ellipse(double cx, double cy, double rx, double ry, double rotation) = 0;
        virtual void Rect(double x, double y, double width, double height) = 0;
        virtual void RoundedRect(double x, double y, double width, double height, double radius) = 0;

        virtual void FillPathPreserve() = 0;
        virtual void StrokePathPreserve() = 0;
        virtual Rect2Df GetPathExtents() = 0;

        // === Gradient Methods ===
        virtual std::shared_ptr<IPaintPattern> CreateLinearGradientPattern(double x1, double y1, double x2, double y2,
                                                                           const std::vector<GradientStop>& stops) = 0;
        virtual std::shared_ptr<IPaintPattern> CreateRadialGradientPattern(double cx1, double cy1, double r1,
                                                                           double cx2, double cy2, double r2,
                                                                           const std::vector<GradientStop>& stops) = 0;
        virtual void SetFillPaint(std::shared_ptr<IPaintPattern> pattern) = 0;
        virtual void SetFillPaint(const Color& color) = 0;
        virtual void SetStrokePaint(std::shared_ptr<IPaintPattern> pattern) = 0;
        virtual void SetStrokePaint(const Color& color) = 0;
        virtual void SetTextPaint(std::shared_ptr<IPaintPattern> pattern) = 0;
        virtual void SetTextPaint(const Color& color) = 0;

        virtual void SetCurrentPaint(const Color& color) = 0;
        virtual void SetCurrentPaint(std::shared_ptr<IPaintPattern> pattern) = 0;

        virtual void Fill() = 0;
        virtual void Stroke() = 0;


        // ===== STYLE MANAGEMENT =====
//        virtual void SetDrawingStyle(const DrawingStyle& style) = 0;
        virtual void SetAlpha(double alpha) = 0;
        virtual double GetAlpha() const = 0;
//        virtual const DrawingStyle& GetDrawingStyle() const = 0;

        // === Style Methods ===
        virtual void SetStrokeWidth(double width) = 0;
        virtual void SetLineCap(LineCap cap) = 0;
        virtual void SetLineJoin(LineJoin join) = 0;
        virtual void SetMiterLimit(double limit) = 0;
        virtual void SetLineDash(const UCDashPattern& pattern) = 0;

        // === Text Methods ===
        virtual std::unique_ptr<ITextLayout> CreateTextLayout(const std::string& text, bool isMarkup) = 0;
        std::unique_ptr<ITextLayout> CreateTextLayout() {
            return CreateTextLayout("", false);
        };
        virtual std::shared_ptr<ITextLayout> GetOrCreateTextLayout(const std::string& text, const Size2Di& sz, bool isMarkup) = 0;

        virtual void DrawTextLayout(ITextLayout &layout, const Point2Df &pos) = 0;


        virtual void SetFontFace(const std::string& family, FontWeight fw, FontSlant fs) = 0;
        virtual void SetFontFamily(const std::string& family) = 0;
        virtual void SetFontSize(double size) = 0;
        virtual void SetFontWeight(FontWeight fw) = 0;
        virtual void SetFontSlant(FontSlant fs) = 0;
        virtual void SetTextLineHeight(double height) = 0;
        virtual void SetTextWrap(TextWrap wrap) = 0;

        void SetFontStyle(const FontStyle& style) {
            SetFontFace(style.fontFamily, style.fontWeight, style.fontSlant);
            SetFontSize(style.fontSize);
        }

        virtual const TextStyle& GetTextStyle() const = 0;
        virtual void SetTextStyle(const TextStyle& style) = 0;
        virtual void SetTextAlignment(TextAlignment align) = 0;
        virtual void SetTextVerticalAlignment(VerticalAlignment align) = 0;
        virtual void SetTextIsMarkup(bool isMarkup) = 0;

        virtual void FillText(const std::string& text, double x, double y) = 0;
        virtual void StrokeText(const std::string& text, double x, double y) = 0;

        // === Transform Methods ===

        // ===== TEXT RENDERING =====
        virtual void DrawText(const std::string &text, const Point2Df &pos) = 0;
        virtual void DrawTextInRect(const std::string &text, const Rect2Df &rect) = 0;
        virtual Size2Di GetTextDimensions(const std::string &text, const Size2Di& explicitSize) = 0;
        Size2Di GetTextLineDimensions(const std::string& text) { return GetTextDimensions(text, {0, 0}); };
        int GetTextLineWidth(const std::string& text) { return GetTextLineDimensions(text).width; };
        int GetTextLineHeight(const std::string& text) { return GetTextLineDimensions(text).height; };

        virtual int GetTextIndexForXY(const std::string &text, int x, int y, int w = 0, int h = 0) = 0;

        // ===== IMAGE RENDERING =====
        virtual void DrawPartOfPixmap(UCPixmap& pixmap, const Rect2Df& srcRect, const Rect2Df& destRect) = 0;
        virtual void DrawPixmap(UCPixmap& pixmap, const Rect2Df& rect, ImageFitMode fitMode) = 0;
        // DrawMasked used mainly for B/W icons to replace non-transparent areas by specfied color
        virtual void DrawMask(const Color& drawColor, UCPixmap& mask, const Rect2Df& rect, ImageFitMode fitMode) = 0;

        virtual void Clear(const Color& color) = 0;

        // ===== UTILITY FUNCTIONS =====
        virtual void* GetNativeContext() = 0;

        void DrawLine(const Point2Df& start, const Point2Df& end, const Color &col) {
            SetStrokePaint(col);
            DrawLine(start, end);
        }

        void DrawImage(UCImage& img, const Point2Df& pos) {
            auto pixmap = img.GetPixmap();
            if (pixmap) {
                DrawPixmap(*pixmap,
                    Rect2Df(pos.x, pos.y, pixmap->GetWidth(), pixmap->GetHeight()),
                    ImageFitMode::NoScale);
            }
        }

        void DrawImage(const std::string& imagePath, const Point2Df& pos) {
            auto img = UCImage::Get(imagePath);
            if (img) {
                DrawImage(*img, pos);
            }
        }

        void DrawImage(UCImage& img, const Rect2Df& rect, ImageFitMode fitMode) {
            auto pixmap = img.GetPixmap(static_cast<int>(rect.width),
                                        static_cast<int>(rect.height), fitMode);
            if (pixmap) {
                DrawPixmap(*pixmap, rect, fitMode);
            }
        }
        void DrawImage(const std::string& imagePath, const Rect2Df& rect, ImageFitMode fitMode) {
            auto img = UCImage::Get(imagePath);
            if (img) {
                DrawImage(*img, rect, fitMode);
            }
        }

        void DrawMask(const Color& drawColor, UCImage& img, const Point2Df& pos) {
            auto pixmap = img.GetPixmap();
            if (pixmap) {
                DrawMask(drawColor, *pixmap, 
                    Rect2Df(pos.x, pos.y, pixmap->GetWidth(), pixmap->GetHeight()),
                    ImageFitMode::NoScale);
            }
        }

        void DrawMask(const Color& drawColor, const std::string& imagePath, const Point2Df& pos) {
            auto img = UCImage::Get(imagePath);
            if (img) {
                DrawMask(drawColor, *img, pos);
            }
        }
        
        void DrawMask(const Color& drawColor, UCImage& img, const Rect2Df& rect, ImageFitMode fitMode) {
            auto pixmap = img.GetPixmap(static_cast<int>(rect.width),
                                        static_cast<int>(rect.height), fitMode);
            if (pixmap) {
                DrawMask(drawColor, *pixmap.get(), rect, fitMode);
            }
        }

        void DrawMask(const Color& drawColor, const std::string& imagePath, const Rect2Df& rect, ImageFitMode fitMode) {
            auto img = UCImage::Get(imagePath);
            if (img) {
                DrawMask(drawColor, *img, rect, fitMode);
            }
        }
        
        void DrawPartOfImage(const std::string& imagePath, const Rect2Df& srcRect, const Rect2Df& destRect) {
        
            auto img = UCImage::Get(imagePath);
            DrawPartOfImage(*img.get(), srcRect, destRect);
        }
        
        void DrawPartOfImage(UCImage& img, const Rect2Df& srcRect, const Rect2Df& destRect) {
            auto pixmap = img.GetPixmap();
            DrawPartOfPixmap(*pixmap.get(), srcRect, destRect);
        }

        Point2Di GetTextDimension(const std::string& text) {
            Size2Di sz = GetTextLineDimensions(text);
            return Point2Di(sz.width, sz.height);
        }

        Point2Df CalculateCenteredTextPosition(const std::string& text, const Rect2Df& bounds) {
            Size2Di sz = GetTextLineDimensions(text);
            return Point2Df(
                    bounds.x + (bounds.width - static_cast<float>(sz.width)) / 2,     // Center horizontally
                    bounds.y + (bounds.height - static_cast<float>(sz.height)) / 2   // Center vertically (baseline adjusted)
            );
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

        void DrawFilledCircle(const Point2Df& center, float radius, const Color& fillColor, const Color& borderColor = Colors::Transparent, float borderWidth = 1.0f) {
            PushState();
            ClearPath();
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

    };

    /* TEXT LAYOUT */
    enum class UCUnderlineType {
        UnderlineNone,
        UnderlineSingle,
        UnderlineDouble,
        UnderlineLow,
        UnderlineError
    };

    enum class UCFontVariant {
        VariantNormal,
        VariantSmallCaps
    };

    enum class UCFontStretch {
        UltraCondensed,
        ExtraCondensed,
        Condensed,
        SemiCondensed,
        Normal,
        SemiExpanded,
        Expanded,
        ExtraExpanded,
        UltraExpanded
    };

    enum class TextAttributeType {
        INVALID = 0,           /* 0 is an invalid attribute type */
        LANGUAGE = 1,          /* PangoAttrLanguage */
        FONT_FAMILY = 2,            /* PangoAttrString */
        FONT_STYLE = 3,             /* PangoAttrInt */
        FONT_WEIGHT = 4,            /* PangoAttrInt */
        FONT_VARIANT = 5,           /* PangoAttrInt */
        FONT_STRETCH = 6,           /* PangoAttrInt */
        FONT_SIZE = 7,              /* PangoAttrSize */
        FONT_DESC = 8,         /* PangoAttrFontDesc */
        FOREGROUND = 9,        /* PangoAttrColor */
        BACKGROUND = 10,        /* PangoAttrColor */
        UNDERLINE = 11,         /* PangoAttrInt */
        STRIKETHROUGH = 12,     /* PangoAttrInt */
        RISE = 13,              /* PangoAttrInt */
        SHAPE = 14,             /* PangoAttrShape */
        SCALE = 15,             /* PangoAttrFloat */
        FALLBACK = 16,          /* PangoAttrInt */
        LETTER_SPACING = 17,    /* PangoAttrInt */
        UNDERLINE_COLOR = 18,   /* PangoAttrColor */
        STRIKETHROUGH_COLOR = 19,/* PangoAttrColor */
        ABSOLUTE_SIZE = 20,     /* PangoAttrSize */
        GRAVITY = 21,           /* PangoAttrInt */
        GRAVITY_HINT = 22,      /* PangoAttrInt */
        FONT_FEATURES = 23,     /* PangoAttrFontFeatures */
        FOREGROUND_ALPHA = 24,  /* PangoAttrInt */
        BACKGROUND_ALPHA = 25,  /* PangoAttrInt */
        ALLOW_BREAKS = 26,      /* PangoAttrInt */
        SHOW_INVISIBLE = 27,              /* PangoAttrInt */
        INSERT_HYPHENS = 28,    /* PangoAttrInt */
        OVERLINE = 29,          /* PangoAttrInt */
        OVERLINE_COLOR = 30,    /* PangoAttrColor */
        LINE_HEIGHT = 31,       /* PangoAttrFloat */
        ABSOLUTE_LINE_HEIGHT = 32, /* PangoAttrInt */
        TEXT_TRANSFORM = 33,    /* PangoAttrInt */
        IS_WORD = 34,              /* PangoAttrInt */
        IS_SENTENCE = 35,          /* PangoAttrInt */
        BASELINE_SHIFT = 36,    /* PangoAttrSize */
        FONT_SCALE = 37,        /* PangoAttrInt */
    };

    enum class UCLayoutTabAlignment {
        TabLeft = 0,
        TabRight = 1,
        TabCenter = 2,
        TabDecimal = 3
    };

    // ===== RESULT STRUCTS =====

    struct UCLayoutExtents {
        Rect2Di ink;
        Rect2Di logical;
    };

    struct UCLayoutHitResult {
        int index;
        int trailing;
        bool inside;
    };

    struct UCCursorPos {
        Rect2Di strongPos;
        Rect2Di weakPos;
    };

    struct UCLayoutLineXPos {
        int line;
        int xPos;
    };

    struct UCCursorMoveResult {
        int newIndex;
        int newTrailing;
    };

    struct UCLayoutTabPos {
        UCLayoutTabAlignment align = UCLayoutTabAlignment::TabLeft;
        int xPos = 0;
    };

    struct LayoutLineRange {
        int startByte;   // byte offset within layout text
        int lengthBytes; // length in bytes
    };

    // ===== UCTextAttribute =====

    class ITextAttribute {
    public:
        virtual ITextAttribute& SetRange(int startIndex, int endIndex) = 0;
        virtual void *Release() = 0;
        virtual TextAttributeType GetType() = 0;
        virtual ~ITextAttribute() = default;
    };
        // ===== FACTORY METHODS =====
    namespace TextAttributeFactory {
        // Font description (from UltraCanvas FontStyle)
        std::unique_ptr<ITextAttribute> CreateFontStyle(const FontStyle& fontStyle);
        // Font description (from raw PangoFontDescription)
        std::unique_ptr<ITextAttribute> CreateFontDescFromPango(const PangoFontDescription* desc);

        // Font family
        std::unique_ptr<ITextAttribute> CreateFamily(const std::string& family);

        // Font size in points (internally converted to Pango units)
        std::unique_ptr<ITextAttribute> CreateSize(float sizeInPoints);

        // Absolute font size in pixels (internally converted to Pango units)
        std::unique_ptr<ITextAttribute> CreateAbsoluteSize(float sizeInPixels);

        // Weight
        std::unique_ptr<ITextAttribute> CreateWeight(FontWeight weight);

        // Style / slant
        std::unique_ptr<ITextAttribute> CreateStyle(FontSlant slant);

        // Variant
        std::unique_ptr<ITextAttribute> CreateVariant(UCFontVariant variant);

        // Stretch
        std::unique_ptr<ITextAttribute> CreateStretch(UCFontStretch stretch);

        // Underline
        std::unique_ptr<ITextAttribute> CreateUnderline(UCUnderlineType type);
        std::unique_ptr<ITextAttribute> CreateUnderlineColor(const Color& color);

        // Strikethrough
        std::unique_ptr<ITextAttribute> CreateStrikethrough(bool enabled);
        std::unique_ptr<ITextAttribute> CreateStrikethroughColor(const Color& color);

        // Colors
        std::unique_ptr<ITextAttribute> CreateForeground(const Color& color);
        std::unique_ptr<ITextAttribute> CreateBackground(const Color& color);

        // Alpha (0-65535)
        std::unique_ptr<ITextAttribute> CreateForegroundAlpha(uint16_t alpha);
        std::unique_ptr<ITextAttribute> CreateBackgroundAlpha(uint16_t alpha);

        // Letter spacing in pixels (internally converted to Pango units)
        std::unique_ptr<ITextAttribute> CreateLetterSpacing(int spacingInPixels);

        // Rise (superscript/subscript displacement) in pixels
        std::unique_ptr<ITextAttribute> CreateRise(int riseInPixels);

        // Scale factor (e.g. 0.5 for half, 2.0 for double)
        std::unique_ptr<ITextAttribute> CreateScale(double scaleFactor);

        // Fallback font
        std::unique_ptr<ITextAttribute> CreateFallback(bool enable);

        // Language tag (e.g. "en-US")
        std::unique_ptr<ITextAttribute> CreateLanguage(const std::string& lang);

        // spacer (width in pixels)
        std::unique_ptr<ITextAttribute> CreateShapeSpacer(double width);

        // absolute line height, may be used with CreateShapeSpacer to preserve space
        std::unique_ptr<ITextAttribute> CreateAbsoluteLineHeight(double lineHeight);
    };

    // ===== UCTextAttributeList =====

//    class UCTextAttributeList {
//    private:
//        PangoAttrList* attrList = nullptr;
//
//    public:
//        UCTextAttributeList();
//        ~UCTextAttributeList();
//
//        // Move-only
//        UCTextAttributeList(UCTextAttributeList&& other) noexcept;
//        UCTextAttributeList& operator=(UCTextAttributeList&& other) noexcept;
//        UCTextAttributeList(const UCTextAttributeList&) = delete;
//        UCTextAttributeList& operator=(const UCTextAttributeList&) = delete;
//
//        // Insert attribute (takes ownership from UCTextAttribute)
//        void Insert(UCTextAttribute& attr);
//        void InsertBefore(UCTextAttribute& attr);
//        void Change(UCTextAttribute& attr);
//
//        PangoAttrList* GetHandle() const;
//        bool IsValid() const;
//
//        // Serialize to string representation
//        std::string ToString() const;
//        // Create from string representation (replaces current list)
//        static UCTextAttributeList FromString(const std::string& str);
//        // Filter attributes, returns a new list containing attributes for which
//        // the predicate returns true. Filtered attributes are removed from this list.
//        UCTextAttributeList Filter(std::function<bool(const PangoAttribute*)> predicate);
//    };

    // ===== UCTextLayout =====

    class ITextLayout {
    public:
        virtual ~ITextLayout() = default;
//        virtual ITextLayout(ITextLayout&& other) noexcept = 0;
//        virtual ITextLayout& operator=(ITextLayout&& other) noexcept = 0;

        virtual bool IsValid() const = 0;
        virtual void* GetHandle() const = 0;

        // ===== DIMENSIONS (pixel API) =====
        virtual void SetExplicitWidth(int widthPixels) = 0;       // -1 to unset
        virtual void SetExplicitHeight(int heightPixels) = 0;     // -1 to unset
        virtual int GetExplicitWidth() const = 0;       // -1 to unset
        virtual int GetExplicitHeight() const = 0;     // -1 to unset
        //virtual int GetLayoutVerticalOffset() const  = 0;

        // ===== TEXT CONTENT =====
        virtual void SetText(const std::string& text) = 0;
        virtual std::string GetText() const = 0;
        virtual void SetMarkup(const std::string& markup) = 0;

        // ===== FONT =====
        virtual void SetFontStyle(const FontStyle& fontStyle) = 0;

        // ===== ALIGNMENT & JUSTIFICATION =====
        virtual void SetAlignment(TextAlignment align) = 0;
        virtual TextAlignment GetAlignment() const = 0;
        virtual void SetVerticalAlignment(VerticalAlignment align) = 0;
        virtual VerticalAlignment GetVerticalAlignment() = 0;
        
        // ===== WRAPPING & ELLIPSIZATION =====
        virtual void SetWrap(TextWrap wrap) = 0;
        virtual TextWrap GetWrap() const = 0;
        virtual void SetEllipsize(EllipsizeMode mode) = 0;
        virtual EllipsizeMode GetEllipsize() const = 0;

        // ===== SPACING & INDENTATION (pixels) =====
        virtual void SetIndent(int indentPixels) = 0;
        virtual int GetIndent() const = 0;
        virtual void SetLineSpacing(float factor) = 0;
        virtual float GetLineSpacing() const = 0;
        virtual void SetSpacing(int spacingPixels) = 0;   // Inter-paragraph spacing
        virtual int GetSpacing() const = 0;

        // ===== MODE =====
        virtual void SetSingleParagraphMode(bool single) = 0;
        virtual bool GetSingleParagraphMode() const = 0;
        virtual void SetAutoDir(bool autoDir) = 0;
        virtual bool GetAutoDir() const = 0;

        // ===== ATTRIBUTES =====
        virtual void InsertAttribute(std::unique_ptr<ITextAttribute> attr) = 0;
        virtual void ChangeAttribute(std::unique_ptr<ITextAttribute> attr) = 0;
        virtual void SetAttributesFromString(const std::string& str) = 0;
        virtual void RemoveAllAttributes() = 0;
        virtual void UpdateAttributesAccordingToText(int textBytePos, int addedTextBytes, int removedTextBytes) = 0;
//        virtual void FilterOutAttributes(std::function<bool(const PangoAttribute*)> predicate) = 0;

        // ===== TABS =====
        virtual void ResetTabs() = 0;
        virtual void SetTabs(const std::vector<UCLayoutTabPos>& tabs) = 0;
        virtual std::vector<UCLayoutTabPos> GetTabs() = 0;

        // ===== MEASUREMENT =====
        virtual UCLayoutExtents GetLayoutExtents() = 0;
        Size2Di GetLayoutSize() { return GetLayoutExtents().logical.Size(); }
        int GetLayoutWidth() { return GetLayoutExtents().logical.width; }
        int GetLayoutHeight() { return GetLayoutExtents().logical.height; }
        virtual int GetLayoutVerticalOffset() = 0;

//        void GetSize(int& widthPangoUnits, int& heightPangoUnits) const = 0;
        virtual int GetBaseline() const = 0;
//        int GetBaselinePangoUnits() const = 0;
        virtual int GetLineCount() const = 0;

        // ===== HIT TESTING & POSITION =====
        virtual UCLayoutHitResult XYToIndex(int pixelX, int pixelY) const = 0;
        virtual Rect2Di IndexToPos(int byteIndex) const = 0;
        virtual UCLayoutLineXPos IndexToLineX(int byteIndex, bool trailing) const = 0;
        virtual UCCursorPos GetCursorPos(int byteIndex) const = 0;
        virtual UCCursorMoveResult MoveCursorVisually(bool strongCursor, int oldIndex,
                                              int oldTrailing, int direction) const = 0;

        // ===== LINE ACCESS =====
        virtual std::vector<LayoutLineRange> GetLineByteRanges() const = 0;

        // ===== ITERATOR =====
//        UCTextLayoutIter GetIter() const = 0;

    };
} // namespace UltraCanvas