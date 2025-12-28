// Plugins/Vector/XAR/UltraCanvasXARPlugin.h
// Xara XAR vector graphics format plugin for UltraCanvas
// Version: 1.1.0
// Last Modified: 2025-06-22
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasGraphicsPluginSystem.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <stack>
#include <cstdint>
#include <functional>

namespace UltraCanvas {

// ===== XAR FORMAT CONSTANTS =====
    namespace XARConstants {
        // Magic bytes: "XARA" followed by 0xA3 0xA3 0x0D 0x0A
        constexpr uint32_t MAGIC_XARA = 0x41524158;  // "XARA" in little-endian
        constexpr uint32_t MAGIC_SIGNATURE = 0x0A0DA3A3;

        // Coordinate resolution: 72000 dpi (millipoints)
        constexpr float MILLIPOINTS_PER_INCH = 72000.0f;
        constexpr float MILLIPOINTS_TO_PIXELS = 72.0f / 72000.0f;  // Assuming 72 dpi screen
    }

// ===== XAR TAG DEFINITIONS =====
    enum class XARTag : uint32_t {
        // Navigation Records
        TAG_UP = 0,
        TAG_DOWN = 1,

        // File Structure Records
        TAG_FILEHEADER = 2,
        TAG_ENDOFFILE = 3,

        // Compression Records
        TAG_STARTCOMPRESSION = 10,
        TAG_ENDCOMPRESSION = 11,

        // Document Structure Records
        TAG_DOCUMENT = 20,
        TAG_CHAPTER = 21,
        TAG_SPREAD = 22,
        TAG_LAYER = 23,
        TAG_PAGE = 24,
        TAG_SPREAD_PHASE2 = 25,
        TAG_SPREADINFORMATION = 26,

        // Path Records
        TAG_PATH = 100,
        TAG_PATH_FILLED = 101,
        TAG_PATH_STROKED = 102,
        TAG_PATH_FILLED_STROKED = 103,
        TAG_PATH_RELATIVE = 110,
        TAG_PATH_RELATIVE_FILLED = 111,
        TAG_PATH_RELATIVE_STROKED = 112,
        TAG_PATH_RELATIVE_FILLED_STROKED = 113,

        // Shape Records
        TAG_RECTANGLE = 200,
        TAG_RECTANGLE_SIMPLE = 201,
        TAG_RECTANGLE_SIMPLE_REFORMED = 202,
        TAG_RECTANGLE_SIMPLE_STELLATED = 203,
        TAG_RECTANGLE_SIMPLE_STELLATED_REFORMED = 204,
        TAG_RECTANGLE_SIMPLE_ROUNDED = 205,
        TAG_RECTANGLE_SIMPLE_ROUNDED_REFORMED = 206,
        TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED = 207,
        TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED_REFORMED = 208,
        TAG_RECTANGLE_COMPLEX = 209,
        TAG_RECTANGLE_COMPLEX_REFORMED = 210,
        TAG_RECTANGLE_COMPLEX_STELLATED = 211,
        TAG_RECTANGLE_COMPLEX_STELLATED_REFORMED = 212,
        TAG_RECTANGLE_COMPLEX_ROUNDED = 213,
        TAG_RECTANGLE_COMPLEX_ROUNDED_REFORMED = 214,
        TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED = 215,
        TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED_REFORMED = 216,

        TAG_ELLIPSE = 300,
        TAG_ELLIPSE_SIMPLE = 301,
        TAG_ELLIPSE_COMPLEX = 302,

        TAG_POLYGON = 400,
        TAG_POLYGON_COMPLEX = 401,
        TAG_POLYGON_COMPLEX_REFORMED = 402,
        TAG_POLYGON_COMPLEX_STELLATED = 403,
        TAG_POLYGON_COMPLEX_STELLATED_REFORMED = 404,
        TAG_POLYGON_COMPLEX_ROUNDED = 405,
        TAG_POLYGON_COMPLEX_ROUNDED_REFORMED = 406,
        TAG_POLYGON_COMPLEX_ROUNDED_STELLATED = 407,
        TAG_POLYGON_COMPLEX_ROUNDED_STELLATED_REFORMED = 408,

        // Group Records
        TAG_GROUP = 500,
        TAG_GROUPA = 501,
        TAG_COMPOUNDRENDER = 502,

