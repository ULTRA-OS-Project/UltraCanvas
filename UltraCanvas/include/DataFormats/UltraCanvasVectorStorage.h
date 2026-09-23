// UltraCanvasVectorStorage.h
// Internal Vector Graphics Storage System for UltraCanvas
// Version: 2.0.0
// Last Modified: 2025-01-20
// Author: UltraCanvas Framework
//
// REFACTORED: Uses types from UltraCanvasRenderContext.h (GradientStop, FontWeight, FontSlant)
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <memory>
#include <string>
#include <variant>
#include <optional>
#include <map>
#include <array>
#include <cmath>
#include <algorithm>
#include <limits>

namespace UltraCanvas {
    namespace VectorStorage {

// Forward declarations
        class VectorElement;

        class VectorDocument;

        class VectorLayer;

        class VectorGroup;

// ===== ENUMS =====

        enum class VectorElementType {
            NoneType = 0,
            Rectangle,
            RoundedRectangle,
            Circle,
            Ellipse,
            Line,
            Polyline,
            Polygon,
            Path,
            Text,
            TextPath,
            TextSpan,
            Group,
            Layer,
            Symbol,
            Use,
            Star,
            RegularPolygon,
            Arc,
            Image,
            ClipPath,
            Mask,
            Pattern,
            Marker,
            Filter,
            LinearGradient,
            RadialGradient,
            ConicalGradient,
            MeshGradient,
            // Xara-class containers (phase 5): a group clipped to its first
            // children, a blend between its children, a group moulded into
            // a shape.
            ClipView,
            Blend,
            Mould
        };
        // Element kinds that hold children (all derive from VectorGroup).
        constexpr bool IsGroupType(VectorElementType t) {
            return t == VectorElementType::Group || t == VectorElementType::Layer || t == VectorElementType::Symbol ||
                   t == VectorElementType::ClipView || t == VectorElementType::Blend || t == VectorElementType::Mould;
        }

        enum class PathCommandType {
            MoveTo,
            LineTo,
            HorizontalLineTo,
            VerticalLineTo,
            CurveTo,
            SmoothCurveTo,
            QuadraticTo,
            SmoothQuadraticTo,
            ArcTo,
            ClosePath
        };
        enum class FillRule {
            NonZero, EvenOdd
        };
        enum class StrokeLineCap {
            Butt, Round, Square
        };
        enum class StrokeLineJoin {
            Miter, Round, Bevel
        };
        enum class TextAnchor {
            Start, Middle, End
        };
        enum class TextBaseline {
            Auto, Alphabetic, Hanging, Central, Middle
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
            Exclusion
        };
        enum class GradientSpreadMethod {
            Pad, Reflect, Repeat
        };
        enum class GradientUnits {
            UserSpaceOnUse, ObjectBoundingBox
        };
        enum class FilterType {
            GaussianBlur, DropShadow, ColorMatrix, Offset, Composite
        };
        enum class MarkerOrientation {
            Auto, AutoStartReverse, Angle
        };

// ===== MATRIX 3x3 =====

        // Row-major affine matrix: x' = m[0][0] x + m[0][1] y + m[0][2],
        // y' = m[1][0] x + m[1][1] y + m[1][2]. Double precision, so the
        // coordinates of CAD drawings (hundreds of thousands of units) keep
        // their sub-unit accuracy through nested block transforms.
        class Matrix3x3 {
        public:
            double m[3][3];

            Matrix3x3();
            static Matrix3x3 Identity();
            static Matrix3x3 Translate(double tx, double ty);
            static Matrix3x3 Scale(double sx, double sy);
            static Matrix3x3 Rotate(double a);
            static Matrix3x3 RotateDegrees(double degrees);
            static Matrix3x3 SkewX(double angle);
            static Matrix3x3 SkewY(double angle);
            // Row-major: x' = a x + b y + e, y' = c x + d y + f. Note this is
            // NOT the SVG/PostScript matrix(a, b, c, d, e, f) order, where
            // b and c are swapped - callers converting from those formats
            // pass FromValues(a, c, b, d, e, f).
            static Matrix3x3 FromValues(double a, double b, double c, double d, double e, double f);
            Matrix3x3 operator*(const Matrix3x3 &o) const;
            Point2Dd Transform(const Point2Dd &p) const;
            Rect2Dd Transform(const Rect2Dd& rect) const;
            double Determinant() const;
            Matrix3x3 Inverse() const;
            bool IsIdentity(double epsilon = 1e-9) const;
        };

// ===== UNITS =====

