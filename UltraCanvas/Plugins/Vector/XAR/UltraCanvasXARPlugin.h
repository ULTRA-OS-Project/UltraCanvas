// Plugins/Vector/XAR/UltraCanvasXARPlugin.h
// Xara XAR vector graphics format plugin for UltraCanvas
// Version: 2.0.1
// Last Modified: 2026-05-07
// Author: UltraCanvas Framework
//
// Tag values are taken verbatim from the Xar Format Specification, Appendix A
// (1997-2007 Xara Group Ltd). Earlier versions of this header used incorrect
// tag IDs and could not parse files written by any modern Xara writer.
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
        // 8-byte file ID: "XARA" + 0xA3 0xA3 0x0D 0x0A
        constexpr uint32_t MAGIC_XARA = 0x41524158;      // "XARA" little-endian
        constexpr uint32_t MAGIC_SIGNATURE = 0x0A0DA3A3;

        // Coordinate resolution: 72000 dpi (millipoints)
        constexpr float MILLIPOINTS_PER_INCH = 72000.0f;
        constexpr float MILLIPOINTS_TO_PIXELS = 72.0f / 72000.0f;
    }

// ===== XAR TAG DEFINITIONS (Appendix A) =====
    enum class XARTag : uint32_t {
        // Navigation records
        TAG_UP = 0,
        TAG_DOWN = 1,
        TAG_FILEHEADER = 2,
        TAG_ENDOFFILE = 3,

        // Tag management
        TAG_ATOMICTAGS = 10,
        TAG_ESSENTIALTAGS = 11,
        TAG_TAGDESCRIPTION = 12,

        // Compression
        TAG_STARTCOMPRESSION = 30,
        TAG_ENDCOMPRESSION = 31,

        // Document structure
        TAG_DOCUMENT = 40,
        TAG_CHAPTER = 41,
        TAG_SPREAD = 42,
        TAG_LAYER = 43,
        TAG_PAGE = 44,
        TAG_SPREADINFORMATION = 45,
        TAG_GRIDRULERSETTINGS = 46,
        TAG_GRIDRULERORIGIN = 47,
        TAG_LAYERDETAILS = 48,
        TAG_GUIDELAYERDETAILS = 49,

        // Colour reference
        TAG_DEFINERGBCOLOUR = 50,
        TAG_DEFINECOMPLEXCOLOUR = 51,

        // Spread scaling
        TAG_SPREADSCALING_ACTIVE = 52,
        TAG_SPREADSCALING_INACTIVE = 53,

        // Bitmap reference: preview/define
        TAG_PREVIEWBITMAP_GIF = 61,
        TAG_PREVIEWBITMAP_JPEG = 62,
        TAG_PREVIEWBITMAP_PNG = 63,
        TAG_DEFINEBITMAP_JPEG = 67,
        TAG_DEFINEBITMAP_PNG = 68,
        TAG_DEFINEBITMAP_JPEG8BPP = 71,

        // View
        TAG_VIEWPORT = 80,
        TAG_VIEWQUALITY = 81,
        TAG_DOCUMENTVIEW = 82,

        // Document units
        TAG_DEFINE_PREFIXUSERUNIT = 85,
        TAG_DEFINE_SUFFIXUSERUNIT = 86,
        TAG_DEFINE_DEFAULTUNITS = 87,

        // Document info (small)
        TAG_DOCUMENTCOMMENT = 90,
        TAG_DOCUMENTDATES = 91,
        TAG_DOCUMENTUNDOSIZE = 92,
        TAG_DOCUMENTFLAGS = 93,

        // Object tags
        TAG_PATH = 100,
        TAG_PATH_FILLED = 101,
        TAG_PATH_STROKED = 102,
        TAG_PATH_FILLED_STROKED = 103,
        TAG_GROUP = 104,
        TAG_BLEND = 105,
        TAG_BLENDER = 106,
        TAG_MOULD_ENVELOPE = 107,
        TAG_MOULD_PERSPECTIVE = 108,
        TAG_MOULD_GROUP = 109,
        TAG_MOULD_PATH = 110,
        TAG_PATH_FLAGS = 111,
        TAG_GUIDELINE = 112,
        TAG_PATH_RELATIVE = 113,
        TAG_PATH_RELATIVE_FILLED = 114,
        TAG_PATH_RELATIVE_STROKED = 115,
        TAG_PATH_RELATIVE_FILLED_STROKED = 116,
        TAG_PATHREF_TRANSFORM = 118,

        // Attribute tags: fills + lines
        TAG_FLATFILL = 150,
        TAG_LINECOLOUR = 151,
        TAG_LINEWIDTH = 152,
        TAG_LINEARFILL = 153,
        TAG_CIRCULARFILL = 154,
        TAG_ELLIPTICALFILL = 155,
        TAG_CONICALFILL = 156,
        TAG_BITMAPFILL = 157,
        TAG_CONTONEBITMAPFILL = 158,
        TAG_FRACTALFILL = 159,
        TAG_FILLEFFECT_FADE = 160,
        TAG_FILLEFFECT_RAINBOW = 161,
        TAG_FILLEFFECT_ALTRAINBOW = 162,
        TAG_FILL_REPEATING = 163,
        TAG_FILL_NONREPEATING = 164,
        TAG_FILL_REPEATINGINVERTED = 165,
        TAG_FLATTRANSPARENTFILL = 166,
        TAG_LINEARTRANSPARENTFILL = 167,
        TAG_CIRCULARTRANSPARENTFILL = 168,
        TAG_ELLIPTICALTRANSPARENTFILL = 169,
        TAG_CONICALTRANSPARENTFILL = 170,
        TAG_BITMAPTRANSPARENTFILL = 171,
        TAG_FRACTALTRANSPARENTFILL = 172,
        TAG_LINETRANSPARENCY = 173,
        TAG_STARTCAP = 174,
        TAG_ENDCAP = 175,
        TAG_JOINSTYLE = 176,
        TAG_MITRELIMIT = 177,
        TAG_WINDINGRULE = 178,
        TAG_QUALITY = 179,
        TAG_TRANSPARENTFILL_REPEATING = 180,
        TAG_TRANSPARENTFILL_NONREPEATING = 181,
        TAG_TRANSPARENTFILL_REPEATINGINVERTED = 182,

        // Arrows and dash patterns
        TAG_DASHSTYLE = 183,
        TAG_DEFINEDASH = 184,
        TAG_ARROWHEAD = 185,
        TAG_ARROWTAIL = 186,
        TAG_DEFINEARROW = 187,
        TAG_DEFINEDASH_SCALED = 188,

        // User attributes
        TAG_USERVALUE = 189,

        // Special colour fills
        TAG_FLATFILL_NONE = 190,
        TAG_FLATFILL_BLACK = 191,
        TAG_FLATFILL_WHITE = 192,
        TAG_LINECOLOUR_NONE = 193,
        TAG_LINECOLOUR_BLACK = 194,
        TAG_LINECOLOUR_WHITE = 195,

        // Embedded bitmap objects
        TAG_NODE_BITMAP = 198,
        TAG_NODE_CONTONEDBITMAP = 199,

        // New fill types
        TAG_DIAMONDFILL = 200,
        TAG_DIAMONDTRANSPARENTFILL = 201,
        TAG_THREECOLFILL = 202,
        TAG_THREECOLTRANSPARENTFILL = 203,
        TAG_FOURCOLFILL = 204,
        TAG_FOURCOLTRANSPARENTFILL = 205,
        TAG_FILL_REPEATING_EXTRA = 206,
        TAG_TRANSPARENTFILL_REPEATING_EXTRA = 207,

        // Regular shapes - ellipses
        TAG_ELLIPSE_SIMPLE = 1000,
        TAG_ELLIPSE_COMPLEX = 1001,

        // Regular shapes - rectangles
        TAG_RECTANGLE_SIMPLE = 1100,
        TAG_RECTANGLE_SIMPLE_REFORMED = 1101,
        TAG_RECTANGLE_SIMPLE_STELLATED = 1102,
        TAG_RECTANGLE_SIMPLE_STELLATED_REFORMED = 1103,
        TAG_RECTANGLE_SIMPLE_ROUNDED = 1104,
        TAG_RECTANGLE_SIMPLE_ROUNDED_REFORMED = 1105,
        TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED = 1106,
        TAG_RECTANGLE_SIMPLE_ROUNDED_STELLATED_REFORMED = 1107,
        TAG_RECTANGLE_COMPLEX = 1108,
        TAG_RECTANGLE_COMPLEX_REFORMED = 1109,
        TAG_RECTANGLE_COMPLEX_STELLATED = 1110,
        TAG_RECTANGLE_COMPLEX_STELLATED_REFORMED = 1111,
        TAG_RECTANGLE_COMPLEX_ROUNDED = 1112,
        TAG_RECTANGLE_COMPLEX_ROUNDED_REFORMED = 1113,
        TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED = 1114,
        TAG_RECTANGLE_COMPLEX_ROUNDED_STELLATED_REFORMED = 1115,

        // Regular shapes - polygons
        TAG_POLYGON_COMPLEX = 1200,
        TAG_POLYGON_COMPLEX_REFORMED = 1201,
        TAG_POLYGON_COMPLEX_STELLATED = 1212,
        TAG_POLYGON_COMPLEX_STELLATED_REFORMED = 1213,
        TAG_POLYGON_COMPLEX_ROUNDED = 1214,
        TAG_POLYGON_COMPLEX_ROUNDED_REFORMED = 1215,
        TAG_POLYGON_COMPLEX_ROUNDED_STELLATED = 1216,
        TAG_POLYGON_COMPLEX_ROUNDED_STELLATED_REFORMED = 1217,

        TAG_REGULAR_SHAPE_PHASE_1 = 1900,
        TAG_REGULAR_SHAPE_PHASE_2 = 1901,

        // Text - font definitions
        TAG_FONT_DEF_TRUETYPE = 2000,
        TAG_FONT_DEF_ATM = 2001,

        // Text - story objects
        TAG_TEXT_STORY_SIMPLE = 2100,
        TAG_TEXT_STORY_COMPLEX = 2101,
        TAG_TEXT_STORY_SIMPLE_START_LEFT = 2110,
        TAG_TEXT_STORY_SIMPLE_START_RIGHT = 2111,
        TAG_TEXT_STORY_SIMPLE_END_LEFT = 2112,
        TAG_TEXT_STORY_SIMPLE_END_RIGHT = 2113,
        TAG_TEXT_STORY_COMPLEX_START_LEFT = 2114,
        TAG_TEXT_STORY_COMPLEX_START_RIGHT = 2115,
        TAG_TEXT_STORY_COMPLEX_END_LEFT = 2116,
        TAG_TEXT_STORY_COMPLEX_END_RIGHT = 2117,

        // Text story info
        TAG_TEXT_STORY_WORD_WRAP_INFO = 2150,
        TAG_TEXT_STORY_INDENT_INFO = 2151,

        // Text records
        TAG_TEXT_LINE = 2200,
        TAG_TEXT_STRING = 2201,
        TAG_TEXT_CHAR = 2202,
        TAG_TEXT_EOL = 2203,
        TAG_TEXT_KERN = 2204,
        TAG_TEXT_CARET = 2205,
        TAG_TEXT_LINE_INFO = 2206,

        // Text attributes
        TAG_TEXT_LINESPACE_RATIO = 2900,
        TAG_TEXT_LINESPACE_ABSOLUTE = 2901,
        TAG_TEXT_JUSTIFICATION_LEFT = 2902,
        TAG_TEXT_JUSTIFICATION_CENTRE = 2903,
        TAG_TEXT_JUSTIFICATION_RIGHT = 2904,
        TAG_TEXT_JUSTIFICATION_FULL = 2905,
        TAG_TEXT_FONT_SIZE = 2906,
        TAG_TEXT_FONT_TYPEFACE = 2907,
        TAG_TEXT_BOLD_ON = 2908,
        TAG_TEXT_BOLD_OFF = 2909,
        TAG_TEXT_ITALIC_ON = 2910,
        TAG_TEXT_ITALIC_OFF = 2911,
        TAG_TEXT_UNDERLINE_ON = 2912,
        TAG_TEXT_UNDERLINE_OFF = 2913,
        TAG_TEXT_SCRIPT_ON = 2914,
        TAG_TEXT_SCRIPT_OFF = 2915,
        TAG_TEXT_SUPERSCRIPT_ON = 2916,
        TAG_TEXT_SUBSCRIPT_ON = 2917,
        TAG_TEXT_TRACKING = 2918,
        TAG_TEXT_ASPECT_RATIO = 2919,
        TAG_TEXT_BASELINE = 2920,

        // Imagesetting
        TAG_OVERPRINTLINEON = 3500,
        TAG_OVERPRINTLINEOFF = 3501,
        TAG_OVERPRINTFILLON = 3502,
        TAG_OVERPRINTFILLOFF = 3503,
        TAG_PRINTONALLPLATESON = 3504,
        TAG_PRINTONALLPLATESOFF = 3505,

        // Document print/imagesetting options
        TAG_PRINTERSETTINGS = 3506,
        TAG_IMAGESETTING = 3507,
        TAG_COLOURPLATE = 3508,
        TAG_PRINTMARKDEFAULT = 3509,

        // Stroking
        TAG_VARIABLEWIDTHFUNC = 4000,   // not currently used
        TAG_VARIABLEWIDTHTABLE = 4001,
        TAG_STROKETYPE = 4002,
        TAG_STROKEDEFINITION = 4003,    // not currently used
        TAG_STROKEAIRBRUSH = 4004,      // not currently used

        // Fractal noise fills
        TAG_NOISEFILL = 4010,
        TAG_NOISETRANSPARENTFILL = 4011,

        // Mould bounds
        TAG_MOULD_BOUNDS = 4012,

        // Bitmap export hint
        TAG_EXPORT_HINT = 4015,

        // Web address
        TAG_WEBADDRESS = 4020,
        TAG_WEBADDRESS_BOUNDINGBOX = 4021,

        // Frame layer
        TAG_LAYER_FRAMEPROPS = 4030,
        TAG_SPREAD_ANIMPROPS = 4031,

        // Wizard
        TAG_WIZOP = 4040,
        TAG_WIZOP_STYLE = 4041,
        TAG_WIZOP_STYLEREF = 4042,

        // Shadow
        TAG_SHADOWCONTROLLER = 4050,
        TAG_SHADOW = 4051,

        // Bevel
        TAG_BEVEL = 4052,
        TAG_BEVATTR_INDENT = 4053,      // deprecated
        TAG_BEVATTR_LIGHTANGLE = 4054,  // deprecated
        TAG_BEVATTR_CONTRAST = 4055,    // deprecated
        TAG_BEVATTR_TYPE = 4056,        // deprecated
        TAG_BEVELINK = 4057,

        // Blend on a curve
        TAG_BLENDER_CURVEPROP = 4060,
        TAG_BLEND_PATH = 4061,
        TAG_BLENDER_CURVEANGLES = 4062,

        // Contouring
        TAG_CONTOURCONTROLLER = 4066,
        TAG_CONTOUR = 4067,

        // Set
        TAG_SETSENTINEL = 4070,
        TAG_SETPROPERTY = 4071,

        // More blend
        TAG_BLENDPROFILES = 4072,
        TAG_BLENDERADDITIONAL = 4073,
        TAG_NODEBLENDPATH_FILLED = 4074,

        // Multistage fills
        TAG_LINEARFILLMULTISTAGE = 4075,
        TAG_CIRCULARFILLMULTISTAGE = 4076,
        TAG_ELLIPTICALFILLMULTISTAGE = 4077,
        TAG_CONICALFILLMULTISTAGE = 4078,

        // Brushes
        TAG_BRUSHATTR = 4079,
        TAG_BRUSHDEFINITION = 4080,
        TAG_BRUSHDATA = 4081,
        TAG_MOREBRUSHDATA = 4082,
        TAG_MOREBRUSHATTR = 4083,

        // ClipView
        TAG_CLIPVIEWCONTROLLER = 4084,
        TAG_CLIPVIEW = 4085,

        // Feather
        TAG_FEATHER = 4086,

        // Bar properties
        TAG_BARPROPERTY = 4087,

        // Other multistage fills
        TAG_SQUAREFILLMULTISTAGE = 4088,

        // More brushes
        TAG_EVENMOREBRUSHDATA = 4102,
        TAG_EVENMOREBRUSHATTR = 4103,
        TAG_TIMESTAMPBRUSHDATA = 4104,
        TAG_BRUSHPRESSUREINFO = 4105,
        TAG_BRUSHPRESSUREDATA = 4106,
        TAG_BRUSHATTRPRESSUREINFO = 4107,
        TAG_BRUSHCOLOURDATA = 4108,
        TAG_BRUSHPRESSURESAMPLEDATA = 4109,
        TAG_BRUSHTIMESAMPLEDATA = 4110,
        TAG_BRUSHATTRFILLFLAGS = 4111,
        TAG_BRUSHTRANSPINFO = 4112,
        TAG_BRUSHATTRTRANSPINFO = 4113,

        // Document/bitmap properties
        TAG_DOCUMENTNUDGE = 4114,
        TAG_BITMAP_PROPERTIES = 4115,
        TAG_DOCUMENTBITMAPSMOOTHING = 4116,
        TAG_XPE_BITMAP_PROPERTIES = 4117,
        TAG_DEFINEBITMAP_XPE = 4118,

        // Current attributes
        TAG_CURRENTATTRIBUTES = 4119,
        TAG_CURRENTATTRIBUTEBOUNDS = 4120,

        // 3-point linear fills
        TAG_LINEARFILL3POINT = 4121,
        TAG_LINEARFILLMULTISTAGE3POINT = 4122,
        TAG_LINEARTRANSPARENTFILL3POINT = 4123,

        // Duplication
        TAG_DUPLICATIONOFFSET = 4124,

        // Bitmap effects
        TAG_LIVE_EFFECT = 4125,
        TAG_LOCKED_EFFECT = 4126,
        TAG_FEATHER_EFFECT = 4127,

        // Misc
        TAG_COMPOUNDRENDER = 4128,
        TAG_OBJECTBOUNDS = 4129,
        TAG_SPREAD_PHASE2 = 4131,
        TAG_CURRENTATTRIBUTES_PHASE2 = 4132,
        TAG_SPREAD_FLASHPROPS = 4134,
        TAG_PRINTERSETTINGS_PHASE2 = 4135,
        TAG_DOCUMENTINFORMATION = 4136,
        TAG_CLIPVIEW_PATH = 4137,
        TAG_DEFINEBITMAP_PNG_REAL = 4138,
        TAG_TEXT_STRING_POS = 4139,
        TAG_SPREAD_FLASHPROPS2 = 4140,
        TAG_TEXT_LINESPACE_LEADING = 4141,

        // New text records
        TAG_TEXT_TAB = 4200,
        TAG_TEXT_LEFT_INDENT = 4201,
        TAG_TEXT_FIRST_INDENT = 4202,
        TAG_TEXT_RIGHT_INDENT = 4203,
        TAG_TEXT_RULER = 4204,
        TAG_TEXT_STORY_HEIGHT_INFO = 4205,
        TAG_TEXT_STORY_LINK_INFO = 4206,
        TAG_TEXT_STORY_TRANSLATION_INFO = 4207,
        TAG_TEXT_SPACE_BEFORE = 4208,
        TAG_TEXT_SPACE_AFTER = 4209,
        TAG_TEXT_SPECIAL_HYPHEN = 4210,
        TAG_TEXT_SOFT_RETURN = 4211,
        TAG_TEXT_EXTRA_FONT_INFO = 4212,
        TAG_TEXT_EXTRA_TT_FONT_DEF = 4213,
        TAG_TEXT_EXTRA_ATM_FONT_DEF = 4214,

        TAG_UNKNOWN = 0xFFFFFFFF
    };

