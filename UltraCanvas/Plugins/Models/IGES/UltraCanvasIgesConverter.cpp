// Plugins/Models/IGES/UltraCanvasIgesConverter.cpp
// IGES 5.3 entity meanings: the layer between the entity table that
// UltraCanvasIgesFile.h produces and the B-rep that UltraCanvasBrepStorage.h
// holds. Declared in UltraCanvasIgesConverter.h.
//
// Version: 1.0.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework

#include "Models/IGES/UltraCanvasIgesConverter.h"
#include "Models/IGES/UltraCanvasIgesFile.h"
#include "Models/STEP/UltraCanvasStepConverter.h"   // FacetMeshesIntoBrep
#include "UltraCanvasTextUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include "../../../include/UltraCanvasPathUtf8.h"

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;
namespace IF = UltraCanvas::IgesFile;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// ===== SHARED GEOMETRY HELPERS =====

// A 3x4 affine map: IGES's transformation matrix entity (124).
struct Affine {
    double R[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vec3d T{0.0, 0.0, 0.0};
    bool Identity = true;

    Vec3d Direction(const Vec3d& d) const {
        return {R[0][0] * d.x + R[0][1] * d.y + R[0][2] * d.z,
                R[1][0] * d.x + R[1][1] * d.y + R[1][2] * d.z,
                R[2][0] * d.x + R[2][1] * d.y + R[2][2] * d.z};
    }
    Vec3d Point(const Vec3d& p) const { return Direction(p) + T; }

    // outer ∘ this: apply this map first, then `outer`.
    Affine Then(const Affine& outer) const {
        if (Identity) return outer;
        if (outer.Identity) return *this;
        Affine result;
        result.Identity = false;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                double sum = 0.0;
                for (int k = 0; k < 3; ++k) sum += outer.R[i][k] * R[k][j];
                result.R[i][j] = sum;
            }
        result.T = outer.Point(T);
        return result;
    }

    double Determinant() const {
        return R[0][0] * (R[1][1] * R[2][2] - R[1][2] * R[2][1]) -
               R[0][1] * (R[1][0] * R[2][2] - R[1][2] * R[2][0]) +
               R[0][2] * (R[1][0] * R[2][1] - R[1][1] * R[2][0]);
    }
};

Vec3d AnyPerpendicular(const Vec3d& axis) {
    const Vec3d a = axis.Normalized();
    const Vec3d helper = std::fabs(a.x) < 0.9 ? Vec3d(1.0, 0.0, 0.0) : Vec3d(0.0, 1.0, 0.0);
    return (helper - a * helper.Dot(a)).Normalized();
}

// A curve's frame mapped so its (x, y) parameterisation survives: X and Y
// are carried, Z is rebuilt from them. Returns the scale the map applies to
// lengths in the plane, which a circle's radius has to follow.
double TransformCurveFrame(BrepPlacement& frame, const Affine& map) {
    const Vec3d x = map.Direction(frame.AxisX);
    const Vec3d y = map.Direction(frame.AxisY());
    frame.Origin = map.Point(frame.Origin);
    const double scale = x.Length();
    frame.AxisX = x.Normalized();
    frame.AxisZ = x.Cross(y).Normalized();
    frame.Normalize();
    return scale;
}

// A surface's frame: its axis is carried, so a cylinder's v still runs
// along it.
double TransformSurfaceFrame(BrepPlacement& frame, const Affine& map) {
    const Vec3d z = map.Direction(frame.AxisZ);
    const Vec3d x = map.Direction(frame.AxisX);
    frame.Origin = map.Point(frame.Origin);
    const double scale = x.Length();
    frame.AxisZ = z.Normalized();
    frame.AxisX = x.Normalized();
    frame.Normalize();
    return scale;
}

void TransformCurve(BrepCurve& curve, const Affine& map) {
    if (map.Identity) return;
    switch (curve.Type) {
        case BrepCurveType::Line:
            curve.Placement.Origin = map.Point(curve.Placement.Origin);
            curve.Placement.AxisZ = map.Direction(curve.Placement.AxisZ);
            break;
        case BrepCurveType::Circle:
        case BrepCurveType::Ellipse:
        case BrepCurveType::Parabola:
        case BrepCurveType::Hyperbola: {
            const double scale = TransformCurveFrame(curve.Placement, map);
            curve.Radius *= scale;
            curve.MinorRadius *= scale;
            curve.FocalDistance *= scale;
            break;
        }
        case BrepCurveType::Polyline:
        case BrepCurveType::BSpline:
            for (Vec3d& point : curve.ControlPoints) point = map.Point(point);
            break;
    }
}

// The same curve run the other way: parameter t of the result is
// (tMin + tMax - t) of the original. Possible for the line, the polyline and
// the B-spline; a circle's parameter only runs one way round its frame.
bool ReverseCurve(BrepCurve& curve) {
    double tMin = 0.0, tMax = 0.0;
    curve.ParameterRange(tMin, tMax);
    switch (curve.Type) {
        case BrepCurveType::Line:
            curve.Placement.Origin = curve.Placement.Origin + curve.Placement.AxisZ * (tMin + tMax);
            curve.Placement.AxisZ = curve.Placement.AxisZ * -1.0;
            return true;
        case BrepCurveType::Polyline: {
            std::reverse(curve.ControlPoints.begin(), curve.ControlPoints.end());
            if (curve.HasRange()) {
                const double last = static_cast<double>(curve.ControlPoints.size()) - 1.0;
                const double from = last - curve.TMax, to = last - curve.TMin;
                curve.TMin = from;
                curve.TMax = to;
            }
            return true;
        }
        case BrepCurveType::BSpline: {
            if (curve.Knots.empty()) return false;
            const double sum = curve.Knots.front() + curve.Knots.back();
            std::reverse(curve.ControlPoints.begin(), curve.ControlPoints.end());
            std::reverse(curve.Weights.begin(), curve.Weights.end());
            std::vector<double> knots(curve.Knots.rbegin(), curve.Knots.rend());
            for (double& k : knots) k = sum - k;
            curve.Knots = std::move(knots);
            if (curve.HasRange()) {
                const double from = sum - curve.TMax, to = sum - curve.TMin;
                curve.TMin = from;
                curve.TMax = to;
            }
            return true;
        }
        default:
            return false;
    }
}

bool ReverseCurve2D(BrepCurve2D& curve) {
    double tMin = 0.0, tMax = 0.0;
    curve.ParameterRange(tMin, tMax);
    switch (curve.Type) {
        case BrepCurve2DType::Line:
            curve.Origin = curve.Origin + curve.Direction * (tMin + tMax);
            curve.Direction = curve.Direction * -1.0;
            return true;
        case BrepCurve2DType::Polyline: {
            std::reverse(curve.ControlPoints.begin(), curve.ControlPoints.end());
            if (curve.HasRange()) {
                const double last = static_cast<double>(curve.ControlPoints.size()) - 1.0;
                const double from = last - curve.TMax, to = last - curve.TMin;
                curve.TMin = from;
                curve.TMax = to;
            }
            return true;
        }
        case BrepCurve2DType::BSpline: {
            if (curve.Knots.empty()) return false;
            const double sum = curve.Knots.front() + curve.Knots.back();
            std::reverse(curve.ControlPoints.begin(), curve.ControlPoints.end());
            std::reverse(curve.Weights.begin(), curve.Weights.end());
            std::vector<double> knots(curve.Knots.rbegin(), curve.Knots.rend());
            for (double& k : knots) k = sum - k;
            curve.Knots = std::move(knots);
            if (curve.HasRange()) {
                const double from = sum - curve.TMax, to = sum - curve.TMin;
                curve.TMin = from;
                curve.TMax = to;
            }
            return true;
        }
        default:
            return false;
    }
}

// A polyline as the degree-1 B-spline it is, so its parameter can be moved.
void PolylineToBSpline(BrepCurve& curve) {
    if (curve.Type != BrepCurveType::Polyline) return;
    const size_t n = curve.ControlPoints.size();
    curve.Type = BrepCurveType::BSpline;
    curve.Degree = 1;
    curve.Weights.clear();
    curve.Knots.clear();
    curve.Knots.push_back(0.0);
    for (size_t i = 0; i < n; ++i) curve.Knots.push_back(static_cast<double>(i));
    curve.Knots.push_back(n > 0 ? static_cast<double>(n - 1) : 0.0);
}

// Re-parameterise a curve linearly onto [from, to]. Not possible for the
// angle-parameterised conics unless the span stays the same length, in which
// case the frame is turned instead.
bool RemapCurveRange(BrepCurve& curve, double from, double to) {
    double a = 0.0, b = 0.0;
    curve.ParameterRange(a, b);
    if (std::fabs((b - a) - (to - from)) < 1e-12 && std::fabs(a - from) < 1e-12) return true;
    if (to <= from || b <= a) return false;
    const double k = (b - a) / (to - from);   // d(old)/d(new)
    switch (curve.Type) {
        case BrepCurveType::Line:
            curve.Placement.Origin = curve.Placement.Origin + curve.Placement.AxisZ * (a - from * k);
            curve.Placement.AxisZ = curve.Placement.AxisZ * k;
            curve.TMin = from;
            curve.TMax = to;
            return true;
        case BrepCurveType::Polyline:
            PolylineToBSpline(curve);
            [[fallthrough]];
        case BrepCurveType::BSpline:
            for (double& knot : curve.Knots) knot = from + (knot - a) / k;
            curve.TMin = from;
            curve.TMax = to;
            return true;
        case BrepCurveType::Circle:
        case BrepCurveType::Ellipse: {
            if (std::fabs(k - 1.0) > 1e-9) return false;
            // Same span, shifted start: turn the frame by the shift.
            const double delta = a - from;
            const Vec3d x = curve.Placement.AxisX, y = curve.Placement.AxisY();
            curve.Placement.AxisX = (x * std::cos(delta) + y * std::sin(delta)).Normalized();
            curve.Placement.Normalize();
            curve.TMin = from;
            curve.TMax = to;
            return true;
        }
        default:
            return false;
    }
}

// The parameter of the point on `curve` nearest `point`.
double ParameterAt(const BrepCurve& curve, const Vec3d& point) {
    double tMin = 0.0, tMax = 1.0;
    curve.ParameterRange(tMin, tMax);
    switch (curve.Type) {
        case BrepCurveType::Line: {
            const Vec3d direction = curve.Placement.AxisZ;
            const double squared = direction.Dot(direction);
            if (squared <= 1e-30) return tMin;
            return (point - curve.Placement.Origin).Dot(direction) / squared;
        }
        case BrepCurveType::Circle:
        case BrepCurveType::Ellipse: {
            const Vec3d local = curve.Placement.ToLocal(point);
            double x = local.x, y = local.y;
            if (curve.Type == BrepCurveType::Ellipse) {
                if (curve.Radius > 1e-30) x /= curve.Radius;
                if (curve.MinorRadius > 1e-30) y /= curve.MinorRadius;
            }
            double angle = std::atan2(y, x);
            while (angle < tMin - 1e-9) angle += kTwoPi;
            while (angle >= tMin + kTwoPi - 1e-9) angle -= kTwoPi;
            return angle;
        }
        default: break;
    }
    constexpr int kSamples = 64;
    double best = tMin;
    double bestDistance = std::numeric_limits<double>::max();
    for (int i = 0; i <= kSamples; ++i) {
        const double t = tMin + (tMax - tMin) * (static_cast<double>(i) / kSamples);
        const double distance = (curve.Evaluate(t) - point).Length();
        if (distance < bestDistance) { bestDistance = distance; best = t; }
    }
    double step = (tMax - tMin) / kSamples;
    for (int refine = 0; refine < 48; ++refine) {
        bool improved = false;
        for (double direction : {1.0, -1.0}) {
            const double t = std::max(tMin, std::min(tMax, best + direction * step));
            const double distance = (curve.Evaluate(t) - point).Length();
            if (distance < bestDistance) { bestDistance = distance; best = t; improved = true; }
        }
        if (!improved) step *= 0.5;
    }
    return best;
}

// How an IGES surface's (u, v) maps onto the document's for the same surface:
// optionally swapped, then scaled and offset per axis. The IGES analytic
// surfaces are parameterised over [0, 1] where the document uses angles.
struct ParamMap {
    bool Known = true;
    bool Swap = false;
    double ScaleU = 1.0, OffsetU = 0.0, ScaleV = 1.0, OffsetV = 0.0;

    Vec2d Apply(Vec2d p) const {
        if (Swap) std::swap(p.x, p.y);
        return {ScaleU * p.x + OffsetU, ScaleV * p.y + OffsetV};
    }
    Vec2d Linear(Vec2d d) const {
        if (Swap) std::swap(d.x, d.y);
        return {ScaleU * d.x, ScaleV * d.y};
    }
    // A circle stays a circle, going the same way round, only under a
    // uniform positive scale without a swap.
    bool KeepsCircles() const {
        return !Swap && ScaleU > 0.0 && std::fabs(ScaleU - ScaleV) <= 1e-12 * ScaleU;
    }
};

std::string ToUpper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string Trimmed(const std::string& text) {
    size_t begin = 0, end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

// ===== READER =====

class Reader {
public:
    Reader(const IF::Model& model, ModelDocument& document, const ConversionOptions& options)
            : model_(model), document_(document), brep_(document.Brep), options_(options) {}

    void Run();

private:
    struct Segment {
        int Directory = 0;
        Affine Outer;     // transforms of the composites above it
    };

    const IF::Model& model_;
    ModelDocument& document_;
    BrepData& brep_;
    const ConversionOptions& options_;

    double tolerance_ = 1e-6;
    std::map<int, int> curveFor_;
    std::map<int, std::pair<int, ParamMap>> surfaceFor_;
    std::map<std::pair<int, int>, int> vertexFor_;
    std::map<std::pair<int, int>, int> msboEdgeFor_;
    std::map<int, int> materialForColour_;
    std::set<int> surfacesInFaces_;
    std::set<std::string> warned_;
    std::map<int, int> unsupported_;   // entity type -> count
    size_t droppedParameterCurves_ = 0;
    size_t keptParameterCurves_ = 0;

    void Warn(const std::string& message) { options_.Warn("IGES: " + message); }
    void WarnOnce(const std::string& message) {
        if (warned_.insert(message).second) Warn(message);
    }

    const IF::Entity* At(int directory) const { return model_.At(directory); }

    Affine Matrix(int directory, int depth = 0) const;
    Affine TransformOf(const IF::Entity& entity) const {
        return entity.Transform > 0 ? Matrix(entity.Transform) : Affine();
    }
    bool PointOf(int directory, Vec3d& out) const;
    bool DirectionOf(int directory, Vec3d& out) const;

    bool BuildCurve(const IF::Entity& entity, BrepCurve& out);
    void Flatten(int directory, const Affine& outer, std::vector<Segment>& out, int depth = 0);
    int Curve(int directory);
    int CurveOf(const Segment& segment);
    bool Curve2DFrom(const Segment& segment, const ParamMap& map, BrepCurve2D& out);

    int Surface(int directory, ParamMap& map);
    int MaterialForColour(int colourNumber, const std::string& name = std::string());
    std::string NameOf(const IF::Entity& entity, size_t consumed) const;

    int Vertex(int listDirectory, int index);
    int MsboEdge(int listDirectory, int index);
    int AttachParameterCurve(BrepCurve2D curve, int surfaceIndex, int edgeIndex);
    bool MsboLoop(int directory, int surfaceIndex, const ParamMap& map, BrepLoop& out);
    int MsboFace(int directory, bool sameSense);
    int MsboShell(int directory, bool orientation, bool& closed);
    void ReadSolids();

    bool LoopFromCurves(const std::vector<Segment>& model, const std::vector<Segment>& parameter,
                        int surfaceIndex, const ParamMap& map, BrepLoop& out);
    int RectangleLoop(int surfaceIndex);
    void ReadSurfaceModel();

    void ReadUnits();
    void ReadHeader();
    void ComputeTolerance();
};

Affine Reader::Matrix(int directory, int depth) const {
    Affine matrix;
    const IF::Entity* entity = At(directory);
    if (!entity || entity->Type != 124 || depth > 16) return matrix;
    matrix.Identity = false;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            matrix.R[row][column] = entity->Real(static_cast<size_t>(row * 4 + column),
                                                 row == column ? 1.0 : 0.0);
    }
    matrix.T = Vec3d(entity->Real(3), entity->Real(7), entity->Real(11));
    if (entity->Transform > 0 && entity->Transform != directory)
        matrix = matrix.Then(Matrix(entity->Transform, depth + 1));
    return matrix;
}