        // Blend Records
        TAG_BLEND = 600,
        TAG_BLENDER = 601,
        TAG_BLENDERADDRESS = 602,
        TAG_BLENDERPATH = 603,
        TAG_BLENDERANTICLOCKWISE = 604,
        TAG_BLENDERCLOCKWISE = 605,

        // Mould Records
        TAG_MOULD_ENVELOPE = 700,
        TAG_MOULD_PERSPECTIVE = 701,
        TAG_MOULDPATH = 702,
        TAG_MOULDGROUP = 703,
        TAG_MOULD_BOUNDS = 704,

        // Bitmap Records
        TAG_NODE_BITMAP = 800,
        TAG_NODE_CONTONED_BITMAP = 801,
        TAG_DEFINEBITMAP_JPEG = 810,
        TAG_DEFINEBITMAP_PNG = 811,
        TAG_DEFINEBITMAP_JPEG8BPP = 812,
        TAG_DEFINEBITMAP_PNG_REAL = 813,
        TAG_DEFINEBITMAP_BMP = 814,
        TAG_DEFINEBITMAP_GIF = 815,
        TAG_DEFINEBITMAP_BMP_SCREEN = 816,

        // Fill Attribute Records
        TAG_FLATFILL = 1000,
        TAG_FLATFILL_NONE = 1001,
        TAG_FLATFILL_BLACK = 1002,
        TAG_FLATFILL_WHITE = 1003,
        TAG_LINEARGRADIENTFILL = 1010,
        TAG_CIRCULARGRADIENTFILL = 1011,
        TAG_ELLIPTICALGRADIENTFILL = 1012,
        TAG_CONICALGRADIENTFILL = 1013,
        TAG_BITMAPFILL = 1020,
        TAG_CONTONEDFILL = 1021,
        TAG_FRACTALFILL = 1022,
        TAG_NOISEFILL = 1023,
        TAG_FILL3POINT = 1024,
        TAG_FILL4POINT = 1025,
        TAG_FILLREPEAT = 1030,
        TAG_FILLREPEAT_X = 1031,
        TAG_FILLREPEAT_Y = 1032,
        TAG_FILLREPEAT_NONE = 1033,
        TAG_FILLEFFECT_FADE = 1040,
        TAG_FILLEFFECT_RAINBOW = 1041,
        TAG_FILLEFFECT_ALTRAINBOW = 1042,
        TAG_MULTISTAGEFILL = 1050,
        TAG_MULTISTAGELINEARGRADIENTFILL = 1051,
        TAG_MULTISTAGECIRCULARGRADIENTFILL = 1052,
        TAG_MULTISTAGEELLIPTICALGRADIENTFILL = 1053,
        TAG_MULTISTAGECONICALGRADIENTFILL = 1054,

        // Transparency Attribute Records
        TAG_FLATTRANSPARENTFILL = 1100,
        TAG_LINEARGRADIENTTRANSPARENTFILL = 1110,
        TAG_CIRCULARGRADIENTTRANSPARENTFILL = 1111,
        TAG_ELLIPTICALGRADIENTTRANSPARENTFILL = 1112,
        TAG_CONICALGRADIENTTRANSPARENTFILL = 1113,
        TAG_BITMAPTRANSPARENTFILL = 1120,
        TAG_FRACTALTRANSPARENTFILL = 1121,
        TAG_NOISETRANSPARENTFILL = 1122,
        TAG_TRANSPARENTFILL3POINT = 1123,
        TAG_TRANSPARENTFILL4POINT = 1124,

        // Line Attribute Records
        TAG_LINEWIDTH = 1200,
        TAG_LINECOLOUR = 1201,
        TAG_LINECOLOUR_NONE = 1202,
        TAG_LINECOLOUR_BLACK = 1203,
        TAG_LINECOLOUR_WHITE = 1204,
        TAG_LINECAP = 1210,
        TAG_LINEJOIN = 1211,
        TAG_MITRELIMIT = 1212,
        TAG_WINDINGRULE = 1213,
        TAG_DASHPATTERN = 1220,
        TAG_DEFINEDASHPATTERN = 1221,
        TAG_STARTARROW = 1230,
        TAG_ENDARROW = 1231,
        TAG_DEFINEARROW = 1232,
        TAG_STROKETRANSPARENCY = 1240,
        TAG_STROKETYPE = 1241,
        TAG_VARIABLEWIDTHFUNC = 1242,
        TAG_VARIABLEWIDTHTABLE = 1243,
        TAG_STROKEAIRBRUSH = 1244,
        TAG_STROKEDEFINITION = 1245,

