// UltraCanvasVectorStorage.h
// Comprehensive Internal Vector Graphics Storage System for UltraCanvas
// Version: 1.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <memory>
#include <string>
#include <variant>
#include <optional>
#include <map>
#include <array>

namespace UltraCanvas {
namespace VectorStorage {

// ===== FORWARD DECLARATIONS =====
class VectorElement;
class VectorDocument;
class VectorLayer;
class VectorGroup;
class VectorTransform;

// ===== CORE ENUMS =====

enum class VectorElementType {
    None = 0,
    
    // Basic Shapes
    Rectangle,
    RoundedRectangle,
    Circle,
    Ellipse,
    Line,
    Polyline,
    Polygon,
    Path,
    
    // Text
    Text,
    TextPath,
    TextSpan,
    
    // Grouping & Structure
    Group,
    Layer,
    Symbol,
    Use,  // Reference to symbol
    
    // Advanced Shapes
    Star,
    RegularPolygon,
    Spiral,
    Arc,
    Chord,
    Pie,
    
    // Special Elements
    Image,
    ClipPath,
    Mask,
    Pattern,
    Marker,
    Filter,
    
    // Gradients & Fills
    LinearGradient,
    RadialGradient,
    ConicalGradient,
    MeshGradient,
    
    // Effects
    DropShadow,
    InnerShadow,
    Glow,
    Blur,
    
    // 3D Elements (for future extension)
    Extrude,
    Revolve,
    Bevel
};

enum class PathCommandType {
    MoveTo,           // M x y
    LineTo,           // L x y
    HorizontalLineTo, // H x
    VerticalLineTo,   // V y
    CurveTo,          // C x1 y1 x2 y2 x y (cubic bezier)
    SmoothCurveTo,    // S x2 y2 x y
    QuadraticTo,      // Q x1 y1 x y
    SmoothQuadraticTo,// T x y
    ArcTo,            // A rx ry x-axis-rotation large-arc sweep x y
    ClosePath,        // Z
    
    // Extended commands (beyond SVG)
    CatmullRom,       // Catmull-Rom spline
    BSpline,          // B-Spline curve
    NurbsCurve        // NURBS curve
};

enum class FillRule {
    NonZero,    // SVG: nonzero
    EvenOdd,    // SVG: evenodd
    Winding     // Extended: winding number rule
};

enum class StrokeLineCap {
    Butt,
    Round,
    Square
};

enum class StrokeLineJoin {
    Miter,
    Round,
    Bevel,
    ArcsJoin,    // Extended: arc join
    MiterClip    // Extended: clipped miter
};

enum class TextAnchor {
    Start,
    Middle,
    End
};

enum class TextBaseline {
    Auto,
    Alphabetic,
    Hanging,
    Mathematical,
    Central,
    Middle,
    TextAfterEdge,
    TextBeforeEdge,
    Ideographic
};

enum class BlendMode {
    Normal,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity,
    
    // Extended blend modes
    PinLight,
    VividLight,
    LinearLight,
    HardMix,
    Subtract,
    Divide
};

enum class GradientSpreadMethod {
    Pad,      // Default: extend with edge colors
    Reflect,  // Mirror gradient
    Repeat,   // Tile gradient
    Truncate  // Extended: hard stop at edges
};

enum class GradientUnits {
    UserSpaceOnUse,     // Absolute coordinates
    ObjectBoundingBox   // Relative to object bounds (0-1)
};

enum class FilterType {
    GaussianBlur,
    MotionBlur,
    BoxBlur,
    DropShadow,
    InnerShadow,
    ColorMatrix,
    ConvolveMatrix,
    Morphology,
    Displacement,
    Turbulence,
    Composite,
    Merge,
    Offset,
    Flood,
    Image,
    Tile,
    
    // Extended filters
    LensBlur,
    RadialBlur,
    ZoomBlur,
    SmartBlur,
    UnsharpMask,
    HighPass,
    Emboss,
    FindEdges,
    Posterize,
    Threshold
};

enum class MarkerOrientation {
    Auto,           // Follow path direction
    AutoStartReverse, // Reverse at start
    Angle          // Fixed angle
};

// ===== CORE DATA STRUCTURES =====

struct PathCommand {
    PathCommandType Type;
    std::vector<float> Parameters;
    bool Relative = false;  // Relative vs absolute coordinates
};

struct PathData {
    std::vector<PathCommand> Commands;
    bool Closed = false;
    
