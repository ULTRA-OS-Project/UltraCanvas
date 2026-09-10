// core/DataFormats/UltraCanvasBrepStorage.cpp
// Implementation of the boundary-representation types declared in
// include/DataFormats/UltraCanvasBrepStorage.h: parametric evaluation,
// inverse projection, structural validation and trimmed-surface tessellation.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasBrepStorage.h"
#include "DataFormats/UltraCanvasPolygonTriangulation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace UltraCanvas {
namespace ModelStorage {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// ===== NURBS BASIS =====
//
// The Cox-de Boor recurrence, in the form Piegl & Tiller give it (The NURBS
// Book, A2.1 and A2.2). Every one of STEP, IGES, ACIS and OpenNURBS stores
// exactly these numbers — degree, knot vector, control points, optional
// weights — so one evaluator serves all four.

// The knot span u falls in. n is the last control-point index, p the degree.
int FindSpan(int n, int p, double u, const std::vector<double>& knots) {
    if (static_cast<int>(knots.size()) < n + p + 2) return p;
    if (u >= knots[static_cast<size_t>(n) + 1]) return n;
    if (u <= knots[static_cast<size_t>(p)]) return p;
    int low = p, high = n + 1, mid = (low + high) / 2;
    while (u < knots[static_cast<size_t>(mid)] || u >= knots[static_cast<size_t>(mid) + 1]) {
        if (u < knots[static_cast<size_t>(mid)]) high = mid; else low = mid;
        mid = (low + high) / 2;
        if (mid <= p) return p;
        if (mid >= n) return n;
    }
    return mid;
}

// The p+1 basis functions that are non-zero on that span.
void BasisFunctions(int span, double u, int p, const std::vector<double>& knots,
                    std::vector<double>& out) {
    out.assign(static_cast<size_t>(p) + 1, 0.0);
    std::vector<double> left(static_cast<size_t>(p) + 1, 0.0);
    std::vector<double> right(static_cast<size_t>(p) + 1, 0.0);
    out[0] = 1.0;
    for (int j = 1; j <= p; ++j) {
        left[static_cast<size_t>(j)] = u - knots[static_cast<size_t>(span + 1 - j)];
        right[static_cast<size_t>(j)] = knots[static_cast<size_t>(span + j)] - u;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            const double denom = right[static_cast<size_t>(r) + 1] +
                                 left[static_cast<size_t>(j - r)];
            const double temp = denom != 0.0 ? out[static_cast<size_t>(r)] / denom : 0.0;
            out[static_cast<size_t>(r)] = saved + right[static_cast<size_t>(r) + 1] * temp;
            saved = left[static_cast<size_t>(j - r)] * temp;
        }
        out[static_cast<size_t>(j)] = saved;
    }
}

// True when the knot vector is the length the degree and control-point count
// require. A file that disagrees is not evaluable, and the callers below fall
// back to treating the control points as a polyline rather than reading out of
// bounds.
bool KnotsUsable(size_t controlPoints, int degree, const std::vector<double>& knots) {
    if (degree < 1 || controlPoints < static_cast<size_t>(degree) + 1) return false;
    return knots.size() == controlPoints + static_cast<size_t>(degree) + 1;
}

// Evaluate a polyline through `points` at t in [0, count-1].
template <typename V>
V EvaluatePolyline(const std::vector<V>& points, double t) {
    if (points.empty()) return V();
    if (points.size() == 1) return points[0];
    const double clamped = std::max(0.0, std::min(t, static_cast<double>(points.size() - 1)));
    const size_t i = std::min(static_cast<size_t>(clamped), points.size() - 2);
    const double f = clamped - static_cast<double>(i);
    return points[i] + (points[i + 1] - points[i]) * f;
}

// Rational or polynomial B-spline evaluation, shared by the 2D and 3D curves.
template <typename V>
V EvaluateBSpline(const std::vector<V>& controlPoints, const std::vector<double>& weights,
                  const std::vector<double>& knots, int degree, double t) {
    if (controlPoints.empty()) return V();
    if (!KnotsUsable(controlPoints.size(), degree, knots))
        return EvaluatePolyline(controlPoints, t);

    const int n = static_cast<int>(controlPoints.size()) - 1;
    const double lo = knots[static_cast<size_t>(degree)];
    const double hi = knots[static_cast<size_t>(n) + 1];
    const double u = std::max(lo, std::min(t, hi));

    const int span = FindSpan(n, degree, u, knots);
    std::vector<double> basis;
    BasisFunctions(span, u, degree, knots, basis);

    const bool rational = weights.size() == controlPoints.size();
    V numerator;
    double denominator = 0.0;
    for (int j = 0; j <= degree; ++j) {
        const size_t index = static_cast<size_t>(span - degree + j);
        if (index >= controlPoints.size()) continue;
        const double w = rational ? weights[index] : 1.0;
        const double coefficient = basis[static_cast<size_t>(j)] * w;
        numerator += controlPoints[index] * coefficient;
        denominator += coefficient;
    }
    if (denominator == 0.0) return controlPoints.front();
    return numerator * (1.0 / denominator);
}

double NormalizeAngleInto(double angle, double lower) {
    while (angle < lower) angle += kTwoPi;
    while (angle >= lower + kTwoPi) angle -= kTwoPi;
    return angle;
}

} // namespace

// ===== PLACEMENT =====

Vec3d BrepPlacement::ToWorld(double x, double y, double z) const {
    const Vec3d axisY = AxisY();
    return Origin + AxisX * x + axisY * y + AxisZ * z;
}

Vec3d BrepPlacement::ToLocal(const Vec3d& world) const {
    const Vec3d d = world - Origin;
    return Vec3d(d.Dot(AxisX), d.Dot(AxisY()), d.Dot(AxisZ));
}

void BrepPlacement::Normalize() {
    AxisZ = AxisZ.Normalized();
    if (AxisZ.Length() < 0.5) AxisZ = Vec3d(0.0, 0.0, 1.0);
    // Gram-Schmidt: keep the file's reference direction, but only the part of
    // it that is actually perpendicular to the axis.
    AxisX = (AxisX - AxisZ * AxisZ.Dot(AxisX)).Normalized();
    if (AxisX.Length() < 0.5) {
        const Vec3d seed = std::fabs(AxisZ.x) > 0.9 ? Vec3d(0.0, 1.0, 0.0) : Vec3d(1.0, 0.0, 0.0);
        AxisX = (seed - AxisZ * AxisZ.Dot(seed)).Normalized();
    }
}

// ===== 3D CURVE =====

void BrepCurve::ParameterRange(double& tMin, double& tMax) const {
    if (HasRange()) { tMin = TMin; tMax = TMax; return; }
    switch (Type) {
        case BrepCurveType::Circle:
        case BrepCurveType::Ellipse:
            tMin = 0.0; tMax = kTwoPi; return;
        case BrepCurveType::Parabola:
        case BrepCurveType::Hyperbola:
            tMin = -1.0; tMax = 1.0; return;
        case BrepCurveType::Polyline:
            tMin = 0.0;
            tMax = ControlPoints.size() > 1 ? static_cast<double>(ControlPoints.size() - 1) : 0.0;
            return;
        case BrepCurveType::BSpline:
            if (KnotsUsable(ControlPoints.size(), Degree, Knots)) {
                tMin = Knots[static_cast<size_t>(Degree)];
                tMax = Knots[ControlPoints.size()];
                return;
            }
            tMin = 0.0;
            tMax = ControlPoints.size() > 1 ? static_cast<double>(ControlPoints.size() - 1) : 1.0;
            return;
        case BrepCurveType::Line:
        default:
            tMin = 0.0; tMax = 1.0; return;
    }
}

Vec3d BrepCurve::Evaluate(double t) const {
    const Vec3d axisY = Placement.AxisY();
    switch (Type) {
        case BrepCurveType::Line:
            return Placement.Origin + Placement.AxisZ * t;
        case BrepCurveType::Circle:
            return Placement.Origin + Placement.AxisX * (Radius * std::cos(t)) +
                   axisY * (Radius * std::sin(t));
        case BrepCurveType::Ellipse:
            return Placement.Origin + Placement.AxisX * (Radius * std::cos(t)) +
                   axisY * (MinorRadius * std::sin(t));
        case BrepCurveType::Parabola:
            // STEP's parameterisation: the focal distance scales both axes, so
            // t is arc-like rather than an x coordinate.
            return Placement.Origin + Placement.AxisX * (FocalDistance * t * t) +
                   axisY * (2.0 * FocalDistance * t);
        case BrepCurveType::Hyperbola:
            return Placement.Origin + Placement.AxisX * (Radius * std::cosh(t)) +
                   axisY * (MinorRadius * std::sinh(t));
        case BrepCurveType::Polyline:
            return EvaluatePolyline(ControlPoints, t);
        case BrepCurveType::BSpline:
            return EvaluateBSpline(ControlPoints, Weights, Knots, Degree, t);
    }
    return Placement.Origin;
}