// ===== XAR-SPECIFIC TYPES =====

    struct XARMatrix {
        double a = 1.0, b = 0.0;
        double c = 0.0, d = 1.0;
        double e = 0.0, f = 0.0;  // translation in millipoints

        XARMatrix() = default;
        XARMatrix(double a_, double b_, double c_, double d_, double e_, double f_)
            : a(a_), b(b_), c(c_), d(d_), e(e_), f(f_) {}

        bool IsIdentity() const {
            return a == 1.0 && b == 0.0 && c == 0.0 && d == 1.0 && e == 0.0 && f == 0.0;
        }

        void ApplyToContext(IRenderContext* ctx) const {
            ctx->Transform(
                static_cast<float>(a), static_cast<float>(b),
                static_cast<float>(c), static_cast<float>(d),
                static_cast<float>(e) * XARConstants::MILLIPOINTS_TO_PIXELS,
                static_cast<float>(f) * XARConstants::MILLIPOINTS_TO_PIXELS);
        }

        Point2Di Transform(const Point2Di& coord) const {
            return Point2Di(
                static_cast<int>(a * coord.x + c * coord.y + e),
                static_cast<int>(b * coord.x + d * coord.y + f));
        }
    };

    enum class XARPathVerb : uint8_t {
        MoveTo = 6,
        LineTo = 2,
        BezierTo = 4,
        ClosePath = 1
    };

    struct XARPathCommand {
        XARPathVerb verb = XARPathVerb::MoveTo;
        std::vector<Point2Di> points;  // millipoints
        XARPathCommand() = default;
        XARPathCommand(XARPathVerb v) : verb(v) {}
    };