bool Reader::PointOf(int directory, Vec3d& out) const {
    const IF::Entity* entity = At(directory);
    if (!entity || entity->Type != 116) return false;
    out = TransformOf(*entity).Point(Vec3d(entity->Real(0), entity->Real(1), entity->Real(2)));
    return true;
}

bool Reader::DirectionOf(int directory, Vec3d& out) const {
    const IF::Entity* entity = At(directory);
    if (!entity || (entity->Type != 123 && entity->Type != 116)) return false;
    out = TransformOf(*entity).Direction(Vec3d(entity->Real(0), entity->Real(1), entity->Real(2)));
    return out.Length() > 1e-15;
}

// ----- curves -----

bool Reader::BuildCurve(const IF::Entity& e, BrepCurve& curve) {
    bool built = false;
    switch (e.Type) {
        case 110: {   // line
            const Vec3d from(e.Real(0), e.Real(1), e.Real(2));
            const Vec3d to(e.Real(3), e.Real(4), e.Real(5));
            if ((to - from).Length() <= 1e-15) break;
            curve.Type = BrepCurveType::Line;
            curve.Placement.Origin = from;
            curve.Placement.AxisZ = to - from;
            curve.Placement.AxisX = AnyPerpendicular(to - from);
            curve.TMin = 0.0;
            curve.TMax = 1.0;
            built = true;
            break;
        }
        case 100: {   // circular arc, counter-clockwise in its own plane
            const double z = e.Real(0);
            const Vec3d centre(e.Real(1), e.Real(2), z);
            const Vec3d start(e.Real(3), e.Real(4), z);
            const Vec3d end(e.Real(5), e.Real(6), z);
            const double radius = (start - centre).Length();
            if (radius <= 1e-15) break;
            const double a0 = std::atan2(start.y - centre.y, start.x - centre.x);
            double a1 = std::atan2(end.y - centre.y, end.x - centre.x);
            if ((end - start).Length() <= radius * 1e-9) a1 = a0 + kTwoPi;
            while (a1 <= a0 + 1e-12) a1 += kTwoPi;
            curve.Type = BrepCurveType::Circle;
            curve.Placement.Origin = centre;
            curve.Placement.AxisZ = Vec3d(0.0, 0.0, 1.0);
            curve.Placement.AxisX = Vec3d(1.0, 0.0, 0.0);
            curve.Radius = radius;
            curve.TMin = a0;
            curve.TMax = a1;
            built = true;
            break;
        }
        case 104: {   // conic arc
            const double A = e.Real(0), B = e.Real(1), C = e.Real(2);
            const double D = e.Real(3), E = e.Real(4), F = e.Real(5);
            const double z = e.Real(6);
            const Vec3d start(e.Real(7), e.Real(8), z);
            const Vec3d end(e.Real(9), e.Real(10), z);
            const double discriminant = B * B - 4.0 * A * C;
            const bool ellipse = e.Form == 1 || (e.Form == 0 && discriminant < 0.0);
            if (ellipse) {
                // Centre where the gradient vanishes; axes where the cross
                // term does.
                const double determinant = 4.0 * A * C - B * B;
                if (std::fabs(determinant) <= 1e-300) break;
                const double x0 = (B * E - 2.0 * C * D) / determinant;
                const double y0 = (B * D - 2.0 * A * E) / determinant;
                const double theta = 0.5 * std::atan2(B, A - C);
                const double c = std::cos(theta), s = std::sin(theta);
                const double a2 = A * c * c + B * c * s + C * s * s;
                const double c2 = A * s * s - B * c * s + C * c * c;
                const double f2 = A * x0 * x0 + B * x0 * y0 + C * y0 * y0 + D * x0 + E * y0 + F;
                if (a2 == 0.0 || c2 == 0.0 || -f2 / a2 <= 0.0 || -f2 / c2 <= 0.0) break;
                const double ra = std::sqrt(-f2 / a2), rb = std::sqrt(-f2 / c2);
                curve.Type = BrepCurveType::Ellipse;
                curve.Placement.Origin = Vec3d(x0, y0, z);
                curve.Placement.AxisZ = Vec3d(0.0, 0.0, 1.0);
                curve.Placement.AxisX = Vec3d(c, s, 0.0);
                curve.Radius = ra;
                curve.MinorRadius = rb;
                auto angleOf = [&](const Vec3d& p) {
                    const Vec3d local = curve.Placement.ToLocal(p);
                    return std::atan2(local.y / rb, local.x / ra);
                };
                const double a0 = angleOf(start);
                double a1 = angleOf(end);
                if ((end - start).Length() <= std::max(ra, rb) * 1e-9) a1 = a0 + kTwoPi;
                while (a1 <= a0 + 1e-12) a1 += kTwoPi;
                curve.TMin = a0;
                curve.TMax = a1;
                built = true;
            } else {
                WarnOnce("parabolic and hyperbolic arcs (entity 104) are read as the straight "
                         "chord between their ends");
                if ((end - start).Length() <= 1e-15) break;
                curve.Type = BrepCurveType::Line;
                curve.Placement.Origin = start;
                curve.Placement.AxisZ = end - start;
                curve.Placement.AxisX = AnyPerpendicular(end - start);
                curve.TMin = 0.0;
                curve.TMax = 1.0;
                built = true;
            }
            break;
        }
        case 106: {   // copious data: the curve forms only
            if (e.Form != 11 && e.Form != 12 && e.Form != 13 && e.Form != 63) break;
            const int layout = e.Integer(0);
            const int count = e.Integer(1);
            if (count < 2 || count > 10000000) break;
            std::vector<Vec3d> points;
            points.reserve(static_cast<size_t>(count) + 1);
            if (layout == 1) {
                const double z = e.Real(2);
                for (int i = 0; i < count; ++i)
                    points.emplace_back(e.Real(static_cast<size_t>(3 + 2 * i)),
                                        e.Real(static_cast<size_t>(4 + 2 * i)), z);
            } else {
                const size_t stride = layout == 3 ? 6 : 3;
                for (int i = 0; i < count; ++i) {
                    const size_t at = 2 + stride * static_cast<size_t>(i);
                    points.emplace_back(e.Real(at), e.Real(at + 1), e.Real(at + 2));
                }
            }
            if (e.Form == 63 && (points.front() - points.back()).Length() > 1e-15)
                points.push_back(points.front());
            curve.Type = BrepCurveType::Polyline;
            curve.ControlPoints = std::move(points);
            built = true;
            break;
        }
        case 112: {   // parametric spline: piecewise cubic, converted exactly
            const int dimensions = e.Integer(2);
            const int pieces = e.Integer(3);
            if (pieces < 1 || pieces > 1000000) break;
            std::vector<double> breaks;
            for (int i = 0; i <= pieces; ++i) breaks.push_back(e.Real(static_cast<size_t>(4 + i)));
            const size_t base = static_cast<size_t>(5 + pieces);
            curve.Type = BrepCurveType::BSpline;
            curve.Degree = 3;
            for (int piece = 0; piece < pieces; ++piece) {
                const size_t at = base + 12 * static_cast<size_t>(piece);
                const double h = breaks[static_cast<size_t>(piece) + 1] - breaks[static_cast<size_t>(piece)];
                Vec3d p[4];
                for (int axis = 0; axis < 3; ++axis) {
                    if (axis == 2 && dimensions == 2) {
                        for (Vec3d& point : p) point.z = 0.0;
                        break;
                    }
                    const double a = e.Real(at + 4 * axis), b = e.Real(at + 4 * axis + 1) * h;
                    const double c = e.Real(at + 4 * axis + 2) * h * h;
                    const double d = e.Real(at + 4 * axis + 3) * h * h * h;
                    const double bezier[4] = {a, a + b / 3.0, a + 2.0 * b / 3.0 + c / 3.0,
                                              a + b + c + d};
                    for (int k = 0; k < 4; ++k) {
                        if (axis == 0) p[k].x = bezier[k];
                        else if (axis == 1) p[k].y = bezier[k];
                        else p[k].z = bezier[k];
                    }
                }
                for (int k = piece == 0 ? 0 : 1; k < 4; ++k) curve.ControlPoints.push_back(p[k]);
            }
            curve.Knots.assign(4, breaks.front());
            for (int i = 1; i < pieces; ++i)
                for (int k = 0; k < 3; ++k) curve.Knots.push_back(breaks[static_cast<size_t>(i)]);
            for (int k = 0; k < 4; ++k) curve.Knots.push_back(breaks.back());
            built = curve.ControlPoints.size() == curve.Knots.size() - 4;
            break;
        }
        case 126: {   // rational B-spline curve
            const int upper = e.Integer(0);
            const int degree = e.Integer(1);
            if (upper < 1 || degree < 1 || upper > 10000000 || degree > 64) break;
            const bool polynomial = e.Integer(4) != 0;
            const size_t knotCount = static_cast<size_t>(upper + degree + 2);
            const size_t points = static_cast<size_t>(upper + 1);
            const size_t knotsAt = 6, weightsAt = knotsAt + knotCount;
            const size_t pointsAt = weightsAt + points, rangeAt = pointsAt + 3 * points;
            if (e.Params.size() < rangeAt) break;
            curve.Type = BrepCurveType::BSpline;
            curve.Degree = degree;
            for (size_t i = 0; i < knotCount; ++i) curve.Knots.push_back(e.Real(knotsAt + i));
            bool allEqual = true;
            for (size_t i = 0; i < points; ++i) {
                curve.Weights.push_back(e.Real(weightsAt + i, 1.0));
                if (std::fabs(curve.Weights.back() - curve.Weights.front()) > 1e-15) allEqual = false;
            }
            if (polynomial || allEqual) curve.Weights.clear();
            for (size_t i = 0; i < points; ++i)
                curve.ControlPoints.emplace_back(e.Real(pointsAt + 3 * i), e.Real(pointsAt + 3 * i + 1),
                                                 e.Real(pointsAt + 3 * i + 2));
            const double v0 = e.Real(rangeAt, curve.Knots[static_cast<size_t>(degree)]);
            const double v1 = e.Real(rangeAt + 1, curve.Knots[points]);
            if (v1 > v0) { curve.TMin = v0; curve.TMax = v1; }
            curve.Periodic = e.Integer(5) != 0 && false;   // IGES keeps the full net; nothing wraps
            built = true;
            break;
        }
        default:
            break;
    }
    if (built) TransformCurve(curve, TransformOf(e));
    return built;
}

void Reader::Flatten(int directory, const Affine& outer, std::vector<Segment>& out, int depth) {
    const IF::Entity* entity = At(directory);
    if (!entity || depth > 16) return;
    if (entity->Type == 102) {
        const Affine here = TransformOf(*entity).Then(outer);
        const int count = entity->Integer(0);
        for (int i = 0; i < count; ++i)
            Flatten(entity->Pointer(static_cast<size_t>(1 + i)), here, out, depth + 1);
        return;
    }
    if (entity->Type == 116) return;   // a point in a composite carries no span
    Segment segment;
    segment.Directory = directory;
    segment.Outer = outer;
    out.push_back(segment);
}

int Reader::Curve(int directory) {
    auto cached = curveFor_.find(directory);
    if (cached != curveFor_.end()) return cached->second;
    curveFor_[directory] = -1;

    const IF::Entity* entity = At(directory);
    if (!entity) return -1;

    BrepCurve curve;
    if (entity->Type == 102) {
        // A composite where one curve is needed (a generatrix, an edge list's
        // edge): sampled into a polyline, which is said.
        std::vector<Segment> segments;
        Flatten(directory, Affine(), segments);
        std::vector<Vec3d> points;
        for (const Segment& segment : segments) {
            const int index = CurveOf(segment);
            if (index < 0) continue;
            const BrepCurve& part = brep_.Curves[static_cast<size_t>(index)];
            double a = 0.0, b = 0.0;
            part.ParameterRange(a, b);
            const Vec3d first = part.Evaluate(a), last = part.Evaluate(b);
            bool reversed = false;
            if (!points.empty())
                reversed = (last - points.back()).Length() < (first - points.back()).Length();
            const int samples = part.Type == BrepCurveType::Line ? 1 : 32;
            for (int i = points.empty() ? 0 : 1; i <= samples; ++i) {
                const double s = static_cast<double>(i) / samples;
                points.push_back(part.Evaluate(reversed ? b - (b - a) * s : a + (b - a) * s));
            }
        }
        if (points.size() < 2) return -1;
        WarnOnce("a composite curve used as a single curve is approximated by a polyline");
        curve.Type = BrepCurveType::Polyline;
        curve.ControlPoints = std::move(points);
    } else if (!BuildCurve(*entity, curve)) {
        ++unsupported_[entity->Type];
        return -1;
    }
    const int index = brep_.AddCurve(std::move(curve));
    curveFor_[directory] = index;
    return index;
}

int Reader::CurveOf(const Segment& segment) {
    if (segment.Outer.Identity) return Curve(segment.Directory);
    const int base = Curve(segment.Directory);
    if (base < 0) return -1;
    BrepCurve copy = brep_.Curves[static_cast<size_t>(base)];
    TransformCurve(copy, segment.Outer);
    return brep_.AddCurve(std::move(copy));
}