    // Computed properties (cached)
    mutable std::optional<Rect2Df> BoundingBox;
    mutable std::optional<float> Length;
    mutable std::optional<std::vector<Point2Df>> FlattenedPoints;
};

struct GradientStop {
    float Offset;       // 0.0 to 1.0
    Color StopColor;
    float Opacity = 1.0f;
};

struct LinearGradientData {
    Point2Df Start;     // x1, y1
    Point2Df End;       // x2, y2
    std::vector<GradientStop> Stops;
    GradientUnits Units = GradientUnits::ObjectBoundingBox;
    GradientSpreadMethod SpreadMethod = GradientSpreadMethod::Pad;
    std::optional<Matrix3x3> Transform;
};

struct RadialGradientData {
    Point2Df Center;    // cx, cy
    float Radius;       // r
    Point2Df FocalPoint; // fx, fy
    float FocalRadius = 0.0f; // fr (SVG 2.0)
    std::vector<GradientStop> Stops;
    GradientUnits Units = GradientUnits::ObjectBoundingBox;
    GradientSpreadMethod SpreadMethod = GradientSpreadMethod::Pad;
    std::optional<Matrix3x3> Transform;
};

struct ConicalGradientData {
    Point2Df Center;
    float StartAngle;
    float EndAngle;
    std::vector<GradientStop> Stops;
    GradientUnits Units = GradientUnits::ObjectBoundingBox;
    std::optional<Matrix3x3> Transform;
};

struct MeshPatch {
    std::array<Point2Df, 4> Corners;       // Corner points
    std::array<Point2Df, 8> ControlPoints; // Bezier control points
    std::array<Color, 4> Colors;           // Corner colors
    std::array<float, 4> Opacities;        // Corner opacities
};

struct MeshGradientData {
    std::vector<MeshPatch> Patches;
    GradientUnits Units = GradientUnits::ObjectBoundingBox;
    std::optional<Matrix3x3> Transform;
};

using GradientData = std::variant<
    LinearGradientData,
    RadialGradientData,
    ConicalGradientData,
    MeshGradientData
>;

struct PatternData {
    std::shared_ptr<VectorGroup> Content;
    Rect2Df ViewBox;
    Rect2Df PatternRect;  // x, y, width, height
    GradientUnits Units = GradientUnits::ObjectBoundingBox;
    GradientUnits ContentUnits = GradientUnits::UserSpaceOnUse;
    std::optional<Matrix3x3> Transform;
};

using FillData = std::variant<
    std::monostate,      // No fill
    Color,               // Solid color
    GradientData,        // Gradient
    PatternData,         // Pattern
    std::string          // Reference to gradient/pattern by ID
>;

struct StrokeData {
    FillData Fill;
    float Width = 1.0f;
    StrokeLineCap LineCap = StrokeLineCap::Butt;
    StrokeLineJoin LineJoin = StrokeLineJoin::Miter;
    float MiterLimit = 4.0f;
    std::vector<float> DashArray;
    float DashOffset = 0.0f;
    float Opacity = 1.0f;
    
    // Extended properties
    bool ScaleWithObject = true;
    float MinWidth = 0.0f;
    float MaxWidth = std::numeric_limits<float>::max();
};

struct FilterEffect {
    FilterType Type;
    std::map<std::string, std::variant<float, int, std::string, Color>> Parameters;
    std::optional<std::string> Input;   // Previous result or source
    std::optional<std::string> Result;  // Name for this result
};

struct FilterData {
    std::vector<FilterEffect> Effects;
    Rect2Df FilterRegion;  // Filter effects region
    GradientUnits Units = GradientUnits::ObjectBoundingBox;
    std::optional<std::string> Id;
};

struct MarkerData {
    std::shared_ptr<VectorGroup> Content;
    Rect2Df ViewBox;
    Point2Df RefPoint;     // Reference point (refX, refY)
    Size2Df MarkerSize;    // Marker width/height
    MarkerOrientation Orientation = MarkerOrientation::Auto;
    float OrientationAngle = 0.0f;
    GradientUnits Units = GradientUnits::StrokeWidth;
};

struct TextStyle {
    std::string FontFamily = "sans-serif";
    float FontSize = 12.0f;
    FontWeight Weight = FontWeight::Normal;
    FontStyle Style = FontStyle::Normal;
    TextAnchor Anchor = TextAnchor::Start;
    TextBaseline Baseline = TextBaseline::Alphabetic;
    float LetterSpacing = 0.0f;
    float WordSpacing = 0.0f;
    float LineHeight = 1.2f;
    