Vec3d BrepCurve::Tangent(double t) const {
    const Vec3d axisY = Placement.AxisY();
    switch (Type) {
        case BrepCurveType::Line:
            return Placement.AxisZ;
        case BrepCurveType::Circle:
            return Placement.AxisX * (-Radius * std::sin(t)) + axisY * (Radius * std::cos(t));
        case BrepCurveType::Ellipse:
            return Placement.AxisX * (-Radius * std::sin(t)) +
                   axisY * (MinorRadius * std::cos(t));
        default: {
            // A central difference over a step scaled to the curve's own
            // domain: for the spline forms, writing out the derivative basis
            // buys accuracy tessellation does not need.
            double tMin = 0.0, tMax = 1.0;
            ParameterRange(tMin, tMax);
            const double h = std::max((tMax - tMin) * 1e-6, 1e-9);
            const double a = std::max(tMin, t - h);
            const double b = std::min(tMax, t + h);
            if (b <= a) return Vec3d(0.0, 0.0, 0.0);
            return (Evaluate(b) - Evaluate(a)) * (1.0 / (b - a));
        }
    }
}

bool BrepCurve::IsClosed() const {
    if (Type == BrepCurveType::Circle || Type == BrepCurveType::Ellipse) {
        double tMin = 0.0, tMax = 0.0;
        ParameterRange(tMin, tMax);
        return std::fabs((tMax - tMin) - kTwoPi) < 1e-9;
    }
    if (Periodic) return true;
    double tMin = 0.0, tMax = 0.0;
    ParameterRange(tMin, tMax);
    if (tMax <= tMin) return false;
    return (Evaluate(tMax) - Evaluate(tMin)).Length() < 1e-9;
}

// ===== 2D CURVE =====

void BrepCurve2D::ParameterRange(double& tMin, double& tMax) const {
    if (HasRange()) { tMin = TMin; tMax = TMax; return; }
    switch (Type) {
        case BrepCurve2DType::Circle:
        case BrepCurve2DType::Ellipse:
            tMin = 0.0; tMax = kTwoPi; return;
        case BrepCurve2DType::Polyline:
            tMin = 0.0;
            tMax = ControlPoints.size() > 1 ? static_cast<double>(ControlPoints.size() - 1) : 0.0;
            return;
        case BrepCurve2DType::BSpline:
            if (KnotsUsable(ControlPoints.size(), Degree, Knots)) {
                tMin = Knots[static_cast<size_t>(Degree)];
                tMax = Knots[ControlPoints.size()];
                return;
            }
            tMin = 0.0;
            tMax = ControlPoints.size() > 1 ? static_cast<double>(ControlPoints.size() - 1) : 1.0;
            return;
        case BrepCurve2DType::Line:
        default:
            tMin = 0.0; tMax = 1.0; return;
    }
}

Vec2d BrepCurve2D::Evaluate(double t) const {
    switch (Type) {
        case BrepCurve2DType::Line:
            return Origin + Direction * t;
        case BrepCurve2DType::Circle: {
            const Vec2d ref = RefDirection.Normalized();
            const Vec2d perp(-ref.y, ref.x);
            return Origin + ref * (Radius * std::cos(t)) + perp * (Radius * std::sin(t));
        }
        case BrepCurve2DType::Ellipse: {
            const Vec2d ref = RefDirection.Normalized();
            const Vec2d perp(-ref.y, ref.x);
            return Origin + ref * (Radius * std::cos(t)) + perp * (MinorRadius * std::sin(t));
        }
        case BrepCurve2DType::Polyline:
            return EvaluatePolyline(ControlPoints, t);
        case BrepCurve2DType::BSpline:
            return EvaluateBSpline(ControlPoints, Weights, Knots, Degree, t);
    }
    return Origin;
}

// ===== SURFACE =====

void BrepSurface::ParameterRange(double& uMin, double& uMax, double& vMin, double& vMax) const {
    // A stored range always wins: it is what the file said the patch covers.
    const bool storedU = HasURange();
    const bool storedV = HasVRange();
    uMin = 0.0; uMax = 1.0; vMin = 0.0; vMax = 1.0;

    switch (Type) {
        case BrepSurfaceType::Plane:
            uMin = -1.0; uMax = 1.0; vMin = -1.0; vMax = 1.0;
            break;
        case BrepSurfaceType::Cylinder:
        case BrepSurfaceType::Cone:
            uMin = 0.0; uMax = kTwoPi; vMin = -1.0; vMax = 1.0;
            break;
        case BrepSurfaceType::Sphere:
            uMin = 0.0; uMax = kTwoPi; vMin = -kPi * 0.5; vMax = kPi * 0.5;
            break;
        case BrepSurfaceType::Torus:
            uMin = 0.0; uMax = kTwoPi; vMin = 0.0; vMax = kTwoPi;
            break;
        case BrepSurfaceType::Revolution:
            uMin = 0.0; uMax = kTwoPi; vMin = 0.0; vMax = 1.0;
            break;
        case BrepSurfaceType::Extrusion:
        case BrepSurfaceType::Ruled:
            uMin = 0.0; uMax = 1.0; vMin = 0.0; vMax = 1.0;
            break;
        case BrepSurfaceType::BSpline:
            if (KnotsUsable(static_cast<size_t>(ControlPointsU), DegreeU, KnotsU)) {
                uMin = KnotsU[static_cast<size_t>(DegreeU)];
                uMax = KnotsU[static_cast<size_t>(ControlPointsU)];
            }
            if (KnotsUsable(static_cast<size_t>(ControlPointsV), DegreeV, KnotsV)) {
                vMin = KnotsV[static_cast<size_t>(DegreeV)];
                vMax = KnotsV[static_cast<size_t>(ControlPointsV)];
            }
            break;
    }
    if (storedU) { uMin = UMin; uMax = UMax; }
    if (storedV) { vMin = VMin; vMax = VMax; }
}

Vec3d BrepSurface::Evaluate(double u, double v, const std::vector<BrepCurve>& curves) const {
    const Vec3d axisY = Placement.AxisY();
    auto curveAt = [&curves](int index, double t, Vec3d& out) -> bool {
        if (index < 0 || static_cast<size_t>(index) >= curves.size()) return false;
        out = curves[static_cast<size_t>(index)].Evaluate(t);
        return true;
    };

    switch (Type) {
        case BrepSurfaceType::Plane:
            return Placement.Origin + Placement.AxisX * u + axisY * v;

        case BrepSurfaceType::Cylinder:
            return Placement.Origin +
                   Placement.AxisX * (Radius * std::cos(u)) +
                   axisY * (Radius * std::sin(u)) +
                   Placement.AxisZ * v;

        case BrepSurfaceType::Cone: {
            const double r = Radius + v * std::tan(HalfAngleRadians);
            return Placement.Origin +
                   Placement.AxisX * (r * std::cos(u)) +
                   axisY * (r * std::sin(u)) +
                   Placement.AxisZ * v;
        }

        case BrepSurfaceType::Sphere: {
            const double ring = Radius * std::cos(v);
            return Placement.Origin +
                   Placement.AxisX * (ring * std::cos(u)) +
                   axisY * (ring * std::sin(u)) +
                   Placement.AxisZ * (Radius * std::sin(v));
        }

        case BrepSurfaceType::Torus: {
            const double ring = Radius + MinorRadius * std::cos(v);
            return Placement.Origin +
                   Placement.AxisX * (ring * std::cos(u)) +
                   axisY * (ring * std::sin(u)) +
                   Placement.AxisZ * (MinorRadius * std::sin(v));
        }

        case BrepSurfaceType::Extrusion: {
            Vec3d base;
            if (!curveAt(ProfileCurve, u, base)) return Placement.Origin;
            return base + ExtrusionDirection * v;
        }

        case BrepSurfaceType::Revolution: {
            Vec3d profile;
            if (!curveAt(ProfileCurve, v, profile)) return Placement.Origin;
            // Rotate the profile point about the placement's axis by u.
            const Vec3d axis = Placement.AxisZ.Normalized();
            const Vec3d d = profile - Placement.Origin;
            const double along = d.Dot(axis);
            const Vec3d radial = d - axis * along;
            const Vec3d perp = axis.Cross(radial);
            return Placement.Origin + axis * along +
                   radial * std::cos(u) + perp * std::sin(u);
        }

        case BrepSurfaceType::Ruled: {
            Vec3d a, b;
            if (!curveAt(ProfileCurve, u, a)) return Placement.Origin;
            if (!curveAt(SecondCurve, u, b)) return a;
            return a + (b - a) * v;
        }

        case BrepSurfaceType::BSpline: {
            if (ControlPointsU <= 0 || ControlPointsV <= 0 ||
                ControlNet.size() != static_cast<size_t>(ControlPointsU) *
                                     static_cast<size_t>(ControlPointsV))
                return Placement.Origin;

            // Tensor product: evaluate each row of the net in u, then evaluate
            // the resulting curve in v. The weights ride along so a rational
            // surface stays rational through both stages.
            std::vector<Vec3d> column(static_cast<size_t>(ControlPointsV));
            std::vector<double> columnWeights;
            const bool rational =
                    NetWeights.size() == ControlNet.size();
            if (rational) columnWeights.resize(static_cast<size_t>(ControlPointsV), 1.0);

            for (int row = 0; row < ControlPointsV; ++row) {
                std::vector<Vec3d> rowPoints(static_cast<size_t>(ControlPointsU));
                std::vector<double> rowWeights;
                if (rational) rowWeights.resize(static_cast<size_t>(ControlPointsU), 1.0);
                for (int col = 0; col < ControlPointsU; ++col) {
                    const size_t idx = static_cast<size_t>(row) *
                                       static_cast<size_t>(ControlPointsU) +
                                       static_cast<size_t>(col);
                    rowPoints[static_cast<size_t>(col)] = ControlNet[idx];
                    if (rational) rowWeights[static_cast<size_t>(col)] = NetWeights[idx];
                }
                column[static_cast<size_t>(row)] =
                        EvaluateBSpline(rowPoints, rowWeights, KnotsU, DegreeU, u);
                if (rational) {
                    // The weight field is itself a B-spline scalar; evaluating
                    // it the same way keeps the second stage consistent.
                    std::vector<Vec3d> asPoints(rowWeights.size());
                    for (size_t k = 0; k < rowWeights.size(); ++k)
                        asPoints[k] = Vec3d(rowWeights[k], 0.0, 0.0);
                    columnWeights[static_cast<size_t>(row)] =
                            EvaluateBSpline(asPoints, {}, KnotsU, DegreeU, u).x;
                }
            }
            return EvaluateBSpline(column, columnWeights, KnotsV, DegreeV, v);
        }
    }
    return Placement.Origin;
}