// ===== FILLS =====

    enum class XARFillType {
        NoneFill,
        Flat,
        LinearGradient,
        CircularGradient,
        EllipticalGradient,
        ConicalGradient,
        Diamond,
        ThreeColour,
        FourColour,
        Bitmap,
        ContoneBitmap,
        Fractal,
        Noise
    };

    enum class XARFillRepeat { NonRepeating, Repeating, RepeatingInverted, RepeatingExtra };
    enum class XARFillEffect { Fade, Rainbow, AltRainbow };

    struct XARFillStop {
        double position = 0.0;          // 0..1
        Color color;
        int32_t colourRef = 0;
    };

    struct XARFillAttribute {
        XARFillType type = XARFillType::Flat;
        Color startColor = Color(255, 255, 255, 255);
        Color endColor = Color(0, 0, 0, 255);
        Color thirdColor;
        Color fourthColor;
        Point2Di startPoint;            // a.k.a. centre / bottom-left
        Point2Di endPoint;              // a.k.a. edge / bottom-right
        Point2Di endPoint2;             // a.k.a. top-left or shear point
        Point2Di majorAxis;
        Point2Di minorAxis;
        std::vector<XARFillStop> stops; // multistage
        int32_t bitmapRef = -1;
        double profileBias = 0.5;
        double profileGain = 0.5;
        XARFillRepeat repeat = XARFillRepeat::NonRepeating;
        XARFillEffect effect = XARFillEffect::Fade;
        // Fractal/noise extras
        int32_t fractalSeed = 0;
        float graininess = 1.0f;
        float gravity = 1.0f;
        float squash = 1.0f;
        uint32_t resolution = 96;
        bool tileable = false;
    };