    // Text decoration
    bool Underline = false;
    bool Overline = false;
    bool StrikeThrough = false;
    
    // Advanced typography
    std::vector<std::string> FontFeatures;  // OpenType features
    std::map<std::string, float> FontVariations; // Variable font axes
};

struct TextSpanData {
    std::string Text;
    TextStyle Style;
    std::optional<Point2Df> Position;  // x, y
    std::vector<float> DeltaX;         // Relative x positions
    std::vector<float> DeltaY;         // Relative y positions
    std::vector<float> Rotate;         // Per-character rotation
};

struct TextPathData {
    std::vector<TextSpanData> Spans;
    std::string PathReference;         // ID of path to follow
    float StartOffset = 0.0f;
    std::string Method = "align";      // align or stretch
    std::string Spacing = "auto";      // auto or exact
    std::string Side = "left";         // left or right of path
};

struct ClipPathData {
    std::vector<std::shared_ptr<VectorElement>> Elements;
    FillRule ClipRule = FillRule::NonZero;
    GradientUnits Units = GradientUnits::UserSpaceOnUse;
};

struct MaskData {
    std::vector<std::shared_ptr<VectorElement>> Elements;
    Rect2Df MaskRegion;
    GradientUnits Units = GradientUnits::ObjectBoundingBox;
    GradientUnits ContentUnits = GradientUnits::UserSpaceOnUse;
    std::string MaskType = "luminance";  // luminance or alpha
};

// ===== TRANSFORM SYSTEM =====

class Matrix3x3 {
public:
    float m[3][3];
    
    Matrix3x3();  // Identity
    
    static Matrix3x3 Identity();
    static Matrix3x3 Translate(float tx, float ty);
    static Matrix3x3 Scale(float sx, float sy);
    static Matrix3x3 Rotate(float angle);  // In radians
    static Matrix3x3 RotateDegrees(float degrees);
    static Matrix3x3 SkewX(float angle);
    static Matrix3x3 SkewY(float angle);
    static Matrix3x3 FromValues(float a, float b, float c, float d, float e, float f);
    
    Matrix3x3 operator*(const Matrix3x3& other) const;
    Point2Df Transform(const Point2Df& point) const;
    Rect2Df Transform(const Rect2Df& rect) const;
    
    Matrix3x3 Inverse() const;
    float Determinant() const;
};

// ===== STYLE SYSTEM =====

struct VectorStyle {
    // Fill & Stroke
    std::optional<FillData> Fill;
    std::optional<StrokeData> Stroke;
    
    // Opacity
    float Opacity = 1.0f;
    float FillOpacity = 1.0f;
    float StrokeOpacity = 1.0f;
    
    // Blending
    BlendMode BlendMode = BlendMode::Normal;
    
    // Clipping & Masking
    std::optional<std::string> ClipPath;  // Reference by ID
    std::optional<std::string> Mask;      // Reference by ID
    FillRule ClipRule = FillRule::NonZero;
    
    // Filters & Effects
    std::vector<std::string> Filters;     // References by ID
    
    // Markers (for paths)
    std::optional<std::string> MarkerStart;
    std::optional<std::string> MarkerMid;
    std::optional<std::string> MarkerEnd;
    
    // Visibility
    bool Visible = true;
    bool Display = true;
    
    // Paint order (SVG 2.0)
    std::vector<std::string> PaintOrder = {"fill", "stroke", "markers"};
    
    // Vector effect (non-scaling stroke, etc.)
    std::string VectorEffect = "none";
    
    // Shape rendering hint
    std::string ShapeRendering = "auto";  // auto, optimizeSpeed, crispEdges, geometricPrecision
    
    // Extended properties
    std::optional<Color> ShadowColor;
    std::optional<Point2Df> ShadowOffset;
    std::optional<float> ShadowBlur;
    
    // Merge with parent style
    void Inherit(const VectorStyle& parent);
};

// ===== ELEMENT CLASSES =====

class VectorElement {
public:
    // Identification
    std::string Id;
    std::string Class;
    std::vector<std::string> ClassList;
    
    // Hierarchy
    VectorElementType Type = VectorElementType::None;
    std::weak_ptr<VectorElement> Parent;
    
    // Transformation
    std::optional<Matrix3x3> Transform;
    
    // Style
    VectorStyle Style;
    
    // Metadata
    std::string Title;
    std::string Description;
    std::map<std::string, std::string> Metadata;
    