Vec3d BrepSurface::Normal(double u, double v, const std::vector<BrepCurve>& curves) const {
    // Analytic normals where they are one line; a difference quotient where
    // writing the partials out would only restate the evaluator.
    const Vec3d axisY = Placement.AxisY();
    switch (Type) {
        case BrepSurfaceType::Plane:
            return Placement.AxisZ.Normalized();
        case BrepSurfaceType::Cylinder:
            return (Placement.AxisX * std::cos(u) + axisY * std::sin(u)).Normalized();
        case BrepSurfaceType::Sphere: {
            const Vec3d outward = Placement.AxisX * (std::cos(v) * std::cos(u)) +
                                  axisY * (std::cos(v) * std::sin(u)) +
                                  Placement.AxisZ * std::sin(v);
            return outward.Normalized();
        }
        case BrepSurfaceType::Torus: {
            const Vec3d outward = Placement.AxisX * (std::cos(v) * std::cos(u)) +
                                  axisY * (std::cos(v) * std::sin(u)) +
                                  Placement.AxisZ * std::sin(v);
            return outward.Normalized();
        }
        default: break;
    }

    double uMin = 0.0, uMax = 1.0, vMin = 0.0, vMax = 1.0;
    ParameterRange(uMin, uMax, vMin, vMax);
    const double hu = std::max((uMax - uMin) * 1e-6, 1e-9);
    const double hv = std::max((vMax - vMin) * 1e-6, 1e-9);

    const double ua = std::max(uMin, u - hu), ub = std::min(uMax, u + hu);
    const double va = std::max(vMin, v - hv), vb = std::min(vMax, v + hv);
    if (ub <= ua || vb <= va) return Vec3d(0.0, 0.0, 0.0);

    const Vec3d dU = (Evaluate(ub, v, curves) - Evaluate(ua, v, curves)) * (1.0 / (ub - ua));
    const Vec3d dV = (Evaluate(u, vb, curves) - Evaluate(u, va, curves)) * (1.0 / (vb - va));
    return dU.Cross(dV).Normalized();
}

bool BrepSurface::IsPeriodicInU() const {
    switch (Type) {
        case BrepSurfaceType::Cylinder:
        case BrepSurfaceType::Cone:
        case BrepSurfaceType::Sphere:
        case BrepSurfaceType::Torus:
        case BrepSurfaceType::Revolution:
            return true;
        case BrepSurfaceType::BSpline:
            return PeriodicU;
        default:
            return false;
    }
}

bool BrepSurface::IsPeriodicInV() const {
    if (Type == BrepSurfaceType::Torus) return true;
    if (Type == BrepSurfaceType::BSpline) return PeriodicV;
    return false;
}

bool BrepSurface::Project(const Vec3d& point, const std::vector<BrepCurve>& curves,
                          double& u, double& v) const {
    double uMin = 0.0, uMax = 1.0, vMin = 0.0, vMax = 1.0;
    ParameterRange(uMin, uMax, vMin, vMax);

    // The analytic types invert in closed form, which is exact and is what a
    // STEP file without pcurves needs most of the time.
    const Vec3d local = Placement.ToLocal(point);
    switch (Type) {
        case BrepSurfaceType::Plane:
            u = local.x; v = local.y;
            return true;
        case BrepSurfaceType::Cylinder:
            u = NormalizeAngleInto(std::atan2(local.y, local.x), uMin);
            v = local.z;
            return true;
        case BrepSurfaceType::Cone:
            u = NormalizeAngleInto(std::atan2(local.y, local.x), uMin);
            v = local.z;
            return true;
        case BrepSurfaceType::Sphere: {
            u = NormalizeAngleInto(std::atan2(local.y, local.x), uMin);
            const double r = local.Length();
            v = r > 1e-15 ? std::asin(std::max(-1.0, std::min(1.0, local.z / r))) : 0.0;
            return true;
        }
        case BrepSurfaceType::Torus: {
            u = NormalizeAngleInto(std::atan2(local.y, local.x), uMin);
            const double ring = std::sqrt(local.x * local.x + local.y * local.y) - Radius;
            v = NormalizeAngleInto(std::atan2(local.z, ring), vMin);
            return true;
        }
        default: break;
    }

    // Everything else: seed from a coarse grid, then descend. A grid seed is
    // what makes this reliable — a Newton step from an arbitrary start on a
    // wavy NURBS patch finds the wrong local minimum, and a wrong (u, v) puts
    // a trimming loop through the middle of the face.
    constexpr int kSeeds = 12;
    double bestU = uMin, bestV = vMin;
    double bestDistance = std::numeric_limits<double>::max();
    for (int i = 0; i <= kSeeds; ++i) {
        const double su = uMin + (uMax - uMin) * (static_cast<double>(i) / kSeeds);
        for (int j = 0; j <= kSeeds; ++j) {
            const double sv = vMin + (vMax - vMin) * (static_cast<double>(j) / kSeeds);
            const double d = (Evaluate(su, sv, curves) - point).Length();
            if (d < bestDistance) { bestDistance = d; bestU = su; bestV = sv; }
        }
    }

    // Damped Gauss-Newton on the squared distance, with the step clamped to
    // the domain so it cannot walk off a trimmed patch.
    double stepU = (uMax - uMin) / kSeeds;
    double stepV = (vMax - vMin) / kSeeds;
    for (int iteration = 0; iteration < 64; ++iteration) {
        bool improved = false;
        const double offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& offset : offsets) {
            const double cu = std::max(uMin, std::min(uMax, bestU + offset[0] * stepU));
            const double cv = std::max(vMin, std::min(vMax, bestV + offset[1] * stepV));
            const double d = (Evaluate(cu, cv, curves) - point).Length();
            if (d < bestDistance) {
                bestDistance = d; bestU = cu; bestV = cv; improved = true;
            }
        }
        if (!improved) {
            stepU *= 0.5;
            stepV *= 0.5;
            if (stepU < (uMax - uMin) * 1e-10 && stepV < (vMax - vMin) * 1e-10) break;
        }
    }

    u = bestU;
    v = bestV;
    // A projection that lands far from the point is a wrong answer, not a
    // slow one — the caller needs to know so it can warn rather than mesh
    // nonsense.
    const double extent = std::max(1e-12, (Evaluate(uMax, vMax, curves) -
                                           Evaluate(uMin, vMin, curves)).Length());
    return bestDistance <= extent * 1e-3 + 1e-9;
}

// ===== POOL: BUILDING AND QUERIES =====

void BrepData::Clear() {
    Curves.clear(); Curves2D.clear(); Surfaces.clear(); Vertices.clear();
    Edges.clear(); Loops.clear(); Faces.clear(); Shells.clear(); Solids.clear();
}

int BrepData::AddCurve(BrepCurve curve) {
    Curves.push_back(std::move(curve));
    return static_cast<int>(Curves.size()) - 1;
}
int BrepData::AddCurve2D(BrepCurve2D curve) {
    Curves2D.push_back(std::move(curve));
    return static_cast<int>(Curves2D.size()) - 1;
}
int BrepData::AddSurface(BrepSurface surface) {
    Surfaces.push_back(std::move(surface));
    return static_cast<int>(Surfaces.size()) - 1;
}
int BrepData::AddVertex(const Vec3d& point) {
    BrepVertex vertex;
    vertex.Point = point;
    Vertices.push_back(vertex);
    return static_cast<int>(Vertices.size()) - 1;
}
int BrepData::AddEdge(BrepEdge edge) {
    Edges.push_back(std::move(edge));
    return static_cast<int>(Edges.size()) - 1;
}
int BrepData::AddLoop(BrepLoop loop) {
    Loops.push_back(std::move(loop));
    return static_cast<int>(Loops.size()) - 1;
}
int BrepData::AddFace(BrepFace face) {
    Faces.push_back(std::move(face));
    return static_cast<int>(Faces.size()) - 1;
}
int BrepData::AddShell(BrepShell shell) {
    Shells.push_back(std::move(shell));
    return static_cast<int>(Shells.size()) - 1;
}
int BrepData::AddSolid(BrepSolid solid) {
    Solids.push_back(std::move(solid));
    return static_cast<int>(Solids.size()) - 1;
}