bool Reader::Curve2DFrom(const Segment& segment, const ParamMap& map, BrepCurve2D& out) {
    if (!map.Known) return false;
    const IF::Entity* entity = At(segment.Directory);
    if (!entity) return false;
    BrepCurve curve;
    if (!BuildCurve(*entity, curve)) return false;
    TransformCurve(curve, segment.Outer);

    auto xy = [](const Vec3d& p) { return Vec2d(p.x, p.y); };
    out = BrepCurve2D();
    switch (curve.Type) {
        case BrepCurveType::Line:
            out.Type = BrepCurve2DType::Line;
            out.Origin = map.Apply(xy(curve.Placement.Origin));
            out.Direction = map.Linear(xy(curve.Placement.AxisZ));
            out.TMin = curve.TMin;
            out.TMax = curve.TMax;
            return out.Direction.Length() > 1e-300;
        case BrepCurveType::Circle:
        case BrepCurveType::Ellipse:
            if (!map.KeepsCircles() || curve.Placement.AxisZ.z < 0.5) return false;
            out.Type = curve.Type == BrepCurveType::Circle ? BrepCurve2DType::Circle
                                                           : BrepCurve2DType::Ellipse;
            out.Origin = map.Apply(xy(curve.Placement.Origin));
            out.RefDirection = xy(curve.Placement.AxisX).Normalized();
            out.Radius = curve.Radius * map.ScaleU;
            out.MinorRadius = curve.MinorRadius * map.ScaleU;
            out.TMin = curve.TMin;
            out.TMax = curve.TMax;
            return true;
        case BrepCurveType::Polyline:
        case BrepCurveType::BSpline:
            out.Type = curve.Type == BrepCurveType::Polyline ? BrepCurve2DType::Polyline
                                                             : BrepCurve2DType::BSpline;
            for (const Vec3d& p : curve.ControlPoints) out.ControlPoints.push_back(map.Apply(xy(p)));
            out.Degree = curve.Degree;
            out.Knots = curve.Knots;
            out.Weights = curve.Weights;
            out.TMin = curve.TMin;
            out.TMax = curve.TMax;
            return true;
        default:
            return false;
    }
}

// ----- surfaces -----

int Reader::Surface(int directory, ParamMap& map) {
    auto cached = surfaceFor_.find(directory);
    if (cached != surfaceFor_.end()) {
        map = cached->second.second;
        return cached->second.first;
    }
    surfaceFor_[directory] = {-1, ParamMap()};

    const IF::Entity* entity = At(directory);
    if (!entity) return -1;
    const IF::Entity& e = *entity;
    const Affine transform = TransformOf(e);

    BrepSurface surface;
    map = ParamMap();
    bool built = false;

    // A frame from a location, an axis and an optional reference direction.
    auto frame = [&](int locationPointer, int axisPointer, int referencePointer,
                     BrepPlacement& placement) {
        Vec3d location, axis(0.0, 0.0, 1.0), reference;
        if (!PointOf(locationPointer, location)) return false;
        if (axisPointer > 0 && !DirectionOf(axisPointer, axis)) return false;
        placement.Origin = location;
        placement.AxisZ = axis.Normalized();
        if (referencePointer > 0 && DirectionOf(referencePointer, reference) &&
            reference.Cross(axis).Length() > 1e-12)
            placement.AxisX = reference;
        else
            placement.AxisX = AnyPerpendicular(axis);
        placement.Normalize();
        return true;
    };

    switch (e.Type) {
        case 128: {   // rational B-spline surface
            const int upperU = e.Integer(0), upperV = e.Integer(1);
            const int degreeU = e.Integer(2), degreeV = e.Integer(3);
            if (upperU < 1 || upperV < 1 || degreeU < 1 || degreeV < 1 || upperU > 100000 ||
                upperV > 100000 || degreeU > 64 || degreeV > 64)
                break;
            const bool polynomial = e.Integer(6) != 0;
            const size_t knotsU = static_cast<size_t>(upperU + degreeU + 2);
            const size_t knotsV = static_cast<size_t>(upperV + degreeV + 2);
            const size_t points = static_cast<size_t>(upperU + 1) * static_cast<size_t>(upperV + 1);
            const size_t knotsUAt = 9, knotsVAt = knotsUAt + knotsU, weightsAt = knotsVAt + knotsV;
            const size_t pointsAt = weightsAt + points, rangeAt = pointsAt + 3 * points;
            if (e.Params.size() < rangeAt) break;
            surface.Type = BrepSurfaceType::BSpline;
            surface.DegreeU = degreeU;
            surface.DegreeV = degreeV;
            surface.ControlPointsU = upperU + 1;
            surface.ControlPointsV = upperV + 1;
            for (size_t i = 0; i < knotsU; ++i) surface.KnotsU.push_back(e.Real(knotsUAt + i));
            for (size_t i = 0; i < knotsV; ++i) surface.KnotsV.push_back(e.Real(knotsVAt + i));
            bool allEqual = true;
            for (size_t i = 0; i < points; ++i) {
                surface.NetWeights.push_back(e.Real(weightsAt + i, 1.0));
                if (std::fabs(surface.NetWeights.back() - surface.NetWeights.front()) > 1e-15)
                    allEqual = false;
            }
            if (polynomial || allEqual) surface.NetWeights.clear();
            // IGES runs the first index fastest - the same row-major-in-u
            // order the document stores.
            for (size_t i = 0; i < points; ++i)
                surface.ControlNet.push_back(transform.Point(Vec3d(e.Real(pointsAt + 3 * i),
                                                                   e.Real(pointsAt + 3 * i + 1),
                                                                   e.Real(pointsAt + 3 * i + 2))));
            const double u0 = e.Real(rangeAt), u1 = e.Real(rangeAt + 1);
            const double v0 = e.Real(rangeAt + 2), v1 = e.Real(rangeAt + 3);
            if (u1 > u0) { surface.UMin = u0; surface.UMax = u1; }
            if (v1 > v0) { surface.VMin = v0; surface.VMax = v1; }
            surface.PeriodicU = e.Integer(7) != 0;
            surface.PeriodicV = e.Integer(8) != 0;
            built = true;
            break;
        }
        case 190:   // plane surface: S(u, v) = L + u X + v Y
            if (!frame(e.Pointer(0), e.Pointer(1), e.Form == 1 ? e.Pointer(2) : 0,
                       surface.Placement))
                break;
            surface.Type = BrepSurfaceType::Plane;
            TransformSurfaceFrame(surface.Placement, transform);
            built = true;
            break;
        case 192:   // right circular cylinder: u in [0, 1] is one turn
            if (!frame(e.Pointer(0), e.Pointer(1), e.Form == 1 ? e.Pointer(3) : 0,
                       surface.Placement))
                break;
            surface.Type = BrepSurfaceType::Cylinder;
            surface.Radius = e.Real(2);
            surface.Radius *= TransformSurfaceFrame(surface.Placement, transform);
            map.ScaleU = kTwoPi;
            built = surface.Radius > 0.0;
            break;
        case 194:   // right circular cone; the half-angle is in degrees
            if (!frame(e.Pointer(0), e.Pointer(1), e.Form == 1 ? e.Pointer(4) : 0,
                       surface.Placement))
                break;
            surface.Type = BrepSurfaceType::Cone;
            surface.Radius = e.Real(2);
            surface.HalfAngleRadians = e.Real(3) * kPi / 180.0;
            surface.Radius *= TransformSurfaceFrame(surface.Placement, transform);
            map.ScaleU = kTwoPi;
            built = surface.HalfAngleRadians > 0.0 && surface.HalfAngleRadians < kPi * 0.5;
            break;
        case 196:   // sphere: u one turn, v pole to pole
            if (!frame(e.Pointer(0), e.Form == 1 ? e.Pointer(2) : 0,
                       e.Form == 1 ? e.Pointer(3) : 0, surface.Placement))
                break;
            surface.Type = BrepSurfaceType::Sphere;
            surface.Radius = e.Real(1);
            surface.Radius *= TransformSurfaceFrame(surface.Placement, transform);
            map.ScaleU = kTwoPi;
            map.ScaleV = kPi;
            map.OffsetV = -kPi * 0.5;
            built = surface.Radius > 0.0;
            break;
        case 198:   // torus: both parameters one turn
            if (!frame(e.Pointer(0), e.Pointer(1), e.Form == 1 ? e.Pointer(4) : 0,
                       surface.Placement))
                break;
            surface.Type = BrepSurfaceType::Torus;
            surface.Radius = e.Real(2);
            surface.MinorRadius = e.Real(3);
            {
                const double scale = TransformSurfaceFrame(surface.Placement, transform);
                surface.Radius *= scale;
                surface.MinorRadius *= scale;
            }
            map.ScaleU = kTwoPi;
            map.ScaleV = kTwoPi;
            built = surface.Radius > 0.0 && surface.MinorRadius > 0.0;
            break;
        case 108: {   // plane as a coefficient equation, A x + B y + C z = D
            const Vec3d normal(e.Real(0), e.Real(1), e.Real(2));
            const double length = normal.Length();
            if (length <= 1e-15) break;
            surface.Type = BrepSurfaceType::Plane;
            const Vec3d unit = normal * (1.0 / length);
            Vec3d origin = unit * (e.Real(3) / length);
            const Vec3d symbol(e.Real(5), e.Real(6), e.Real(7));
            if (!e.Param(5).IsEmpty())
                origin = symbol - unit * (symbol - origin).Dot(unit);
            surface.Placement.Origin = origin;
            surface.Placement.AxisZ = unit;
            surface.Placement.AxisX = AnyPerpendicular(unit);
            surface.Placement.Normalize();
            TransformSurfaceFrame(surface.Placement, transform);
            map.Known = false;   // not a parametric surface at all
            built = true;
            break;
        }
        case 118: {   // ruled surface between two curves
            int first = Curve(e.Pointer(0));
            int second = Curve(e.Pointer(1));
            if (first < 0 || second < 0) break;
            BrepCurve a = brep_.Curves[static_cast<size_t>(first)];
            BrepCurve b = brep_.Curves[static_cast<size_t>(second)];
            TransformCurve(a, transform);
            TransformCurve(b, transform);
            if (e.Integer(2) == 1 && !ReverseCurve(b)) {
                if (b.Type == BrepCurveType::Polyline) { PolylineToBSpline(b); ReverseCurve(b); }
            }
            double a0 = 0.0, a1 = 1.0;
            a.ParameterRange(a0, a1);
            if (!RemapCurveRange(b, a0, a1))
                WarnOnce("a ruled surface's second curve could not be matched to the first's "
                         "parameter range; its rulings are approximate");
            if (e.Form == 0)
                WarnOnce("ruled surfaces by equal arc length (entity 118 form 0) are read with "
                         "rulings by equal parameter");
            surface.Type = BrepSurfaceType::Ruled;
            surface.ProfileCurve = brep_.AddCurve(std::move(a));
            surface.SecondCurve = brep_.AddCurve(std::move(b));
            surface.UMin = a0;
            surface.UMax = a1;
            surface.VMin = 0.0;
            surface.VMax = 1.0;
            map.ScaleU = a1 - a0;
            map.OffsetU = a0;
            built = true;
            break;
        }
        case 120: {   // surface of revolution
            const IF::Entity* axisEntity = At(e.Pointer(0));
            BrepCurve axis;
            if (!axisEntity || !BuildCurve(*axisEntity, axis) || axis.Type != BrepCurveType::Line)
                break;
            const int profile = Curve(e.Pointer(1));
            if (profile < 0) break;
            BrepCurve generatrix = brep_.Curves[static_cast<size_t>(profile)];
            TransformCurve(generatrix, transform);
            TransformCurve(axis, transform);
            double t0 = 0.0, t1 = 1.0;
            generatrix.ParameterRange(t0, t1);
            surface.Type = BrepSurfaceType::Revolution;
            surface.Placement.Origin = axis.Placement.Origin;
            surface.Placement.AxisZ = axis.Placement.AxisZ.Normalized();
            surface.Placement.AxisX = AnyPerpendicular(surface.Placement.AxisZ);
            surface.Placement.Normalize();
            surface.ProfileCurve = brep_.AddCurve(std::move(generatrix));
            double start = e.Real(2), end = e.Real(3, kTwoPi);
            if (end <= start) end = start + kTwoPi;
            surface.UMin = start;
            surface.UMax = end;
            surface.VMin = t0;
            surface.VMax = t1;
            // IGES puts the generatrix parameter first and the angle second.
            map.Swap = true;
            built = true;
            break;
        }
        case 122: {   // tabulated cylinder: a directrix swept along a line
            const int directrix = Curve(e.Pointer(0));
            if (directrix < 0) break;
            BrepCurve curve = brep_.Curves[static_cast<size_t>(directrix)];
            double t0 = 0.0, t1 = 1.0;
            curve.ParameterRange(t0, t1);
            // The end point is stated in the directrix's own space, before
            // the surface's transform.
            const Vec3d start = curve.Evaluate(t0);
            const Vec3d end(e.Real(1), e.Real(2), e.Real(3));
            TransformCurve(curve, transform);
            const Vec3d sweep = transform.Identity ? end - start
                                                   : transform.Point(end) - transform.Point(start);
            if (sweep.Length() <= 1e-15) break;
            surface.Type = BrepSurfaceType::Extrusion;
            surface.ProfileCurve = brep_.AddCurve(std::move(curve));
            surface.ExtrusionDirection = sweep;
            surface.UMin = t0;
            surface.UMax = t1;
            surface.VMin = 0.0;
            surface.VMax = 1.0;
            map.ScaleU = t1 - t0;
            map.OffsetU = t0;
            built = true;
            break;
        }
        default:
            break;
    }

    if (!built) {
        ++unsupported_[e.Type];
        return -1;
    }
    surface.Name = e.Label;
    const int index = brep_.AddSurface(std::move(surface));
    surfaceFor_[directory] = {index, map};
    return index;
}

// ----- attributes -----

int Reader::MaterialForColour(int colourNumber, const std::string& name) {
    if (colourNumber == 0) return -1;
    auto cached = materialForColour_.find(colourNumber);
    if (cached != materialForColour_.end()) return cached->second;

    Vec3f colour;
    std::string label = name;
    if (colourNumber > 0) {
        static const struct { const char* Name; float R, G, B; } kStandard[] = {
                {"Black", 0.0f, 0.0f, 0.0f},   {"Red", 1.0f, 0.0f, 0.0f},
                {"Green", 0.0f, 1.0f, 0.0f},   {"Blue", 0.0f, 0.0f, 1.0f},
                {"Yellow", 1.0f, 1.0f, 0.0f},  {"Magenta", 1.0f, 0.0f, 1.0f},
                {"Cyan", 0.0f, 1.0f, 1.0f},    {"White", 1.0f, 1.0f, 1.0f}};
        if (colourNumber > 8) { materialForColour_[colourNumber] = -1; return -1; }
        const auto& standard = kStandard[colourNumber - 1];
        colour = Vec3f(standard.R, standard.G, standard.B);
        if (label.empty()) label = standard.Name;
    } else {
        const IF::Entity* definition = At(-colourNumber);
        if (!definition || definition->Type != 314) {
            materialForColour_[colourNumber] = -1;
            return -1;
        }
        auto channel = [&](size_t index) {
            return static_cast<float>(std::max(0.0, std::min(100.0, definition->Real(index))) / 100.0);
        };
        colour = Vec3f(channel(0), channel(1), channel(2));
        if (label.empty()) label = definition->Text(3);
    }

    ModelMaterial material;
    material.Name = label.empty() ? std::string("colour_") + std::to_string(std::abs(colourNumber))
                                  : label;
    material.BaseColorFactor = Vec4f(colour.x, colour.y, colour.z, 1.0f);
    material.MetallicFactor = 0.0f;
    material.RoughnessFactor = 0.6f;
    PhongParams phong;
    phong.Diffuse = colour;
    phong.Ambient = Vec3f(colour.x * 0.2f, colour.y * 0.2f, colour.z * 0.2f);
    phong.Specular = Vec3f(0.2f, 0.2f, 0.2f);
    phong.Shininess = 32.0f;
    material.Phong = phong;
    const int index = document_.AddMaterial(std::move(material));
    materialForColour_[colourNumber] = index;
    return index;
}

