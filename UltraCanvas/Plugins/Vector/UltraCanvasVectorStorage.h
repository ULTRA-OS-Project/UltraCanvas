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
            MeshGradient
        };

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

        class Matrix3x3 {
        public:
            float m[3][3];

            Matrix3x3();
            static Matrix3x3 Identity();
            static Matrix3x3 Translate(float tx, float ty);
            static Matrix3x3 Scale(float sx, float sy);
            static Matrix3x3 Rotate(float a);
            Matrix3x3 RotateDegrees(float degrees);
            Matrix3x3 SkewX(float angle);
            Matrix3x3 SkewY(float angle);
            static Matrix3x3 FromValues(float a, float b, float c, float d, float e, float f);
            Matrix3x3 operator*(const Matrix3x3 &o) const;
            Point2Df Transform(const Point2Df &p) const;
            Rect2Df Transform(const Rect2Df& rect) const;
            float Determinant() const;
            Matrix3x3 Inverse() const;
        };

// ===== PATH DATA =====

        struct PathCommand {
            PathCommandType Type;
            std::vector<float> Parameters;
            bool Relative = false;
        };

        struct PathData {
            std::vector<PathCommand> commands;
            bool Closed = false;
            mutable std::optional<Rect2Df> cachedBounds;

            Rect2Df GetBounds() const {
                if (cachedBounds) return *cachedBounds;
                if (commands.empty()) return {0, 0, 0, 0};
                float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
                Point2Df cur{0, 0};
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
                cachedBounds = Rect2Df{minX, minY, maxX - minX, maxY - minY};
                return *cachedBounds;
            }

            void InvalidateCache() { cachedBounds.reset(); }
        };

// ===== GRADIENT DATA (Uses GradientStop from RenderContext.h) =====

        struct LinearGradientData {
            Point2Df Start{0, 0};
            Point2Df End{1, 0};
            std::vector<GradientStop> Stops;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
            GradientSpreadMethod SpreadMethod = GradientSpreadMethod::Pad;
            std::optional<Matrix3x3> Transform;
        };
        struct RadialGradientData {
            Point2Df Center{0.5f, 0.5f};
            float Radius = 0.5f;
            Point2Df FocalPoint{0.5f, 0.5f};
            float FocalRadius = 0;
            std::vector<GradientStop> Stops;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
            GradientSpreadMethod SpreadMethod = GradientSpreadMethod::Pad;
            std::optional<Matrix3x3> Transform;
        };
        struct ConicalGradientData {
            Point2Df Center{0.5f, 0.5f};
            float StartAngle = 0;
            float EndAngle = 360;
            std::vector<GradientStop> Stops;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
            std::optional<Matrix3x3> Transform;
        };
        struct MeshPatch {
            std::array<Point2Df, 4> Corners;
            std::array<Point2Df, 8> ControlPoints;
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
            Rect2Df ViewBox;
            Rect2Df PatternRect;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
            std::optional<Matrix3x3> Transform;
        };
        using FillData = std::variant<std::monostate, Color, GradientData, PatternData, std::string>;

        struct StrokeData {
            FillData Fill = Color(0, 0, 0, 255);
            float Width = 1.0f;
            StrokeLineCap LineCap = StrokeLineCap::Butt;
            StrokeLineJoin LineJoin = StrokeLineJoin::Miter;
            float MiterLimit = 4.0f;
            std::vector<double> DashArray;
            double DashOffset = 0.0f;
            float Opacity = 1.0f;
        };

// ===== TEXT (Uses FontWeight, FontSlant from RenderContext.h) =====

        struct VectorTextStyle {
            std::string FontFamily = "Sans";
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
            std::optional<Point2Df> Position;
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
            Rect2Df FilterRegion;
            GradientUnits Units = GradientUnits::ObjectBoundingBox;
        };
        struct ClipPathData {
            std::vector<std::shared_ptr<VectorElement>> Elements;
            FillRule ClipRule = FillRule::NonZero;
        };
        struct MaskData {
            std::vector<std::shared_ptr<VectorElement>> Elements;
            Rect2Df MaskRegion;
        };
        struct MarkerData {
            std::shared_ptr<VectorGroup> Content;
            Rect2Df ViewBox;
            Point2Df RefPoint;
            Size2Df MarkerSize{3, 3};
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
            std::optional<std::string> ClipPath;
            std::optional<std::string> Mask;
            FillRule ClipRule = FillRule::NonZero;
            std::vector<std::string> Filters;
            bool Visible = true;
            bool Display = true;

            void Inherit(const VectorStyle& parent);
        };