        // Colour Records
        TAG_DEFINERGBCOLOUR = 1300,
        TAG_DEFINECOMPLEXCOLOUR = 1301,

        // Text Records
        TAG_TEXT_STORY_SIMPLE = 2000,
        TAG_TEXT_STORY_COMPLEX = 2001,
        TAG_TEXT_STORY_SIMPLE_START = 2002,
        TAG_TEXT_STORY_SIMPLE_END = 2003,
        TAG_TEXT_STORY_COMPLEX_START = 2004,
        TAG_TEXT_STORY_COMPLEX_END = 2005,
        TAG_TEXT_LINE = 2100,
        TAG_TEXT_STRING = 2200,
        TAG_TEXT_CHAR = 2201,
        TAG_TEXT_EOL = 2202,
        TAG_TEXT_KERN = 2203,
        TAG_TEXT_CARET = 2204,
        TAG_TEXT_TAB = 2205,
        TAG_TEXT_LINESPACE_RATIO = 2210,
        TAG_TEXT_LINESPACE_ABSOLUTE = 2211,
        TAG_TEXT_JUSTIFICATION_LEFT = 2220,
        TAG_TEXT_JUSTIFICATION_CENTRE = 2221,
        TAG_TEXT_JUSTIFICATION_RIGHT = 2222,
        TAG_TEXT_JUSTIFICATION_FULL = 2223,
        TAG_TEXT_FONT_DEF_TRUETYPE = 2300,
        TAG_TEXT_FONT_DEF_ATM = 2301,
        TAG_TEXT_FONT_TYPEFACE = 2310,
        TAG_TEXT_BOLD_ON = 2311,
        TAG_TEXT_BOLD_OFF = 2312,
        TAG_TEXT_ITALIC_ON = 2313,
        TAG_TEXT_ITALIC_OFF = 2314,
        TAG_TEXT_UNDERLINE_ON = 2315,
        TAG_TEXT_UNDERLINE_OFF = 2316,
        TAG_TEXT_FONT_SIZE = 2320,
        TAG_TEXT_SCRIPT_ON = 2321,
        TAG_TEXT_SCRIPT_OFF = 2322,
        TAG_TEXT_SUPERSCRIPT_ON = 2323,
        TAG_TEXT_SUBSCRIPT_ON = 2324,
        TAG_TEXT_ASPECT_RATIO = 2330,
        TAG_TEXT_TRACKING = 2331,
        TAG_TEXT_BASELINE_SHIFT = 2332,

        // Bevel/Contour/Shadow Records
        TAG_BEVEL = 3000,
        TAG_BEVELATTR_INDENT = 3001,
        TAG_BEVELATTR_LIGHTANGLE = 3002,
        TAG_BEVELATTR_CONTRAST = 3003,
        TAG_BEVELATTR_TYPE = 3004,
        TAG_BEVELATTR_LIGHTCOLOUR = 3005,
        TAG_BEVELATTR_DARKCOLOUR = 3006,
        TAG_CONTOUR = 3100,
        TAG_CONTOUR_CONTROLLER = 3101,
        TAG_INSET_PATH = 3102,
        TAG_SHADOW = 3200,
        TAG_SHADOW_CONTROLLER = 3201,
        TAG_SHADOW_FLOOR = 3202,
        TAG_SHADOW_WALL = 3203,
        TAG_SHADOW_GLOW = 3204,
        TAG_SHADOW_FEATHER = 3205,

        // Brush Records
        TAG_BRUSH = 3300,
        TAG_BRUSH_ATTR = 3301,
        TAG_BRUSH_DEFINITION = 3302,
        TAG_BRUSH_DATA = 3303,

        // ClipView Records
        TAG_CLIPVIEW = 3400,
        TAG_CLIPVIEWATTR = 3401,
        TAG_CLIPVIEW_PATH = 3402,

        // Feather Records
        TAG_FEATHER = 3500,
        TAG_FEATHER_CONTROLLER = 3501,

        // Live Effect Records
        TAG_LIVEEFFECT = 3600,
        TAG_LOCKED_EFFECT = 3601,
        TAG_EFFECT_PARAM = 3602,