        // Physical length units a source file can measure in. The model
        // itself is always in points (1/72 in); readers record which unit
        // the file used and how many points they made of one unit, so a
        // consumer can recover the source measurements and a writer with a
        // unit field (DXF $INSUNITS) can round-trip them.
        enum class LengthUnit {
            Unspecified,   // unitless drawing units, or unknown
            Point,
            Pixel,         // CSS pixel, 96 per inch
            Inch,
            Foot,
            Yard,
            Mile,
            Mil,           // 1/1000 in
            Millimeter,
            Centimeter,
            Decimeter,
            Meter,
            Kilometer,
            Micrometer,
            Nanometer
        };

        // Points per one unit; 0 for Unspecified.
        double PointsPerUnit(LengthUnit unit);
        // Short symbol ("mm", "in", "pt", ...); "" for Unspecified.
        const char* LengthUnitSymbol(LengthUnit unit);

// ===== PATH DATA =====

        struct PathCommand {
            PathCommandType Type;
            std::vector<float> Parameters;
            bool Relative = false;
        };

        struct PathData {
            std::vector<PathCommand> commands;
            bool Closed = false;
            mutable std::optional<Rect2Dd> cachedBounds;
            mutable std::optional<float> length;
            mutable std::optional<std::vector<Point2Dd>> flattenedPoints;

            Rect2Dd GetBounds() const {
                if (cachedBounds) return *cachedBounds;
                if (commands.empty()) return {0, 0, 0, 0};
                float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
                Point2Dd cur{0, 0};
                for (const auto &c: commands) {
                    if ((c.Type == PathCommandType::MoveTo || c.Type == PathCommandType::LineTo) &&
                        c.Parameters.size() >= 2) {
                        float x = c.Relative ? cur.x + c.Parameters[0] : c.Parameters[0];
                        float y = c.Relative ? cur.y + c.Parameters[1] : c.Parameters[1];
                        minX = std::min(minX, x);
                        minY = std::min(minY, y);
                        maxX = std::max(maxX, x);
                        maxY = std::max(maxY, y);
                        cur = {x, y};
                    } else if (c.Type == PathCommandType::CurveTo && c.Parameters.size() >= 6) {
                        for (int i = 0; i < 6; i += 2) {
                            float x = c.Parameters[i], y = c.Parameters[i + 1];
                            minX = std::min(minX, x);
                            minY = std::min(minY, y);
                            maxX = std::max(maxX, x);
                            maxY = std::max(maxY, y);
                        }
                        cur = {c.Parameters[4], c.Parameters[5]};
                    }
                }
                if (minX > maxX) return {0, 0, 0, 0};
                cachedBounds = Rect2Dd{minX, minY, maxX - minX, maxY - minY};
                return *cachedBounds;
            }

            void InvalidateCache() { cachedBounds.reset(); }
        };

// ===== GRADIENT DATA (Uses GradientStop from RenderContext.h) =====

        struct LinearGradientData {
            Point2Dd Start{0, 0};
            Point2Dd End{1, 0};
            std::vector<GradientStop> Stops;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
            GradientSpreadMethod SpreadMethod = GradientSpreadMethod::Pad;
            std::optional<Matrix3x3> Transform;
        };
        struct RadialGradientData {
            Point2Dd Center{0.5f, 0.5f};
            float Radius = 0.5f;
            Point2Dd FocalPoint{0.5f, 0.5f};
            float FocalRadius = 0;
            std::vector<GradientStop> Stops;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
            GradientSpreadMethod SpreadMethod = GradientSpreadMethod::Pad;
            std::optional<Matrix3x3> Transform;
        };
        struct ConicalGradientData {
            Point2Dd Center{0.5f, 0.5f};
            float StartAngle = 0;
            float EndAngle = 360;
            std::vector<GradientStop> Stops;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
            std::optional<Matrix3x3> Transform;
        };
        struct MeshPatch {
            std::array<Point2Dd, 4> Corners;
            std::array<Point2Dd, 8> ControlPoints;
            std::array<Color, 4> Colors;
        };
        struct MeshGradientData {
            std::vector<MeshPatch> Patches;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
            std::optional<Matrix3x3> Transform;
        };
        using GradientData = std::variant<LinearGradientData, RadialGradientData, ConicalGradientData, MeshGradientData>;

// ===== PATTERN & FILL =====