// ===== TRANSPARENCY =====

    // Note: avoid the identifier `None` here — X11's headers define `None` as
    // a macro (0L) which would mangle the enumerator name on Linux builds.
    enum class XARTransparencyType {
        NoTrans, Flat, LinearGradient, CircularGradient, EllipticalGradient,
        ConicalGradient, Diamond, ThreeColour, FourColour,
        Bitmap, Fractal, Noise
    };

    // Transparency composition modes (spec p.99-101).
    // `NoMix` instead of `None` to avoid X11's `None` macro on Linux.
    enum class XARTransparencyMix {
        NoMix = 0, Mix = 1, Stained = 2, Bleach = 3, Contrast = 4,
        Saturation = 5, Darken = 6, Lighten = 7, Brightness = 8,
        Luminosity = 9, Hue = 10
    };

    struct XARTransparencyStop {
        double position = 0.0;
        uint8_t level = 0;
    };

    struct XARTransparencyAttribute {
        XARTransparencyType type = XARTransparencyType::NoTrans;
        uint8_t startTransparency = 0;
        uint8_t endTransparency = 0;
        uint8_t thirdTransparency = 0;
        uint8_t fourthTransparency = 0;
        Point2Di startPoint;
        Point2Di endPoint;
        Point2Di endPoint2;
        Point2Di majorAxis;
        Point2Di minorAxis;
        std::vector<XARTransparencyStop> stops;
        int32_t bitmapRef = -1;
        XARTransparencyMix mix = XARTransparencyMix::Mix;
        double profileBias = 0.5;
        double profileGain = 0.5;
        XARFillRepeat repeat = XARFillRepeat::NonRepeating;
    };