        // Current Attributes Records
        TAG_CURRENTATTRIBUTES = 4000,
        TAG_CURRENTATTRIBUTES_PHASE2 = 4001,

        // Application Records
        TAG_SPREADFLASHPROPS = 4050,
        TAG_PRINTERSETTINGS = 4051,
        TAG_PRINTERSETTINGS_PHASE2 = 4052,
        TAG_DOCUMENTINFORMATION = 4053,
        TAG_IMPORTSETTING = 4054,
        TAG_DEFINEDEFAULTUNITS = 4055,
        TAG_DEFINEPREFIX_USER_UNIT = 4056,
        TAG_DEFINESUFFIX_USER_UNIT = 4057,
        TAG_DEFINESCALAR_USER_UNIT = 4058,
        TAG_OBJECTBOUNDS = 4100,

        // Unknown/Reserved
        TAG_UNKNOWN = 0xFFFFFFFF
    };

// ===== XAR-SPECIFIC TYPES (only what's truly needed) =====

// XAR Matrix (transformation) - needed because XAR stores 6-element matrix
    struct XARMatrix {
        double a = 1.0, b = 0.0;  // First row
        double c = 0.0, d = 1.0;  // Second row
        double e = 0.0, f = 0.0;  // Translation (in millipoints)

        XARMatrix() = default;

        void ApplyToContext(IRenderContext* ctx) const {
            ctx->Transform(
                    static_cast<float>(a), static_cast<float>(b),
                    static_cast<float>(c), static_cast<float>(d),
                    static_cast<float>(e) * XARConstants::MILLIPOINTS_TO_PIXELS,
                    static_cast<float>(f) * XARConstants::MILLIPOINTS_TO_PIXELS
            );
        }

        Point2Di Transform(const Point2Di& coord) const {
            return Point2Di(
                    static_cast<int>(a * coord.x + c * coord.y + e),
                    static_cast<int>(b * coord.x + d * coord.y + f)
            );
        }
    };

// XAR Path Verb - XAR-specific path encoding
    enum class XARPathVerb : uint8_t {
        MoveTo = 0x06,
        LineTo = 0x02,
        BezierTo = 0x04,
        ClosePath = 0x01
    };

// XAR Path Command - uses Point2Di for millipoint coordinates
    struct XARPathCommand {
        XARPathVerb verb = XARPathVerb::MoveTo;
        std::vector<Point2Di> points;  // Using UltraCanvas Point2Di

        XARPathCommand() = default;
        XARPathCommand(XARPathVerb v) : verb(v) {}
    };

// ===== XAR FILL TYPES =====

    enum class XARFillType {
        NoneFill,
        Flat,
        LinearGradient,
        CircularGradient,
        EllipticalGradient,
        ConicalGradient,
        Bitmap,
        Fractal,
        Noise,
        MultiStage
    };

    struct XARFillAttribute {
        XARFillType type = XARFillType::Flat;
        Color startColor;                      // Using UltraCanvas Color
        Color endColor;                        // Using UltraCanvas Color
        Point2Di startPoint;                   // Using UltraCanvas Point2Di (millipoints)
        Point2Di endPoint;                     // Using UltraCanvas Point2Di (millipoints)
        Point2Di endPoint2;                    // For complex fills
        std::vector<GradientStop> gradientStops;  // Using UltraCanvas GradientStop
        int32_t bitmapRef = -1;

        enum class RepeatMode { NoneRepeat, RepeatX, RepeatY, RepeatXY } repeatMode = RepeatMode::NoneRepeat;
        enum class Effect { Fade, Rainbow, AltRainbow } effect = Effect::Fade;
    };

// ===== XAR TRANSPARENCY TYPES =====

    enum class XARTransparencyType {
        NoTranparent,
        Flat,
        LinearGradient,
        CircularGradient,
        EllipticalGradient,
        ConicalGradient,
        Bitmap,
        Fractal,
        Noise
    };

    struct XARTransparencyAttribute {
        XARTransparencyType type = XARTransparencyType::NoTranparent;
        uint8_t startTransparency = 0;
        uint8_t endTransparency = 0;
        Point2Di startPoint;
        Point2Di endPoint;
        Point2Di endPoint2;