// The name property (406 form 15) among the entity's property pointers,
// which follow its own parameters: a count of associativities and their
// pointers, then a count of properties and theirs.
std::string Reader::NameOf(const IF::Entity& entity, size_t consumed) const {
    if (entity.Params.size() > consumed + 1) {
        const int associativities = std::max(0, entity.Integer(consumed));
        const size_t propertiesAt = consumed + 1 + static_cast<size_t>(associativities);
        const int properties = std::max(0, entity.Integer(propertiesAt));
        for (int i = 0; i < properties && i < 64; ++i) {
            const IF::Entity* property = At(entity.Pointer(propertiesAt + 1 + static_cast<size_t>(i)));
            if (property && property->Type == 406 && property->Form == 15) {
                const std::string name = Trimmed(property->Text(1));
                if (!name.empty()) return name;
            }
        }
    }
    return entity.Label;
}

// ----- solids: 186 and the topology under it -----

int Reader::Vertex(int listDirectory, int index) {
    const std::pair<int, int> key{listDirectory, index};
    auto cached = vertexFor_.find(key);
    if (cached != vertexFor_.end()) return cached->second;
    vertexFor_[key] = -1;
    const IF::Entity* list = At(listDirectory);
    if (!list || list->Type != 502 || index < 1 || index > list->Integer(0)) return -1;
    const size_t at = 1 + 3 * static_cast<size_t>(index - 1);
    const Vec3d point = TransformOf(*list).Point(
            Vec3d(list->Real(at), list->Real(at + 1), list->Real(at + 2)));
    const int vertex = brep_.AddVertex(point);
    vertexFor_[key] = vertex;
    return vertex;
}

int Reader::MsboEdge(int listDirectory, int index) {
    const std::pair<int, int> key{listDirectory, index};
    auto cached = msboEdgeFor_.find(key);
    if (cached != msboEdgeFor_.end()) return cached->second;
    msboEdgeFor_[key] = -1;

    const IF::Entity* list = At(listDirectory);
    if (!list || list->Type != 504 || index < 1 || index > list->Integer(0)) return -1;
    const size_t at = 1 + 5 * static_cast<size_t>(index - 1);

    BrepEdge edge;
    edge.Start = Vertex(list->Pointer(at + 1), list->Integer(at + 2));
    edge.End = Vertex(list->Pointer(at + 3), list->Integer(at + 4));
    edge.Curve = Curve(list->Pointer(at));
    if (edge.Curve >= 0 && !TransformOf(*list).Identity) {
        BrepCurve copy = brep_.Curves[static_cast<size_t>(edge.Curve)];
        TransformCurve(copy, TransformOf(*list));
        edge.Curve = brep_.AddCurve(std::move(copy));
    }

    if (edge.Curve >= 0 && edge.Start >= 0 && edge.End >= 0) {
        const BrepCurve& curve = brep_.Curves[static_cast<size_t>(edge.Curve)];
        double a = 0.0, b = 0.0;
        curve.ParameterRange(a, b);
        const Vec3d start = brep_.Vertices[static_cast<size_t>(edge.Start)].Point;
        const Vec3d end = brep_.Vertices[static_cast<size_t>(edge.End)].Point;
        const Vec3d curveStart = curve.Evaluate(a), curveEnd = curve.Evaluate(b);
        const double slack = std::max(tolerance_ * 100.0, (curveEnd - curveStart).Length() * 1e-6);

        edge.TStart = a;
        edge.TEnd = b;
        if (edge.Start != edge.End) {
            if ((curveStart - start).Length() <= slack && (curveEnd - end).Length() <= slack) {
                // The ordinary case: the edge is its curve, start to end.
            } else if ((curveEnd - start).Length() <= slack && (curveStart - end).Length() <= slack) {
                std::swap(edge.TStart, edge.TEnd);
                edge.SameSense = false;
            } else {
                // A curve longer than its edge: find where the ends sit on it.
                edge.TStart = ParameterAt(curve, start);
                edge.TEnd = ParameterAt(curve, end);
                const bool periodic = curve.Type == BrepCurveType::Circle ||
                                      curve.Type == BrepCurveType::Ellipse;
                if (periodic) {
                    while (edge.TEnd <= edge.TStart + 1e-12) edge.TEnd += kTwoPi;
                }
                edge.SameSense = edge.TEnd >= edge.TStart;
            }
        } else if (!curve.IsClosed() && (curveEnd - curveStart).Length() <= slack) {
            edge.Degenerate = true;
        }
    } else if (edge.Curve < 0 && edge.Start >= 0 && edge.End >= 0) {
        const Vec3d from = brep_.Vertices[static_cast<size_t>(edge.Start)].Point;
        const Vec3d to = brep_.Vertices[static_cast<size_t>(edge.End)].Point;
        if ((to - from).Length() <= 1e-12) {
            edge.Degenerate = true;
        } else {
            BrepCurve line;
            line.Type = BrepCurveType::Line;
            line.Placement.Origin = from;
            line.Placement.AxisZ = to - from;
            line.TMin = 0.0;
            line.TMax = 1.0;
            edge.Curve = brep_.AddCurve(std::move(line));
            edge.TStart = 0.0;
            edge.TEnd = 1.0;
        }
    } else if (edge.Start < 0 || edge.End < 0) {
        return -1;
    }

    const int result = brep_.AddEdge(std::move(edge));
    msboEdgeFor_[key] = result;
    return result;
}

// Keeps a parameter-space curve only when it is the edge: its ends and its
// middle, lifted onto the surface, have to lie on the edge's curve, and it has
// to run the same way. A curve that runs the other way is turned round; one
// that does not lie on the edge at all - a parameterisation other than the
// one assumed for the surface - is dropped, and the edge is projected instead.
int Reader::AttachParameterCurve(BrepCurve2D curve, int surfaceIndex, int edgeIndex) {
    if (surfaceIndex < 0 || edgeIndex < 0) return -1;
    const BrepSurface& surface = brep_.Surfaces[static_cast<size_t>(surfaceIndex)];
    const BrepEdge& edge = brep_.Edges[static_cast<size_t>(edgeIndex)];

    // The edge as points, start to end.
    std::vector<Vec3d> samples;
    constexpr int kSamples = 64;
    if (edge.Curve >= 0) {
        const BrepCurve& space = brep_.Curves[static_cast<size_t>(edge.Curve)];
        for (int i = 0; i <= kSamples; ++i)
            samples.push_back(space.Evaluate(edge.TStart + (edge.TEnd - edge.TStart) * i / kSamples));
    } else if (edge.Start >= 0 && edge.End >= 0) {
        const Vec3d a = brep_.Vertices[static_cast<size_t>(edge.Start)].Point;
        const Vec3d b = brep_.Vertices[static_cast<size_t>(edge.End)].Point;
        for (int i = 0; i <= kSamples; ++i) samples.push_back(a + (b - a) * (static_cast<double>(i) / kSamples));
    } else {
        return -1;
    }
    double length = 0.0;
    for (size_t i = 1; i < samples.size(); ++i) length += (samples[i] - samples[i - 1]).Length();
    const double slack = std::max({tolerance_ * 100.0, length * 0.02, 1e-9});

    auto nearest = [&samples](const Vec3d& point, double& distance) {
        size_t best = 0;
        distance = std::numeric_limits<double>::max();
        for (size_t i = 0; i < samples.size(); ++i) {
            const double d = (samples[i] - point).Length();
            if (d < distance) { distance = d; best = i; }
        }
        return best;
    };

    auto check = [&](const BrepCurve2D& candidate) {
        double t0 = 0.0, t1 = 0.0;
        candidate.ParameterRange(t0, t1);
        auto lift = [&](double s) {
            const Vec2d p = candidate.Evaluate(t0 + (t1 - t0) * s);
            return surface.Evaluate(p.x, p.y, brep_.Curves);
        };
        if ((lift(0.0) - samples.front()).Length() > slack) return false;
        if ((lift(1.0) - samples.back()).Length() > slack) return false;
        double d1 = 0.0, d2 = 0.0, d3 = 0.0;
        const size_t quarter = nearest(lift(0.25), d1);
        nearest(lift(0.5), d2);
        const size_t threeQuarters = nearest(lift(0.75), d3);
        if (d1 > slack || d2 > slack || d3 > slack) return false;
        // Direction: a degenerate edge has none to get wrong.
        return length <= slack || quarter <= threeQuarters;
    };

    if (!check(curve)) {
        BrepCurve2D reversed = curve;
        if (!ReverseCurve2D(reversed) || !check(reversed)) {
            ++droppedParameterCurves_;
            return -1;
        }
        curve = std::move(reversed);
    }
    ++keptParameterCurves_;
    return brep_.AddCurve2D(std::move(curve));
}

bool Reader::MsboLoop(int directory, int surfaceIndex, const ParamMap& map, BrepLoop& out) {
    const IF::Entity* loop = At(directory);
    if (!loop || loop->Type != 508) return false;
    const int count = loop->Integer(0);
    size_t at = 1;
    for (int i = 0; i < count && at < loop->Params.size(); ++i) {
        const int type = loop->Integer(at);
        const int list = loop->Pointer(at + 1);
        const int index = loop->Integer(at + 2);
        const bool forward = loop->Integer(at + 3, 1) != 0;
        const int curves = std::max(0, loop->Integer(at + 4));
        const size_t curvesAt = at + 5;
        at = curvesAt + 2 * static_cast<size_t>(curves);

        BrepCoedge coedge;
        coedge.Forward = forward;
        if (type == 1) {
            // A vertex use: a pole or an apex, a span of no length.
            const int vertex = Vertex(list, index);
            if (vertex < 0) continue;
            BrepEdge pole;
            pole.Start = vertex;
            pole.End = vertex;
            pole.Degenerate = true;
            coedge.Edge = brep_.AddEdge(std::move(pole));
        } else {
            coedge.Edge = MsboEdge(list, index);
            if (coedge.Edge < 0) {
                Warn("a loop references edge " + std::to_string(index) + " of list " +
                     std::to_string(list) + ", which does not exist");
                continue;
            }
        }

        if (curves > 0 && map.Known) {
            const int pointer = loop->Pointer(curvesAt + 1);
            const IF::Entity* parameterCurve = At(pointer);
            if (parameterCurve && parameterCurve->Type != 102) {
                Segment segment;
                segment.Directory = pointer;
                BrepCurve2D candidate;
                if (Curve2DFrom(segment, map, candidate))
                    coedge.ParameterCurve = AttachParameterCurve(std::move(candidate), surfaceIndex,
                                                                 coedge.Edge);
            }
        }
        out.Coedges.push_back(coedge);
    }
    return !out.Coedges.empty();
}

int Reader::MsboFace(int directory, bool sameSense) {
    const IF::Entity* face = At(directory);
    if (!face || face->Type != 510) return -1;
    ParamMap map;
    const int surfaceIndex = Surface(face->Pointer(0), map);
    if (surfaceIndex < 0) {
        Warn("face " + std::to_string(directory) + " has a surface this reader cannot build");
        return -1;
    }
    surfacesInFaces_.insert(face->Pointer(0));

    BrepFace result;
    result.Surface = surfaceIndex;
    result.SameSense = sameSense;
    const int loops = face->Integer(1);
    const bool firstIsOuter = face->Integer(2) != 0;
    for (int i = 0; i < loops; ++i) {
        BrepLoop loop;
        if (!MsboLoop(face->Pointer(3 + static_cast<size_t>(i)), surfaceIndex, map, loop)) continue;
        loop.Kind = (i == 0 && firstIsOuter) ? BrepLoopKind::Outer : BrepLoopKind::Inner;
        result.Loops.push_back(brep_.AddLoop(std::move(loop)));
    }
    if (result.Loops.empty()) {
        Warn("face " + std::to_string(directory) + " has no loop that could be read");
        return -1;
    }
    result.Name = NameOf(*face, 3 + static_cast<size_t>(std::max(0, loops)));
    int material = MaterialForColour(face->Color);
    if (material < 0) {
        const IF::Entity* surface = At(face->Pointer(0));
        if (surface) material = MaterialForColour(surface->Color);
    }
    result.Material = material;
    result.Extras["igesDirectory"] = std::to_string(directory);
    return brep_.AddFace(std::move(result));
}

int Reader::MsboShell(int directory, bool orientation, bool& closed) {
    const IF::Entity* shell = At(directory);
    if (!shell || shell->Type != 514) return -1;
    BrepShell result;
    result.Closed = shell->Form != 2;
    closed = result.Closed;
    const int faces = shell->Integer(0);
    for (int i = 0; i < faces; ++i) {
        const size_t at = 1 + 2 * static_cast<size_t>(i);
        const bool agrees = shell->Integer(at + 1, 1) != 0;
        const int face = MsboFace(shell->Pointer(at), agrees == orientation);
        if (face >= 0) result.Faces.push_back(face);
    }
    if (result.Faces.empty()) return -1;
    result.Name = shell->Label;
    return brep_.AddShell(std::move(result));
}

void Reader::ReadSolids() {
    for (const auto& entry : model_.Entities) {
        const IF::Entity& msbo = entry.second;
        if (msbo.Type != 186) continue;

        BrepSolid solid;
        bool closed = true;
        const int outer = MsboShell(msbo.Pointer(0), msbo.Integer(1, 1) != 0, closed);
        if (outer < 0) {
            Warn("solid " + std::to_string(msbo.Directory) + " has no shell that could be read");
            continue;
        }
        solid.Shells.push_back(outer);
        solid.Closed = closed;
        const int voids = std::max(0, msbo.Integer(2));
        for (int i = 0; i < voids; ++i) {
            const size_t at = 3 + 2 * static_cast<size_t>(i);
            bool voidClosed = true;
            const int shell = MsboShell(msbo.Pointer(at), msbo.Integer(at + 1, 1) != 0, voidClosed);
            if (shell >= 0) solid.Shells.push_back(shell);
        }
        solid.Name = NameOf(msbo, 3 + 2 * static_cast<size_t>(voids));
        solid.Material = MaterialForColour(msbo.Color);
        solid.Extras["igesDirectory"] = std::to_string(msbo.Directory);
        const int index = brep_.AddSolid(std::move(solid));

        ModelNode node;
        node.Name = brep_.Solids[static_cast<size_t>(index)].Name.empty()
                            ? ("solid_" + std::to_string(msbo.Directory))
                            : brep_.Solids[static_cast<size_t>(index)].Name;
        node.Solid = index;
        document_.AddNode(std::move(node));
    }
}