// ===== LINE ATTRIBUTES =====

    struct XARLineAttribute {
        int32_t width = 250;                    // millipoints
        Color color = Color(0, 0, 0, 255);
        bool hasColor = true;
        LineCap cap = LineCap::Butt;
        LineJoin join = LineJoin::Miter;
        float mitreLimit = 4.0f;
        std::vector<double> dashPattern;        // pixels (UCDashPattern uses double)
        int32_t startArrowRef = -1;
        int32_t endArrowRef = -1;
        uint8_t lineTransparency = 0;
        XARTransparencyMix lineTransparencyMix = XARTransparencyMix::Mix;

        float GetWidthInPixels() const {
            return static_cast<float>(width) * XARConstants::MILLIPOINTS_TO_PIXELS;
        }
    };

    enum class XARWindingRule { NonZero = 0, EvenOdd = 2 };

// ===== TEXT ATTRIBUTES =====

    struct XARTextAttribute {
        int32_t fontRef = -1;
        std::string fontName;
        int32_t fontSize = 12000;       // millipoints
        bool bold = false;
        bool italic = false;
        bool underline = false;
        float aspectRatio = 1.0f;
        int32_t tracking = 0;
        int32_t baselineShift = 0;
        float lineSpaceRatio = 1.2f;
        int32_t lineSpaceAbsolute = 0;
        enum class Justification { Left, Centre, Right, Full } justification = Justification::Left;

        float GetFontSizeInPixels() const {
            return static_cast<float>(fontSize) * XARConstants::MILLIPOINTS_TO_PIXELS;
        }
    };

// ===== RENDERING CONTEXT (attribute stack) =====

    struct XARRenderingContext {
        XARFillAttribute fill;
        XARTransparencyAttribute transparency;
        XARLineAttribute line;
        XARWindingRule windingRule = XARWindingRule::NonZero;
        XARTextAttribute text;
        bool hasFill = false;
        bool hasLine = false;
        bool hasTransparency = false;
    };

// ===== RECORD =====

    struct XARRecord {
        XARTag tag = XARTag::TAG_UNKNOWN;
        uint32_t size = 0;
        std::vector<uint8_t> data;
    };