        enum class MixType {
            Normal, Stained, Bleach, Contrast,
            Saturation, Darken, Lighten,
            Brightness, Luminosity, Hue, Color
        } mixType = MixType::Normal;
    };

// ===== XAR LINE ATTRIBUTES =====

    struct XARLineAttribute {
        int32_t width = 250;                   // In millipoints
        Color color = Color(0, 0, 0, 255);     // Using UltraCanvas Color
        LineCap cap = LineCap::Butt;           // Using UltraCanvas LineCap
        LineJoin join = LineJoin::Miter;       // Using UltraCanvas LineJoin
        float mitreLimit = 4.0f;
        std::vector<int32_t> dashPattern;
        int32_t dashOffset = 0;
        int32_t startArrowRef = -1;
        int32_t endArrowRef = -1;

        float GetWidthInPixels() const {
            return static_cast<float>(width) * XARConstants::MILLIPOINTS_TO_PIXELS;
        }
    };

// ===== XAR WINDING RULE =====

    enum class XARWindingRule {
        NonZero = 0,
        EvenOdd = 1,
        Positive = 2,
        Negative = 3
    };

// ===== XAR TEXT ATTRIBUTES =====

    struct XARTextAttribute {
        int32_t fontRef = -1;
        std::string fontName;
        int32_t fontSize = 12000;              // In millipoints
        bool bold = false;
        bool italic = false;
        bool underline = false;
        float aspectRatio = 1.0f;
        int32_t tracking = 0;
        int32_t baselineShift = 0;

        enum class Justification { Left, Centre, Right, Full } justification = Justification::Left;

        float GetFontSizeInPixels() const {
            return static_cast<float>(fontSize) * XARConstants::MILLIPOINTS_TO_PIXELS;
        }
    };

// ===== XAR RENDERING CONTEXT (Attribute Stack) =====

    struct XARRenderingContext {
        XARFillAttribute fill;
        XARTransparencyAttribute transparency;
        XARLineAttribute line;
        XARWindingRule windingRule = XARWindingRule::NonZero;
        XARTextAttribute text;

        XARRenderingContext() {
            fill.type = XARFillType::Flat;
            fill.startColor = Color(255, 255, 255, 255);
            line.color = Color(0, 0, 0, 255);
            line.width = 250;
        }
    };

// ===== XAR RECORD STRUCTURE =====

    struct XARRecord {
        XARTag tag = XARTag::TAG_UNKNOWN;
        uint32_t size = 0;
        std::vector<uint8_t> data;

        bool IsNavigation() const {
            return tag == XARTag::TAG_UP || tag == XARTag::TAG_DOWN;
        }

        bool IsCompression() const {
            return tag == XARTag::TAG_STARTCOMPRESSION || tag == XARTag::TAG_ENDCOMPRESSION;
        }
    };

// ===== XAR NODE TYPES =====

    class XARNode;
    using XARNodePtr = std::shared_ptr<XARNode>;

    enum class XARNodeType {
        Document,
        Chapter,
        Spread,
        Layer,
        Page,
        Group,
        Path,
        Rectangle,
        Ellipse,
        Polygon,
        Text,
        TextLine,
        TextString,
        Bitmap,
        Blend,
        Mould,
        Bevel,
        Contour,
        Shadow,
        ClipView,
        Feather,
        LiveEffect,
        Unknown
    };

// ===== XAR NODE BASE CLASS =====

    class XARNode {
    public:
        XARNodeType type = XARNodeType::Unknown;
        std::vector<XARNodePtr> children;
        XARNodePtr parent;

        // Rendering attributes
        XARFillAttribute fill;
        XARTransparencyAttribute transparency;
        XARLineAttribute line;
        XARWindingRule windingRule = XARWindingRule::NonZero;
        bool hasFill = false;
        bool hasLine = false;
        bool hasTransparency = false;

        Rect2Df bounds;                        // Using UltraCanvas Rect2Df

        virtual ~XARNode() = default;

        virtual void Render(IRenderContext* ctx, float scale = 1.0f) {
            for (auto& child : children) {
                child->Render(ctx, scale);
            }
        }

        void AddChild(XARNodePtr child) {
            child->parent = std::shared_ptr<XARNode>(this, [](XARNode*){});
            children.push_back(child);
        }

        Rect2Df CalculateBounds() const;
    };

// ===== XAR PATH NODE =====

    class XARPathNode : public XARNode {
    public:
        std::vector<XARPathCommand> commands;
        bool isFilled = true;
        bool isStroked = true;