// ----- surfaces: 144 trimmed surfaces, bounded planes, loose surfaces -----

// A loop from a chain of model-space curves, each one edge. The chain is
// connected head to tail by definition, but which way each piece runs is
// read from the geometry rather than trusted.
bool Reader::LoopFromCurves(const std::vector<Segment>& model, const std::vector<Segment>& parameter,
                            int surfaceIndex, const ParamMap& map, BrepLoop& out) {
    struct Piece { int Curve; double A, B; Vec3d First, Last; bool Forward; };
    std::vector<Piece> pieces;
    for (const Segment& segment : model) {
        const int index = CurveOf(segment);
        if (index < 0) continue;
        const BrepCurve& curve = brep_.Curves[static_cast<size_t>(index)];
        Piece piece;
        piece.Curve = index;
        curve.ParameterRange(piece.A, piece.B);
        piece.First = curve.Evaluate(piece.A);
        piece.Last = curve.Evaluate(piece.B);
        piece.Forward = true;
        pieces.push_back(piece);
    }

    if (pieces.empty()) {
        // No model-space boundary at all: the parameter curves alone, trusted
        // because there is nothing to hold them against.
        if (parameter.empty() || surfaceIndex < 0 || !map.Known) return false;
        const BrepSurface& surface = brep_.Surfaces[static_cast<size_t>(surfaceIndex)];
        std::vector<BrepCurve2D> curves;
        for (const Segment& segment : parameter) {
            BrepCurve2D curve;
            if (Curve2DFrom(segment, map, curve)) curves.push_back(std::move(curve));
        }
        if (curves.empty()) return false;
        std::vector<int> vertices;
        for (const BrepCurve2D& curve : curves) {
            double t0 = 0.0, t1 = 0.0;
            curve.ParameterRange(t0, t1);
            const Vec2d p = curve.Evaluate(t0);
            vertices.push_back(brep_.AddVertex(surface.Evaluate(p.x, p.y, brep_.Curves)));
        }
        for (size_t i = 0; i < curves.size(); ++i) {
            BrepEdge edge;
            edge.Start = vertices[i];
            edge.End = vertices[(i + 1) % vertices.size()];
            BrepCoedge coedge;
            coedge.Edge = brep_.AddEdge(std::move(edge));
            coedge.ParameterCurve = brep_.AddCurve2D(std::move(curves[i]));
            ++keptParameterCurves_;
            out.Coedges.push_back(coedge);
        }
        return true;
    }

    // Which way each piece runs.
    const double slack = std::max(tolerance_ * 1000.0, 1e-9);
    if (pieces.size() >= 2) {
        const Piece& next = pieces[1];
        auto gap = [&next](const Vec3d& p) {
            return std::min((p - next.First).Length(), (p - next.Last).Length());
        };
        // A piece is turned round only when the other way is clearly better:
        // a closed piece - a whole circle - meets its neighbours at both ends,
        // and rounding must not decide which way it runs.
        pieces[0].Forward = !(gap(pieces[0].Last) > gap(pieces[0].First) + slack);
        for (size_t i = 1; i < pieces.size(); ++i) {
            const Vec3d previous = pieces[i - 1].Forward ? pieces[i - 1].Last : pieces[i - 1].First;
            pieces[i].Forward = !((pieces[i].First - previous).Length() >
                                  (pieces[i].Last - previous).Length() + slack);
        }
    }
    size_t gaps = 0;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const Piece& a = pieces[i];
        const Piece& b = pieces[(i + 1) % pieces.size()];
        const Vec3d end = a.Forward ? a.Last : a.First;
        const Vec3d start = b.Forward ? b.First : b.Last;
        if ((end - start).Length() > slack * 10.0) ++gaps;
    }
    if (gaps > 0)
        WarnOnce("a trimming boundary does not close; its gaps are bridged");

    // A vertex at the start of each piece, shared with the end of the one
    // before it, so the loop closes by construction.
    std::vector<int> vertices;
    for (const Piece& piece : pieces)
        vertices.push_back(brep_.AddVertex(piece.Forward ? piece.First : piece.Last));

    const bool pairParameterCurves = parameter.size() == pieces.size() && map.Known;
    for (size_t i = 0; i < pieces.size(); ++i) {
        const Piece& piece = pieces[i];
        const int from = vertices[i];
        const int to = vertices[(i + 1) % vertices.size()];
        BrepEdge edge;
        edge.Curve = piece.Curve;
        edge.TStart = piece.A;
        edge.TEnd = piece.B;
        edge.Start = piece.Forward ? from : to;
        edge.End = piece.Forward ? to : from;
        BrepCoedge coedge;
        coedge.Forward = piece.Forward;
        coedge.Edge = brep_.AddEdge(std::move(edge));
        if (pairParameterCurves) {
            BrepCurve2D candidate;
            if (Curve2DFrom(parameter[i], map, candidate))
                coedge.ParameterCurve = AttachParameterCurve(std::move(candidate), surfaceIndex,
                                                             coedge.Edge);
        }
        out.Coedges.push_back(coedge);
    }
    return true;
}

// The parameter rectangle as a loop, for a trimmed surface whose outer
// boundary is the surface's own and which has holes in it.
int Reader::RectangleLoop(int surfaceIndex) {
    const BrepSurface& surface = brep_.Surfaces[static_cast<size_t>(surfaceIndex)];
    double u0 = 0.0, u1 = 1.0, v0 = 0.0, v1 = 1.0;
    surface.ParameterRange(u0, u1, v0, v1);
    const Vec2d corners[4] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    int vertices[4];
    for (int i = 0; i < 4; ++i)
        vertices[i] = brep_.AddVertex(surface.Evaluate(corners[i].x, corners[i].y, brep_.Curves));
    BrepLoop loop;
    loop.Kind = BrepLoopKind::Outer;
    for (int i = 0; i < 4; ++i) {
        BrepEdge edge;
        edge.Start = vertices[i];
        edge.End = vertices[(i + 1) % 4];
        if (brep_.Vertices[static_cast<size_t>(edge.Start)].Point.x ==
                    brep_.Vertices[static_cast<size_t>(edge.End)].Point.x &&
            (brep_.Vertices[static_cast<size_t>(edge.Start)].Point -
             brep_.Vertices[static_cast<size_t>(edge.End)].Point).Length() <= 1e-15)
            edge.Degenerate = true;
        BrepCurve2D side;
        side.Type = BrepCurve2DType::Line;
        side.Origin = corners[i];
        side.Direction = corners[(i + 1) % 4] - corners[i];
        side.TMin = 0.0;
        side.TMax = 1.0;
        BrepCoedge coedge;
        coedge.Edge = brep_.AddEdge(std::move(edge));
        coedge.ParameterCurve = brep_.AddCurve2D(std::move(side));
        loop.Coedges.push_back(coedge);
    }
    return brep_.AddLoop(std::move(loop));
}

void Reader::ReadSurfaceModel() {
    BrepShell shell;
    shell.Closed = false;
    shell.Name = "surfaces";
    std::set<int> boundedPlanesAsHoles;

    auto materialOf = [&](const IF::Entity& owner, int surfaceDirectory) {
        int material = MaterialForColour(owner.Color);
        if (material < 0) {
            const IF::Entity* surface = At(surfaceDirectory);
            if (surface) material = MaterialForColour(surface->Color);
        }
        return material;
    };

    // Curves on a surface, as the 142 states them.
    auto boundary = [&](int curveOnSurface, int surfaceIndex, const ParamMap& map, BrepLoop& loop) {
        const IF::Entity* entity = At(curveOnSurface);
        if (!entity || entity->Type != 142) return false;
        std::vector<Segment> model, parameter;
        if (entity->Pointer(3) > 0) Flatten(entity->Pointer(3), Affine(), model);
        if (entity->Pointer(2) > 0) Flatten(entity->Pointer(2), Affine(), parameter);
        return LoopFromCurves(model, parameter, surfaceIndex, map, loop);
    };

    for (const auto& entry : model_.Entities) {
        const IF::Entity& e = entry.second;

        if (e.Type == 144) {
            ParamMap map;
            const int surfaceIndex = Surface(e.Pointer(0), map);
            if (surfaceIndex < 0) {
                Warn("trimmed surface " + std::to_string(e.Directory) +
                     " has a surface this reader cannot build");
                continue;
            }
            surfacesInFaces_.insert(e.Pointer(0));
            BrepFace face;
            face.Surface = surfaceIndex;
            const bool outerGiven = e.Integer(1) != 0;
            const int holes = std::max(0, e.Integer(2));
            if (outerGiven) {
                BrepLoop loop;
                if (boundary(e.Pointer(3), surfaceIndex, map, loop)) {
                    loop.Kind = BrepLoopKind::Outer;
                    face.Loops.push_back(brep_.AddLoop(std::move(loop)));
                }
            }
            for (int i = 0; i < holes; ++i) {
                BrepLoop loop;
                if (boundary(e.Pointer(4 + static_cast<size_t>(i)), surfaceIndex, map, loop)) {
                    loop.Kind = BrepLoopKind::Inner;
                    face.Loops.push_back(brep_.AddLoop(std::move(loop)));
                }
            }
            if (!outerGiven && !face.Loops.empty())
                face.Loops.insert(face.Loops.begin(), RectangleLoop(surfaceIndex));
            if (outerGiven && face.Loops.empty()) {
                Warn("trimmed surface " + std::to_string(e.Directory) +
                     " has a boundary that could not be read; it is skipped");
                continue;
            }
            face.Name = NameOf(e, 4 + static_cast<size_t>(holes));
            face.Material = materialOf(e, e.Pointer(0));
            face.Extras["igesDirectory"] = std::to_string(e.Directory);
            shell.Faces.push_back(brep_.AddFace(std::move(face)));
            continue;
        }

        if (e.Type == 108 && e.Form == 1 && e.SubordinateSwitch() == 0 && e.Pointer(4) > 0) {
            ParamMap map;
            const int surfaceIndex = Surface(e.Directory, map);
            if (surfaceIndex < 0) continue;
            std::vector<Segment> model;
            Flatten(e.Pointer(4), Affine(), model);
            BrepLoop loop;
            if (!LoopFromCurves(model, {}, surfaceIndex, map, loop)) continue;
            loop.Kind = BrepLoopKind::Outer;
            BrepFace face;
            face.Surface = surfaceIndex;
            face.Loops.push_back(brep_.AddLoop(std::move(loop)));
            face.Name = e.Label;
            face.Material = MaterialForColour(e.Color);
            face.Extras["igesDirectory"] = std::to_string(e.Directory);
            surfacesInFaces_.insert(e.Directory);
            shell.Faces.push_back(brep_.AddFace(std::move(face)));
            continue;
        }
        if (e.Type == 108 && e.Form == -1) boundedPlanesAsHoles.insert(e.Directory);
    }

    // Surfaces standing on their own: untrimmed, but bounded by their own
    // parameter range. The unbounded analytic ones - a plane, a cylinder, a
    // cone - have no extent to draw and are left out.
    size_t unbounded = 0;
    for (const auto& entry : model_.Entities) {
        const IF::Entity& e = entry.second;
        const bool bounded = e.Type == 128 || e.Type == 118 || e.Type == 120 || e.Type == 122 ||
                             e.Type == 196 || e.Type == 198;
        const bool unboundedType = e.Type == 190 || e.Type == 192 || e.Type == 194;
        if (!bounded && !unboundedType) continue;
        if (e.SubordinateSwitch() != 0 || e.EntityUseFlag() == 5) continue;
        if (surfacesInFaces_.count(e.Directory)) continue;
        if (unboundedType) { ++unbounded; continue; }
        ParamMap map;
        const int surfaceIndex = Surface(e.Directory, map);
        if (surfaceIndex < 0) continue;
        BrepFace face;
        face.Surface = surfaceIndex;
        face.Name = e.Label;
        face.Material = MaterialForColour(e.Color);
        face.Extras["igesDirectory"] = std::to_string(e.Directory);
        face.Extras["untrimmed"] = "true";
        shell.Faces.push_back(brep_.AddFace(std::move(face)));
    }
    if (unbounded > 0)
        Warn(std::to_string(unbounded) + " unbounded surface(s) (planes, cylinders, cones with "
             "no trimming) have no extent to draw and were left out");
    if (!boundedPlanesAsHoles.empty())
        WarnOnce("bounded planes marked as holes (entity 108 form -1) are not read");

    if (shell.Faces.empty()) return;
    BrepSolid solid;
    solid.Closed = false;
    solid.Name = "Surfaces";
    solid.Shells.push_back(brep_.AddShell(std::move(shell)));
    const int index = brep_.AddSolid(std::move(solid));
    ModelNode node;
    node.Name = "Surfaces";
    node.Solid = index;
    document_.AddNode(std::move(node));
}

// ----- the global section -----

void Reader::ReadUnits() {
    const int flag = model_.GlobalInteger(14, 0);
    std::string name = ToUpper(Trimmed(model_.GlobalText(15)));
    struct Named { int Flag; const char* Name; ModelUnit Unit; double Metres; };
    static const Named kUnits[] = {
            {1, "IN", ModelUnit::Inch, 0.0254},           {1, "INCH", ModelUnit::Inch, 0.0254},
            {2, "MM", ModelUnit::Millimeter, 1e-3},       {4, "FT", ModelUnit::Foot, 0.3048},
            {5, "MI", ModelUnit::Mile, 1609.344},         {6, "M", ModelUnit::Meter, 1.0},
            {7, "KM", ModelUnit::Kilometer, 1e3},         {8, "MIL", ModelUnit::Mil, 2.54e-5},
            {9, "UM", ModelUnit::Micrometer, 1e-6},       {10, "CM", ModelUnit::Centimeter, 1e-2},
            {11, "UIN", ModelUnit::Unspecified, 2.54e-8}};
    for (const Named& unit : kUnits) {
        const bool byFlag = flag == unit.Flag && flag != 3;
        const bool byName = flag == 3 && name == unit.Name;
        if (!byFlag && !byName) continue;
        document_.SourceUnit = unit.Unit;
        document_.UnitScaleToMeters = unit.Metres;
        return;
    }
    if (flag == 3 && !name.empty())
        Warn("the file's unit '" + name + "' is not one this reader knows; geometry is read as "
             "it stands");
}

void Reader::ReadHeader() {
    const std::string product = Trimmed(model_.GlobalText(3));
    const std::string fileName = Trimmed(model_.GlobalText(4));
    const std::string system = Trimmed(model_.GlobalText(5));
    const std::string author = Trimmed(model_.GlobalText(21));
    const std::string organisation = Trimmed(model_.GlobalText(22));
    const std::string date = Trimmed(model_.GlobalText(18));

    if (!product.empty()) document_.Title = product;
    if (!fileName.empty()) document_.Metadata["fileName"] = fileName;
    if (!system.empty()) document_.Metadata["originatingSystem"] = system;
    if (!organisation.empty()) document_.Metadata["organization"] = organisation;
    if (!date.empty()) document_.Metadata["timeStamp"] = date;
    if (!author.empty()) document_.Author = author;
    const int version = model_.GlobalInteger(23, 0);
    if (version > 0) document_.Metadata["igesVersionFlag"] = std::to_string(version);
}