namespace {

// Adaptive chord sampling, shared by the 3D and 2D curve walkers: split while
// the curve's midpoint is further from the chord's midpoint than the
// tolerance allows. Depth-limited, so a cusp cannot recurse for ever.
template <typename Eval, typename Point>
void SampleRecursive(const Eval& evaluate, double t0, double t1,
                     const Point& p0, const Point& p1,
                     double tolerance, int depth, std::vector<double>& out) {
    if (depth <= 0) return;
    const double tm = 0.5 * (t0 + t1);
    const Point pm = evaluate(tm);
    const Point chord = (p0 + p1) * 0.5;
    if ((pm - chord).Length() <= tolerance) return;
    SampleRecursive(evaluate, t0, tm, p0, pm, tolerance, depth - 1, out);
    out.push_back(tm);
    SampleRecursive(evaluate, tm, t1, pm, p1, tolerance, depth - 1, out);
}

// The parameters at which to sample a curve between t0 and t1, inclusive of
// both ends.
template <typename Eval, typename Point>
std::vector<double> SampleCurve(const Eval& evaluate, double t0, double t1,
                                double tolerance, int minSegments, int maxSegments) {
    std::vector<double> parameters;
    if (t1 == t0) return {t0};
    minSegments = std::max(1, minSegments);
    maxSegments = std::max(minSegments, maxSegments);

    // A uniform pre-split first: it costs nothing and it stops the midpoint
    // test from terminating early on a curve that happens to be symmetric
    // about its own chord — a full circle is exactly that.
    const int depth = 8;
    parameters.push_back(t0);
    for (int i = 0; i < minSegments; ++i) {
        const double a = t0 + (t1 - t0) * (static_cast<double>(i) / minSegments);
        const double b = t0 + (t1 - t0) * (static_cast<double>(i + 1) / minSegments);
        SampleRecursive<Eval, Point>(evaluate, a, b, evaluate(a), evaluate(b),
                                     tolerance, depth, parameters);
        parameters.push_back(b);
    }

    if (static_cast<int>(parameters.size()) - 1 > maxSegments) {
        std::vector<double> capped;
        capped.reserve(static_cast<size_t>(maxSegments) + 1);
        for (int i = 0; i <= maxSegments; ++i)
            capped.push_back(t0 + (t1 - t0) * (static_cast<double>(i) / maxSegments));
        return capped;
    }
    return parameters;
}

// How many segments an arc of `sweep` radians needs to stay inside both
// tolerances. The chord term is the standard sagitta inversion.
int SegmentsForArc(double sweep, double radius, double chordTolerance,
                   double angularTolerance, int minSegments, int maxSegments) {
    sweep = std::fabs(sweep);
    if (sweep <= 0.0) return minSegments;
    int byAngle = static_cast<int>(std::ceil(sweep / std::max(1e-6, angularTolerance)));
    int byChord = minSegments;
    if (radius > 0.0 && chordTolerance > 0.0 && chordTolerance < radius) {
        const double halfAngle = std::acos(1.0 - chordTolerance / radius);
        if (halfAngle > 1e-9)
            byChord = static_cast<int>(std::ceil(sweep / (2.0 * halfAngle)));
    }
    return std::max(minSegments, std::min(maxSegments, std::max(byAngle, byChord)));
}

} // namespace

double BrepData::EdgeLength(int edgeIndex) const {
    if (edgeIndex < 0 || static_cast<size_t>(edgeIndex) >= Edges.size()) return 0.0;
    const BrepEdge& edge = Edges[static_cast<size_t>(edgeIndex)];
    if (edge.Degenerate) return 0.0;
    if (edge.Curve < 0 || static_cast<size_t>(edge.Curve) >= Curves.size()) {
        if (edge.Start >= 0 && edge.End >= 0 &&
            static_cast<size_t>(edge.Start) < Vertices.size() &&
            static_cast<size_t>(edge.End) < Vertices.size())
            return (Vertices[static_cast<size_t>(edge.End)].Point -
                    Vertices[static_cast<size_t>(edge.Start)].Point).Length();
        return 0.0;
    }

    const BrepCurve& curve = Curves[static_cast<size_t>(edge.Curve)];
    double t0 = edge.TStart, t1 = edge.TEnd;
    if (t0 == t1) curve.ParameterRange(t0, t1);

    // 32 chords is enough for a length used only to pick a segment count.
    double length = 0.0;
    Vec3d previous = curve.Evaluate(t0);
    for (int i = 1; i <= 32; ++i) {
        const Vec3d current = curve.Evaluate(t0 + (t1 - t0) * (static_cast<double>(i) / 32.0));
        length += (current - previous).Length();
        previous = current;
    }
    return length;
}

Bounds3D BrepData::ComputeBounds() const {
    Bounds3D bounds;
    for (const BrepVertex& vertex : Vertices) bounds.Expand(vertex.Point);
    // Vertices alone under-report a body whose faces bulge — a sphere made of
    // two caps has four vertices on its equator and none at its poles — so
    // every edge is sampled too.
    for (size_t e = 0; e < Edges.size(); ++e) {
        const BrepEdge& edge = Edges[e];
        if (edge.Curve < 0 || static_cast<size_t>(edge.Curve) >= Curves.size()) continue;
        const BrepCurve& curve = Curves[static_cast<size_t>(edge.Curve)];
        double t0 = edge.TStart, t1 = edge.TEnd;
        if (t0 == t1) curve.ParameterRange(t0, t1);
        for (int i = 0; i <= 16; ++i)
            bounds.Expand(curve.Evaluate(t0 + (t1 - t0) * (static_cast<double>(i) / 16.0)));
    }
    return bounds;
}

Bounds3D BrepData::SolidBounds(int solidIndex) const {
    Bounds3D bounds;
    if (solidIndex < 0 || static_cast<size_t>(solidIndex) >= Solids.size()) return bounds;
    for (int shellIndex : Solids[static_cast<size_t>(solidIndex)].Shells) {
        if (shellIndex < 0 || static_cast<size_t>(shellIndex) >= Shells.size()) continue;
        for (int faceIndex : Shells[static_cast<size_t>(shellIndex)].Faces) {
            if (faceIndex < 0 || static_cast<size_t>(faceIndex) >= Faces.size()) continue;
            for (int loopIndex : Faces[static_cast<size_t>(faceIndex)].Loops) {
                if (loopIndex < 0 || static_cast<size_t>(loopIndex) >= Loops.size()) continue;
                for (const BrepCoedge& coedge : Loops[static_cast<size_t>(loopIndex)].Coedges) {
                    if (coedge.Edge < 0 || static_cast<size_t>(coedge.Edge) >= Edges.size())
                        continue;
                    const BrepEdge& edge = Edges[static_cast<size_t>(coedge.Edge)];
                    if (edge.Curve >= 0 && static_cast<size_t>(edge.Curve) < Curves.size()) {
                        const BrepCurve& curve = Curves[static_cast<size_t>(edge.Curve)];
                        double t0 = edge.TStart, t1 = edge.TEnd;
                        if (t0 == t1) curve.ParameterRange(t0, t1);
                        for (int i = 0; i <= 16; ++i)
                            bounds.Expand(curve.Evaluate(
                                    t0 + (t1 - t0) * (static_cast<double>(i) / 16.0)));
                    }
                    for (int vertexIndex : {edge.Start, edge.End})
                        if (vertexIndex >= 0 && static_cast<size_t>(vertexIndex) < Vertices.size())
                            bounds.Expand(Vertices[static_cast<size_t>(vertexIndex)].Point);
                }
            }
        }
    }
    return bounds;
}

size_t BrepData::FaceCountOfSolid(int solidIndex) const {
    if (solidIndex < 0 || static_cast<size_t>(solidIndex) >= Solids.size()) return 0;
    size_t count = 0;
    for (int shellIndex : Solids[static_cast<size_t>(solidIndex)].Shells)
        if (shellIndex >= 0 && static_cast<size_t>(shellIndex) < Shells.size())
            count += Shells[static_cast<size_t>(shellIndex)].Faces.size();
    return count;
}

double BrepData::ResolveChordTolerance(int solidIndex,
                                       const BrepTessellationOptions& options) const {
    if (options.ChordTolerance > 0.0) return options.ChordTolerance;
    Bounds3D bounds = solidIndex >= 0 ? SolidBounds(solidIndex) : ComputeBounds();
    if (!bounds.IsValid()) return 1e-3;
    // A thousandth of the body's radius: the deviation a viewer cannot see at
    // any framing that shows the whole part, and the default every CAD
    // tessellator lands near.
    return std::max(1e-9, bounds.Radius() * 1e-3);
}

// ===== VALIDATION =====