        XARPathNode() { type = XARNodeType::Path; }

        void Render(IRenderContext* ctx, float scale = 1.0f) override;
        void RenderPath(IRenderContext* ctx, float scale) const;
    };

// ===== XAR RECTANGLE NODE =====

    class XARRectangleNode : public XARNode {
    public:
        Point2Di centre;                       // Using UltraCanvas Point2Di (millipoints)
        Point2Di majorAxis;
        Point2Di minorAxis;
        float cornerRadius = 0.0f;
        bool isSimple = true;
        XARMatrix transform;

        XARRectangleNode() { type = XARNodeType::Rectangle; }

        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

// ===== XAR ELLIPSE NODE =====

    class XAREllipseNode : public XARNode {
    public:
        Point2Di centre;                       // Using UltraCanvas Point2Di
        Point2Di majorAxis;
        Point2Di minorAxis;
        bool isSimple = true;
        XARMatrix transform;

        XAREllipseNode() { type = XARNodeType::Ellipse; }

        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

// ===== XAR POLYGON NODE =====

    class XARPolygonNode : public XARNode {
    public:
        int32_t numSides = 3;
        Point2Di centre;                       // Using UltraCanvas Point2Di
        Point2Di majorAxis;
        Point2Di minorAxis;
        float curvature = 0.0f;
        float stellationRadius = 0.0f;
        float stellationOffset = 0.0f;
        bool isRounded = false;
        bool isStellated = false;
        XARMatrix transform;

        XARPolygonNode() { type = XARNodeType::Polygon; }

        void Render(IRenderContext* ctx, float scale = 1.0f) override;
        std::vector<Point2Df> GeneratePolygonPoints(float scale) const;
    };

// ===== XAR GROUP NODE =====

    class XARGroupNode : public XARNode {
    public:
        XARGroupNode() { type = XARNodeType::Group; }

        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

// ===== XAR TEXT NODE =====

    class XARTextNode : public XARNode {
    public:
        std::string text;
        Point2Di position;                     // Using UltraCanvas Point2Di
        XARMatrix transform;
        XARTextAttribute textAttr;

        XARTextNode() { type = XARNodeType::Text; }

        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

// ===== XAR LAYER NODE =====

    class XARLayerNode : public XARNode {
    public:
        std::string name;
        bool visible = true;
        bool locked = false;
        bool printable = true;

        XARLayerNode() { type = XARNodeType::Layer; }

        void Render(IRenderContext* ctx, float scale = 1.0f) override {
            if (visible) {
                XARNode::Render(ctx, scale);
            }
        }
    };

// ===== XAR SPREAD NODE =====

    class XARSpreadNode : public XARNode {
    public:
        int32_t width = 0;
        int32_t height = 0;
        int32_t margin = 0;
        int32_t bleed = 0;

        XARSpreadNode() { type = XARNodeType::Spread; }

        float GetWidthInPixels() const {
            return static_cast<float>(width) * XARConstants::MILLIPOINTS_TO_PIXELS;
        }

        float GetHeightInPixels() const {
            return static_cast<float>(height) * XARConstants::MILLIPOINTS_TO_PIXELS;
        }
    };

// ===== XAR BITMAP DEFINITION =====

    struct XARBitmapDefinition {
        int32_t sequenceNumber = 0;
        int32_t width = 0;
        int32_t height = 0;
        std::vector<uint8_t> data;

        enum class Format { JPEG, PNG, BMP, GIF } format = Format::PNG;
    };

// ===== XAR COLOR DEFINITION =====

    struct XARColorDefinition {
        int32_t sequenceNumber = 0;
        std::string name;
        Color color;                           // Using UltraCanvas Color

        enum class Model { RGB, HSV, CMYK, Greyscale } model = Model::RGB;

        int32_t parentRef = -1;
        float tintValue = 1.0f;
    };

// ===== XAR ARROW DEFINITION =====

    struct XARArrowDefinition {
        int32_t sequenceNumber = 0;
        std::vector<XARPathCommand> path;
        Point2Di centre;                       // Using UltraCanvas Point2Di
        float width = 0.0f;
        float height = 0.0f;
    };

// ===== XAR FONT DEFINITION =====