void Reader::ComputeTolerance() {
    double resolution = model_.GlobalReal(19, 0.0);
    double extent = model_.GlobalReal(20, 0.0);
    if (extent <= 0.0) {
        // The file did not say: measure the vertices and points it carries.
        for (const auto& entry : model_.Entities) {
            const IF::Entity& e = entry.second;
            if (e.Type == 116) {
                for (size_t i = 0; i < 3; ++i) extent = std::max(extent, std::fabs(e.Real(i)));
            } else if (e.Type == 502) {
                for (size_t i = 1; i < e.Params.size(); ++i)
                    extent = std::max(extent, std::fabs(e.Real(i)));
            }
        }
    }
    tolerance_ = std::max({resolution, extent * 1e-7, 1e-9});
}

void Reader::Run() {
    ReadHeader();
    ReadUnits();
    ComputeTolerance();

    for (const auto& entry : model_.Entities) {
        const int type = entry.second.Type;
        if (type == 140 || type == 143 || type == 408 || type == 308 || type == 141)
            ++unsupported_[type];
    }

    ReadSolids();
    ReadSurfaceModel();

    // A body with no colour at all still needs something to draw with.
    if (!brep_.Solids.empty()) {
        int fallback = -1;
        for (BrepSolid& solid : brep_.Solids) {
            if (solid.Material >= 0) continue;
            if (fallback < 0) {
                ModelMaterial material;
                material.Name = "Steel";
                material.BaseColorFactor = Vec4f(0.72f, 0.74f, 0.78f, 1.0f);
                material.MetallicFactor = 0.1f;
                material.RoughnessFactor = 0.45f;
                material.DeriveMissingModel();
                fallback = document_.AddMaterial(std::move(material));
            }
            solid.Material = fallback;
        }
    }

    if (!unsupported_.empty()) {
        std::string list;
        for (const auto& entry : unsupported_) {
            if (!list.empty()) list += ", ";
            list += std::to_string(entry.first) + " (x" + std::to_string(entry.second) + ")";
        }
        Warn("entity types not read: " + list);
    }
    if (droppedParameterCurves_ > 0)
        Warn(std::to_string(droppedParameterCurves_) + " parameter-space trimming curve(s) did "
             "not lie on their edges and were replaced by projecting the edge onto its surface");

    std::vector<std::string> problems;
    if (!brep_.Validate(problems)) {
        // A surface standing on its own has no loops by design.
        problems.erase(std::remove_if(problems.begin(), problems.end(),
                                      [](const std::string& p) {
                                          return p.find("has no loops") != std::string::npos;
                                      }),
                       problems.end());
        const size_t show = std::min<size_t>(problems.size(), 6);
        for (size_t i = 0; i < show; ++i) Warn(problems[i]);
        if (problems.size() > show)
            Warn("and " + std::to_string(problems.size() - show) +
                 " further structural problem(s)");
    }
}

// ===== WRITER =====

class Writer {
public:
    Writer(const ModelDocument& document, const ConversionOptions& options)
            : document_(document), brep_(document.Brep), options_(options) {
        digits_ = options.Precision == NumericPrecision::Full
                          ? std::numeric_limits<double>::max_digits10
                          : 15;   // the STEP writer's reasoning: an exchange format's numbers are exact
    }

    bool Run(std::ostream& out, const std::string& name);

private:
    const ModelDocument& document_;
    const BrepData& brep_;
    const ConversionOptions& options_;
    IF::FileWriter file_;
    int digits_ = 15;

    std::map<int, int> surfaceIds_;
    std::map<int, int> colourIds_;
    std::set<std::string> warned_;

    // Topology for the solids, numbered from 1 as IGES lists are.
    std::vector<Vec3d> vertexPoints_;
    std::map<int, int> vertexIndex_;   // document vertex -> list index
    std::map<int, int> edgeIndex_;     // document edge -> list index
    int vertexList_ = 0, edgeList_ = 0;

    static constexpr const char* kSubordinate = "00010000";
    static constexpr const char* kParametric = "00010500";
    static constexpr const char* kIndependent = "00000000";

    void WarnOnce(const std::string& message) {
        if (warned_.insert(message).second) options_.Warn("IGES: " + message);
    }

    std::string R(double value) const { return IF::FormatReal(value, digits_); }
    static std::string I(long long value) { return IF::FormatInteger(value); }

    int Add(int type, int form, std::vector<std::string> params,
            const char* status = kSubordinate, int transform = 0, int colour = 0,
            const std::string& label = std::string()) {
        IF::FileWriter::EntitySpec spec;
        spec.Type = type;
        spec.Form = form;
        spec.Params = std::move(params);
        spec.StatusNumber = status;
        spec.Transform = transform;
        spec.Color = colour;
        spec.Label = label;
        return file_.Add(std::move(spec));
    }

    int Point(const Vec3d& p) { return Add(116, 0, {R(p.x), R(p.y), R(p.z), "0"}); }
    int Direction(const Vec3d& d) {
        const Vec3d unit = d.Normalized();
        return Add(123, 0, {R(unit.x), R(unit.y), R(unit.z)});
    }
    int Frame(const BrepPlacement& frame) {
        const Vec3d x = frame.AxisX, y = frame.AxisY(), z = frame.AxisZ;
        return Add(124, 0, {R(x.x), R(y.x), R(z.x), R(frame.Origin.x),
                            R(x.y), R(y.y), R(z.y), R(frame.Origin.y),
                            R(x.z), R(y.z), R(z.z), R(frame.Origin.z)});
    }

    int Polyline(const std::vector<Vec3d>& points, const char* status = kSubordinate);
    int BSplineCurve(const BrepCurve& curve, double from, double to, bool planar,
                     const char* status = kSubordinate);
    int CurveSegment(const BrepCurve& curve, double from, double to);
    int ParameterCurve(const BrepCurve2D& curve);
    int Surface(int index);
    int Colour(int material);
    int NameProperty(const std::string& name) {
        return Add(406, 15, {"1", IF::FormatString(name)}, kSubordinate);
    }

    int EdgeCurve(const BrepEdge& edge);
    bool BuildTopology(const std::vector<int>& solids);
    int Loop(const BrepLoop& loop, bool withParameterCurves);
    int Face(int faceIndex, int solidMaterial);
    int Shell(int shellIndex, int solidMaterial);
    int Solid(int solidIndex);
    void OpenSolid(int solidIndex);
    int Boundary(const BrepLoop& loop, int surfaceId, bool withParameterCurves);
};

int Writer::Polyline(const std::vector<Vec3d>& points, const char* status) {
    std::vector<std::string> params = {"2", I(static_cast<long long>(points.size()))};
    for (const Vec3d& p : points) {
        params.push_back(R(p.x));
        params.push_back(R(p.y));
        params.push_back(R(p.z));
    }
    return Add(106, 12, std::move(params), status);
}

int Writer::BSplineCurve(const BrepCurve& source, double from, double to, bool planar,
                         const char* status) {
    BrepCurve curve = source;
    if (from > to) {
        if (!ReverseCurve(curve)) return -1;
        const double sum = source.Knots.front() + source.Knots.back();
        from = sum - from;
        to = sum - to;
    }
    const size_t n = curve.ControlPoints.size();
    if (n < 2 || curve.Knots.size() != n + static_cast<size_t>(curve.Degree) + 1) return -1;
    bool polynomial = curve.Weights.size() != n;
    if (!polynomial) {
        polynomial = true;
        for (double w : curve.Weights)
            if (std::fabs(w - curve.Weights.front()) > 1e-15) polynomial = false;
    }
    std::vector<std::string> params = {I(static_cast<long long>(n) - 1), I(curve.Degree),
                                       planar ? "1" : "0", "0", polynomial ? "1" : "0", "0"};
    for (double knot : curve.Knots) params.push_back(R(knot));
    for (size_t i = 0; i < n; ++i)
        params.push_back(R(curve.Weights.size() == n ? curve.Weights[i] : 1.0));
    for (const Vec3d& p : curve.ControlPoints) {
        params.push_back(R(p.x));
        params.push_back(R(p.y));
        params.push_back(R(p.z));
    }
    params.push_back(R(from));
    params.push_back(R(to));
    params.push_back("0.");
    params.push_back("0.");
    params.push_back(planar ? "1." : "0.");
    return Add(126, 0, std::move(params), status);
}

// The piece of `curve` from parameter `from` to `to`, as a bounded IGES curve
// that starts where the piece starts.
int Writer::CurveSegment(const BrepCurve& curve, double from, double to) {
    switch (curve.Type) {
        case BrepCurveType::Line: {
            const Vec3d a = curve.Evaluate(from), b = curve.Evaluate(to);
            return Add(110, 0, {R(a.x), R(a.y), R(a.z), R(b.x), R(b.y), R(b.z)});
        }
        case BrepCurveType::Circle:
        case BrepCurveType::Ellipse: {
            // Arcs run counter-clockwise in their own plane; one running the
            // other way is written in the mirrored frame, where it does.
            BrepPlacement frame = curve.Placement;
            double a0 = from, a1 = to;
            if (to < from) {
                frame.AxisZ = frame.AxisZ * -1.0;
                a0 = -from;
                a1 = -to;
            }
            const int transform = Frame(frame);
            const bool whole = a1 - a0 >= kTwoPi - 1e-9;
            if (curve.Type == BrepCurveType::Circle) {
                const double r = curve.Radius;
                const double x0 = r * std::cos(a0), y0 = r * std::sin(a0);
                const double x1 = whole ? x0 : r * std::cos(a1), y1 = whole ? y0 : r * std::sin(a1);
                return Add(100, 0, {"0.", "0.", "0.", R(x0), R(y0), R(x1), R(y1)}, kSubordinate,
                           transform);
            }
            const double a = curve.Radius, b = curve.MinorRadius;
            if (a <= 0.0 || b <= 0.0) return -1;
            const double x0 = a * std::cos(a0), y0 = b * std::sin(a0);
            const double x1 = whole ? x0 : a * std::cos(a1), y1 = whole ? y0 : b * std::sin(a1);
            return Add(104, 1, {R(1.0 / (a * a)), "0.", R(1.0 / (b * b)), "0.", "0.", "-1.", "0.",
                                R(x0), R(y0), R(x1), R(y1)},
                       kSubordinate, transform);
        }
        case BrepCurveType::Polyline: {
            std::vector<Vec3d> points;
            const double lo = std::min(from, to), hi = std::max(from, to);
            points.push_back(curve.Evaluate(lo));
            for (size_t i = 0; i < curve.ControlPoints.size(); ++i) {
                const double t = static_cast<double>(i);
                if (t > lo + 1e-12 && t < hi - 1e-12) points.push_back(curve.ControlPoints[i]);
            }
            points.push_back(curve.Evaluate(hi));
            if (to < from) std::reverse(points.begin(), points.end());
            return Polyline(points);
        }
        case BrepCurveType::BSpline:
            return BSplineCurve(curve, from, to, false);
        default: {
            WarnOnce("parabolic and hyperbolic curves are written as 64-segment polylines");
            std::vector<Vec3d> points;
            for (int i = 0; i <= 64; ++i) points.push_back(curve.Evaluate(from + (to - from) * i / 64.0));
            return Polyline(points);
        }
    }
}

// A trimming curve in a B-spline surface's own parameter space, where the
// document's (u, v) and IGES's are the same numbers.
int Writer::ParameterCurve(const BrepCurve2D& curve) {
    double t0 = 0.0, t1 = 0.0;
    curve.ParameterRange(t0, t1);
    switch (curve.Type) {
        case BrepCurve2DType::Line: {
            const Vec2d a = curve.Evaluate(t0), b = curve.Evaluate(t1);
            return Add(110, 0, {R(a.x), R(a.y), "0.", R(b.x), R(b.y), "0."}, kParametric);
        }
        case BrepCurve2DType::Circle: {
            // A full turn is not referenced to RefDirection by a 100, whose
            // start and end coincide; an arc's ends carry its position.
            const Vec2d a = curve.Evaluate(t0), b = curve.Evaluate(t1);
            return Add(100, 0, {"0.", R(curve.Origin.x), R(curve.Origin.y), R(a.x), R(a.y),
                                R(b.x), R(b.y)},
                       kParametric);
        }
        case BrepCurve2DType::Polyline: {
            std::vector<Vec3d> points;
            for (const Vec2d& p : curve.ControlPoints) points.emplace_back(p.x, p.y, 0.0);
            if (curve.HasRange()) {
                BrepCurve space;
                space.Type = BrepCurveType::Polyline;
                space.ControlPoints = points;
                std::vector<Vec3d> trimmed = {space.Evaluate(t0)};
                for (size_t i = 0; i < points.size(); ++i) {
                    const double t = static_cast<double>(i);
                    if (t > t0 + 1e-12 && t < t1 - 1e-12) trimmed.push_back(points[i]);
                }
                trimmed.push_back(space.Evaluate(t1));
                points = std::move(trimmed);
            }
            return Polyline(points, kParametric);
        }
        case BrepCurve2DType::BSpline: {
            BrepCurve space;
            space.Type = BrepCurveType::BSpline;
            space.Degree = curve.Degree;
            space.Knots = curve.Knots;
            space.Weights = curve.Weights;
            for (const Vec2d& p : curve.ControlPoints) space.ControlPoints.emplace_back(p.x, p.y, 0.0);
            return BSplineCurve(space, t0, t1, true, kParametric);
        }
        default:
            return -1;
    }
}