bool BrepData::Validate(std::vector<std::string>& problems) const {
    const size_t before = problems.size();
    auto report = [&problems](const std::string& message) { problems.push_back(message); };
    auto inRange = [](int index, size_t size) {
        return index >= 0 && static_cast<size_t>(index) < size;
    };

    for (size_t e = 0; e < Edges.size(); ++e) {
        const BrepEdge& edge = Edges[e];
        const std::string tag = "edge " + std::to_string(e);
        if (!edge.Degenerate && edge.Curve >= 0 && !inRange(edge.Curve, Curves.size()))
            report(tag + " references curve " + std::to_string(edge.Curve) +
                   ", which does not exist");
        if (edge.Start >= 0 && !inRange(edge.Start, Vertices.size()))
            report(tag + " references start vertex " + std::to_string(edge.Start) +
                   ", which does not exist");
        if (edge.End >= 0 && !inRange(edge.End, Vertices.size()))
            report(tag + " references end vertex " + std::to_string(edge.End) +
                   ", which does not exist");
    }

    for (size_t l = 0; l < Loops.size(); ++l) {
        const BrepLoop& loop = Loops[l];
        const std::string tag = "loop " + std::to_string(l);
        if (loop.Coedges.empty()) { report(tag + " is empty"); continue; }

        for (const BrepCoedge& coedge : loop.Coedges) {
            if (!inRange(coedge.Edge, Edges.size())) {
                report(tag + " references edge " + std::to_string(coedge.Edge) +
                       ", which does not exist");
                continue;
            }
            if (coedge.ParameterCurve >= 0 && !inRange(coedge.ParameterCurve, Curves2D.size()))
                report(tag + " references parameter curve " +
                       std::to_string(coedge.ParameterCurve) + ", which does not exist");
        }

        // Closure: the vertex a coedge ends at must be the one the next starts
        // from. A loop that does not close cannot bound anything, and is the
        // single most common corruption in a hand-edited STEP file.
        bool resolvable = true;
        for (const BrepCoedge& coedge : loop.Coedges)
            if (!inRange(coedge.Edge, Edges.size())) resolvable = false;
        if (!resolvable) continue;

        for (size_t c = 0; c < loop.Coedges.size(); ++c) {
            const BrepCoedge& current = loop.Coedges[c];
            const BrepCoedge& next = loop.Coedges[(c + 1) % loop.Coedges.size()];
            const BrepEdge& currentEdge = Edges[static_cast<size_t>(current.Edge)];
            const BrepEdge& nextEdge = Edges[static_cast<size_t>(next.Edge)];
            const int endsAt = current.Forward ? currentEdge.End : currentEdge.Start;
            const int startsAt = next.Forward ? nextEdge.Start : nextEdge.End;
            if (endsAt < 0 || startsAt < 0) continue;   // unvertexed: nothing to check
            if (endsAt != startsAt) {
                report(tag + " does not close: coedge " + std::to_string(c) +
                       " ends at vertex " + std::to_string(endsAt) + " but the next starts at " +
                       std::to_string(startsAt));
                break;
            }
        }
    }

    for (size_t f = 0; f < Faces.size(); ++f) {
        const BrepFace& face = Faces[f];
        const std::string tag = "face " + std::to_string(f);
        if (!inRange(face.Surface, Surfaces.size()))
            report(tag + " references surface " + std::to_string(face.Surface) +
                   ", which does not exist");
        if (face.Loops.empty())
            report(tag + " has no loops, so nothing bounds it");
        for (int loopIndex : face.Loops)
            if (!inRange(loopIndex, Loops.size()))
                report(tag + " references loop " + std::to_string(loopIndex) +
                       ", which does not exist");
        int outerCount = 0;
        for (int loopIndex : face.Loops)
            if (inRange(loopIndex, Loops.size()) &&
                Loops[static_cast<size_t>(loopIndex)].Kind == BrepLoopKind::Outer)
                ++outerCount;
        if (outerCount > 1)
            report(tag + " has " + std::to_string(outerCount) +
                   " loops marked outer; a face has at most one");
    }

    for (size_t s = 0; s < Shells.size(); ++s) {
        for (int faceIndex : Shells[s].Faces)
            if (!inRange(faceIndex, Faces.size()))
                report("shell " + std::to_string(s) + " references face " +
                       std::to_string(faceIndex) + ", which does not exist");
    }

    for (size_t s = 0; s < Solids.size(); ++s) {
        const BrepSolid& solid = Solids[s];
        if (solid.Shells.empty())
            report("solid " + std::to_string(s) + " has no shells");
        for (int shellIndex : solid.Shells)
            if (!inRange(shellIndex, Shells.size()))
                report("solid " + std::to_string(s) + " references shell " +
                       std::to_string(shellIndex) + ", which does not exist");
    }

    // The manifold condition, checked per closed shell: every edge used by
    // exactly two of its faces, once in each direction. This is what
    // distinguishes a solid from a bag of patches, and a reader that gets an
    // orientation wrong produces a body that looks right and has an inverted
    // face — visible only as a black triangle, or as a wrong volume.
    for (size_t s = 0; s < Shells.size(); ++s) {
        const BrepShell& shell = Shells[s];
        if (!shell.Closed) continue;

        std::map<int, std::pair<int, int>> usage;   // edge -> (forward, reverse)
        bool resolvable = true;
        for (int faceIndex : shell.Faces) {
            if (!inRange(faceIndex, Faces.size())) { resolvable = false; break; }
            for (int loopIndex : Faces[static_cast<size_t>(faceIndex)].Loops) {
                if (!inRange(loopIndex, Loops.size())) { resolvable = false; break; }
                for (const BrepCoedge& coedge : Loops[static_cast<size_t>(loopIndex)].Coedges) {
                    if (!inRange(coedge.Edge, Edges.size())) { resolvable = false; break; }
                    if (Edges[static_cast<size_t>(coedge.Edge)].Degenerate) continue;
                    auto& counts = usage[coedge.Edge];
                    if (coedge.Forward) ++counts.first; else ++counts.second;
                }
            }
        }
        if (!resolvable) continue;

        for (const auto& entry : usage) {
            const int forward = entry.second.first;
            const int reverse = entry.second.second;
            if (forward + reverse == 2 && forward == 1 && reverse == 1) continue;
            report("shell " + std::to_string(s) + " is marked closed but edge " +
                   std::to_string(entry.first) + " is used " +
                   std::to_string(forward + reverse) + " time(s) (" +
                   std::to_string(forward) + " forward, " + std::to_string(reverse) +
                   " reverse); a closed shell uses each edge once in each direction");
        }
    }

    return problems.size() == before;
}

// ===== TESSELLATION =====