// ===== NODES =====

    class XARNode;
    using XARNodePtr = std::shared_ptr<XARNode>;

    enum class XARNodeType {
        Document, Chapter, Spread, Layer, Page,
        Group, Path, Rectangle, Ellipse, Polygon,
        Text, TextStory, TextLine, TextString,
        Bitmap, ContonedBitmap,
        Blend, Mould, Bevel, Contour, Shadow,
        ClipView, Feather, LiveEffect, Brush,
        Unknown
    };

    class XARNode {
    public:
        XARNodeType type = XARNodeType::Unknown;
        std::vector<XARNodePtr> children;
        std::weak_ptr<XARNode> parent;

        // Captured attribute snapshot at parse time
        XARFillAttribute fill;
        XARTransparencyAttribute transparency;
        XARLineAttribute line;
        XARWindingRule windingRule = XARWindingRule::NonZero;
        XARTextAttribute textAttr;
        bool hasFill = false;
        bool hasLine = false;
        bool hasTransparency = false;

        Rect2Dd bounds;
        bool boundsCached = false;

        virtual ~XARNode() = default;

        virtual void Render(IRenderContext* ctx, float scale = 1.0f) {
            for (auto& child : children) child->Render(ctx, scale);
        }

        void AddChild(XARNodePtr child) { children.push_back(child); }
        Rect2Dd CalculateBounds() const;
    };

    class XARGroupNode : public XARNode {
    public:
        XARGroupNode() { type = XARNodeType::Group; }
        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

    class XARPathNode : public XARNode {
    public:
        std::vector<XARPathCommand> commands;
        bool isFilled = true;
        bool isStroked = true;
        XARMatrix transform;
        bool hasTransform = false;
        XARPathNode() { type = XARNodeType::Path; }
        void Render(IRenderContext* ctx, float scale = 1.0f) override;
        void EmitPath(IRenderContext* ctx, float scale, const XARMatrix* extra = nullptr) const;
    };

    class XARRectangleNode : public XARNode {
    public:
        Point2Di centre;
        Point2Di majorAxis;
        Point2Di minorAxis;
        int32_t halfWidth = 0;
        int32_t halfHeight = 0;
        int32_t cornerRadius = 0;
        bool isSimple = true;
        bool isRounded = false;
        XARMatrix transform;
        XARRectangleNode() { type = XARNodeType::Rectangle; }
        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

    class XAREllipseNode : public XARNode {
    public:
        Point2Di centre;
        Point2Di majorAxis;
        Point2Di minorAxis;
        bool isSimple = true;
        XARMatrix transform;
        XAREllipseNode() { type = XARNodeType::Ellipse; }
        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

    class XARPolygonNode : public XARNode {
    public:
        int32_t numSides = 3;
        Point2Di centre;
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
        std::vector<Point2Dd> GeneratePolygonPoints(float scale) const;
    };

    class XARTextStoryNode : public XARNode {
    public:
        Point2Di position;
        XARMatrix transform;
        bool hasTransform = false;
        XARTextStoryNode() { type = XARNodeType::TextStory; }
        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

    class XARTextLineNode : public XARNode {
    public:
        XARTextLineNode() { type = XARNodeType::TextLine; }
    };

    class XARTextStringNode : public XARNode {
    public:
        std::string text;                       // UTF-8
        XARTextStringNode() { type = XARNodeType::TextString; }
        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

    class XARLayerNode : public XARNode {
    public:
        std::string name;
        bool visible = true;
        bool locked = false;
        bool printable = true;
        bool isGuide = false;
        XARLayerNode() { type = XARNodeType::Layer; }
        void Render(IRenderContext* ctx, float scale = 1.0f) override {
            if (visible && !isGuide) XARNode::Render(ctx, scale);
        }
    };

    class XARSpreadNode : public XARNode {
    public:
        int32_t width = 0;
        int32_t height = 0;
        int32_t margin = 0;
        int32_t bleed = 0;
        uint8_t flags = 0;
        XARSpreadNode() { type = XARNodeType::Spread; }
        float GetWidthInPixels() const {
            return static_cast<float>(width) * XARConstants::MILLIPOINTS_TO_PIXELS;
        }
        float GetHeightInPixels() const {
            return static_cast<float>(height) * XARConstants::MILLIPOINTS_TO_PIXELS;
        }
    };

    class XARChapterNode : public XARNode {
    public:
        XARChapterNode() { type = XARNodeType::Chapter; }
    };

    class XARPageNode : public XARNode {
    public:
        Point2Di bottomLeft;
        Point2Di topRight;
        int32_t colourRef = -2;                 // default white
        XARPageNode() { type = XARNodeType::Page; }
    };

    class XARBitmapNode : public XARNode {
    public:
        Point2Di bottomLeft;
        Point2Di bottomRight;
        Point2Di topLeft;
        int32_t bitmapRef = -1;
        int32_t startColourRef = -1;            // contone variant
        int32_t endColourRef = -1;
        bool isContoned = false;
        XARBitmapNode() { type = XARNodeType::Bitmap; }
        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

    class XARShadowNode : public XARNode {
    public:
        uint8_t shadowType = 0;                 // 0=floor 1=wall 2=glow
        int32_t offsetX = 0;
        int32_t offsetY = 0;
        int32_t blurRadius = 0;
        Color shadowColor = Color(0, 0, 0, 128);
        XARShadowNode() { type = XARNodeType::Shadow; }
        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

    class XARBevelNode : public XARNode {
    public:
        int32_t indent = 0;
        float lightAngle = 45.0f;
        float contrast = 0.5f;
        Color lightColor = Color(255, 255, 255, 255);
        Color darkColor = Color(0, 0, 0, 255);
        XARBevelNode() { type = XARNodeType::Bevel; }
    };

    class XARContourNode : public XARNode {
    public:
        int32_t numContours = 1;
        int32_t contourWidth = 0;
        XARContourNode() { type = XARNodeType::Contour; }
    };

    class XARBlendNode : public XARNode {
    public:
        int32_t numSteps = 1;
        XARBlendNode() { type = XARNodeType::Blend; }
    };

    class XARMouldNode : public XARNode {
    public:
        bool isPerspective = false;
        XARMouldNode() { type = XARNodeType::Mould; }
    };

    class XARClipViewNode : public XARNode {
    public:
        XARClipViewNode() { type = XARNodeType::ClipView; }
        void Render(IRenderContext* ctx, float scale = 1.0f) override;
    };

    class XARFeatherNode : public XARNode {
    public:
        int32_t featherRadius = 0;
        XARFeatherNode() { type = XARNodeType::Feather; }
    };

    class XARLiveEffectNode : public XARNode {
    public:
        std::string effectId;
        XARLiveEffectNode() { type = XARNodeType::LiveEffect; }
    };

    class XARBrushNode : public XARNode {
    public:
        XARBrushNode() { type = XARNodeType::Brush; }
    };

// ===== REUSABLE DEFINITIONS =====

    struct XARBitmapDefinition {
        int32_t sequenceNumber = 0;
        std::string name;
        int32_t width = 0;
        int32_t height = 0;
        std::vector<uint8_t> data;              // raw encoded bytes
        enum class Format { JPEG, PNG, BMP, GIF, JPEG8BPP, PNG_REAL, XPE } format = Format::PNG;
        // Cached decoded pixmap, populated lazily by renderer
        void* decodedPixmap = nullptr;
    };

    struct XARColorDefinition {
        int32_t sequenceNumber = 0;
        std::string name;
        Color color = Color(0, 0, 0, 255);
        Color simpleRGB = Color(0, 0, 0, 255);
        enum class Type { Spot, Normal, Linked, Shaded, Tint } colorType = Type::Normal;
        enum class Model { RGB, HSV, CMYK, Greyscale } model = Model::RGB;
        uint32_t entryIndex = 0;
        int32_t parentRef = 0;
        float components[4] = {0, 0, 0, 0};
        float tintValue = 1.0f;
    };

    struct XARArrowDefinition {
        int32_t sequenceNumber = 0;
        std::vector<XARPathCommand> path;
        Point2Di centre;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct XARFontDefinition {
        int32_t sequenceNumber = 0;
        std::string fontName;
        std::string familyName;
        uint8_t panose[10] = {0};
        bool isTrueType = true;
    };

    struct XARDashDefinition {
        int32_t sequenceNumber = 0;
        std::vector<double> pattern;            // pixels
        bool scaled = false;
    };

// ===== HELPERS =====

    inline Point2Dd MillipointsToPixels(const Point2Di& mp, float scale = 1.0f) {
        return Point2Dd(
            static_cast<float>(mp.x) * XARConstants::MILLIPOINTS_TO_PIXELS * scale,
            static_cast<float>(mp.y) * XARConstants::MILLIPOINTS_TO_PIXELS * scale);
    }

// ===== DOCUMENT =====

    class XARDocument {
    public:
        XARDocument();
        ~XARDocument();

        bool LoadFromFile(const std::string& filepath);
        bool LoadFromMemory(const uint8_t* data, size_t size);

        float GetWidth() const { return width; }
        float GetHeight() const { return height; }
        Rect2Dd GetViewBox() const { return Rect2Dd(0, 0, width, height); }

        XARNodePtr GetRoot() const { return root; }

        XARColorDefinition* GetColor(int32_t ref);
        XARBitmapDefinition* GetBitmap(int32_t ref);
        XARFontDefinition* GetFont(int32_t ref);
        XARArrowDefinition* GetArrow(int32_t ref);
        XARDashDefinition* GetDash(int32_t ref);
        Color ResolveColorRef(int32_t ref);

        void Render(IRenderContext* ctx, float scale = 1.0f);

        const std::string& GetProducer() const { return producer; }
        const std::string& GetProducerVersion() const { return producerVersion; }
        const std::string& GetProducerBuild() const { return producerBuild; }
        const std::string& GetFileType() const { return fileType; }

    private:
        // Stream reader: switches between outer (raw) and inflated streams
        struct StreamFrame {
            const uint8_t* data;
            size_t size;
            size_t offset;
        };

        bool ParseHeader(StreamFrame& outer);
        bool ParseRecords();
        bool ReadRecord(XARRecord& record);

        bool DecompressZlib(const uint8_t* compressedData, size_t compressedSize,
                            std::vector<uint8_t>& decompressedData,
                            size_t& consumedCompressedBytes);

        void ProcessRecord(const XARRecord& record);

        // Record handlers
        void ParseFileHeaderRecord(const XARRecord& record);
        void ParsePathRecord(const XARRecord& record, bool filled, bool stroked, bool relative);
        void ParsePathFlagsRecord(const XARRecord& record);
        void ParsePathRefTransformRecord(const XARRecord& record);
        void ParseRectangleRecord(const XARRecord& record);
        void ParseEllipseRecord(const XARRecord& record);
        void ParsePolygonRecord(const XARRecord& record);
        void ParseGroupRecord(const XARRecord& record);
        void ParseLayerRecord(const XARRecord& record);
        void ParseLayerDetailsRecord(const XARRecord& record, bool isGuide);
        void ParseSpreadRecord(const XARRecord& record);
        void ParseSpreadInfoRecord(const XARRecord& record);
        void ParsePageRecord(const XARRecord& record);
        void ParseViewportRecord(const XARRecord& record);
        void ParseTextStoryRecord(const XARRecord& record);
        void ParseTextLineRecord(const XARRecord& record);
        void ParseTextStringRecord(const XARRecord& record);
        void ParseTextAttrRecord(const XARRecord& record);
        void ParseFontDefRecord(const XARRecord& record, bool isTrueType);
        void ParseBitmapDefRecord(const XARRecord& record, XARBitmapDefinition::Format fmt);
        void ParseNodeBitmapRecord(const XARRecord& record, bool contoned);
        void ParseColorRecord(const XARRecord& record);
        void ParseComplexColorRecord(const XARRecord& record);
        void ParseFlatFillRecord(const XARRecord& record);
        void ParseLinearFillRecord(const XARRecord& record, bool threePoint, bool multistage);
        void ParseRadialFillRecord(const XARRecord& record, XARFillType ft, bool elliptical, bool multistage);
        void ParseConicalFillRecord(const XARRecord& record, bool multistage);
        void ParseBitmapFillRecord(const XARRecord& record, bool contoned);
        void ParseFractalFillRecord(const XARRecord& record, bool noise);
        void ParseDiamondFillRecord(const XARRecord& record);
        void ParseThreeFourColFillRecord(const XARRecord& record, bool four);
        void ParseFlatTransparentFillRecord(const XARRecord& record);
        void ParseLinearTransparentFillRecord(const XARRecord& record, bool threePoint);
        void ParseRadialTransparentFillRecord(const XARRecord& record, XARTransparencyType tt, bool elliptical);
        void ParseConicalTransparentFillRecord(const XARRecord& record);
        void ParseBitmapTransparentFillRecord(const XARRecord& record);
        void ParseFractalTransparentFillRecord(const XARRecord& record, bool noise);
        void ParseDiamondTransparentFillRecord(const XARRecord& record);
        void ParseThreeFourColTransparentFillRecord(const XARRecord& record, bool four);
        void ParseLineColourRecord(const XARRecord& record);
        void ParseLineWidthRecord(const XARRecord& record);
        void ParseLineCapRecord(const XARRecord& record, bool isStart);
        void ParseLineJoinRecord(const XARRecord& record);
        void ParseLineMitreLimitRecord(const XARRecord& record);
        void ParseLineTransparencyRecord(const XARRecord& record);
        void ParseDashStyleRecord(const XARRecord& record);
        void ParseDefineDashRecord(const XARRecord& record, bool scaled);
        void ParseArrowRecord(const XARRecord& record, bool isStart);
        void ParseDefineArrowRecord(const XARRecord& record);
        void ParseWindingRuleRecord(const XARRecord& record);
        void ParseShadowRecord(const XARRecord& record);
        void ParseBevelRecord(const XARRecord& record);
        void ParseContourRecord(const XARRecord& record);
        void ParseBlendRecord(const XARRecord& record);
        void ParseMouldRecord(const XARRecord& record, bool perspective);
        void ParseClipViewRecord(const XARRecord& record);
        void ParseFeatherRecord(const XARRecord& record);
        void ParseLiveEffectRecord(const XARRecord& record);
        void ParseBrushRecord(const XARRecord& record);
        void ParseObjectBoundsRecord(const XARRecord& record);
        void ParseDocumentInfoRecord(const XARRecord& record);

        // Binary readers (operate on the active stream)
        uint8_t ReadByte(const uint8_t* d, size_t& o);
        uint16_t ReadUInt16(const uint8_t* d, size_t& o);
        int16_t ReadInt16(const uint8_t* d, size_t& o);
        uint32_t ReadUInt32(const uint8_t* d, size_t& o);
        int32_t ReadInt32(const uint8_t* d, size_t& o);
        double ReadDouble(const uint8_t* d, size_t& o);
        float ReadFloat(const uint8_t* d, size_t& o);
        std::string ReadUTF16String(const uint8_t* d, size_t& o, size_t maxBytes);
        std::string ReadASCIIString(const uint8_t* d, size_t& o, size_t maxBytes);
        Point2Di ReadCoord(const uint8_t* d, size_t& o);
        XARMatrix ReadMatrix(const uint8_t* d, size_t& o);
        Color ReadColor3(const uint8_t* d, size_t& o);
        // Refined relative coord (interleaved MSB-first, delta-encoded)
        Point2Di ReadRelativeCoord(const uint8_t* d, size_t& o, Point2Di& last);

        void PushNode(XARNodePtr node);
        void PopNode();
        XARNodePtr CurrentNode();
        void RegisterRenderableNode(XARNodePtr node);
        void ApplyCurrentAttributesTo(XARNodePtr node);

        // Active stream
        const uint8_t* curData = nullptr;
        size_t curSize = 0;
        size_t curOffset = 0;
        std::vector<StreamFrame> streamStack;   // outer stream(s) when inside compression
        std::vector<std::vector<uint8_t>> inflatedBuffers;  // owned inflated payloads

        float width = 0.0f;
        float height = 0.0f;
        int32_t spreadWidthMP = 0;
        int32_t spreadHeightMP = 0;
        Rect2Dd viewport;
        bool haveViewport = false;
        bool haveSpreadInfo = false;

        XARNodePtr root;
        std::stack<XARNodePtr> nodeStack;
        XARRenderingContext currentContext;
        std::stack<XARRenderingContext> contextStack;

        std::unordered_map<int32_t, XARColorDefinition> colors;
        std::unordered_map<int32_t, XARBitmapDefinition> bitmaps;
        std::unordered_map<int32_t, XARFontDefinition> fonts;
        std::unordered_map<int32_t, XARArrowDefinition> arrows;
        std::unordered_map<int32_t, XARDashDefinition> dashes;
        std::unordered_map<int32_t, std::shared_ptr<XARPathNode>> pathsBySequence;

        int32_t currentSequenceNumber = 0;
        Rect2Dd pendingObjectBounds;
        bool havePendingBounds = false;

        std::string fileType;
        uint32_t refinementFlags = 0;
        uint32_t fileSizeHint = 0;
        std::string producer;
        std::string producerVersion;
        std::string producerBuild;
    };

// ===== UI ELEMENT =====

    class UltraCanvasXARElement : public UltraCanvasUIElement {
    public:
        UltraCanvasXARElement(const std::string& identifier,
                              float x, float y, float w, float h);
        virtual ~UltraCanvasXARElement() = default;

        bool LoadFromFile(const std::string& filepath);
        bool LoadFromMemory(const uint8_t* data, size_t size);

        // Reason for the most recent failed load (locked / missing / not a valid
        // XAR file). Empty after a successful load.
        const std::string& GetLastError() const { return lastError; }

        void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

        void SetScale(float s) { scale = s; }
        float GetScale() const { return scale; }
        void SetPreserveAspectRatio(bool preserve) { preserveAspectRatio = preserve; }
        bool GetPreserveAspectRatio() const { return preserveAspectRatio; }

        const XARDocument* GetDocument() const { return document.get(); }

    private:
        std::unique_ptr<XARDocument> document;
        std::string lastError;
        float scale = 1.0f;
        bool preserveAspectRatio = true;
    };

// ===== PLUGIN =====

    class UltraCanvasXARPlugin : public IGraphicsPlugin {
    public:
        UltraCanvasXARPlugin() = default;
        ~UltraCanvasXARPlugin() override = default;

        std::string GetPluginName() const override { return "UltraCanvas XAR Plugin"; }
        std::string GetPluginVersion() const override { return "2.0.0"; }
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
            return GraphicsManipulation::Move | GraphicsManipulation::Rotate |
                   GraphicsManipulation::Scale | GraphicsManipulation::Flip |
                   GraphicsManipulation::Transform;
        }

        GraphicsFileInfo GetFileInfo(const std::string& filePath) override;
        bool ValidateFile(const std::string& filePath) override;

    private:
        std::string GetFileExtension(const std::string& filePath) const;
    };

    inline std::shared_ptr<UltraCanvasXARPlugin> CreateXARPlugin() {
        return std::make_shared<UltraCanvasXARPlugin>();
    }

    inline void RegisterXARPlugin() {
        UltraCanvasGraphicsPluginRegistry::RegisterPlugin(CreateXARPlugin());
    }

// ===== BUILDER =====

    class XARElementBuilder {
    private:
        std::string identifier = "XARElement";
        long x = 0, y = 0, w = 400, h = 400;
        std::string filePath;
        float scale = 1.0f;
        bool preserveAspectRatio = true;

    public:
        XARElementBuilder& SetIdentifier(const std::string& s) { identifier = s; return *this; }
        XARElementBuilder& SetPosition(long px, long py) { x = px; y = py; return *this; }
        XARElementBuilder& SetSize(long ww, long hh) { w = ww; h = hh; return *this; }
        XARElementBuilder& SetFilePath(const std::string& p) { filePath = p; return *this; }
        XARElementBuilder& SetScale(float s) { scale = s; return *this; }
        XARElementBuilder& SetPreserveAspectRatio(bool p) { preserveAspectRatio = p; return *this; }

        std::shared_ptr<UltraCanvasXARElement> Build() {
            auto element = std::make_shared<UltraCanvasXARElement>(identifier, x, y, w, h);
            element->SetScale(scale);
            element->SetPreserveAspectRatio(preserveAspectRatio);
            if (!filePath.empty()) element->LoadFromFile(filePath);
            return element;
        }
    };

} // namespace UltraCanvas