    struct XARFontDefinition {
        int32_t sequenceNumber = 0;
        std::string fontName;
        std::string familyName;
        uint8_t panose[10] = {0};
        bool isTrueType = true;
    };

// ===== HELPER: Convert millipoint coordinates to pixels =====

    inline Point2Df MillipointsToPixels(const Point2Di& mp, float scale = 1.0f) {
        return Point2Df(
                static_cast<float>(mp.x) * XARConstants::MILLIPOINTS_TO_PIXELS * scale,
                static_cast<float>(mp.y) * XARConstants::MILLIPOINTS_TO_PIXELS * scale
        );
    }

// ===== XAR DOCUMENT =====

    class XARDocument {
    public:
        XARDocument();
        ~XARDocument();

        bool LoadFromFile(const std::string& filepath);
        bool LoadFromMemory(const uint8_t* data, size_t size);

        float GetWidth() const { return width; }
        float GetHeight() const { return height; }
        Rect2Df GetViewBox() const { return Rect2Df(0, 0, width, height); }

        XARNodePtr GetRoot() const { return root; }

        XARColorDefinition* GetColor(int32_t ref);
        XARBitmapDefinition* GetBitmap(int32_t ref);
        XARFontDefinition* GetFont(int32_t ref);
        XARArrowDefinition* GetArrow(int32_t ref);

        void Render(IRenderContext* ctx, float scale = 1.0f);

    private:
        bool ParseHeader(const uint8_t* data, size_t size, size_t& offset);
        bool ParseRecords(const uint8_t* data, size_t size, size_t& offset);
        bool ReadRecord(const uint8_t* data, size_t size, size_t& offset, XARRecord& record);

        bool DecompressZlib(const uint8_t* compressedData, size_t compressedSize,
                            std::vector<uint8_t>& decompressedData);

        void ProcessRecord(const XARRecord& record);
        void ParsePathRecord(const XARRecord& record, bool filled, bool stroked, bool relative);
        void ParseRectangleRecord(const XARRecord& record);
        void ParseEllipseRecord(const XARRecord& record);
        void ParsePolygonRecord(const XARRecord& record);
        void ParseGroupRecord(const XARRecord& record);
        void ParseLayerRecord(const XARRecord& record);
        void ParseSpreadRecord(const XARRecord& record);
        void ParseSpreadInfoRecord(const XARRecord& record);
        void ParseTextRecord(const XARRecord& record);
        void ParseBitmapDefRecord(const XARRecord& record);
        void ParseColorRecord(const XARRecord& record);
        void ParseFillRecord(const XARRecord& record);
        void ParseTransparencyRecord(const XARRecord& record);
        void ParseLineRecord(const XARRecord& record);
        void ParseFontDefRecord(const XARRecord& record);
        void ParseArrowDefRecord(const XARRecord& record);

        // Binary reading utilities
        uint8_t ReadByte(const uint8_t* data, size_t& offset);
        uint16_t ReadUInt16(const uint8_t* data, size_t& offset);
        int16_t ReadInt16(const uint8_t* data, size_t& offset);
        uint32_t ReadUInt32(const uint8_t* data, size_t& offset);
        int32_t ReadInt32(const uint8_t* data, size_t& offset);
        double ReadDouble(const uint8_t* data, size_t& offset);
        float ReadFloat(const uint8_t* data, size_t& offset);
        std::string ReadString(const uint8_t* data, size_t& offset);
        std::string ReadASCIIString(const uint8_t* data, size_t& offset);
        Point2Di ReadCoord(const uint8_t* data, size_t& offset);
        XARMatrix ReadMatrix(const uint8_t* data, size_t& offset);
        Color ReadColor(const uint8_t* data, size_t& offset);

        void PushNode(XARNodePtr node);
        void PopNode();
        XARNodePtr CurrentNode() { return nodeStack.empty() ? root : nodeStack.top(); }

        float width = 0.0f;
        float height = 0.0f;
        XARNodePtr root;

        std::stack<XARNodePtr> nodeStack;
        int32_t currentLevel = 0;

        std::stack<XARRenderingContext> contextStack;
        XARRenderingContext currentContext;

        std::unordered_map<int32_t, XARColorDefinition> colors;
        std::unordered_map<int32_t, XARBitmapDefinition> bitmaps;
        std::unordered_map<int32_t, XARFontDefinition> fonts;
        std::unordered_map<int32_t, XARArrowDefinition> arrows;