int Writer::Surface(int index) {
    auto cached = surfaceIds_.find(index);
    if (cached != surfaceIds_.end()) return cached->second;
    surfaceIds_[index] = -1;
    if (index < 0 || static_cast<size_t>(index) >= brep_.Surfaces.size()) return -1;
    const BrepSurface& s = brep_.Surfaces[static_cast<size_t>(index)];
    const BrepPlacement& p = s.Placement;

    auto curveAt = [this](int curveIndex) -> const BrepCurve* {
        if (curveIndex < 0 || static_cast<size_t>(curveIndex) >= brep_.Curves.size()) return nullptr;
        return &brep_.Curves[static_cast<size_t>(curveIndex)];
    };

    int id = -1;
    switch (s.Type) {
        case BrepSurfaceType::Plane:
            id = Add(190, 1, {I(Point(p.Origin)), I(Direction(p.AxisZ)), I(Direction(p.AxisX))});
            break;
        case BrepSurfaceType::Cylinder:
            id = Add(192, 1, {I(Point(p.Origin)), I(Direction(p.AxisZ)), R(s.Radius),
                              I(Direction(p.AxisX))});
            break;
        case BrepSurfaceType::Cone:
            id = Add(194, 1, {I(Point(p.Origin)), I(Direction(p.AxisZ)), R(s.Radius),
                              R(s.HalfAngleRadians * 180.0 / kPi), I(Direction(p.AxisX))});
            break;
        case BrepSurfaceType::Sphere:
            id = Add(196, 1, {I(Point(p.Origin)), R(s.Radius), I(Direction(p.AxisZ)),
                              I(Direction(p.AxisX))});
            break;
        case BrepSurfaceType::Torus:
            id = Add(198, 1, {I(Point(p.Origin)), I(Direction(p.AxisZ)), R(s.Radius),
                              R(s.MinorRadius), I(Direction(p.AxisX))});
            break;
        case BrepSurfaceType::Extrusion: {
            const BrepCurve* profile = curveAt(s.ProfileCurve);
            if (!profile) break;
            double u0 = 0.0, u1 = 1.0;
            profile->ParameterRange(u0, u1);
            if (s.HasURange()) { u0 = s.UMin; u1 = s.UMax; }
            const double v0 = s.HasVRange() ? s.VMin : 0.0, v1 = s.HasVRange() ? s.VMax : 1.0;
            // The directrix sits at v0, so the sweep covers [v0, v1].
            BrepCurve directrix = *profile;
            Affine shift;
            shift.Identity = false;
            shift.T = s.ExtrusionDirection * v0;
            TransformCurve(directrix, shift);
            const int base = CurveSegment(directrix, u0, u1);
            if (base < 0) break;
            const Vec3d end = directrix.Evaluate(u0) + s.ExtrusionDirection * (v1 - v0);
            id = Add(122, 0, {I(base), R(end.x), R(end.y), R(end.z)});
            break;
        }
        case BrepSurfaceType::Revolution: {
            const BrepCurve* profile = curveAt(s.ProfileCurve);
            if (!profile) break;
            double v0 = 0.0, v1 = 1.0;
            profile->ParameterRange(v0, v1);
            if (s.HasVRange()) { v0 = s.VMin; v1 = s.VMax; }
            const double u0 = s.HasURange() ? s.UMin : 0.0, u1 = s.HasURange() ? s.UMax : kTwoPi;
            const Vec3d axisEnd = p.Origin + p.AxisZ.Normalized();
            const int axis = Add(110, 0, {R(p.Origin.x), R(p.Origin.y), R(p.Origin.z),
                                          R(axisEnd.x), R(axisEnd.y), R(axisEnd.z)});
            const int generatrix = CurveSegment(*profile, v0, v1);
            if (generatrix < 0) break;
            id = Add(120, 0, {I(axis), I(generatrix), R(u0), R(u1)});
            break;
        }
        case BrepSurfaceType::Ruled: {
            const BrepCurve* first = curveAt(s.ProfileCurve);
            const BrepCurve* second = curveAt(s.SecondCurve);
            if (!first || !second) break;
            double u0 = 0.0, u1 = 1.0, v0 = 0.0, v1 = 1.0;
            s.ParameterRange(u0, u1, v0, v1);
            const int a = CurveSegment(*first, u0, u1);
            const int b = CurveSegment(*second, u0, u1);
            if (a < 0 || b < 0) break;
            id = Add(118, 1, {I(a), I(b), "0", "0"});
            break;
        }
        case BrepSurfaceType::BSpline: {
            const size_t nu = static_cast<size_t>(std::max(0, s.ControlPointsU));
            const size_t nv = static_cast<size_t>(std::max(0, s.ControlPointsV));
            if (nu < 2 || nv < 2 || s.ControlNet.size() != nu * nv ||
                s.KnotsU.size() != nu + static_cast<size_t>(s.DegreeU) + 1 ||
                s.KnotsV.size() != nv + static_cast<size_t>(s.DegreeV) + 1)
                break;
            const bool rational = s.NetWeights.size() == nu * nv;
            bool polynomial = !rational;
            if (rational) {
                polynomial = true;
                for (double w : s.NetWeights)
                    if (std::fabs(w - s.NetWeights.front()) > 1e-15) polynomial = false;
            }
            std::vector<std::string> params = {I(static_cast<long long>(nu) - 1),
                                               I(static_cast<long long>(nv) - 1), I(s.DegreeU),
                                               I(s.DegreeV), "0", "0", polynomial ? "1" : "0",
                                               s.PeriodicU ? "1" : "0", s.PeriodicV ? "1" : "0"};
            for (double k : s.KnotsU) params.push_back(R(k));
            for (double k : s.KnotsV) params.push_back(R(k));
            for (size_t i = 0; i < nu * nv; ++i) params.push_back(R(rational ? s.NetWeights[i] : 1.0));
            for (const Vec3d& q : s.ControlNet) {
                params.push_back(R(q.x));
                params.push_back(R(q.y));
                params.push_back(R(q.z));
            }
            double u0 = 0.0, u1 = 1.0, v0 = 0.0, v1 = 1.0;
            s.ParameterRange(u0, u1, v0, v1);
            params.push_back(R(u0));
            params.push_back(R(u1));
            params.push_back(R(v0));
            params.push_back(R(v1));
            id = Add(128, 0, std::move(params));
            break;
        }
    }
    if (id < 0) WarnOnce("a surface could not be written; the faces on it are left out");
    surfaceIds_[index] = id;
    return id;
}

int Writer::Colour(int material) {
    if (material < 0 || static_cast<size_t>(material) >= document_.Materials.size()) return 0;
    auto cached = colourIds_.find(material);
    if (cached != colourIds_.end()) return -cached->second;
    const ModelMaterial& m = document_.Materials[static_cast<size_t>(material)];
    auto percent = [this](float channel) {
        return R(std::max(0.0, std::min(100.0, static_cast<double>(channel) * 100.0)));
    };
    std::vector<std::string> params = {percent(m.BaseColorFactor.x), percent(m.BaseColorFactor.y),
                                       percent(m.BaseColorFactor.z)};
    if (!m.Name.empty()) params.push_back(IF::FormatString(m.Name));
    const int id = Add(314, 0, std::move(params), "00000200");
    colourIds_[material] = id;
    return -id;
}

// ----- solids -----

int Writer::EdgeCurve(const BrepEdge& edge) {
    if (edge.Curve >= 0 && static_cast<size_t>(edge.Curve) < brep_.Curves.size()) {
        const BrepCurve& curve = brep_.Curves[static_cast<size_t>(edge.Curve)];
        double t0 = edge.TStart, t1 = edge.TEnd;
        if (t0 == t1) curve.ParameterRange(t0, t1);
        return CurveSegment(curve, t0, t1);
    }
    if (edge.Start < 0 || edge.End < 0) return -1;
    const Vec3d a = brep_.Vertices[static_cast<size_t>(edge.Start)].Point;
    const Vec3d b = brep_.Vertices[static_cast<size_t>(edge.End)].Point;
    return Add(110, 0, {R(a.x), R(a.y), R(a.z), R(b.x), R(b.y), R(b.z)});
}

// One vertex list and one edge list for every closed solid in the file.
bool Writer::BuildTopology(const std::vector<int>& solids) {
    std::vector<int> edges;
    std::set<int> seenEdges;
    auto vertex = [this](int index, const Vec3d& fallback) {
        if (index >= 0) {
            auto found = vertexIndex_.find(index);
            if (found != vertexIndex_.end()) return found->second;
            vertexPoints_.push_back(brep_.Vertices[static_cast<size_t>(index)].Point);
            const int listIndex = static_cast<int>(vertexPoints_.size());
            vertexIndex_[index] = listIndex;
            return listIndex;
        }
        vertexPoints_.push_back(fallback);
        return static_cast<int>(vertexPoints_.size());
    };

    std::vector<std::array<int, 5>> rows;   // curve, start, end
    for (int solidIndex : solids) {
        const BrepSolid& solid = brep_.Solids[static_cast<size_t>(solidIndex)];
        for (int shellIndex : solid.Shells) {
            if (shellIndex < 0 || static_cast<size_t>(shellIndex) >= brep_.Shells.size()) continue;
            for (int faceIndex : brep_.Shells[static_cast<size_t>(shellIndex)].Faces) {
                if (faceIndex < 0 || static_cast<size_t>(faceIndex) >= brep_.Faces.size()) continue;
                for (int loopIndex : brep_.Faces[static_cast<size_t>(faceIndex)].Loops) {
                    if (loopIndex < 0 || static_cast<size_t>(loopIndex) >= brep_.Loops.size()) continue;
                    for (const BrepCoedge& coedge : brep_.Loops[static_cast<size_t>(loopIndex)].Coedges) {
                        if (coedge.Edge < 0 || static_cast<size_t>(coedge.Edge) >= brep_.Edges.size())
                            continue;
                        const BrepEdge& edge = brep_.Edges[static_cast<size_t>(coedge.Edge)];
                        if (edge.Degenerate || (edge.Curve < 0 && edge.Start == edge.End)) {
                            if (edge.Start >= 0) vertex(edge.Start, Vec3d());
                            continue;
                        }
                        if (!seenEdges.insert(coedge.Edge).second) continue;
                        const int curve = EdgeCurve(edge);
                        if (curve < 0) continue;
                        double t0 = edge.TStart, t1 = edge.TEnd;
                        Vec3d start, end;
                        if (edge.Curve >= 0) {
                            const BrepCurve& c = brep_.Curves[static_cast<size_t>(edge.Curve)];
                            if (t0 == t1) c.ParameterRange(t0, t1);
                            start = c.Evaluate(t0);
                            end = c.Evaluate(t1);
                        }
                        const int from = vertex(edge.Start, start);
                        const int to = edge.End == edge.Start && edge.Start >= 0 ? from
                                                                                  : vertex(edge.End, end);
                        rows.push_back({curve, from, to, 0, 0});
                        edgeIndex_[coedge.Edge] = static_cast<int>(rows.size());
                    }
                }
            }
        }
    }
    if (vertexPoints_.empty()) return false;

    std::vector<std::string> vertexParams = {I(static_cast<long long>(vertexPoints_.size()))};
    for (const Vec3d& p : vertexPoints_) {
        vertexParams.push_back(R(p.x));
        vertexParams.push_back(R(p.y));
        vertexParams.push_back(R(p.z));
    }
    vertexList_ = Add(502, 1, std::move(vertexParams));

    std::vector<std::string> edgeParams = {I(static_cast<long long>(rows.size()))};
    for (const auto& row : rows) {
        edgeParams.push_back(I(row[0]));
        edgeParams.push_back(I(vertexList_));
        edgeParams.push_back(I(row[1]));
        edgeParams.push_back(I(vertexList_));
        edgeParams.push_back(I(row[2]));
    }
    edgeList_ = Add(504, 1, std::move(edgeParams));
    return true;
}

int Writer::Loop(const BrepLoop& loop, bool withParameterCurves) {
    std::vector<std::string> params;
    int count = 0;
    for (const BrepCoedge& coedge : loop.Coedges) {
        if (coedge.Edge < 0 || static_cast<size_t>(coedge.Edge) >= brep_.Edges.size()) continue;
        const BrepEdge& edge = brep_.Edges[static_cast<size_t>(coedge.Edge)];
        int parameterCurve = -1;
        if (withParameterCurves && coedge.ParameterCurve >= 0 &&
            static_cast<size_t>(coedge.ParameterCurve) < brep_.Curves2D.size())
            parameterCurve = ParameterCurve(brep_.Curves2D[static_cast<size_t>(coedge.ParameterCurve)]);

        auto found = edgeIndex_.find(coedge.Edge);
        if (found != edgeIndex_.end()) {
            params.push_back("0");
            params.push_back(I(edgeList_));
            params.push_back(I(found->second));
            params.push_back(coedge.Forward ? "1" : "0");
        } else {
            auto vertex = vertexIndex_.find(edge.Start);
            if (vertex == vertexIndex_.end()) continue;
            params.push_back("1");
            params.push_back(I(vertexList_));
            params.push_back(I(vertex->second));
            params.push_back("1");
        }
        if (parameterCurve > 0) {
            params.push_back("1");
            params.push_back("0");
            params.push_back(I(parameterCurve));
        } else {
            params.push_back("0");
        }
        ++count;
    }
    if (count == 0) return -1;
    params.insert(params.begin(), I(count));
    return Add(508, 1, std::move(params));
}

int Writer::Face(int faceIndex, int solidMaterial) {
    const BrepFace& face = brep_.Faces[static_cast<size_t>(faceIndex)];
    const int surface = Surface(face.Surface);
    if (surface < 0) return -1;
    const bool nurbs = brep_.Surfaces[static_cast<size_t>(face.Surface)].Type == BrepSurfaceType::BSpline;

    // The outer loop first, which is what the face's flag says.
    std::vector<int> order;
    bool outerFirst = false;
    for (int loopIndex : face.Loops)
        if (loopIndex >= 0 && static_cast<size_t>(loopIndex) < brep_.Loops.size() &&
            brep_.Loops[static_cast<size_t>(loopIndex)].Kind == BrepLoopKind::Outer) {
            order.push_back(loopIndex);
            outerFirst = true;
            break;
        }
    for (int loopIndex : face.Loops)
        if (order.empty() || loopIndex != order.front()) order.push_back(loopIndex);
    if (!outerFirst && order.size() == 1) outerFirst = true;

    std::vector<std::string> loops;
    for (int loopIndex : order) {
        if (loopIndex < 0 || static_cast<size_t>(loopIndex) >= brep_.Loops.size()) continue;
        const int loop = Loop(brep_.Loops[static_cast<size_t>(loopIndex)], nurbs);
        if (loop > 0) loops.push_back(I(loop));
    }
    if (loops.empty()) return -1;
    std::vector<std::string> params = {I(surface), I(static_cast<long long>(loops.size())),
                                       outerFirst ? "1" : "0"};
    params.insert(params.end(), loops.begin(), loops.end());
    const int colour = face.Material >= 0 && face.Material != solidMaterial ? Colour(face.Material) : 0;
    return Add(510, 1, std::move(params), kSubordinate, 0, colour);
}

int Writer::Shell(int shellIndex, int solidMaterial) {
    if (shellIndex < 0 || static_cast<size_t>(shellIndex) >= brep_.Shells.size()) return -1;
    const BrepShell& shell = brep_.Shells[static_cast<size_t>(shellIndex)];
    std::vector<std::string> faces;
    for (int faceIndex : shell.Faces) {
        if (faceIndex < 0 || static_cast<size_t>(faceIndex) >= brep_.Faces.size()) continue;
        const int face = Face(faceIndex, solidMaterial);
        if (face < 0) continue;
        faces.push_back(I(face));
        faces.push_back(brep_.Faces[static_cast<size_t>(faceIndex)].SameSense ? "1" : "0");
    }
    if (faces.empty()) return -1;
    std::vector<std::string> params = {I(static_cast<long long>(faces.size() / 2))};
    params.insert(params.end(), faces.begin(), faces.end());
    return Add(514, shell.Closed ? 1 : 2, std::move(params));
}

int Writer::Solid(int solidIndex) {
    const BrepSolid& solid = brep_.Solids[static_cast<size_t>(solidIndex)];
    std::vector<int> shells;
    for (int shellIndex : solid.Shells) {
        const int shell = Shell(shellIndex, solid.Material);
        if (shell > 0) shells.push_back(shell);
    }
    if (shells.empty()) return -1;
    std::vector<std::string> params = {I(shells.front()), "1",
                                       I(static_cast<long long>(shells.size()) - 1)};
    for (size_t i = 1; i < shells.size(); ++i) {
        params.push_back(I(shells[i]));
        params.push_back("1");
    }
    if (!solid.Name.empty()) {
        params.push_back("0");
        params.push_back("1");
        params.push_back(I(NameProperty(solid.Name)));
    }
    return Add(186, 0, std::move(params), kIndependent, 0, Colour(solid.Material),
               solid.Name.substr(0, 8));
}