    // Animation (for future extension)
    std::map<std::string, std::string> AnimationAttributes;
    
    // Interactivity
    std::map<std::string, std::string> EventHandlers;
    std::string Cursor = "default";
    bool PointerEvents = true;
    
    // Methods
    virtual ~VectorElement() = default;
    virtual Rect2Df GetBoundingBox() const = 0;
    virtual std::shared_ptr<VectorElement> Clone() const = 0;
    virtual void Accept(class IVectorVisitor* visitor) = 0;
    
    // Transform helpers
    Matrix3x3 GetGlobalTransform() const;
    Point2Df LocalToGlobal(const Point2Df& point) const;
    Point2Df GlobalToLocal(const Point2Df& point) const;
};

// Basic Shapes

class VectorRect : public VectorElement {
public:
    Rect2Df Bounds;
    float RadiusX = 0.0f;  // Rounded corners
    float RadiusY = 0.0f;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorCircle : public VectorElement {
public:
    Point2Df Center;
    float Radius;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorEllipse : public VectorElement {
public:
    Point2Df Center;
    float RadiusX;
    float RadiusY;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorLine : public VectorElement {
public:
    Point2Df Start;
    Point2Df End;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorPolyline : public VectorElement {
public:
    std::vector<Point2Df> Points;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorPolygon : public VectorElement {
public:
    std::vector<Point2Df> Points;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorPath : public VectorElement {
public:
    PathData Path;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
    
    // Path manipulation
    void AddCommand(const PathCommand& cmd);
    void MoveTo(float x, float y, bool relative = false);
    void LineTo(float x, float y, bool relative = false);
    void CurveTo(float x1, float y1, float x2, float y2, float x, float y, bool relative = false);
    void QuadraticTo(float x1, float y1, float x, float y, bool relative = false);
    void ArcTo(float rx, float ry, float rotation, bool largeArc, bool sweep, float x, float y, bool relative = false);
    void ClosePath();
    
    // Path analysis
    float GetLength() const;
    Point2Df GetPointAtLength(float length) const;
    float GetAngleAtLength(float length) const;
    std::vector<Point2Df> Flatten(float tolerance = 0.1f) const;
};

// Text Elements

class VectorText : public VectorElement {
public:
    Point2Df Position;
    std::vector<TextSpanData> Spans;
    TextStyle BaseStyle;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
    
    // Text manipulation
    void SetText(const std::string& text);
    void AddSpan(const TextSpanData& span);
    std::string GetPlainText() const;
};

class VectorTextPath : public VectorElement {
public:
    TextPathData Data;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

// Container Elements

class VectorGroup : public VectorElement {
public:
    std::vector<std::shared_ptr<VectorElement>> Children;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
    
    // Child management
    void AddChild(std::shared_ptr<VectorElement> child);
    void RemoveChild(const std::string& id);
    std::shared_ptr<VectorElement> FindChild(const std::string& id) const;
    void ClearChildren();
};

class VectorSymbol : public VectorGroup {
public:
    Rect2Df ViewBox;
    std::string PreserveAspectRatio = "xMidYMid meet";
    
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorUse : public VectorElement {
public:
    std::string Reference;  // ID of symbol to use
    Point2Df Position;
    Size2Df Size;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

// Special Elements

class VectorImage : public VectorElement {
public:
    Rect2Df Bounds;
    std::string Source;     // URL or embedded data
    std::vector<uint8_t> EmbeddedData;
    std::string MimeType;
    std::string PreserveAspectRatio = "xMidYMid meet";
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

// Definition Elements (stored but not rendered directly)

class VectorGradient : public VectorElement {
public:
    GradientData Data;
    
    Rect2Df GetBoundingBox() const override { return Rect2Df(); }
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorPattern : public VectorElement {
public:
    PatternData Data;
    
    Rect2Df GetBoundingBox() const override { return Rect2Df(); }
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorFilter : public VectorElement {
public:
    FilterData Data;
    
    Rect2Df GetBoundingBox() const override { return Rect2Df(); }
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorClipPath : public VectorElement {
public:
    ClipPathData Data;
    
    Rect2Df GetBoundingBox() const override;
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorMask : public VectorElement {
public:
    MaskData Data;
    
    Rect2Df GetBoundingBox() const override { return Data.MaskRegion; }
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorMarker : public VectorElement {
public:
    MarkerData Data;
    
    Rect2Df GetBoundingBox() const override { return Rect2Df(); }
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

// ===== DOCUMENT STRUCTURE =====

class VectorLayer : public VectorGroup {
public:
    std::string Name;
    bool Locked = false;
    bool Visible = true;
    float Opacity = 1.0f;
    BlendMode BlendMode = BlendMode::Normal;
    
    std::shared_ptr<VectorElement> Clone() const override;
    void Accept(IVectorVisitor* visitor) override;
};

class VectorDocument {
public:
    // Document properties
    Size2Df Size;                          // Document dimensions
    Rect2Df ViewBox;                       // Coordinate system
    std::string PreserveAspectRatio = "xMidYMid meet";
    Color BackgroundColor = Colors::Transparent;
    
    // Content
    std::vector<std::shared_ptr<VectorLayer>> Layers;
    
    // Definitions (reusable elements)
    std::map<std::string, std::shared_ptr<VectorElement>> Definitions;
    
    // Metadata
    std::string Title;
    std::string Description;
    std::string Author;
    std::string Copyright;
    std::map<std::string, std::string> Metadata;
    
    // Style sheets
    std::string StyleSheet;  // CSS-like styles
    std::map<std::string, VectorStyle> NamedStyles;
    
    // Methods
    std::shared_ptr<VectorLayer> AddLayer(const std::string& name);
    void RemoveLayer(const std::string& name);
    std::shared_ptr<VectorLayer> GetLayer(const std::string& name) const;
    
    void AddDefinition(const std::string& id, std::shared_ptr<VectorElement> element);
    std::shared_ptr<VectorElement> GetDefinition(const std::string& id) const;
    
    std::shared_ptr<VectorElement> FindElementById(const std::string& id) const;
    std::vector<std::shared_ptr<VectorElement>> FindElementsByClass(const std::string& className) const;
    
    Rect2Df GetBoundingBox() const;
    void FitToContent(float padding = 0.0f);
    
    // Import/Export helpers
    void Clear();
    std::shared_ptr<VectorDocument> Clone() const;
};

// ===== VISITOR PATTERN FOR TRAVERSAL =====

class IVectorVisitor {
public:
    virtual ~IVectorVisitor() = default;
    
    virtual void Visit(VectorRect* element) = 0;
    virtual void Visit(VectorCircle* element) = 0;
    virtual void Visit(VectorEllipse* element) = 0;
    virtual void Visit(VectorLine* element) = 0;
    virtual void Visit(VectorPolyline* element) = 0;
    virtual void Visit(VectorPolygon* element) = 0;
    virtual void Visit(VectorPath* element) = 0;
    virtual void Visit(VectorText* element) = 0;
    virtual void Visit(VectorTextPath* element) = 0;
    virtual void Visit(VectorGroup* element) = 0;
    virtual void Visit(VectorSymbol* element) = 0;
    virtual void Visit(VectorUse* element) = 0;
    virtual void Visit(VectorImage* element) = 0;
    virtual void Visit(VectorGradient* element) = 0;
    virtual void Visit(VectorPattern* element) = 0;
    virtual void Visit(VectorFilter* element) = 0;
    virtual void Visit(VectorClipPath* element) = 0;
    virtual void Visit(VectorMask* element) = 0;
    virtual void Visit(VectorMarker* element) = 0;
    virtual void Visit(VectorLayer* element) = 0;
};

// ===== UTILITY FUNCTIONS =====

// Path string parsing (SVG path data format)
PathData ParsePathString(const std::string& pathStr);
std::string SerializePathData(const PathData& path);

// Color parsing
Color ParseColorString(const std::string& colorStr);
std::string SerializeColor(const Color& color);

// Transform parsing
Matrix3x3 ParseTransformString(const std::string& transformStr);
std::string SerializeTransform(const Matrix3x3& transform);

// Bounding box calculations
Rect2Df CalculatePathBounds(const PathData& path);
Rect2Df CalculateTextBounds(const std::vector<TextSpanData>& spans, const TextStyle& style);

// Path operations
PathData CombinePaths(const PathData& path1, const PathData& path2, bool union_op = true);
PathData OffsetPath(const PathData& path, float offset);
PathData SimplifyPath(const PathData& path, float tolerance);
bool IsPointInPath(const PathData& path, const Point2Df& point, FillRule rule = FillRule::NonZero);

// Conversion helpers
std::vector<Point2Df> PathToPolygon(const PathData& path, float tolerance = 0.1f);
PathData PolygonToPath(const std::vector<Point2Df>& points, bool closed = true);

} // namespace VectorStorage
} // namespace UltraCanvas