        int32_t currentSequenceNumber = 0;

        bool isCompressed = false;
        std::vector<uint8_t> decompressedBuffer;

        std::string producer;
        std::string producerVersion;
        std::string producerBuild;
    };

// ===== XAR UI ELEMENT =====

    class UltraCanvasXARElement : public UltraCanvasUIElement {
    public:
        UltraCanvasXARElement(const std::string& identifier, long id,
                              long x, long y, long w, long h);
        virtual ~UltraCanvasXARElement() = default;

        bool LoadFromFile(const std::string& filepath);
        bool LoadFromMemory(const uint8_t* data, size_t size);

        void Render(IRenderContext* ctx) override;

        void SetScale(float s) { scale = s; }
        float GetScale() const { return scale; }

        void SetPreserveAspectRatio(bool preserve) { preserveAspectRatio = preserve; }
        bool GetPreserveAspectRatio() const { return preserveAspectRatio; }

        const XARDocument* GetDocument() const { return document.get(); }

    private:
        std::unique_ptr<XARDocument> document;
        float scale = 1.0f;
        bool preserveAspectRatio = true;
    };

// ===== XAR PLUGIN =====

    class UltraCanvasXARPlugin : public IGraphicsPlugin {
    public:
        UltraCanvasXARPlugin() = default;
        ~UltraCanvasXARPlugin() override = default;

        std::string GetPluginName() const override { return "UltraCanvas XAR Plugin"; }
        std::string GetPluginVersion() const override { return "1.1.0"; }
        std::vector<std::string> GetSupportedExtensions() const override {
            return {"xar", "web", "wix"};
        }

        bool CanHandle(const std::string& filePath) const override;
        bool CanHandle(const GraphicsFileInfo& fileInfo) const override;

        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const std::string& filePath) override;
        std::shared_ptr<UltraCanvasUIElement> LoadGraphics(const GraphicsFileInfo& fileInfo) override;
        std::shared_ptr<UltraCanvasUIElement> CreateGraphics(int width, int height,
                                                             GraphicsFormatType type) override;

        GraphicsManipulation GetSupportedManipulations() const override {
            return GraphicsManipulation::Move |
                   GraphicsManipulation::Rotate |
                   GraphicsManipulation::Scale |
                   GraphicsManipulation::Flip |
                   GraphicsManipulation::Transform;
        }

        GraphicsFileInfo GetFileInfo(const std::string& filePath) override;
        bool ValidateFile(const std::string& filePath) override;

    private:
        std::string GetFileExtension(const std::string& filePath) const;
    };

// ===== FACTORY FUNCTIONS =====

    inline std::shared_ptr<UltraCanvasXARPlugin> CreateXARPlugin() {
        return std::make_shared<UltraCanvasXARPlugin>();
    }

    inline void RegisterXARPlugin() {
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(CreateXARPlugin());
    }

// ===== BUILDER PATTERN =====

    class XARElementBuilder {
    private:
        std::string identifier = "XARElement";
        long id = 0;
        long x = 0, y = 0, w = 400, h = 400;
        std::string filePath;
        float scale = 1.0f;
        bool preserveAspectRatio = true;

    public:
        XARElementBuilder& SetIdentifier(const std::string& elementId) {
            identifier = elementId;
            return *this;
        }

        XARElementBuilder& SetID(long elementId) {
            id = elementId;
            return *this;
        }

        XARElementBuilder& SetPosition(long px, long py) {
            x = px;
            y = py;
            return *this;
        }

        XARElementBuilder& SetSize(long width, long height) {
            w = width;
            h = height;
            return *this;
        }

        XARElementBuilder& SetFilePath(const std::string& path) {
            filePath = path;
            return *this;
        }

        XARElementBuilder& SetScale(float s) {
            scale = s;
            return *this;
        }

        XARElementBuilder& SetPreserveAspectRatio(bool preserve) {
            preserveAspectRatio = preserve;
            return *this;
        }

        std::shared_ptr<UltraCanvasXARElement> Build() {
            auto element = std::make_shared<UltraCanvasXARElement>(identifier, id, x, y, w, h);
            element->SetScale(scale);
            element->SetPreserveAspectRatio(preserveAspectRatio);

            if (!filePath.empty()) {
                element->LoadFromFile(filePath);
            }

            return element;
        }
    };

} // namespace UltraCanvas