namespace {

// A loop walked into parameter space, ready to be triangulated.
struct LoopSamples {
    std::vector<Vec2d> Points;
    bool Complete = true;   // false when a coedge could not be resolved
};

uint64_t EdgeKey(uint32_t a, uint32_t b) {
    const uint32_t lo = std::min(a, b), hi = std::max(a, b);
    return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

// Periodic surfaces are the reason a loop cannot simply be read off. A
// cylindrical face that spans the seam has u values that jump from just under
// 2pi to just over 0; in parameter space that is a polygon folded back on
// itself, and it triangulates into garbage. Unwrapping walks the ring and
// keeps each step on the short way round.
void UnwrapPeriodic(std::vector<Vec2d>& points, bool periodicU, bool periodicV) {
    if (points.size() < 2) return;
    for (size_t i = 1; i < points.size(); ++i) {
        if (periodicU) {
            while (points[i].x - points[i - 1].x > kPi) points[i].x -= kTwoPi;
            while (points[i - 1].x - points[i].x > kPi) points[i].x += kTwoPi;
        }
        if (periodicV) {
            while (points[i].y - points[i - 1].y > kPi) points[i].y -= kTwoPi;
            while (points[i - 1].y - points[i].y > kPi) points[i].y += kTwoPi;
        }
    }
}

void AppendUnique(std::vector<Vec2d>& ring, const Vec2d& point, double epsilon) {
    if (!ring.empty() && (ring.back() - point).Length() <= epsilon) return;
    ring.push_back(point);
}

// Is d inside the circumcircle of the counter-clockwise triangle abc? The
// standard 3x3 determinant, which is the Delaunay condition.
bool InCircumcircle(const Vec2d& a, const Vec2d& b, const Vec2d& c, const Vec2d& d) {
    const double ax = a.x - d.x, ay = a.y - d.y;
    const double bx = b.x - d.x, by = b.y - d.y;
    const double cx = c.x - d.x, cy = c.y - d.y;
    const double determinant =
            (ax * ax + ay * ay) * (bx * cy - by * cx) -
            (bx * bx + by * by) * (ax * cy - ay * cx) +
            (cx * cx + cy * cy) * (ax * by - ay * bx);
    return determinant > 0.0;
}

double SignedArea(const Vec2d& a, const Vec2d& b, const Vec2d& c) {
    return (b - a).Cross(c - a);
}

// Ear clipping is correct but not shapely: it tends to produce long slivers,
// and a sliver is expensive twice over — it approximates the surface badly, so
// the refinement below splits it, and each split inherits the bad shape. On a
// cylinder that difference is two orders of magnitude in triangle count.
//
// Flipping every non-Delaunay interior diagonal fixes the shape without moving
// a single vertex, so the boundary the trimming curves define is untouched.
void ImproveByFlipping(const std::vector<Vec2d>& uv, std::vector<uint32_t>& triangles,
                       const std::set<uint64_t>& boundary) {
    if (triangles.size() < 6) return;
    constexpr int kMaxPasses = 64;

    for (int pass = 0; pass < kMaxPasses; ++pass) {
        // edge -> the (triangle, corner-opposite-the-edge) pairs using it.
        std::map<uint64_t, std::vector<std::pair<size_t, int>>> adjacency;
        for (size_t t = 0; t + 2 < triangles.size(); t += 3)
            for (int e = 0; e < 3; ++e)
                adjacency[EdgeKey(triangles[t + static_cast<size_t>(e)],
                                  triangles[t + static_cast<size_t>((e + 1) % 3)])]
                        .emplace_back(t, (e + 2) % 3);

        int flips = 0;
        std::vector<bool> touched(triangles.size() / 3, false);

        for (const auto& entry : adjacency) {
            if (entry.second.size() != 2) continue;          // border or non-manifold
            if (boundary.count(entry.first)) continue;        // a trimming edge stays

            const size_t t0 = entry.second[0].first, t1 = entry.second[1].first;
            if (touched[t0 / 3] || touched[t1 / 3]) continue;

            const uint32_t c = triangles[t0 + static_cast<size_t>(entry.second[0].second)];
            const uint32_t d = triangles[t1 + static_cast<size_t>(entry.second[1].second)];
            // The shared edge, oriented as the first triangle sees it.
            const int e0 = (entry.second[0].second + 1) % 3;
            const uint32_t a = triangles[t0 + static_cast<size_t>(e0)];
            const uint32_t b = triangles[t0 + static_cast<size_t>((e0 + 1) % 3)];
            if (c == d || a == b) continue;

            // Only a convex quad may be re-cut; flipping a concave one folds
            // the mesh over itself.
            if (SignedArea(uv[c], uv[d], uv[a]) * SignedArea(uv[c], uv[d], uv[b]) >= 0.0)
                continue;
            if (!InCircumcircle(uv[a], uv[b], uv[c], uv[d])) continue;

            // (a, b, c) and (b, a, d) become (c, a, d) and (c, d, b).
            triangles[t0] = c; triangles[t0 + 1] = a; triangles[t0 + 2] = d;
            triangles[t1] = c; triangles[t1 + 1] = d; triangles[t1 + 2] = b;
            touched[t0 / 3] = true;
            touched[t1 / 3] = true;
            ++flips;
        }
        if (flips == 0) break;
    }
}

// How many divisions a parameter direction needs before the chord between
// consecutive samples is inside the tolerance. Doubling from the minimum finds
// it in a handful of evaluations and lands on a power of two, which is what the
// boundary sampler produces too, so the grid and the boundary agree.
template <typename Evaluate>
int DivisionsFor(const Evaluate& evaluate, double from, double to, int otherSamples,
                 double tolerance, int minDivisions, int maxDivisions) {
    if (to <= from) return minDivisions;
    int divisions = std::max(1, minDivisions);
    while (divisions < maxDivisions) {
        double worst = 0.0;
        for (int j = 0; j <= otherSamples; ++j) {
            const double other = static_cast<double>(j) / otherSamples;
            for (int i = 0; i < divisions; ++i) {
                const double a = from + (to - from) * (static_cast<double>(i) / divisions);
                const double b = from + (to - from) * (static_cast<double>(i + 1) / divisions);
                const Vec3d pa = evaluate(a, other);
                const Vec3d pb = evaluate(b, other);
                const Vec3d pm = evaluate(0.5 * (a + b), other);
                worst = std::max(worst, (pm - (pa + pb) * 0.5).Length());
            }
        }
        if (worst <= tolerance) break;
        divisions *= 2;
    }
    return std::min(divisions, maxDivisions);
}

// Add a point inside the triangulation, splitting whichever triangle holds it
// into three. Points that fall outside the trimmed region — the grid does not
// know about the trimming — simply find no triangle and are dropped, which is
// exactly the test that was wanted.
bool InsertInteriorPoint(std::vector<Vec2d>& uv, std::vector<uint32_t>& triangles,
                         const Vec2d& point) {
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const Vec2d& a = uv[triangles[t]];
        const Vec2d& b = uv[triangles[t + 1]];
        const Vec2d& c = uv[triangles[t + 2]];
        const double area = SignedArea(a, b, c);
        if (area <= 0.0) continue;
        const double wa = SignedArea(b, c, point) / area;
        const double wb = SignedArea(c, a, point) / area;
        const double wc = SignedArea(a, b, point) / area;
        // A margin off every edge: a point dropped onto one produces two
        // zero-area triangles, and near one produces slivers the flip pass
        // then has to undo.
        constexpr double kMargin = 0.02;
        if (wa < kMargin || wb < kMargin || wc < kMargin) continue;

        const uint32_t v0 = triangles[t], v1 = triangles[t + 1], v2 = triangles[t + 2];
        const uint32_t fresh = static_cast<uint32_t>(uv.size());
        uv.push_back(point);
        triangles[t] = v0; triangles[t + 1] = v1; triangles[t + 2] = fresh;
        triangles.push_back(v1); triangles.push_back(v2); triangles.push_back(fresh);
        triangles.push_back(v2); triangles.push_back(v0); triangles.push_back(fresh);
        return true;
    }
    return false;
}

} // namespace