// ===== BASE ELEMENT =====

        class VectorElement {
        public:
            VectorElementType Type = VectorElementType::NoneType;
            std::string Id;
            std::vector<std::string> Classes;
            VectorStyle Style;
            std::optional<Matrix3x3> Transform;

            virtual ~VectorElement() = default;

            virtual Rect2Df GetBoundingBox() const { return {0, 0, 0, 0}; }
            virtual std::shared_ptr<VectorElement> Clone() const = 0;
            Matrix3x3 GetGlobalTransform() const;
            Point2Df LocalToGlobal(const Point2Df& point) const;
            Point2Df GlobalToLocal(const Point2Df& point) const;
            bool HasClass(const std::string &c) const {
                return std::find(Classes.begin(), Classes.end(), c) != Classes.end();
            }
        };

// ===== SHAPES =====

        class VectorRect : public VectorElement {
        public:
            Rect2Df Bounds;
            float RadiusX = 0;
            float RadiusY = 0;

            VectorRect() { Type = VectorElementType::Rectangle; }

            Rect2Df GetBoundingBox() const;

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorCircle : public VectorElement {
        public:
            Point2Df Center;
            float Radius = 0;

            VectorCircle() { Type = VectorElementType::Circle; }

            Rect2Df GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const;
        };

        class VectorEllipse : public VectorElement {
        public:
            Point2Df Center;
            float RadiusX = 0;
            float RadiusY = 0;

            VectorEllipse() { Type = VectorElementType::Ellipse; }
            Rect2Df GetBoundingBox() const override;
            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorLine : public VectorElement {
        public:
            Point2Df Start;
            Point2Df End;

            VectorLine() { Type = VectorElementType::Line; }

            Rect2Df GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const;
        };

        class VectorPolyline : public VectorElement {
        public:
            std::vector<Point2Df> Points;

            VectorPolyline() { Type = VectorElementType::Polyline; }

            Rect2Df GetBoundingBox() const;
            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorPolygon : public VectorElement {
        public:
            std::vector<Point2Df> Points;

            VectorPolygon() { Type = VectorElementType::Polygon; }

            Rect2Df GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorPath : public VectorElement {
        public:
            PathData Path;

            VectorPath() { Type = VectorElementType::Path; }

            Rect2Df GetBoundingBox() const;
            void AddCommand(const PathCommand& cmd);

            std::shared_ptr<VectorElement> Clone() const override;

            void MoveTo(float x, float y, bool rel = false);
            void LineTo(float x, float y, bool rel = false);
            void CurveTo(float x1, float y1, float x2, float y2, float x, float y, bool rel = false);
            void QuadraticTo(float x1, float y1, float x, float y, bool relative);
            void ArcTo(float rx, float ry, float rotation, bool largeArc, bool sweep, float x, float y, bool relative);
            void ClosePath();

            float GetLength() const;
            Point2Df GetPointAtLength(float length) const;
            float GetAngleAtLength(float length) const;

            std::vector<Point2Df> Flatten(float tolerance) const;
        };

// ===== TEXT =====

        class VectorText : public VectorElement {
        public:
            Point2Df Position;
            std::vector<TextSpanData> Spans;
            VectorTextStyle BaseStyle;

            VectorText() { Type = VectorElementType::Text; }

            Rect2Df GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const;

            void SetText(const std::string &t);
            void AddSpan(const TextSpanData& span);

            std::string GetPlainText() const;
        };

        class VectorTextPath : public VectorElement {
        public:
            TextPathData Data;

            VectorTextPath() { Type = VectorElementType::TextPath; }
            Rect2Df GetBoundingBox() const override;
            std::shared_ptr<VectorElement> Clone() const override;
        };

// ===== CONTAINERS =====

        class VectorGroup : public VectorElement {
        public:
            std::vector<std::shared_ptr<VectorElement>> Children;

            VectorGroup() { Type = VectorElementType::Group; }

            Rect2Df GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const;

            void AddChild(std::shared_ptr<VectorElement> c);

            void RemoveChild(const std::string &id);

            std::shared_ptr<VectorElement> FindChild(const std::string &id) const;
        };

        class VectorSymbol : public VectorGroup {
        public:
            Rect2Df ViewBox;
            std::string PreserveAspectRatio = "xMidYMid meet";

            VectorSymbol() { Type = VectorElementType::Symbol; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorUse : public VectorElement {
        public:
            std::string Reference;
            Point2Df Position;
            Size2Df Size;

            VectorUse() { Type = VectorElementType::Use; }

            Rect2Df GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

// ===== SPECIAL ELEMENTS =====

        class VectorImage : public VectorElement {
        public:
            Rect2Df Bounds;
            std::string Source;
            std::vector<uint8_t> EmbeddedData;
            std::string MimeType;

            VectorImage() { Type = VectorElementType::Image; }

            Rect2Df GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorGradient : public VectorElement {
        public:
            GradientData Data;

            VectorGradient() { Type = VectorElementType::LinearGradient; }

            Rect2Df GetBoundingBox() const override { return {}; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorPattern : public VectorElement {
        public:
            PatternData Data;

            VectorPattern() { Type = VectorElementType::Pattern; }

            Rect2Df GetBoundingBox() const override { return {}; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorFilter : public VectorElement {
        public:
            FilterData Data;

            VectorFilter() { Type = VectorElementType::Filter; }

            Rect2Df GetBoundingBox() const override { return {}; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorClipPath : public VectorElement {
        public:
            ClipPathData Data;

            VectorClipPath() { Type = VectorElementType::ClipPath; }

            Rect2Df GetBoundingBox() const override;

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorMask : public VectorElement {
        public:
            MaskData Data;

            VectorMask() { Type = VectorElementType::Mask; }

            Rect2Df GetBoundingBox() const override { return Data.MaskRegion; }

            std::shared_ptr<VectorElement> Clone() const override;
        };

        class VectorMarker : public VectorElement {
        public:
            MarkerData Data;

            VectorMarker() { Type = VectorElementType::Marker; }

            Rect2Df GetBoundingBox() const override { return {}; }

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

            VectorLayer() { Type = VectorElementType::Layer; }

            std::shared_ptr<VectorElement> Clone() const;
        };

        class VectorDocument {
        public:
            Size2Df Size;
            Rect2Df ViewBox;
            std::string PreserveAspectRatio = "xMidYMid meet";
            std::optional<Color> BackgroundColor;
            std::vector<std::shared_ptr<VectorLayer>> Layers;
            std::map<std::string, std::shared_ptr<VectorElement>> Definitions;
            std::string Title;
            std::string Description;
            std::string Author;
            std::map<std::string, std::string> Metadata;
            std::map<std::string, VectorStyle> NamedStyles;

            std::shared_ptr<VectorLayer> AddLayer(const std::string &n);
            void RemoveLayer(const std::string &n);
            std::shared_ptr<VectorLayer> GetLayer(const std::string &n) const;
            void AddDefinition(const std::string &id, std::shared_ptr<VectorElement> e);
            std::shared_ptr<VectorElement> GetDefinition(const std::string &id) const;
            std::shared_ptr<VectorElement> FindElementById(const std::string &id) const;
            std::vector<std::shared_ptr<VectorElement>> FindElementsByClass(const std::string& className) const;
            Rect2Df GetBoundingBox() const;
            void Clear();
            std::shared_ptr<VectorDocument> Clone() const;
        };

// Utility function declarations
        PathData ParsePathString(const std::string &pathStr);
        std::string SerializePathData(const PathData &path);
        Color ParseColorString(const std::string &colorStr);
        Matrix3x3 ParseTransformString(const std::string &transformStr);
    } // namespace VectorStorage
} // namespace UltraCanvas