        struct PatternData {
            std::shared_ptr<VectorGroup> Content;
            Rect2Dd ViewBox;
            Rect2Dd PatternRect;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
            std::optional<Matrix3x3> Transform;
        };
        using FillData = std::variant<std::monostate, Color, GradientData, PatternData, std::string>;

// ===== LINE GALLERY (arrowheads, width profiles, brushes) =====

        // A gallery arrowhead at one end of a stroke, drawn in the stroke's
        // paint and sized from the line width. The first row is this
        // framework's own gallery, the tip on the line's end: at Scale 1 a
        // Triangle is four widths long and two wide; OpenArrow is the
        // unfilled chevron, Bar the perpendicular tick. The second row are
        // Xara's eight default arrowheads with Xara's own geometry and
        // placement (they reach past the line's end, as in Xara; Spot and
        // SolidDiamond sit centred on it) - the XAR converter writes them as
        // Xara's own line attributes. For them Scale 1 is Xara's default
        // arrow size (its size 3: a StraightArrow is 10.5 widths long).
        // Scale 0 draws nothing.
        enum class ArrowheadKind {
            NoArrowhead, Triangle, OpenArrow, Circle, Square, Diamond, Bar,
            StraightArrow, AngledArrow, RoundedArrow, Spot, SolidDiamond,
            Feather, Feather2, HollowDiamond
        };
        constexpr int ArrowheadKindCount = 15;
        constexpr bool IsXaraArrowhead(ArrowheadKind k) {
            return k >= ArrowheadKind::StraightArrow && k <= ArrowheadKind::HollowDiamond;
        }
        struct ArrowheadData {
            ArrowheadKind Kind = ArrowheadKind::NoArrowhead;
            float Scale = 1.0f;
            bool IsSet() const { return Kind != ArrowheadKind::NoArrowhead && Scale > 0; }
        };

        // One sample of a variable-width profile: at fraction T (0 = start,
        // 1 = end) of the path the line is Factor times Width wide. Samples
        // are sorted by T; the profile is linear between them and flat
        // outside. Two or more samples turn the stroke into a filled
        // outline (caps and dashes no longer apply).
        struct WidthSample {
            float T = 0.0f;
            float Factor = 1.0f;
        };

        // A vector brush: `Stamp` is drawn repeatedly along the path, scaled
        // so its own height becomes Width * Scale, one copy every Spacing
        // stamp-widths, rotated to the tangent when Rotate is set. A brush
        // replaces the plain stroke.
        struct BrushData {
            std::shared_ptr<VectorGroup> Stamp;
            float Spacing = 1.0f;
            float Scale = 1.0f;
            bool Rotate = true;
        };

        struct StrokeData {
            FillData Fill = Color(0, 0, 0, 255);
            float Width = 1.0f;
            StrokeLineCap LineCap = StrokeLineCap::Butt;
            StrokeLineJoin LineJoin = StrokeLineJoin::Miter;
            float MiterLimit = 4.0f;
            std::vector<double> DashArray;
            double DashOffset = 0.0f;
            float Opacity = 1.0f;
            // Line gallery (see above). Readers of formats without these
            // leave them at their defaults; the renderer draws them, the
            // XAR writer bakes them into geometry.
            ArrowheadData StartArrow;
            ArrowheadData EndArrow;
            std::vector<WidthSample> WidthProfile;
            std::optional<BrushData> Brush;

            bool HasArrowheads() const { return StartArrow.IsSet() || EndArrow.IsSet(); }
            bool HasWidthProfile() const { return WidthProfile.size() >= 2; }
            bool HasBrush() const { return Brush.has_value() && Brush->Stamp != nullptr; }
            // Width * the profile's factor at fraction t of the path.
            float WidthAt(float t) const;
        };

// ===== TRANSPARENCY (Xara-style: a level ramp over the object and a mix) =====

        // How the object composites with what is below it. Mix is normal
        // alpha; the others are Xara's names for the blend modes the
        // renderer maps them to (StainedGlass = multiply, Bleach = screen,
        // Contrast = overlay, Brightness = hard light).
        enum class TransparencyMix {
            Mix, StainedGlass, Bleach, Contrast, Saturation, Darken, Lighten,
            Brightness, Luminosity, Hue
        };
        enum class TransparencyShape { Flat, Linear, Radial, Conical };