bool BrepData::TessellateFace(int faceIndex, const BrepTessellationOptions& options,
                              std::vector<Vec3d>& outPositions,
                              std::vector<Vec3f>& outNormals,
                              std::vector<float>& outUVs,
                              std::vector<uint32_t>& outIndices,
                              std::vector<std::string>* problems) const {
    auto complain = [problems](const std::string& message) {
        if (problems) problems->push_back(message);
    };

    if (faceIndex < 0 || static_cast<size_t>(faceIndex) >= Faces.size()) {
        complain("face " + std::to_string(faceIndex) + " does not exist");
        return false;
    }
    const BrepFace& face = Faces[static_cast<size_t>(faceIndex)];
    if (face.Surface < 0 || static_cast<size_t>(face.Surface) >= Surfaces.size()) {
        complain("face " + std::to_string(faceIndex) + " has no resolvable surface");
        return false;
    }
    const BrepSurface& surface = Surfaces[static_cast<size_t>(face.Surface)];

    const double tolerance = options.ChordTolerance > 0.0
                             ? options.ChordTolerance
                             : ResolveChordTolerance(-1, options);

    double uMin = 0.0, uMax = 1.0, vMin = 0.0, vMax = 1.0;
    surface.ParameterRange(uMin, uMax, vMin, vMax);

    // ----- 1. every loop, walked into (u, v) -----

    std::vector<LoopSamples> rings;
    rings.reserve(face.Loops.size());

    for (int loopIndex : face.Loops) {
        if (loopIndex < 0 || static_cast<size_t>(loopIndex) >= Loops.size()) {
            complain("face " + std::to_string(faceIndex) + " references loop " +
                     std::to_string(loopIndex) + ", which does not exist");
            continue;
        }
        const BrepLoop& loop = Loops[static_cast<size_t>(loopIndex)];
        LoopSamples ring;

        for (const BrepCoedge& coedge : loop.Coedges) {
            std::vector<Vec2d> segment;

            if (coedge.ParameterCurve >= 0 &&
                static_cast<size_t>(coedge.ParameterCurve) < Curves2D.size()) {
                // The file carried a pcurve: the exact answer, and the only one
                // available on a NURBS patch whose 3D edge does not invert.
                const BrepCurve2D& pcurve = Curves2D[static_cast<size_t>(coedge.ParameterCurve)];
                double t0 = 0.0, t1 = 0.0;
                pcurve.ParameterRange(t0, t1);
                // Measure the chord error on the surface, not in parameter
                // space: a tolerance in (u, v) means nothing when u is an angle.
                auto onSurface = [&](double t) {
                    const Vec2d p = pcurve.Evaluate(t);
                    return surface.Evaluate(p.x, p.y, Curves);
                };
                const std::vector<double> parameters =
                        SampleCurve<decltype(onSurface), Vec3d>(
                                onSurface, t0, t1, tolerance,
                                options.MinSegmentsPerCurve, options.MaxSegmentsPerCurve);
                segment.reserve(parameters.size());
                for (double t : parameters) segment.push_back(pcurve.Evaluate(t));

            } else if (coedge.Edge >= 0 && static_cast<size_t>(coedge.Edge) < Edges.size()) {
                const BrepEdge& edge = Edges[static_cast<size_t>(coedge.Edge)];
                if (edge.Degenerate) continue;   // a pole or seam contributes no span

                if (edge.Curve >= 0 && static_cast<size_t>(edge.Curve) < Curves.size()) {
                    const BrepCurve& curve = Curves[static_cast<size_t>(edge.Curve)];
                    double t0 = edge.TStart, t1 = edge.TEnd;
                    if (t0 == t1) curve.ParameterRange(t0, t1);
                    auto evaluate = [&curve](double t) { return curve.Evaluate(t); };
                    const std::vector<double> parameters =
                            SampleCurve<decltype(evaluate), Vec3d>(
                                    evaluate, t0, t1, tolerance,
                                    options.MinSegmentsPerCurve, options.MaxSegmentsPerCurve);
                    segment.reserve(parameters.size());
                    for (double t : parameters) {
                        double u = 0.0, v = 0.0;
                        if (!surface.Project(curve.Evaluate(t), Curves, u, v)) ring.Complete = false;
                        segment.emplace_back(u, v);
                    }
                } else if (edge.Start >= 0 && edge.End >= 0 &&
                           static_cast<size_t>(edge.Start) < Vertices.size() &&
                           static_cast<size_t>(edge.End) < Vertices.size()) {
                    // No curve: a straight span between the two vertices.
                    double u0 = 0.0, v0 = 0.0, u1 = 0.0, v1 = 0.0;
                    surface.Project(Vertices[static_cast<size_t>(edge.Start)].Point,
                                    Curves, u0, v0);
                    surface.Project(Vertices[static_cast<size_t>(edge.End)].Point,
                                    Curves, u1, v1);
                    segment.emplace_back(u0, v0);
                    segment.emplace_back(u1, v1);
                } else {
                    ring.Complete = false;
                    continue;
                }
            } else {
                ring.Complete = false;
                continue;
            }

            if (!coedge.Forward) std::reverse(segment.begin(), segment.end());
            // Each coedge shares its first point with the previous coedge's
            // last, so the join is dropped rather than duplicated.
            for (const Vec2d& point : segment) AppendUnique(ring.Points, point, 1e-12);
        }

        if (ring.Points.size() > 1 &&
            (ring.Points.front() - ring.Points.back()).Length() <= 1e-12)
            ring.Points.pop_back();

        UnwrapPeriodic(ring.Points, surface.IsPeriodicInU(), surface.IsPeriodicInV());

        // Drop samples that earn nothing. The sampler is deliberately
        // conservative — it pre-splits every curve so that a closed one cannot
        // terminate on its own symmetry — and each coedge is sampled without
        // knowing its neighbours, so a straight edge arrives with a midpoint
        // that is exactly on the chord. Asking of each point "would removing
        // it break the tolerance?" is the exact inverse of the sampling test,
        // so nothing that matters is lost: a box face goes from six triangles
        // to two, and a circle keeps every sample it has.
        if (ring.Points.size() > 3) {
            std::vector<Vec2d> kept;
            kept.reserve(ring.Points.size());
            const size_t count = ring.Points.size();
            for (size_t i = 0; i < count; ++i) {
                const Vec2d& previous = kept.empty() ? ring.Points[(i + count - 1) % count]
                                                     : kept.back();
                const Vec2d& next = ring.Points[(i + 1) % count];
                const Vec3d a = surface.Evaluate(previous.x, previous.y, Curves);
                const Vec3d b = surface.Evaluate(next.x, next.y, Curves);
                const Vec2d middle = (previous + next) * 0.5;
                const Vec3d onSurface = surface.Evaluate(middle.x, middle.y, Curves);
                const Vec3d here = surface.Evaluate(ring.Points[i].x, ring.Points[i].y, Curves);
                // Both the chord and the point it would replace have to stay
                // inside the tolerance, or the boundary moves.
                const Vec3d chord = b - a;
                const double length = chord.Length();
                double offEdge = (here - a).Length();
                if (length > 0.0) {
                    const double t = std::max(0.0, std::min(1.0, (here - a).Dot(chord) /
                                                                 (length * length)));
                    offEdge = (here - (a + chord * t)).Length();
                }
                if (kept.size() + (count - i - 1) >= 3 &&
                    (onSurface - (a + b) * 0.5).Length() <= tolerance &&
                    offEdge <= tolerance)
                    continue;
                kept.push_back(ring.Points[i]);
            }
            if (kept.size() >= 3) ring.Points.swap(kept);
        }
        if (ring.Points.size() >= 3) rings.push_back(std::move(ring));
    }

    // ----- 2. an untrimmed patch: mesh its whole parameter rectangle -----

    if (rings.empty()) {
        // A surface model can carry faces with no bounds at all (IGES 128
        // without a 142, a DWG SURFACE). The natural range is the boundary.
        LoopSamples ring;
        const int segmentsU = SegmentsForArc(
                uMax - uMin, surface.IsPeriodicInU() ? std::max(surface.Radius, 1.0) : 0.0,
                tolerance, options.AngularTolerance,
                options.MinSegmentsPerCurve, options.MaxSegmentsPerCurve);
        const int segmentsV = SegmentsForArc(
                vMax - vMin, surface.IsPeriodicInV() ? std::max(surface.MinorRadius, 1.0) : 0.0,
                tolerance, options.AngularTolerance,
                options.MinSegmentsPerCurve, options.MaxSegmentsPerCurve);
        for (int i = 0; i < segmentsU; ++i)
            ring.Points.emplace_back(uMin + (uMax - uMin) * i / segmentsU, vMin);
        for (int j = 0; j < segmentsV; ++j)
            ring.Points.emplace_back(uMax, vMin + (vMax - vMin) * j / segmentsV);
        for (int i = segmentsU; i > 0; --i)
            ring.Points.emplace_back(uMin + (uMax - uMin) * i / segmentsU, vMax);
        for (int j = segmentsV; j > 0; --j)
            ring.Points.emplace_back(uMin, vMin + (vMax - vMin) * j / segmentsV);
        rings.push_back(std::move(ring));
    }

    // ----- 3. which ring bounds the face, and which are holes -----

    size_t outerIndex = 0;
    bool markedOuter = false;
    for (size_t i = 0; i < face.Loops.size() && i < rings.size(); ++i) {
        const int loopIndex = face.Loops[i];
        if (loopIndex < 0 || static_cast<size_t>(loopIndex) >= Loops.size()) continue;
        if (Loops[static_cast<size_t>(loopIndex)].Kind == BrepLoopKind::Outer) {
            outerIndex = i;
            markedOuter = true;
            break;
        }
    }
    if (!markedOuter) {
        // The file did not say. The bounding loop is the one with the largest
        // area — true for any well-formed face, since a hole is inside it.
        double largest = -1.0;
        for (size_t i = 0; i < rings.size(); ++i) {
            const double area = std::fabs(PolygonArea2D(rings[i].Points));
            if (area > largest) { largest = area; outerIndex = i; }
        }
    }

    std::vector<std::vector<Vec2d>> holes;
    for (size_t i = 0; i < rings.size(); ++i)
        if (i != outerIndex) holes.push_back(rings[i].Points);

    // ----- 4. triangulate in parameter space -----

    std::vector<Vec2d> uv;
    std::vector<uint32_t> sources;
    std::vector<uint32_t> triangles;
    const bool clean = TriangulatePolygonWithHoles2D(rings[outerIndex].Points, holes,
                                                     uv, sources, triangles);
    if (triangles.empty()) {
        complain("face " + std::to_string(faceIndex) +
                 " produced no triangles: its trimming loops do not bound an area");
        return false;
    }
    if (!clean)
        complain("face " + std::to_string(faceIndex) +
                 " triangulated only partially; its trimming loops are self-intersecting "
                 "or do not close");

    // The boundary of the region must survive refinement unchanged — its
    // vertices are on the trimming curves, and a midpoint of a boundary chord
    // is not.
    std::set<uint64_t> boundary;
    for (size_t i = 0; i < uv.size(); ++i)
        boundary.insert(EdgeKey(static_cast<uint32_t>(i),
                                static_cast<uint32_t>((i + 1) % uv.size())));

    // How finely each parameter direction has to be divided for the chords
    // between samples to stay inside the tolerance. Everything below is
    // measured in those cells.
    double regionUMin = uv[0].x, regionUMax = uv[0].x;
    double regionVMin = uv[0].y, regionVMax = uv[0].y;
    for (const Vec2d& p : uv) {
        regionUMin = std::min(regionUMin, p.x); regionUMax = std::max(regionUMax, p.x);
        regionVMin = std::min(regionVMin, p.y); regionVMax = std::max(regionVMax, p.y);
    }
    // Sized over the region the trimming loops actually cover, not over the
    // surface's whole domain — a 20 mm hole in a 2 m plate is a sliver of the
    // parameter rectangle, and sizing to the rectangle would mesh it with one
    // triangle.
    int divisionsU = options.MinSegmentsPerCurve, divisionsV = options.MinSegmentsPerCurve;
    if (surface.Type != BrepSurfaceType::Plane) {
        auto alongU = [&](double a, double other) {
            return surface.Evaluate(a, regionVMin + (regionVMax - regionVMin) * other, Curves);
        };
        auto alongV = [&](double a, double other) {
            return surface.Evaluate(regionUMin + (regionUMax - regionUMin) * other, a, Curves);
        };
        divisionsU = DivisionsFor(alongU, regionUMin, regionUMax, 2, tolerance,
                                  options.MinSegmentsPerCurve, options.MaxSegmentsPerCurve);
        divisionsV = DivisionsFor(alongV, regionVMin, regionVMax, 2, tolerance,
                                  options.MinSegmentsPerCurve, options.MaxSegmentsPerCurve);
    }

    // The flip test has to be made in a space where distance means something,
    // and that space is the tessellation grid — one unit per cell in each
    // direction — not parameter space and not arc length.
    //
    // Parameter space is meaningless: on a cylinder one unit of u is R units
    // of arc and one unit of v is one. Arc length is worse, and was the first
    // thing tried: it makes the Delaunay condition prefer equilateral
    // triangles, so on a cylinder — curved in u, flat in v — it deliberately
    // builds triangles spanning twenty degrees of arc to match a ten-unit
    // height, which is precisely the shape with the worst chord error. In grid
    // cells both directions are already divided to the tolerance, so a
    // well-shaped triangle there is a well-approximating one: a strip on a
    // ruled surface, a grid on a sphere.
    const double cellU = (regionUMax - regionUMin) / std::max(1, divisionsU);
    const double cellV = (regionVMax - regionVMin) / std::max(1, divisionsV);
    const double scaleU = cellU > 0.0 ? 1.0 / cellU : 1.0;
    const double scaleV = cellV > 0.0 ? 1.0 / cellV : 1.0;
    auto metricSpace = [&](const std::vector<Vec2d>& source) {
        std::vector<Vec2d> scaled(source.size());
        for (size_t i = 0; i < source.size(); ++i)
            scaled[i] = Vec2d(source[i].x * scaleU, source[i].y * scaleV);
        return scaled;
    };
    ImproveByFlipping(metricSpace(uv), triangles, boundary);

    // ----- 5. seed the interior -----
    //
    // Ear clipping puts no vertex inside the region, so the whole interior of
    // a curved face is spanned by triangles reaching from one boundary to the
    // other. Refining those from nothing costs several passes and compounds:
    // each pass splits edges that are still too long, and the triangles it
    // makes inherit the bad shape. Seeding the grid gets the interior right in
    // one step, and the refinement below then only has to catch what a uniform
    // grid misses.
    //
    // Cell centres, not cell corners: the boundary was sampled on the same
    // doubling schedule, so corner points land on the lines through the
    // boundary vertices — on a triangle edge, where a point cannot go — and
    // almost none of them get in.
    if (surface.Type != BrepSurfaceType::Plane) {
        std::vector<Vec2d> pending;
        pending.reserve(static_cast<size_t>(divisionsU) * static_cast<size_t>(divisionsV));
        for (int i = 0; i < divisionsU; ++i) {
            const double u = regionUMin + cellU * (static_cast<double>(i) + 0.5);
            for (int j = 0; j < divisionsV; ++j)
                pending.emplace_back(u, regionVMin + cellV * (static_cast<double>(j) + 0.5));
        }

        // Insert in rounds, flipping between them. Each insertion leaves the
        // triangles around it badly shaped, and a later point landing in one of
        // those gets refused for want of clearance from an edge — so a single
        // pass places only a fraction of the grid. Re-shaping between rounds
        // lets the rest in; two or three rounds place essentially all of it.
        for (int round = 0; round < 4 && !pending.empty(); ++round) {
            std::vector<Vec2d> stillPending;
            size_t inserted = 0;
            for (const Vec2d& point : pending) {
                if (InsertInteriorPoint(uv, triangles, point)) ++inserted;
                else stillPending.push_back(point);
            }
            if (inserted > 0) ImproveByFlipping(metricSpace(uv), triangles, boundary);
            if (inserted == 0) break;            // the rest are outside the trimmed region
            pending.swap(stillPending);
        }
    }

    std::vector<Vec3d> positions;
    positions.reserve(uv.size());
    for (const Vec2d& p : uv) positions.push_back(surface.Evaluate(p.x, p.y, Curves));

    // ----- 6. refine whatever the grid still missed -----
    //
    // A plane needs none of this and never enters. Everything else is split
    // where — and only where — the mesh still departs from the surface by more
    // than the tolerance, with both triangles sharing a split edge agreeing to
    // split it, so no T-junction is ever created. On a uniformly curved face
    // the grid above has already done the work and this exits on its first
    // pass; it earns its keep on a NURBS patch with a local feature.

    const bool curved = surface.Type != BrepSurfaceType::Plane;
    bool converged = !curved;
    if (curved) {
        constexpr int kMaxLevels = 6;
        for (int level = 0; level < kMaxLevels; ++level) {
            std::map<uint64_t, uint32_t> splits;
            for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
                for (int e = 0; e < 3; ++e) {
                    const uint32_t a = triangles[t + static_cast<size_t>(e)];
                    const uint32_t b = triangles[t + static_cast<size_t>((e + 1) % 3)];
                    const uint64_t key = EdgeKey(a, b);
                    if (boundary.count(key)) continue;
                    if (splits.count(key)) continue;
                    const Vec2d middle = (uv[a] + uv[b]) * 0.5;
                    const Vec3d onSurface = surface.Evaluate(middle.x, middle.y, Curves);
                    const Vec3d onChord = (positions[a] + positions[b]) * 0.5;
                    if ((onSurface - onChord).Length() <= tolerance) continue;
                    splits.emplace(key, static_cast<uint32_t>(uv.size()));
                    uv.push_back(middle);
                    positions.push_back(onSurface);
                }
            }
            if (splits.empty()) { converged = true; break; }

            std::vector<uint32_t> refined;
            refined.reserve(triangles.size() * 2);
            for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
                const uint32_t v0 = triangles[t], v1 = triangles[t + 1], v2 = triangles[t + 2];
                auto midpoint = [&splits](uint32_t a, uint32_t b) -> int {
                    auto it = splits.find(EdgeKey(a, b));
                    return it == splits.end() ? -1 : static_cast<int>(it->second);
                };
                const int m01 = midpoint(v0, v1);
                const int m12 = midpoint(v1, v2);
                const int m20 = midpoint(v2, v0);
                const int marked = (m01 >= 0) + (m12 >= 0) + (m20 >= 0);

                auto emit = [&refined](uint32_t a, uint32_t b, uint32_t c) {
                    refined.push_back(a); refined.push_back(b); refined.push_back(c);
                };

                if (marked == 0) {
                    emit(v0, v1, v2);
                } else if (marked == 3) {
                    const uint32_t a = static_cast<uint32_t>(m01);
                    const uint32_t b = static_cast<uint32_t>(m12);
                    const uint32_t c = static_cast<uint32_t>(m20);
                    emit(v0, a, c); emit(a, v1, b); emit(c, b, v2); emit(a, b, c);
                } else if (marked == 1) {
                    if (m01 >= 0) {
                        emit(v0, static_cast<uint32_t>(m01), v2);
                        emit(static_cast<uint32_t>(m01), v1, v2);
                    } else if (m12 >= 0) {
                        emit(v1, static_cast<uint32_t>(m12), v0);
                        emit(static_cast<uint32_t>(m12), v2, v0);
                    } else {
                        emit(v2, static_cast<uint32_t>(m20), v1);
                        emit(static_cast<uint32_t>(m20), v0, v1);
                    }
                } else {
                    // Two split edges: cut off the corner they share, then
                    // split the remaining quad along its shorter diagonal.
                    uint32_t apex = v0, left = v1, right = v2;
                    int first = m01, second = m20;
                    if (m01 >= 0 && m12 >= 0) { apex = v1; left = v2; right = v0;
                                                first = m12; second = m01; }
                    else if (m12 >= 0 && m20 >= 0) { apex = v2; left = v0; right = v1;
                                                     first = m20; second = m12; }
                    const uint32_t a = static_cast<uint32_t>(first);   // apex -> left
                    const uint32_t b = static_cast<uint32_t>(second);  // right -> apex
                    emit(apex, a, b);
                    if ((positions[a] - positions[right]).Length() <
                        (positions[b] - positions[left]).Length()) {
                        emit(a, left, right); emit(a, right, b);
                    } else {
                        emit(a, left, b); emit(b, left, right);
                    }
                }
            }
            triangles.swap(refined);
        }
    }

    if (!converged)
        complain("face " + std::to_string(faceIndex) + " did not reach the chord tolerance: "
                 "refinement stopped at its depth limit, so parts of it are coarser than asked");

    // ----- 7. hand back positions, normals, uvs and winding -----

    const uint32_t base = static_cast<uint32_t>(outPositions.size());
    outPositions.insert(outPositions.end(), positions.begin(), positions.end());

    if (options.GenerateNormals) {
        outNormals.reserve(outNormals.size() + uv.size());
        for (size_t i = 0; i < uv.size(); ++i) {
            Vec3d n = surface.Normal(uv[i].x, uv[i].y, Curves);
            if (n.Length() < 0.5) {
                // A pole: the surface normal is undefined there, so it is taken
                // from a neighbouring parameter instead of left at zero.
                const double du = (uMax - uMin) * 1e-4;
                const double dv = (vMax - vMin) * 1e-4;
                n = surface.Normal(std::min(uMax, uv[i].x + du),
                                   std::max(vMin, std::min(vMax, uv[i].y - dv)), Curves);
            }
            if (!face.SameSense) n = n * -1.0;
            outNormals.emplace_back(static_cast<float>(n.x), static_cast<float>(n.y),
                                    static_cast<float>(n.z));
        }
    }

    if (options.GenerateUVs) {
        outUVs.reserve(outUVs.size() + uv.size() * 2);
        for (const Vec2d& p : uv) {
            outUVs.push_back(static_cast<float>(p.x));
            outUVs.push_back(static_cast<float>(p.y));
        }
    }

    outIndices.reserve(outIndices.size() + triangles.size());
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        // The parameter-space triangulation is counter-clockwise, which is
        // outward when the face agrees with its surface and inward when it
        // does not.
        if (face.SameSense) {
            outIndices.push_back(base + triangles[t]);
            outIndices.push_back(base + triangles[t + 1]);
            outIndices.push_back(base + triangles[t + 2]);
        } else {
            outIndices.push_back(base + triangles[t + 2]);
            outIndices.push_back(base + triangles[t + 1]);
            outIndices.push_back(base + triangles[t]);
        }
    }
    return true;
}

} // namespace ModelStorage
} // namespace UltraCanvas