// ----- open solids, as trimmed surfaces -----

int Writer::Boundary(const BrepLoop& loop, int surfaceId, bool withParameterCurves) {
    std::vector<int> model, parameter;
    bool parameterComplete = withParameterCurves;
    for (const BrepCoedge& coedge : loop.Coedges) {
        if (coedge.Edge < 0 || static_cast<size_t>(coedge.Edge) >= brep_.Edges.size()) continue;
        const BrepEdge& edge = brep_.Edges[static_cast<size_t>(coedge.Edge)];

        if (parameterComplete) {
            if (coedge.ParameterCurve >= 0 &&
                static_cast<size_t>(coedge.ParameterCurve) < brep_.Curves2D.size()) {
                BrepCurve2D curve = brep_.Curves2D[static_cast<size_t>(coedge.ParameterCurve)];
                if (!coedge.Forward && !ReverseCurve2D(curve)) parameterComplete = false;
                const int id = parameterComplete ? ParameterCurve(curve) : -1;
                if (id > 0) parameter.push_back(id);
                else parameterComplete = false;
            } else {
                parameterComplete = false;
            }
        }

        if (edge.Degenerate) continue;
        int id = -1;
        if (edge.Curve >= 0 && static_cast<size_t>(edge.Curve) < brep_.Curves.size()) {
            const BrepCurve& curve = brep_.Curves[static_cast<size_t>(edge.Curve)];
            double t0 = edge.TStart, t1 = edge.TEnd;
            if (t0 == t1) curve.ParameterRange(t0, t1);
            id = coedge.Forward ? CurveSegment(curve, t0, t1) : CurveSegment(curve, t1, t0);
        } else if (edge.Start >= 0 && edge.End >= 0 && edge.Start != edge.End) {
            Vec3d a = brep_.Vertices[static_cast<size_t>(edge.Start)].Point;
            Vec3d b = brep_.Vertices[static_cast<size_t>(edge.End)].Point;
            if (!coedge.Forward) std::swap(a, b);
            id = Add(110, 0, {R(a.x), R(a.y), R(a.z), R(b.x), R(b.y), R(b.z)});
        }
        if (id > 0) model.push_back(id);
    }
    if (model.empty()) return -1;

    auto composite = [this](const std::vector<int>& pieces, const char* status) {
        if (pieces.size() == 1) return pieces.front();
        std::vector<std::string> params = {I(static_cast<long long>(pieces.size()))};
        for (int piece : pieces) params.push_back(I(piece));
        return Add(102, 0, std::move(params), status);
    };
    const int c = composite(model, kSubordinate);
    const int b = parameterComplete && !parameter.empty() ? composite(parameter, kParametric) : 0;
    return Add(142, 0, {"0", I(surfaceId), I(b), I(c), b > 0 ? "1" : "2"});
}

void Writer::OpenSolid(int solidIndex) {
    const BrepSolid& solid = brep_.Solids[static_cast<size_t>(solidIndex)];
    for (int shellIndex : solid.Shells) {
        if (shellIndex < 0 || static_cast<size_t>(shellIndex) >= brep_.Shells.size()) continue;
        for (int faceIndex : brep_.Shells[static_cast<size_t>(shellIndex)].Faces) {
            if (faceIndex < 0 || static_cast<size_t>(faceIndex) >= brep_.Faces.size()) continue;
            const BrepFace& face = brep_.Faces[static_cast<size_t>(faceIndex)];
            const int material = face.Material >= 0 ? face.Material : solid.Material;
            const int surface = Surface(face.Surface);
            if (surface < 0) continue;
            if (!face.SameSense)
                WarnOnce("a trimmed surface cannot state that its face runs against the "
                         "surface's normal; such faces are written as their surface");
            const bool nurbs = brep_.Surfaces[static_cast<size_t>(face.Surface)].Type ==
                               BrepSurfaceType::BSpline;

            if (face.Loops.empty()) {
                // An untrimmed patch is the surface itself.
                IF::FileWriter::EntitySpec& spec = file_.Get(surface);
                spec.StatusNumber = kIndependent;
                spec.Color = Colour(material);
                continue;
            }

            int outer = -1;
            for (int loopIndex : face.Loops)
                if (loopIndex >= 0 && static_cast<size_t>(loopIndex) < brep_.Loops.size() &&
                    brep_.Loops[static_cast<size_t>(loopIndex)].Kind == BrepLoopKind::Outer) {
                    outer = loopIndex;
                    break;
                }
            if (outer < 0) {
                // Nothing marked: a lone loop bounds the face, and of several the
                // first is taken, which is where every writer puts it.
                bool anyInner = false;
                for (int loopIndex : face.Loops)
                    if (loopIndex >= 0 && static_cast<size_t>(loopIndex) < brep_.Loops.size() &&
                        brep_.Loops[static_cast<size_t>(loopIndex)].Kind == BrepLoopKind::Inner)
                        anyInner = true;
                if (!anyInner || face.Loops.size() > 1) outer = face.Loops.front();
            }

            int outerId = 0;
            std::vector<int> holes;
            for (int loopIndex : face.Loops) {
                if (loopIndex < 0 || static_cast<size_t>(loopIndex) >= brep_.Loops.size()) continue;
                const int id = Boundary(brep_.Loops[static_cast<size_t>(loopIndex)], surface, nurbs);
                if (id < 0) continue;
                if (loopIndex == outer) outerId = id;
                else holes.push_back(id);
            }
            if (outerId == 0 && holes.empty()) continue;
            std::vector<std::string> params = {I(surface), outerId > 0 ? "1" : "0",
                                               I(static_cast<long long>(holes.size())), I(outerId)};
            for (int hole : holes) params.push_back(I(hole));
            Add(144, 0, std::move(params), kIndependent, 0, Colour(material), face.Name.substr(0, 8));
        }
    }
}

bool Writer::Run(std::ostream& out, const std::string& name) {
    if (brep_.Solids.empty()) return false;

    std::vector<int> closed, open;
    for (size_t i = 0; i < brep_.Solids.size(); ++i)
        (brep_.Solids[i].Closed ? closed : open).push_back(static_cast<int>(i));

    size_t written = file_.Count();
    if (!closed.empty() && BuildTopology(closed))
        for (int index : closed) Solid(index);
    for (int index : open) OpenSolid(index);
    if (file_.Count() == written) return false;

    // --- the global section ---
    struct Unit { ModelUnit Unit; int Flag; const char* Name; };
    static const Unit kUnits[] = {
            {ModelUnit::Inch, 1, "INCH"},     {ModelUnit::Millimeter, 2, "MM"},
            {ModelUnit::Foot, 4, "FT"},       {ModelUnit::Mile, 5, "MI"},
            {ModelUnit::Meter, 6, "M"},       {ModelUnit::Kilometer, 7, "KM"},
            {ModelUnit::Mil, 8, "MIL"},       {ModelUnit::Micrometer, 9, "UM"},
            {ModelUnit::Centimeter, 10, "CM"}};
    int unitFlag = 2;
    std::string unitName = "MM";
    bool named = document_.SourceUnit == ModelUnit::Unspecified;   // IGES must state one; mm is the format's own default
    for (const Unit& unit : kUnits)
        if (unit.Unit == document_.SourceUnit) {
            unitFlag = unit.Flag;
            unitName = unit.Name;
            named = true;
        }
    if (!named) {
        unitFlag = 3;
        unitName = ModelUnitSymbol(document_.SourceUnit);
        WarnOnce("the document's unit has no IGES unit flag; it is written by name, which not "
                 "every reader understands");
    }

    double extent = 0.0;
    for (const BrepVertex& vertex : brep_.Vertices)
        extent = std::max({extent, std::fabs(vertex.Point.x), std::fabs(vertex.Point.y),
                           std::fabs(vertex.Point.z)});
    const Bounds3D bounds = brep_.ComputeBounds();
    if (extent <= 0.0 && bounds.Min.x <= bounds.Max.x) {
        extent = std::max({std::fabs(bounds.Min.x), std::fabs(bounds.Min.y), std::fabs(bounds.Min.z),
                           std::fabs(bounds.Max.x), std::fabs(bounds.Max.y), std::fabs(bounds.Max.z)});
    }

    // The date the file was made: the document's own when it carries one in
    // IGES's form, else today's. Only the day, so two writes of one document
    // on the same day are the same bytes.
    std::string date;
    auto stamp = document_.Metadata.find("timeStamp");
    if (stamp != document_.Metadata.end() && stamp->second.size() == 15 && stamp->second[8] == '.')
        date = stamp->second;
    if (date.empty()) {
        const auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        const std::chrono::year_month_day ymd{today};
        const int year = static_cast<int>(ymd.year());
        const unsigned month = static_cast<unsigned>(ymd.month());
        const unsigned day = static_cast<unsigned>(ymd.day());
        date = std::to_string(year) + (month < 10 ? "0" : "") + std::to_string(month) +
               (day < 10 ? "0" : "") + std::to_string(day) + ".000000";
    }

    const std::string generator = options_.Generator.empty() ? std::string("UltraCanvas")
                                                             : options_.Generator;
    const std::vector<std::string> global = {
            IF::FormatString(name),                // 3 sender's product id
            IF::FormatString(name + ".igs"),       // 4 file name
            IF::FormatString(generator),           // 5 native system
            IF::FormatString(generator),           // 6 preprocessor version
            "32", "38", "6", "308", "15",          // 7-11 integer bits and float ranges
            IF::FormatString(name),                // 12 receiver's product id
            "1.",                                  // 13 model space scale
            I(unitFlag), IF::FormatString(unitName),   // 14, 15
            "1", "1.",                             // 16, 17 line weights
            IF::FormatString(date),                // 18 date of the file
            R(std::max(extent, 1.0) * 1e-9),       // 19 minimum resolution
            R(extent),                             // 20 maximum coordinate
            IF::FormatString(document_.Author),    // 21 author
            "",                                    // 22 organisation
            "11",                                  // 23 IGES 5.3
            "0",                                   // 24 no drafting standard
            IF::FormatString(date)};               // 25 date the model was made

    file_.Write(out, "UltraCanvas IGES 5.3 export: " + name, global);
    return true;
}

} // namespace

// ===== CONVERTER =====

FormatCapabilities IgesConverter::GetCapabilities() const {
    FormatCapabilities capabilities;
    capabilities.Brep = true;
    capabilities.NurbsSurfaces = true;
    capabilities.Meshes = true;          // written as a faceted solid, as STEP does
    capabilities.NGons = true;
    capabilities.SceneGraph = false;
    capabilities.Materials = true;       // colours, not shading models
    capabilities.Units = true;
    capabilities.UpAxis = false;
    capabilities.Metadata = true;
    capabilities.DoublePrecision = true;
    capabilities.Assemblies = false;
    return capabilities;
}

std::shared_ptr<ModelDocument> IgesConverter::ImportFromStream(std::istream& stream,
                                                               const ConversionOptions& options) {
    IF::Model file;
    if (!file.Parse(stream, [&options](const std::string& message) { options.Warn(message); }))
        return nullptr;

    auto document = std::make_shared<ModelDocument>();
    document->SourceFormat = "iges";
    document->Generator = options.Generator;
    // IGES has no up axis; its model space is Z-up by every CAD system's
    // convention, as STEP's is.
    document->Up = UpAxis::ZUp;

    Reader reader(file, *document, options);
    reader.Run();

    if (document->Brep.Solids.empty()) {
        options.Warn("IGES: the file holds no solid or surface this reader could build");
        return nullptr;
    }
    if (options.TessellateOnImport) {
        std::vector<std::string> problems;
        document->TessellateBreps(options.Tessellation, &problems);
        for (const std::string& problem : problems) options.Warn("IGES: " + problem);
    }
    if (options.ForceUpAxis.has_value()) document->ConvertUpAxis(*options.ForceUpAxis);
    return document;
}

std::shared_ptr<ModelDocument> IgesConverter::Import(const std::string& filename,
                                                     const ConversionOptions& options) {
    std::ifstream file(UltraCanvas::PathFromUtf8(filename), std::ios::binary);
    if (!file) {
        options.Warn("IGES: cannot open " + filename);
        return nullptr;
    }
    auto document = ImportFromStream(file, options);
    if (document && document->Title.empty()) {
        const size_t slash = filename.find_last_of("/\\");
        const std::string base = slash == std::string::npos ? filename : filename.substr(slash + 1);
        const size_t dot = base.find_last_of('.');
        document->Title = dot == std::string::npos ? base : base.substr(0, dot);
    }
    return document;
}

std::shared_ptr<ModelDocument> IgesConverter::ImportFromMemory(const std::vector<uint8_t>& data,
                                                               const ConversionOptions& options) {
    std::string text(data.begin(), data.end());
    std::istringstream stream(text);
    return ImportFromStream(stream, options);
}

bool IgesConverter::ExportToStream(const ModelDocument& document, std::ostream& stream,
                                   const ConversionOptions& options) {
    std::string name = document.Title.empty() ? "model" : document.Title;

    if (!document.Brep.Solids.empty()) {
        Writer writer(document, options);
        if (writer.Run(stream, name)) return true;
        options.Warn("IGES: none of the document's bodies could be written");
        return false;
    }
    if (document.Meshes.empty()) {
        options.Warn("IGES: the document holds neither solids nor meshes; nothing to write");
        return false;
    }
    ModelDocument faceted = document;
    StepConverter::FacetMeshesIntoBrep(faceted, options);
    if (faceted.Brep.Solids.empty()) {
        options.Warn("IGES: the document's meshes produced no facets that could be written");
        return false;
    }
    options.Warn("IGES: the document holds meshes rather than exact bodies, so it is written as "
                 "a faceted solid - every facet becomes its own planar face, and the result is "
                 "the mesh's accuracy, not a CAD model's");
    Writer writer(faceted, options);
    return writer.Run(stream, name);
}

bool IgesConverter::Export(const ModelDocument& document, const std::string& filename,
                           const ConversionOptions& options) {
    std::ofstream file(UltraCanvas::PathFromUtf8(filename), std::ios::binary);
    if (!file) {
        options.Warn("IGES: cannot write " + filename);
        return false;
    }
    return ExportToStream(document, file, options);
}

bool IgesConverter::ExportToMemory(const ModelDocument& document, std::vector<uint8_t>& outData,
                                   const ConversionOptions& options) {
    std::ostringstream stream;
    if (!ExportToStream(document, stream, options)) return false;
    const std::string text = stream.str();
    outData.assign(text.begin(), text.end());
    return true;
}

bool IgesConverter::ValidateData(const std::vector<uint8_t>& data) const {
    return IF::LooksLikeIgesFile(data);
}

bool IgesConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(UltraCanvas::PathFromUtf8(filename), std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> head(160);
    file.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));
    return IF::LooksLikeIgesFile(head);
}

} // namespace ModelConverter
} // namespace UltraCanvas