        // Level 0 is opaque, 1 fully transparent - Xara's convention, the
        // inverse of alpha.
        struct TransparencyStop {
            double Position = 0.0;
            float Level = 0.0f;
        };

        // A transparency is a level ramp over the whole object (fill and
        // stroke together), unlike VectorStyle::Opacity which is one flat
        // level with the normal mix. Gradient axes are in the element's own
        // coordinate space: Start..End for Linear, Start = centre and End a
        // point on the radius for Radial and Conical (End sets the start
        // angle).
        struct TransparencyData {
            TransparencyShape Shape = TransparencyShape::Flat;
            float Level = 0.0f;                    // Flat
            Point2Dd Start{0, 0};
            Point2Dd End{1, 0};
            std::vector<TransparencyStop> Stops;   // gradients: at least two
            TransparencyMix Mix = TransparencyMix::Mix;

            bool IsGradient() const { return Shape != TransparencyShape::Flat && Stops.size() >= 2; }
            // The level at fraction t of the ramp (Flat: Level).
            float LevelAt(double t) const;
        };

// ===== TEXT (Uses FontWeight, FontSlant from RenderContext.h) =====

        struct VectorTextStyle {
            std::string FontFamily;
            float FontSize = 12.0f;
            FontWeight Weight = FontWeight::Normal;
            FontSlant Slant = FontSlant::Normal;
            TextAnchor Anchor = TextAnchor::Start;
            float LetterSpacing = 0;
            float LineHeight = 1.2f;
            bool Underline = false;
            bool StrikeThrough = false;

            FontStyle ToFontStyle() const {
                FontStyle fs;
                fs.fontFamily = FontFamily;
                fs.fontSize = FontSize;
                fs.fontWeight = Weight;
                fs.fontSlant = Slant;
                return fs;
            }
        };

        struct TextSpanData {
            std::string Text;
            VectorTextStyle Style;
            std::optional<Point2Dd> Position;
        };
        struct TextPathData {
            std::vector<TextSpanData> Spans;
            std::string PathReference;
            float StartOffset = 0;
        };

// ===== FILTER/CLIP/MASK/MARKER =====

        struct FilterEffect {
            FilterType Type;
            std::map<std::string, std::variant<float, int, std::string, Color>> Parameters;
        };
        struct FilterData {
            std::vector<FilterEffect> Effects;
            Rect2Dd FilterRegion;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
        };
        struct ClipPathData {
            std::vector<std::shared_ptr<VectorElement>> Elements;
            FillRule ClipRule = FillRule::NonZero;
        };
        struct MaskData {
            std::vector<std::shared_ptr<VectorElement>> Elements;
            Rect2Dd MaskRegion;
        };
        struct MarkerData {
            std::shared_ptr<VectorGroup> Content;
            Rect2Dd ViewBox;
            Point2Dd RefPoint;
            Size2Dd MarkerSize{3, 3};
            MarkerOrientation Orientation = MarkerOrientation::Auto;
        };

// ===== VECTOR STYLE =====

        struct VectorStyle {
            std::optional<FillData> Fill;
            std::optional<StrokeData> Stroke;
            float Opacity = 1.0f;
            float FillOpacity = 1.0f;
            float StrokeOpacity = 1.0f;
            BlendMode Blend = BlendMode::Normal;
            // Xara-style transparency (a level ramp and a mix) on top of the
            // flat Opacity; not inherited from a parent.
            std::optional<TransparencyData> Transparency;
            std::optional<std::string> ClipPath;
            std::optional<std::string> Mask;
            FillRule ClipRule = FillRule::NonZero;
            std::vector<std::string> Filters;
            bool Visible = true;
            bool Display = true;

            void Inherit(const VectorStyle& parent);
        };

// ===== EFFECTS (Xara-class, per object) =====

        // Wall: the silhouette offset behind the object. Floor: the
        // silhouette squashed and sheared from the object's bottom edge,
        // as if lying on the ground. Glow: the silhouette blurred outward
        // around the object with no offset.
        enum class ShadowKind { Wall, Floor, Glow };
        struct ShadowEffect {
            ShadowKind Kind = ShadowKind::Wall;
            Point2Dd Offset{4, 4};      // Wall: document units in the element's space
            float Blur = 4.0f;          // penumbra width, element units (0 = hard edge)
            Color Colour{0, 0, 0, 255};
            float Darkness = 0.5f;      // 0..1 opacity of the shadow
            float FloorSquash = 0.5f;   // Floor: height factor
            float FloorShear = 0.4f;    // Floor: horizontal shear per unit of height
        };

        // The object's edges fade out over Radius element units.
        struct FeatherEffect {
            float Radius = 4.0f;
        };

        // How a run of colours between two ends is walked (contours and
        // blends): Fade mixes straight between them, Rainbow goes the
        // short way round the hue circle, AltRainbow the long way, Constant
        // keeps the first colour throughout.
        enum class ColourBlendKind { Fade, Rainbow, AltRainbow, Constant };

        // Contour: `Steps` copies of the object's outline between it and a
        // copy offset by `Width` element units (positive: outward, drawn
        // behind the object; negative: inward, drawn over it), each filled
        // with the colour between the object's fill and `Colour` (the
        // outermost step's). Bias / Gain are Xara's step-spacing and
        // colour profiles (0, 0 = even), kept for the round trip.
        struct ContourEffect {
            int Steps = 5;
            float Width = 20.0f;
            ColourBlendKind Blend = ColourBlendKind::Fade;
            Color Colour{255, 255, 255, 255};
            bool InsetPath = false;
            double ObjectBias = 0, ObjectGain = 0, AttributeBias = 0, AttributeGain = 0;
        };

        // Bevel: a lit rim `Indent` element units wide inside the object's
        // edge (outside it when Outer is set), the light at `LightAngle`
        // degrees (0 = from the right, counter-clockwise on the page) and
        // `Tilt` degrees above the page, `Contrast` 0..1 the strength of
        // its highlights and shadows. The kinds are Xara's bevel
        // profiles: how the rim's height runs from the edge inward.
        enum class BevelKind {
            Flat, Round, HalfRound, Frame, Mesa1, Mesa2, Smooth1, Smooth2,
            Point1, Point2a, Point2b, Ruffle2a, Ruffle2b, Ruffle3a, Ruffle3b
        };
        constexpr int BevelKindCount = 15;
        struct BevelEffect {
            BevelKind Kind = BevelKind::Flat;
            float Indent = 8.0f;
            float LightAngle = 135.0f;
            float Tilt = 45.0f;
            float Contrast = 0.5f;
            bool Outer = false;
        };

        struct VectorEffects {
            std::optional<ShadowEffect> Shadow;
            std::optional<FeatherEffect> Feather;
            std::optional<ContourEffect> Contour;
            std::optional<BevelEffect> Bevel;
            bool Any() const { return Shadow.has_value() || Feather.has_value() || Contour.has_value() || Bevel.has_value(); }
        };

// ===== BASE ELEMENT =====

        class VectorElement {
        public:
            std::weak_ptr<VectorGroup> Parent;
            VectorElementType Type = VectorElementType::NoneType;
            std::string Id;
            std::vector<std::string> Classes;
            VectorStyle Style;
            std::optional<Matrix3x3> Transform;
            // Shadow and feather; rendered around the element in its own
            // space, cloned with it, never inherited.
            VectorEffects Effects;

            virtual ~VectorElement() { Parent.reset(); };

            virtual Rect2Dd GetBoundingBox() const { return {0, 0, 0, 0}; }
            virtual std::shared_ptr<VectorElement> Clone() const = 0;
            Matrix3x3 GetGlobalTransform() const;
            Point2Dd LocalToGlobal(const Point2Dd& point) const;
            Point2Dd GlobalToLocal(const Point2Dd& point) const;
            bool HasClass(const std::string &c) const {
                return std::find(Classes.begin(), Classes.end(), c) != Classes.end();
            }
        };

// ===== SHAPES =====

        class VectorRect : public VectorElement {
        public:
            Rect2Dd Bounds;
            float RadiusX = 0;
            float RadiusY = 0;

            VectorRect() { Type = VectorElementType::Rectangle; }

            Rect2Dd GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorCircle : public VectorElement {
        public:
            Point2Dd Center;
            float Radius = 0;

            VectorCircle() { Type = VectorElementType::Circle; }

            Rect2Dd GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorEllipse : public VectorElement {
        public:
            Point2Dd Center;
            float RadiusX = 0;
            float RadiusY = 0;

            VectorEllipse() { Type = VectorElementType::Ellipse; }
            Rect2Dd GetBoundingBox() const override;
            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorLine : public VectorElement {
        public:
            Point2Dd Start;
            Point2Dd End;

            VectorLine() { Type = VectorElementType::Line; }

            Rect2Dd GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorPolyline : public VectorElement {
        public:
            std::vector<Point2Dd> Points;

            VectorPolyline() { Type = VectorElementType::Polyline; }

            Rect2Dd GetBoundingBox() const override;
            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorPolygon : public VectorElement {
        public:
            std::vector<Point2Dd> Points;

            VectorPolygon() { Type = VectorElementType::Polygon; }

            Rect2Dd GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorPath : public VectorElement {
        public:
            PathData Path;

            VectorPath() { Type = VectorElementType::Path; }

            Rect2Dd GetBoundingBox() const override;
            void AddCommand(const PathCommand& cmd);

            std::shared_ptr<VectorElement> Clone() const override;

            void MoveTo(float x, float y, bool rel = false);
            void LineTo(float x, float y, bool rel = false);
            void CurveTo(float x1, float y1, float x2, float y2, float x, float y, bool rel = false);
            void QuadraticTo(float x1, float y1, float x, float y, bool relative);
            void ArcTo(float rx, float ry, float rotation, bool largeArc, bool sweep, float x, float y, bool relative);
            void ClosePath();

            float GetLength() const;
            Point2Dd GetPointAtLength(float length) const;
            float GetAngleAtLength(float length) const;

            std::vector<Point2Dd> Flatten(float tolerance) const;
        };

// ===== TEXT =====

        class VectorText : public VectorElement {
        public:
            Point2Dd Position;
            std::vector<TextSpanData> Spans;
            VectorTextStyle BaseStyle;

            VectorText() { Type = VectorElementType::Text; }

            Rect2Dd GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;

            void SetText(const std::string &t);
            void AddSpan(const TextSpanData& span);

            std::string GetPlainText() const;
        };

        class VectorTextPath : public VectorElement {
        public:
            TextPathData Data;

            VectorTextPath() { Type = VectorElementType::TextPath; }
            Rect2Dd GetBoundingBox() const override;
            std::shared_ptr<VectorElement> Clone() const override;
        };

// ===== CONTAINERS =====

        class VectorGroup : public VectorElement, public std::enable_shared_from_this<VectorGroup> {
        public:
            std::vector<std::shared_ptr<VectorElement>> Children;

            VectorGroup() { Type = VectorElementType::Group; }

            Rect2Dd GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;

            void AddChild(std::shared_ptr<VectorElement> c);

            void RemoveChild(const std::string &id);
            void ClearChildren();
            std::shared_ptr<VectorElement> FindChild(const std::string &id) const;
        };

        class VectorSymbol : public VectorGroup {
        public:
            Rect2Dd ViewBox;
            std::string PreserveAspectRatio = "xMidYMid meet";

            VectorSymbol() { Type = VectorElementType::Symbol; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        // ClipView: the first `Keyholes` children are the clip shapes (not
        // drawn themselves); the rest are drawn clipped to their union.
        class VectorClipView : public VectorGroup {
        public:
            int Keyholes = 1;

            VectorClipView() { Type = VectorElementType::ClipView; }

            // The keyholes' bounds: what shows.
            Rect2Dd GetBoundingBox() const override;
            std::shared_ptr<VectorElement> Clone() const override;
            std::vector<std::shared_ptr<VectorElement>> KeyholeShapes() const;
            std::vector<std::shared_ptr<VectorElement>> Contents() const;
        };

        // Blend: between each pair of consecutive children, `Steps`
        // intermediate shapes interpolate the outline, the colours (as
        // `ColourEffect` says) and the stroke. The children themselves
        // draw as they are. OneToOne maps outline nodes directly instead
        // of resampling; the profiles are Xara's (0, 0 = even).
        class VectorBlend : public VectorGroup {
        public:
            int Steps = 5;
            ColourBlendKind ColourEffect = ColourBlendKind::Fade;
            bool OneToOne = false;
            bool Antialiased = true;
            bool Tangential = false;
            double ObjectBias = 0, ObjectGain = 0, AttributeBias = 0, AttributeGain = 0;

            VectorBlend() { Type = VectorElementType::Blend; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        // Mould: the children (in the mould's space) are warped from
        // `SourceBounds` (their joint bounds when empty) into `Shape`, a
        // closed path of four sides starting at the source's top-left
        // corner and running along its top, right, bottom and left edges
        // in turn - four cubic curves for an Envelope (a Coons patch),
        // four straight lines for a Perspective (a projective map).
        // Threshold is Xara's curve-fitting accuracy, kept for the round
        // trip.
        enum class MouldKind { Envelope, Perspective };
        class VectorMould : public VectorGroup {
        public:
            MouldKind Kind = MouldKind::Envelope;
            PathData Shape;
            Rect2Dd SourceBounds{0, 0, 0, 0};
            int Threshold = 64;

            VectorMould() { Type = VectorElementType::Mould; }

            // The shape's bounds: where the warped children land.
            Rect2Dd GetBoundingBox() const override;
            std::shared_ptr<VectorElement> Clone() const override;
            // The children's joint bounds in the mould's space, or
            // SourceBounds when set.
            Rect2Dd EffectiveSourceBounds() const;
            // The four corners of the shape (its four sides' end points).
            bool ShapeCorners(Point2Dd corners[4]) const;
            // A square shape over `bounds` for the kind: the identity mould.
            static PathData IdentityShape(MouldKind kind, const Rect2Dd& bounds);
            // Maps a point of the source rectangle into the shape.
            Point2Dd Warp(const Point2Dd& p) const;
        };

        class VectorUse : public VectorElement {
        public:
            std::string Reference;
            Point2Dd Position;
            Size2Dd Size;

            VectorUse() { Type = VectorElementType::Use; }

            Rect2Dd GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

// ===== SPECIAL ELEMENTS =====

        class VectorImage : public VectorElement {
        public:
            Rect2Dd Bounds;
            std::string Source;
            std::vector<uint8_t> EmbeddedData;
            std::string MimeType;

            VectorImage() { Type = VectorElementType::Image; }

            Rect2Dd GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorGradient : public VectorElement {
        public:
            GradientData Data;

            VectorGradient() { Type = VectorElementType::LinearGradient; }

            Rect2Dd GetBoundingBox() const override { return {}; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorPattern : public VectorElement {
        public:
            PatternData Data;

            VectorPattern() { Type = VectorElementType::Pattern; }

            Rect2Dd GetBoundingBox() const override { return {}; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorFilter : public VectorElement {
        public:
            FilterData Data;

            VectorFilter() { Type = VectorElementType::Filter; }

            Rect2Dd GetBoundingBox() const override { return {}; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorClipPath : public VectorElement {
        public:
            ClipPathData Data;

            VectorClipPath() { Type = VectorElementType::ClipPath; }

            Rect2Dd GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorMask : public VectorElement {
        public:
            MaskData Data;

            VectorMask() { Type = VectorElementType::Mask; }

            Rect2Dd GetBoundingBox() const override { return Data.MaskRegion; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorMarker : public VectorElement {
        public:
            MarkerData Data;

            VectorMarker() { Type = VectorElementType::Marker; }

            Rect2Dd GetBoundingBox() const override { return {}; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

// ===== LAYER & DOCUMENT =====

        class VectorLayer : public VectorGroup {
        public:
            std::string Name;
            bool Locked = false;
            bool Visible = true;
            float Opacity = 1.0f;
            BlendMode LayerBlendMode = BlendMode::Normal;

            // CAD layer-table properties (DXF/DWG LAYER records). Readers
            // resolve every entity's ByLayer colour, lineweight and linetype
            // into the element's own Style, so these describe the layer's
            // defaults for writers that keep a layer table; they do not
            // affect rendering.
            bool Frozen = false;                    // hidden and not regenerated
            bool Plottable = true;                  // printed when the drawing is plotted
            std::optional<Color> DefaultColor;      // ByLayer colour
            float DefaultStrokeWidth = 0.0f;        // ByLayer lineweight in points (0 = default)
            std::string LineTypeName;               // ByLayer linetype ("Continuous", "DASHED", ...)
            std::vector<double> DefaultDashArray;   // that linetype's dashes in points, empty = solid

            VectorLayer() { Type = VectorElementType::Layer; }

            std::shared_ptr<VectorElement> Clone() const;
        };

        class VectorDocument {
        public:
            Size2Dd Size;
            Rect2Dd ViewBox;
            std::string PreserveAspectRatio = "xMidYMid meet";
            std::optional<Color> BackgroundColor;
            std::vector<std::shared_ptr<VectorLayer>> Layers;
            std::map<std::string, std::shared_ptr<VectorElement>> Definitions;
            std::string Title;
            std::string Description;
            std::string Author;
            std::map<std::string, std::string> Metadata;
            std::map<std::string, VectorStyle> NamedStyles;

            // Coordinates and Size are always points. Readers of formats
            // whose files carry a unit set the unit and the scale they
            // applied (source value = point value / PointsPerSourceUnit);
            // Unspecified with 1.0 means the source was already in points
            // or carried no unit.
            LengthUnit SourceUnit = LengthUnit::Unspecified;
            double PointsPerSourceUnit = 1.0;

            std::shared_ptr<VectorLayer> AddLayer(const std::string &n);
            void FitToContent(float padding);
            void RemoveLayer(const std::string &n);
            std::shared_ptr<VectorLayer> GetLayer(const std::string &n) const;
            void AddDefinition(const std::string &id, std::shared_ptr<VectorElement> e);
            std::shared_ptr<VectorElement> GetDefinition(const std::string &id) const;
            std::shared_ptr<VectorElement> FindElementById(const std::string &id) const;
            std::vector<std::shared_ptr<VectorElement>> FindElementsByClass(const std::string& className) const;
            Rect2Dd GetBoundingBox() const;
            void Clear();
            std::shared_ptr<VectorDocument> Clone() const;
        };

// Utility function declarations
        // Where the drawing actually is, for a view that wants to fit it.
        //
        // VectorDocument::GetBoundingBox() is the union of everything, which
        // is what a writer needs and what AutoCAD's zoom-extents does - and a
        // drawing with one forgotten speck a quarter of a million units away
        // from the plans (media/vector/DWG/womans hostel.dwg has a 130-unit
        // hatched scrap out there) then fits as a postage stamp in the corner
        // of an empty sheet. This is the same box with such specks left out:
        // a run of drawables is ignored only when it holds at most 1% of them
        // AND stands at least a fifth of the drawing's extent clear of the
        // rest, so a title block, a frame or a legend - which touch the rest
        // of the drawing or are simply many - always count. Nothing is
        // removed from the document: the speck is still drawn, still
        // exported, and still reachable by panning.
        //
        // Falls back to GetBoundingBox() for a drawing too small for the
        // question (fewer than 50 drawables) or when the rule would drop too
        // much.
        Rect2Dd ContentBounds(const VectorDocument& document);

        // The element's outline as path data in its own coordinate space
        // (rectangles, rounded rectangles, circles, ellipses, lines,
        // polylines, polygons and paths); false for kinds without one
        // (text, images, groups). What the renderer strokes, the line
        // gallery decorates and the editor converts to a path.
        bool BuildOutlinePath(const VectorElement& element, PathData& out);

        // ----- line gallery geometry (shared by the renderer and the writers) -----
        // Path data flattened to polylines, one per subpath, cubics
        // subdivided by their control-polygon length.
        struct FlatSubpath {
            std::vector<Point2Dd> Points;
            bool Closed = false;
        };
        std::vector<FlatSubpath> FlattenPathData(const PathData& path);
        // The open path's first and last points with the outward unit
        // directions an arrowhead points along; false for a closed or
        // degenerate path.
        bool PathEndpoints(const PathData& path, Point2Dd& start, Point2Dd& startDir,
                           Point2Dd& end, Point2Dd& endDir);
        // A gallery arrowhead's outline at `tip` pointing along `dir` for a
        // line `width` wide. `stroked` is set for the open kinds (OpenArrow,
        // Bar), which are stroked at the line width rather than filled.
        PathData ArrowheadOutline(const ArrowheadData& arrow, const Point2Dd& tip, const Point2Dd& dir,
                                  float width, bool& stroked);
        // The band a width profile turns a stroke into: a filled outline
        // (even-odd for closed subpaths, where it becomes a ring).
        PathData VariableWidthOutline(const PathData& path, const StrokeData& stroke);

        PathData ParsePathString(const std::string &pathStr);
        std::string SerializePathData(const PathData &path);
        Color ParseColorString(const std::string &colorStr);
        Matrix3x3 ParseTransformString(const std::string &transformStr);
        std::string SerializeTransform(const Matrix3x3 &transform);
    } // namespace VectorStorage
} // namespace UltraCanvas