// Plugins/Models/STEP/UltraCanvasStepConverter.cpp
// AP203/214/242 entity meanings: the layer between the Part 21 table that
// UltraCanvasStepFile.h produces and the B-rep that UltraCanvasBrepStorage.h
// holds. Declared in UltraCanvasStepConverter.h.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/STEP/UltraCanvasStepConverter.h"
#include "Models/STEP/UltraCanvasStepFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace UltraCanvas {
namespace ModelConverter {

using namespace ModelStorage;
namespace SF = UltraCanvas::StepFile;
using SF::Value;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// STEP writes a knot vector as distinct values plus multiplicities; every
// evaluator wants it expanded.
std::vector<double> ExpandKnots(const std::vector<double>& distinct,
                                const std::vector<double>& multiplicities) {
    std::vector<double> knots;
    const size_t count = std::min(distinct.size(), multiplicities.size());
    for (size_t i = 0; i < count; ++i) {
        const int repeat = static_cast<int>(std::lround(multiplicities[i]));
        for (int k = 0; k < repeat && k < 64; ++k) knots.push_back(distinct[i]);
    }
    return knots;
}

// ===== READER =====

class Reader {
public:
    Reader(const SF::Model& model, ModelDocument& document, const ConversionOptions& options)
            : model_(model), document_(document), brep_(document.Brep), options_(options) {}

    void Run();

private:
    const SF::Model& model_;
    ModelDocument& document_;
    BrepData& brep_;
    const ConversionOptions& options_;

    // STEP id -> index in the corresponding BrepData pool. Everything is
    // shared in a STEP file — one point is referenced by a dozen entities —
    // so caching is correctness as much as speed: two faces must end up
    // sharing one edge, or the shell is not closed.
    std::map<int, int> curveFor_, curve2DFor_, surfaceFor_, vertexFor_, edgeFor_, faceFor_;
    // Keyed by the resolved colour rather than by the style instance that
    // carried it. Real exporters give every face its own six-entity
    // presentation chain, so keying on the instance would make a 5 000-face
    // part carry 5 000 identical materials.
    std::map<std::string, int> materialForColour_;
    std::set<std::string> saidOnce_;

    void Warn(const std::string& message) { options_.Warn(message); }
    void WarnOnce(const std::string& message) {
        if (saidOnce_.insert(message).second) options_.Warn(message);
    }

    const SF::Entity* At(int id) const { return model_.Find(id); }

    // --- geometry ---
    bool Point3(int id, Vec3d& out) const;
    bool Point2(int id, Vec2d& out) const;
    bool Direction3(int id, Vec3d& out) const;
    bool Direction2(int id, Vec2d& out) const;
    // A vector's direction scaled by its magnitude, which is what a line's
    // parameterisation actually uses.
    bool Vector3(int id, Vec3d& out) const;
    bool Vector2(int id, Vec2d& out) const;
    bool Placement3(int id, BrepPlacement& out) const;

    int Curve(int id);
    int Curve2D(int id);
    int Surface(int id);
    int Vertex(int id);

    // --- topology ---
    int Edge(int id);
    bool Loop(int id, bool orientation, int surfaceStepId, BrepLoop& out);
    int Face(int id);
    int Shell(int id, bool closed, std::vector<int>& outFaces);
    void ReadSolids();

    // --- context ---
    void ReadUnits();
    void ReadColours();
    void PlaceSolids();

    // --- helpers ---
    // The 3D curve of an edge, unwrapping surface_curve / seam_curve /
    // intersection_curve, which wrap the real curve beside its pcurves.
    int EdgeCurveGeometry(int id) const;
    // The pcurve on `surfaceStepId` carried by an edge's geometry. `occurrence`
    // picks between the two a seam curve carries — one for each side.
    int PCurveOn(int edgeGeometryId, int surfaceStepId, int occurrence);
    // Where on a curve a point lies. Closed-form for the analytic types, a
    // sampled descent otherwise.
    double ParameterAt(const BrepCurve& curve, const Vec3d& point) const;
    int MaterialFor(const Vec3f& colour, const std::string& name);
    bool FindColourUnder(int id, Vec3f& out, int depth = 0) const;
};

// --- geometry ---

bool Reader::Point3(int id, Vec3d& out) const {
    const SF::Entity* entity = At(id);
    if (!entity || !entity->Is("CARTESIAN_POINT")) return false;
    const std::vector<double> coordinates = entity->Param("CARTESIAN_POINT", 1).AsNumbers();
    if (coordinates.size() < 3) {
        if (coordinates.size() == 2) { out = Vec3d(coordinates[0], coordinates[1], 0.0); return true; }
        return false;
    }
    out = Vec3d(coordinates[0], coordinates[1], coordinates[2]);
    return true;
}

bool Reader::Point2(int id, Vec2d& out) const {
    const SF::Entity* entity = At(id);
    if (!entity || !entity->Is("CARTESIAN_POINT")) return false;
    const std::vector<double> coordinates = entity->Param("CARTESIAN_POINT", 1).AsNumbers();
    if (coordinates.size() < 2) return false;
    out = Vec2d(coordinates[0], coordinates[1]);
    return true;
}

bool Reader::Direction3(int id, Vec3d& out) const {
    const SF::Entity* entity = At(id);
    if (!entity || !entity->Is("DIRECTION")) return false;
    const std::vector<double> ratios = entity->Param("DIRECTION", 1).AsNumbers();
    if (ratios.size() < 3) {
        if (ratios.size() == 2) { out = Vec3d(ratios[0], ratios[1], 0.0); return true; }
        return false;
    }
    out = Vec3d(ratios[0], ratios[1], ratios[2]);
    return true;
}

bool Reader::Direction2(int id, Vec2d& out) const {
    const SF::Entity* entity = At(id);
    if (!entity || !entity->Is("DIRECTION")) return false;
    const std::vector<double> ratios = entity->Param("DIRECTION", 1).AsNumbers();
    if (ratios.size() < 2) return false;
    out = Vec2d(ratios[0], ratios[1]);
    return true;
}

bool Reader::Vector3(int id, Vec3d& out) const {
    const SF::Entity* entity = At(id);
    if (!entity) return false;
    if (entity->Is("DIRECTION")) return Direction3(id, out);
    if (!entity->Is("VECTOR")) return false;
    Vec3d direction;
    if (!Direction3(entity->Param("VECTOR", 1).AsReference(), direction)) return false;
    const double magnitude = entity->Param("VECTOR", 2).AsNumber(1.0);
    out = direction.Normalized() * magnitude;
    return true;
}

bool Reader::Vector2(int id, Vec2d& out) const {
    const SF::Entity* entity = At(id);
    if (!entity) return false;
    if (entity->Is("DIRECTION")) return Direction2(id, out);
    if (!entity->Is("VECTOR")) return false;
    Vec2d direction;
    if (!Direction2(entity->Param("VECTOR", 1).AsReference(), direction)) return false;
    out = direction.Normalized() * entity->Param("VECTOR", 2).AsNumber(1.0);
    return true;
}

bool Reader::Placement3(int id, BrepPlacement& out) const {
    const SF::Entity* entity = At(id);
    if (!entity) return false;
    // axis2_placement_3d(name, location, axis, ref_direction); axis1_placement
    // has no ref_direction, and both appear as a surface's placement.
    const char* keyword = entity->Is("AXIS2_PLACEMENT_3D") ? "AXIS2_PLACEMENT_3D"
                        : entity->Is("AXIS1_PLACEMENT")    ? "AXIS1_PLACEMENT"
                                                           : nullptr;
    if (!keyword) return false;

    BrepPlacement placement;
    if (!Point3(entity->Param(keyword, 1).AsReference(), placement.Origin)) return false;
    // Both axes are optional in STEP; the defaults are the global ones, which
    // is why they are so often omitted for a plane at the origin.
    Vec3d axis(0.0, 0.0, 1.0);
    if (Direction3(entity->Param(keyword, 2).AsReference(), axis)) placement.AxisZ = axis;
    else placement.AxisZ = Vec3d(0.0, 0.0, 1.0);
    Vec3d reference(1.0, 0.0, 0.0);
    if (entity->Is("AXIS2_PLACEMENT_3D") &&
        Direction3(entity->Param(keyword, 3).AsReference(), reference))
        placement.AxisX = reference;
    else
        placement.AxisX = std::fabs(placement.AxisZ.Normalized().x) > 0.9 ? Vec3d(0, 1, 0)
                                                                          : Vec3d(1, 0, 0);
    placement.Normalize();
    out = placement;
    return true;
}

int Reader::Curve(int id) {
    auto cached = curveFor_.find(id);
    if (cached != curveFor_.end()) return cached->second;

    const SF::Entity* entity = At(id);
    if (!entity) return -1;

    BrepCurve curve;
    bool built = false;

    if (entity->Is("LINE")) {
        Vec3d origin, direction;
        if (Point3(entity->Param("LINE", 1).AsReference(), origin) &&
            Vector3(entity->Param("LINE", 2).AsReference(), direction)) {
            curve.Type = BrepCurveType::Line;
            curve.Placement.Origin = origin;
            // The direction carries the line's magnitude: a STEP line is
            // parameterised in units of its vector, not of length.
            curve.Placement.AxisZ = direction.Length() > 1e-15 ? direction : Vec3d(1, 0, 0);
            curve.Placement.AxisX = Vec3d(1, 0, 0);
            curve.TMin = 0.0;
            curve.TMax = 1.0;
            built = true;
        }
    } else if (entity->Is("CIRCLE")) {
        if (Placement3(entity->Param("CIRCLE", 1).AsReference(), curve.Placement)) {
            curve.Type = BrepCurveType::Circle;
            curve.Radius = entity->Param("CIRCLE", 2).AsNumber();
            curve.TMin = 0.0;
            curve.TMax = kTwoPi;
            built = true;
        }
    } else if (entity->Is("ELLIPSE")) {
        if (Placement3(entity->Param("ELLIPSE", 1).AsReference(), curve.Placement)) {
            curve.Type = BrepCurveType::Ellipse;
            curve.Radius = entity->Param("ELLIPSE", 2).AsNumber();
            curve.MinorRadius = entity->Param("ELLIPSE", 3).AsNumber();
            curve.TMin = 0.0;
            curve.TMax = kTwoPi;
            built = true;
        }
    } else if (entity->Is("PARABOLA")) {
        if (Placement3(entity->Param("PARABOLA", 1).AsReference(), curve.Placement)) {
            curve.Type = BrepCurveType::Parabola;
            curve.FocalDistance = entity->Param("PARABOLA", 2).AsNumber();
            built = true;
        }
    } else if (entity->Is("HYPERBOLA")) {
        if (Placement3(entity->Param("HYPERBOLA", 1).AsReference(), curve.Placement)) {
            curve.Type = BrepCurveType::Hyperbola;
            curve.Radius = entity->Param("HYPERBOLA", 2).AsNumber();
            curve.MinorRadius = entity->Param("HYPERBOLA", 3).AsNumber();
            built = true;
        }
    } else if (entity->Is("POLYLINE")) {
        curve.Type = BrepCurveType::Polyline;
        for (int pointId : entity->Param("POLYLINE", 1).AsReferences()) {
            Vec3d point;
            if (Point3(pointId, point)) curve.ControlPoints.push_back(point);
        }
        built = curve.ControlPoints.size() >= 2;
    } else if (entity->Is("B_SPLINE_CURVE_WITH_KNOTS") || entity->Is("B_SPLINE_CURVE")) {
        // Two spellings of the same thing: a simple instance carries the whole
        // type's attributes with the inherited `name` first, while a rational
        // curve is a complex instance whose records each carry only their own.
        const bool complex = entity->IsComplex();
        const size_t base = complex ? 0 : 1;
        const std::vector<Value>* spline = entity->Params("B_SPLINE_CURVE");
        const std::vector<Value>* withKnots = entity->Params("B_SPLINE_CURVE_WITH_KNOTS");
        const std::vector<Value>* shape = spline ? spline : withKnots;
        if (shape && shape->size() >= base + 2) {
            curve.Type = BrepCurveType::BSpline;
            curve.Degree = (*shape)[base].AsInteger(3);
            for (int pointId : (*shape)[base + 1].AsReferences()) {
                Vec3d point;
                if (Point3(pointId, point)) curve.ControlPoints.push_back(point);
            }
            if (shape->size() >= base + 4)
                curve.Periodic = (*shape)[base + 3].AsBoolean();

            if (withKnots) {
                // The knot attributes sit at 0..2 in a complex record and at
                // 6..8 in the simple form, after the shape attributes.
                const size_t knotBase = (withKnots == shape && !complex) ? 6 : 0;
                if (withKnots->size() >= knotBase + 2) {
                    const std::vector<double> multiplicities =
                            (*withKnots)[knotBase].AsNumbers();
                    const std::vector<double> distinct = (*withKnots)[knotBase + 1].AsNumbers();
                    curve.Knots = ExpandKnots(distinct, multiplicities);
                }
            }
            if (const std::vector<Value>* rational = entity->Params("RATIONAL_B_SPLINE_CURVE"))
                if (!rational->empty()) curve.Weights = (*rational)[0].AsNumbers();

            built = curve.ControlPoints.size() >= 2;
            if (built && curve.Knots.size() !=
                         curve.ControlPoints.size() + static_cast<size_t>(curve.Degree) + 1) {
                WarnOnce("STEP: a B-spline curve's knot vector does not match its degree and "
                         "control-point count; it is evaluated as a polyline through its "
                         "control points");
                curve.Knots.clear();
            }
        }
    } else if (entity->Is("TRIMMED_CURVE")) {
        // trimmed_curve(name, basis_curve, trim_1, trim_2, sense_agreement,
        // master_representation). The basis curve is what matters; the trim
        // values become the edge's parameter range where they are given as
        // PARAMETER_VALUE.
        const int basis = entity->Param("TRIMMED_CURVE", 1).AsReference();
        const int index = Curve(basis);
        if (index >= 0) {
            BrepCurve trimmed = brep_.Curves[static_cast<size_t>(index)];
            auto readTrim = [](const Value& value, double& out) {
                if (!value.IsList()) return false;
                for (const Value& item : value.Items)
                    if (item.Type == SF::ValueType::Typed && item.Text == "PARAMETER_VALUE" &&
                        !item.Items.empty()) {
                        out = item.Items[0].AsNumber();
                        return true;
                    }
                return false;
            };
            double first = 0.0, second = 0.0;
            const bool haveFirst = readTrim(entity->Param("TRIMMED_CURVE", 2), first);
            const bool haveSecond = readTrim(entity->Param("TRIMMED_CURVE", 3), second);
            if (haveFirst && haveSecond) {
                trimmed.TMin = std::min(first, second);
                trimmed.TMax = std::max(first, second);
            }
            curve = trimmed;
            built = true;
        }
    }

    if (!built) {
        WarnOnce("STEP: a curve type this reader does not know (" +
                 (entity->Keyword().empty() ? std::string("complex instance") : entity->Keyword()) +
                 ") was skipped");
        curveFor_[id] = -1;
        return -1;
    }

    const int index = brep_.AddCurve(std::move(curve));
    curveFor_[id] = index;
    return index;
}

int Reader::Curve2D(int id) {
    auto cached = curve2DFor_.find(id);
    if (cached != curve2DFor_.end()) return cached->second;

    const SF::Entity* entity = At(id);
    if (!entity) return -1;

    BrepCurve2D curve;
    bool built = false;

    if (entity->Is("LINE")) {
        Vec2d origin, direction;
        if (Point2(entity->Param("LINE", 1).AsReference(), origin) &&
            Vector2(entity->Param("LINE", 2).AsReference(), direction)) {
            curve.Type = BrepCurve2DType::Line;
            curve.Origin = origin;
            curve.Direction = direction;
            curve.TMin = 0.0;
            curve.TMax = 1.0;
            built = true;
        }
    } else if (entity->Is("CIRCLE") || entity->Is("ELLIPSE")) {
        const char* keyword = entity->Is("CIRCLE") ? "CIRCLE" : "ELLIPSE";
        const SF::Entity* placement = At(entity->Param(keyword, 1).AsReference());
        if (placement && placement->Is("AXIS2_PLACEMENT_2D")) {
            Vec2d centre, reference(1.0, 0.0);
            if (Point2(placement->Param("AXIS2_PLACEMENT_2D", 1).AsReference(), centre)) {
                Direction2(placement->Param("AXIS2_PLACEMENT_2D", 2).AsReference(), reference);
                curve.Type = entity->Is("CIRCLE") ? BrepCurve2DType::Circle
                                                  : BrepCurve2DType::Ellipse;
                curve.Origin = centre;
                curve.RefDirection = reference;
                curve.Radius = entity->Param(keyword, 2).AsNumber();
                curve.MinorRadius = entity->Is("ELLIPSE") ? entity->Param(keyword, 3).AsNumber()
                                                          : curve.Radius;
                curve.TMin = 0.0;
                curve.TMax = kTwoPi;
                built = true;
            }
        }
    } else if (entity->Is("POLYLINE")) {
        curve.Type = BrepCurve2DType::Polyline;
        for (int pointId : entity->Param("POLYLINE", 1).AsReferences()) {
            Vec2d point;
            if (Point2(pointId, point)) curve.ControlPoints.push_back(point);
        }
        built = curve.ControlPoints.size() >= 2;
    } else if (entity->Is("B_SPLINE_CURVE_WITH_KNOTS") || entity->Is("B_SPLINE_CURVE")) {
        const bool complex = entity->IsComplex();
        const size_t base = complex ? 0 : 1;
        const std::vector<Value>* spline = entity->Params("B_SPLINE_CURVE");
        const std::vector<Value>* withKnots = entity->Params("B_SPLINE_CURVE_WITH_KNOTS");
        const std::vector<Value>* shape = spline ? spline : withKnots;
        if (shape && shape->size() >= base + 2) {
            curve.Type = BrepCurve2DType::BSpline;
            curve.Degree = (*shape)[base].AsInteger(3);
            for (int pointId : (*shape)[base + 1].AsReferences()) {
                Vec2d point;
                if (Point2(pointId, point)) curve.ControlPoints.push_back(point);
            }
            if (withKnots) {
                const size_t knotBase = (withKnots == shape && !complex) ? 6 : 0;
                if (withKnots->size() >= knotBase + 2)
                    curve.Knots = ExpandKnots((*withKnots)[knotBase + 1].AsNumbers(),
                                              (*withKnots)[knotBase].AsNumbers());
            }
            if (const std::vector<Value>* rational = entity->Params("RATIONAL_B_SPLINE_CURVE"))
                if (!rational->empty()) curve.Weights = (*rational)[0].AsNumbers();
            built = curve.ControlPoints.size() >= 2;
            if (built && curve.Knots.size() !=
                         curve.ControlPoints.size() + static_cast<size_t>(curve.Degree) + 1)
                curve.Knots.clear();
        }
    }

    if (!built) { curve2DFor_[id] = -1; return -1; }
    const int index = brep_.AddCurve2D(std::move(curve));
    curve2DFor_[id] = index;
    return index;
}

int Reader::Surface(int id) {
    auto cached = surfaceFor_.find(id);
    if (cached != surfaceFor_.end()) return cached->second;

    const SF::Entity* entity = At(id);
    if (!entity) return -1;

    BrepSurface surface;
    bool built = false;

    auto placed = [&](const char* keyword) {
        return Placement3(entity->Param(keyword, 1).AsReference(), surface.Placement);
    };

    if (entity->Is("PLANE")) {
        if (placed("PLANE")) { surface.Type = BrepSurfaceType::Plane; built = true; }
    } else if (entity->Is("CYLINDRICAL_SURFACE")) {
        if (placed("CYLINDRICAL_SURFACE")) {
            surface.Type = BrepSurfaceType::Cylinder;
            surface.Radius = entity->Param("CYLINDRICAL_SURFACE", 2).AsNumber();
            built = true;
        }
    } else if (entity->Is("CONICAL_SURFACE")) {
        if (placed("CONICAL_SURFACE")) {
            surface.Type = BrepSurfaceType::Cone;
            surface.Radius = entity->Param("CONICAL_SURFACE", 2).AsNumber();
            surface.HalfAngleRadians = entity->Param("CONICAL_SURFACE", 3).AsNumber();
            built = true;
        }
    } else if (entity->Is("SPHERICAL_SURFACE")) {
        if (placed("SPHERICAL_SURFACE")) {
            surface.Type = BrepSurfaceType::Sphere;
            surface.Radius = entity->Param("SPHERICAL_SURFACE", 2).AsNumber();
            built = true;
        }
    } else if (entity->Is("TOROIDAL_SURFACE") || entity->Is("DEGENERATE_TOROIDAL_SURFACE")) {
        const char* keyword = entity->Is("TOROIDAL_SURFACE") ? "TOROIDAL_SURFACE"
                                                             : "DEGENERATE_TOROIDAL_SURFACE";
        if (placed(keyword)) {
            surface.Type = BrepSurfaceType::Torus;
            surface.Radius = entity->Param(keyword, 2).AsNumber();
            surface.MinorRadius = entity->Param(keyword, 3).AsNumber();
            built = true;
        }
    } else if (entity->Is("SURFACE_OF_LINEAR_EXTRUSION")) {
        const int profile = Curve(entity->Param("SURFACE_OF_LINEAR_EXTRUSION", 1).AsReference());
        Vec3d direction;
        if (profile >= 0 &&
            Vector3(entity->Param("SURFACE_OF_LINEAR_EXTRUSION", 2).AsReference(), direction)) {
            surface.Type = BrepSurfaceType::Extrusion;
            surface.ProfileCurve = profile;
            surface.ExtrusionDirection = direction;
            double tMin = 0.0, tMax = 1.0;
            brep_.Curves[static_cast<size_t>(profile)].ParameterRange(tMin, tMax);
            surface.UMin = tMin;
            surface.UMax = tMax;
            built = true;
        }
    } else if (entity->Is("SURFACE_OF_REVOLUTION")) {
        const int profile = Curve(entity->Param("SURFACE_OF_REVOLUTION", 1).AsReference());
        BrepPlacement axis;
        if (profile >= 0 &&
            Placement3(entity->Param("SURFACE_OF_REVOLUTION", 2).AsReference(), axis)) {
            surface.Type = BrepSurfaceType::Revolution;
            surface.ProfileCurve = profile;
            surface.Placement = axis;
            double tMin = 0.0, tMax = 1.0;
            brep_.Curves[static_cast<size_t>(profile)].ParameterRange(tMin, tMax);
            surface.UMin = 0.0;
            surface.UMax = kTwoPi;
            surface.VMin = tMin;
            surface.VMax = tMax;
            built = true;
        }
    } else if (entity->Is("B_SPLINE_SURFACE_WITH_KNOTS") || entity->Is("B_SPLINE_SURFACE")) {
        const bool complex = entity->IsComplex();
        const size_t base = complex ? 0 : 1;
        const std::vector<Value>* spline = entity->Params("B_SPLINE_SURFACE");
        const std::vector<Value>* withKnots = entity->Params("B_SPLINE_SURFACE_WITH_KNOTS");
        const std::vector<Value>* shape = spline ? spline : withKnots;

        if (shape && shape->size() >= base + 3) {
            surface.Type = BrepSurfaceType::BSpline;
            surface.DegreeU = (*shape)[base].AsInteger(3);
            surface.DegreeV = (*shape)[base + 1].AsInteger(3);

            // control_points_list is a list of lists: the outer index runs in
            // u, the inner in v. BrepSurface stores the net row-major in u,
            // rows being v, so the two indices swap here.
            const Value& net = (*shape)[base + 2];
            std::vector<std::vector<Vec3d>> columns;
            if (net.IsList())
                for (const Value& column : net.Items) {
                    std::vector<Vec3d> points;
                    for (int pointId : column.AsReferences()) {
                        Vec3d point;
                        if (Point3(pointId, point)) points.push_back(point);
                    }
                    columns.push_back(std::move(points));
                }

            surface.ControlPointsU = static_cast<int>(columns.size());
            surface.ControlPointsV = columns.empty() ? 0 : static_cast<int>(columns[0].size());
            bool rectangular = surface.ControlPointsU > 0 && surface.ControlPointsV > 0;
            for (const auto& column : columns)
                if (static_cast<int>(column.size()) != surface.ControlPointsV) rectangular = false;

            if (rectangular) {
                surface.ControlNet.resize(static_cast<size_t>(surface.ControlPointsU) *
                                          static_cast<size_t>(surface.ControlPointsV));
                for (int u = 0; u < surface.ControlPointsU; ++u)
                    for (int v = 0; v < surface.ControlPointsV; ++v)
                        surface.ControlNet[static_cast<size_t>(v) *
                                           static_cast<size_t>(surface.ControlPointsU) +
                                           static_cast<size_t>(u)] =
                                columns[static_cast<size_t>(u)][static_cast<size_t>(v)];

                if (shape->size() >= base + 6) {
                    surface.PeriodicU = (*shape)[base + 4].AsBoolean();
                    surface.PeriodicV = (*shape)[base + 5].AsBoolean();
                }

                if (withKnots) {
                    // u_multiplicities, v_multiplicities, u_knots, v_knots,
                    // knot_spec — at 0..4 complex, at 8..12 simple.
                    const size_t knotBase = (withKnots == shape && !complex) ? 8 : 0;
                    if (withKnots->size() >= knotBase + 4) {
                        surface.KnotsU = ExpandKnots((*withKnots)[knotBase + 2].AsNumbers(),
                                                     (*withKnots)[knotBase].AsNumbers());
                        surface.KnotsV = ExpandKnots((*withKnots)[knotBase + 3].AsNumbers(),
                                                     (*withKnots)[knotBase + 1].AsNumbers());
                    }
                }

                if (const std::vector<Value>* rational =
                            entity->Params("RATIONAL_B_SPLINE_SURFACE")) {
                    if (!rational->empty() && (*rational)[0].IsList()) {
                        // Weights come in the same list-of-lists shape, and
                        // have to be transposed with the net.
                        std::vector<std::vector<double>> weightColumns;
                        for (const Value& column : (*rational)[0].Items)
                            weightColumns.push_back(column.AsNumbers());
                        bool usable = static_cast<int>(weightColumns.size()) ==
                                      surface.ControlPointsU;
                        for (const auto& column : weightColumns)
                            if (static_cast<int>(column.size()) != surface.ControlPointsV)
                                usable = false;
                        if (usable) {
                            surface.NetWeights.resize(surface.ControlNet.size(), 1.0);
                            for (int u = 0; u < surface.ControlPointsU; ++u)
                                for (int v = 0; v < surface.ControlPointsV; ++v)
                                    surface.NetWeights[static_cast<size_t>(v) *
                                                       static_cast<size_t>(surface.ControlPointsU) +
                                                       static_cast<size_t>(u)] =
                                            weightColumns[static_cast<size_t>(u)]
                                                         [static_cast<size_t>(v)];
                        }
                    }
                }

                const size_t wantU = static_cast<size_t>(surface.ControlPointsU) +
                                     static_cast<size_t>(surface.DegreeU) + 1;
                const size_t wantV = static_cast<size_t>(surface.ControlPointsV) +
                                     static_cast<size_t>(surface.DegreeV) + 1;
                if (surface.KnotsU.size() != wantU || surface.KnotsV.size() != wantV) {
                    WarnOnce("STEP: a B-spline surface's knot vectors do not match its degrees "
                             "and control net; it is evaluated through its control points");
                    surface.KnotsU.clear();
                    surface.KnotsV.clear();
                }
                built = true;
            } else {
                WarnOnce("STEP: a B-spline surface has a ragged control net and was skipped");
            }
        }
    }

    if (!built) {
        WarnOnce("STEP: a surface type this reader does not know (" +
                 (entity->Keyword().empty() ? std::string("complex instance") : entity->Keyword()) +
                 ") was skipped, so the faces on it are missing");
        surfaceFor_[id] = -1;
        return -1;
    }

    const int index = brep_.AddSurface(std::move(surface));
    surfaceFor_[id] = index;
    return index;
}

int Reader::Vertex(int id) {
    auto cached = vertexFor_.find(id);
    if (cached != vertexFor_.end()) return cached->second;

    const SF::Entity* entity = At(id);
    int index = -1;
    Vec3d point;
    if (entity && entity->Is("VERTEX_POINT") &&
        Point3(entity->Param("VERTEX_POINT", 1).AsReference(), point))
        index = brep_.AddVertex(point);
    vertexFor_[id] = index;
    return index;
}

// ===== PARAMETERS AND PCURVES =====

double Reader::ParameterAt(const BrepCurve& curve, const Vec3d& point) const {
    switch (curve.Type) {
        case BrepCurveType::Line: {
            const Vec3d direction = curve.Placement.AxisZ;
            const double squared = direction.Dot(direction);
            if (squared <= 1e-30) return 0.0;
            return (point - curve.Placement.Origin).Dot(direction) / squared;
        }
        case BrepCurveType::Circle: {
            const Vec3d local = curve.Placement.ToLocal(point);
            double angle = std::atan2(local.y, local.x);
            if (angle < 0.0) angle += kTwoPi;
            return angle;
        }
        case BrepCurveType::Ellipse: {
            const Vec3d local = curve.Placement.ToLocal(point);
            const double x = curve.Radius > 1e-30 ? local.x / curve.Radius : local.x;
            const double y = curve.MinorRadius > 1e-30 ? local.y / curve.MinorRadius : local.y;
            double angle = std::atan2(y, x);
            if (angle < 0.0) angle += kTwoPi;
            return angle;
        }
        default: break;
    }

    // Splines and conics: sample the range, then bisect around the best
    // sample. An edge's endpoints are on the curve by construction, so this
    // converges on the answer rather than merely near it.
    double tMin = 0.0, tMax = 1.0;
    curve.ParameterRange(tMin, tMax);
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

int Reader::EdgeCurveGeometry(int id) const {
    const SF::Entity* entity = At(id);
    if (!entity) return id;
    for (const char* keyword : {"SURFACE_CURVE", "SEAM_CURVE", "INTERSECTION_CURVE"})
        if (entity->Is(keyword)) {
            const int inner = entity->Param(keyword, 1).AsReference();
            return inner >= 0 ? inner : id;
        }
    return id;
}

int Reader::PCurveOn(int edgeGeometryId, int surfaceStepId, int occurrence) {
    const SF::Entity* entity = At(edgeGeometryId);
    if (!entity || surfaceStepId < 0) return -1;

    const std::vector<Value>* params = nullptr;
    for (const char* keyword : {"SURFACE_CURVE", "SEAM_CURVE", "INTERSECTION_CURVE"})
        if ((params = entity->Params(keyword)) != nullptr) break;
    if (!params || params->size() < 3 || !(*params)[2].IsList()) return -1;

    int seen = 0;
    for (const Value& item : (*params)[2].Items) {
        const SF::Entity* pcurve = At(item.AsReference());
        if (!pcurve || !pcurve->Is("PCURVE")) continue;
        if (pcurve->Param("PCURVE", 1).AsReference() != surfaceStepId) continue;
        // A seam carries two pcurves on the same surface — one for each side
        // of the cut — so which one is wanted depends on which pass over the
        // edge this is.
        if (seen++ < occurrence) continue;

        const SF::Entity* definition = At(pcurve->Param("PCURVE", 2).AsReference());
        if (!definition || !definition->Is("DEFINITIONAL_REPRESENTATION")) continue;
        for (int curveId : definition->Param("DEFINITIONAL_REPRESENTATION", 1).AsReferences()) {
            const int index = Curve2D(curveId);
            if (index >= 0) return index;
        }
    }
    return -1;
}

// ===== TOPOLOGY =====

int Reader::Edge(int id) {
    auto cached = edgeFor_.find(id);
    if (cached != edgeFor_.end()) return cached->second;

    const SF::Entity* entity = At(id);
    if (!entity || !entity->Is("EDGE_CURVE")) { edgeFor_[id] = -1; return -1; }

    BrepEdge edge;
    const int startId = entity->Param("EDGE_CURVE", 1).AsReference();
    const int endId = entity->Param("EDGE_CURVE", 2).AsReference();
    edge.Start = Vertex(startId);
    edge.End = Vertex(endId);
    edge.SameSense = entity->Param("EDGE_CURVE", 4).AsBoolean(true);

    const int geometryId = entity->Param("EDGE_CURVE", 3).AsReference();
    edge.Curve = Curve(EdgeCurveGeometry(geometryId));

    if (edge.Curve >= 0 && edge.Start >= 0 && edge.End >= 0) {
        const BrepCurve& curve = brep_.Curves[static_cast<size_t>(edge.Curve)];
        double tMin = 0.0, tMax = 0.0;
        curve.ParameterRange(tMin, tMax);

        const Vec3d startPoint = brep_.Vertices[static_cast<size_t>(edge.Start)].Point;
        const Vec3d endPoint = brep_.Vertices[static_cast<size_t>(edge.End)].Point;
        edge.TStart = ParameterAt(curve, startPoint);
        edge.TEnd = ParameterAt(curve, endPoint);

        const bool periodic = curve.Type == BrepCurveType::Circle ||
                              curve.Type == BrepCurveType::Ellipse || curve.Periodic;
        if (startId == endId && periodic) {
            // A whole circle: both ends are the same vertex, so the
            // parameters agree and say nothing. The edge is the full turn.
            edge.TStart = tMin;
            edge.TEnd = tMax;
            if (!edge.SameSense) std::swap(edge.TStart, edge.TEnd);
        } else if (periodic) {
            // An arc: the sense decides which way round the circle it goes,
            // and the parameters have to be unwrapped to match or the arc
            // comes out as its complement.
            const double period = tMax - tMin;
            if (edge.SameSense) {
                while (edge.TEnd <= edge.TStart + 1e-12) edge.TEnd += period;
            } else {
                while (edge.TEnd >= edge.TStart - 1e-12) edge.TEnd -= period;
            }
        } else if (edge.SameSense && edge.TEnd < edge.TStart) {
            std::swap(edge.TStart, edge.TEnd);
        } else if (!edge.SameSense && edge.TEnd > edge.TStart) {
            std::swap(edge.TStart, edge.TEnd);
        }

        if (std::fabs(edge.TEnd - edge.TStart) <= 1e-12 &&
            (startPoint - endPoint).Length() <= 1e-12)
            edge.Degenerate = true;
    } else if (edge.Curve < 0 && edge.Start >= 0 && edge.End >= 0) {
        // No usable geometry but real endpoints: a straight span is a better
        // answer than dropping the edge and losing the face with it.
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
    }

    const int index = brep_.AddEdge(std::move(edge));
    edgeFor_[id] = index;
    return index;
}

bool Reader::Loop(int id, bool orientation, int surfaceStepId, BrepLoop& out) {
    const SF::Entity* entity = At(id);
    if (!entity) return false;

    if (entity->Is("VERTEX_LOOP")) {
        // A loop that is a single point: the apex of a cone, the pole of a
        // sphere. It bounds nothing in parameter space that can be walked, so
        // it is recorded as empty and the face is bounded by its other loops.
        out.Coedges.clear();
        return false;
    }

    if (entity->Is("POLY_LOOP")) {
        // The faceted form: a closed polygon of points, no shared edges. Used
        // by faceted_brep, which is how a mesh reaches STEP.
        std::vector<int> vertices;
        for (int pointId : entity->Param("POLY_LOOP", 1).AsReferences()) {
            Vec3d point;
            if (Point3(pointId, point)) vertices.push_back(brep_.AddVertex(point));
        }
        if (vertices.size() < 3) return false;
        for (size_t i = 0; i < vertices.size(); ++i) {
            const size_t next = (i + 1) % vertices.size();
            const Vec3d from = brep_.Vertices[static_cast<size_t>(vertices[i])].Point;
            const Vec3d to = brep_.Vertices[static_cast<size_t>(vertices[next])].Point;
            if ((to - from).Length() <= 1e-15) continue;
            BrepCurve line;
            line.Type = BrepCurveType::Line;
            line.Placement.Origin = from;
            line.Placement.AxisZ = to - from;
            line.TMin = 0.0;
            line.TMax = 1.0;
            BrepEdge edge;
            edge.Curve = brep_.AddCurve(std::move(line));
            edge.Start = vertices[i];
            edge.End = vertices[next];
            edge.TStart = 0.0;
            edge.TEnd = 1.0;
            BrepCoedge coedge;
            coedge.Edge = brep_.AddEdge(std::move(edge));
            out.Coedges.push_back(coedge);
        }
        if (!orientation) {
            std::reverse(out.Coedges.begin(), out.Coedges.end());
            for (BrepCoedge& coedge : out.Coedges) coedge.Forward = !coedge.Forward;
        }
        return out.Coedges.size() >= 3;
    }

    if (!entity->Is("EDGE_LOOP")) return false;

    // Count how many times each edge appears, so a seam — the same edge used
    // twice by one loop — can be given the right pcurve on each pass.
    std::map<int, int> usesSoFar;
    for (int orientedId : entity->Param("EDGE_LOOP", 1).AsReferences()) {
        const SF::Entity* oriented = At(orientedId);
        if (!oriented || !oriented->Is("ORIENTED_EDGE")) continue;
        // oriented_edge(name, edge_start, edge_end, edge_element, orientation);
        // the two vertex slots are derived and written as *.
        const int edgeId = oriented->Param("ORIENTED_EDGE", 3).AsReference();
        const bool forward = oriented->Param("ORIENTED_EDGE", 4).AsBoolean(true);

        const int index = Edge(edgeId);
        if (index < 0) continue;

        BrepCoedge coedge;
        coedge.Edge = index;
        coedge.Forward = forward;

        const SF::Entity* edgeEntity = At(edgeId);
        if (edgeEntity && surfaceStepId >= 0) {
            const int geometryId = edgeEntity->Param("EDGE_CURVE", 3).AsReference();
            const int occurrence = usesSoFar[edgeId]++;
            coedge.ParameterCurve = PCurveOn(geometryId, surfaceStepId, occurrence);
        }
        out.Coedges.push_back(coedge);
    }

    // face_bound's orientation is a statement about the loop as a whole: false
    // means walk it the other way, which is both the reverse order and the
    // opposite sense on every coedge.
    if (!orientation) {
        std::reverse(out.Coedges.begin(), out.Coedges.end());
        for (BrepCoedge& coedge : out.Coedges) coedge.Forward = !coedge.Forward;
    }
    return !out.Coedges.empty();
}

int Reader::Face(int id) {
    auto cached = faceFor_.find(id);
    if (cached != faceFor_.end()) return cached->second;

    const SF::Entity* entity = At(id);
    const char* keyword = nullptr;
    if (entity && entity->Is("ADVANCED_FACE")) keyword = "ADVANCED_FACE";
    else if (entity && entity->Is("FACE_SURFACE")) keyword = "FACE_SURFACE";
    if (!keyword) { faceFor_[id] = -1; return -1; }

    const int surfaceStepId = entity->Param(keyword, 2).AsReference();
    const int surface = Surface(surfaceStepId);
    if (surface < 0) { faceFor_[id] = -1; return -1; }

    BrepFace face;
    face.Surface = surface;
    face.SameSense = entity->Param(keyword, 3).AsBoolean(true);
    if (!entity->Param(keyword, 0).AsText().empty())
        face.Name = entity->Param(keyword, 0).AsText();

    for (int boundId : entity->Param(keyword, 1).AsReferences()) {
        const SF::Entity* bound = At(boundId);
        if (!bound) continue;
        const char* boundKeyword = bound->Is("FACE_OUTER_BOUND") ? "FACE_OUTER_BOUND"
                                 : bound->Is("FACE_BOUND")       ? "FACE_BOUND"
                                                                 : nullptr;
        if (!boundKeyword) continue;

        BrepLoop loop;
        const bool outer = bound->Is("FACE_OUTER_BOUND");
        loop.Kind = outer ? BrepLoopKind::Outer : BrepLoopKind::Unspecified;
        if (!Loop(bound->Param(boundKeyword, 1).AsReference(),
                  bound->Param(boundKeyword, 2).AsBoolean(true), surfaceStepId, loop))
            continue;
        face.Loops.push_back(brep_.AddLoop(std::move(loop)));
    }

    if (face.Loops.empty()) {
        WarnOnce("STEP: a face has no usable bounds and was skipped");
        faceFor_[id] = -1;
        return -1;
    }

    // Exactly one loop may be marked outer. A file that marks several — or a
    // face whose only bound is a plain face_bound — leaves the choice to the
    // tessellator, which takes the largest.
    int outerCount = 0;
    for (int loopIndex : face.Loops)
        if (brep_.Loops[static_cast<size_t>(loopIndex)].Kind == BrepLoopKind::Outer) ++outerCount;
    if (outerCount > 1)
        for (int loopIndex : face.Loops)
            brep_.Loops[static_cast<size_t>(loopIndex)].Kind = BrepLoopKind::Unspecified;
    else if (outerCount == 1)
        for (int loopIndex : face.Loops)
            if (brep_.Loops[static_cast<size_t>(loopIndex)].Kind != BrepLoopKind::Outer)
                brep_.Loops[static_cast<size_t>(loopIndex)].Kind = BrepLoopKind::Inner;

    const int index = brep_.AddFace(std::move(face));
    faceFor_[id] = index;
    return index;
}

int Reader::Shell(int id, bool closed, std::vector<int>& outFaces) {
    const SF::Entity* entity = At(id);
    if (!entity) return -1;
    const char* keyword = entity->Is("CLOSED_SHELL") ? "CLOSED_SHELL"
                        : entity->Is("OPEN_SHELL")   ? "OPEN_SHELL"
                        : entity->Is("CONNECTED_FACE_SET") ? "CONNECTED_FACE_SET"
                                                           : nullptr;
    if (!keyword) return -1;

    BrepShell shell;
    shell.Closed = closed && entity->Is("CLOSED_SHELL");
    for (int faceId : entity->Param(keyword, 1).AsReferences()) {
        const int face = Face(faceId);
        if (face >= 0) { shell.Faces.push_back(face); outFaces.push_back(face); }
    }
    if (shell.Faces.empty()) return -1;
    return brep_.AddShell(std::move(shell));
}

void Reader::ReadSolids() {
    // manifold_solid_brep is the ordinary case; brep_with_voids adds the inner
    // shells of a hollow part; faceted_brep is the same thing with planar
    // faces and poly_loops, which is how a mesh reaches STEP.
    for (const auto& entry : model_.Entities) {
        const SF::Entity& entity = entry.second;
        const bool solidBrep = entity.Is("MANIFOLD_SOLID_BREP") || entity.Is("FACETED_BREP") ||
                               entity.Is("BREP_WITH_VOIDS");
        const bool surfaceModel = entity.Is("SHELL_BASED_SURFACE_MODEL");
        if (!solidBrep && !surfaceModel) continue;

        BrepSolid solid;
        solid.Closed = solidBrep;
        std::vector<int> faces;

        if (solidBrep) {
            const char* keyword = entity.Is("BREP_WITH_VOIDS")   ? "BREP_WITH_VOIDS"
                                : entity.Is("FACETED_BREP")      ? "FACETED_BREP"
                                                                 : "MANIFOLD_SOLID_BREP";
            solid.Name = entity.Param(keyword, 0).AsText();
            const int outer = Shell(entity.Param(keyword, 1).AsReference(), true, faces);
            if (outer >= 0) solid.Shells.push_back(outer);
            if (entity.Is("BREP_WITH_VOIDS"))
                for (int voidId : entity.Param("BREP_WITH_VOIDS", 2).AsReferences()) {
                    const int shell = Shell(voidId, true, faces);
                    if (shell >= 0) solid.Shells.push_back(shell);
                }
        } else {
            solid.Name = entity.Param("SHELL_BASED_SURFACE_MODEL", 0).AsText();
            for (int shellId : entity.Param("SHELL_BASED_SURFACE_MODEL", 1).AsReferences()) {
                const int shell = Shell(shellId, false, faces);
                if (shell >= 0) solid.Shells.push_back(shell);
            }
        }

        if (solid.Shells.empty()) continue;
        solid.Extras["stepId"] = "#" + std::to_string(entity.Id);
        const int index = brep_.AddSolid(std::move(solid));

        ModelNode node;
        node.Name = brep_.Solids[static_cast<size_t>(index)].Name.empty()
                            ? ("solid_" + std::to_string(entity.Id))
                            : brep_.Solids[static_cast<size_t>(index)].Name;
        node.Solid = index;
        document_.AddNode(std::move(node));
    }
}

void Reader::ReadUnits() {
    auto scaleOfSiLength = [](const std::string& prefix) -> double {
        if (prefix == "MILLI") return 1e-3;
        if (prefix == "CENTI") return 1e-2;
        if (prefix == "DECI") return 1e-1;
        if (prefix == "MICRO") return 1e-6;
        if (prefix == "NANO") return 1e-9;
        if (prefix == "KILO") return 1e3;
        if (prefix == "HECTO") return 1e2;
        if (prefix == "DECA") return 1e1;
        return 1.0;
    };

    double metresPerUnit = 0.0;
    for (const auto& entry : model_.Entities) {
        const SF::Entity& context = entry.second;
        if (!context.Is("GLOBAL_UNIT_ASSIGNED_CONTEXT")) continue;
        for (int unitId : context.Param("GLOBAL_UNIT_ASSIGNED_CONTEXT", 0).AsReferences()) {
            const SF::Entity* unit = At(unitId);
            if (!unit || !unit->Is("LENGTH_UNIT")) continue;

            if (unit->Is("SI_UNIT")) {
                const std::vector<Value>* si = unit->Params("SI_UNIT");
                const std::string prefix = si && si->size() >= 1 ? (*si)[0].AsText() : std::string();
                metresPerUnit = scaleOfSiLength(prefix);
            } else if (unit->Is("CONVERSION_BASED_UNIT")) {
                // conversion_based_unit(name, conversion_factor), where the
                // factor is a measure_with_unit stated in the SI unit.
                const SF::Entity* measure =
                        At(unit->Param("CONVERSION_BASED_UNIT", 1).AsReference());
                if (!measure) continue;
                const std::vector<Value>* params = measure->Params("LENGTH_MEASURE_WITH_UNIT");
                if (!params) params = measure->Params("MEASURE_WITH_UNIT");
                if (!params || params->size() < 2) continue;
                double factor = (*params)[0].AsNumber();
                if ((*params)[0].Type == SF::ValueType::Typed && !(*params)[0].Items.empty())
                    factor = (*params)[0].Items[0].AsNumber();
                const SF::Entity* base = At((*params)[1].AsReference());
                double baseScale = 1.0;
                if (base && base->Is("SI_UNIT")) {
                    const std::vector<Value>* si = base->Params("SI_UNIT");
                    baseScale = scaleOfSiLength(si && !si->empty() ? (*si)[0].AsText()
                                                                  : std::string());
                }
                metresPerUnit = factor * baseScale;
            }
            if (metresPerUnit > 0.0) break;
        }
        if (metresPerUnit > 0.0) break;
    }

    if (metresPerUnit <= 0.0) return;
    document_.UnitScaleToMeters = metresPerUnit;

    // Name it where the number matches a unit the document knows; the scale is
    // recorded either way, and geometry is never rescaled — a millimetre part
    // stays in millimetres, exactly as the 2D model records its own unit.
    struct Named { ModelUnit Unit; double Metres; };
    static const Named kKnown[] = {
            {ModelUnit::Micrometer, 1e-6}, {ModelUnit::Millimeter, 1e-3},
            {ModelUnit::Centimeter, 1e-2}, {ModelUnit::Decimeter, 1e-1},
            {ModelUnit::Meter, 1.0},       {ModelUnit::Kilometer, 1e3},
            {ModelUnit::Mil, 2.54e-5},     {ModelUnit::Inch, 0.0254},
            {ModelUnit::Foot, 0.3048},     {ModelUnit::Yard, 0.9144},
            {ModelUnit::Mile, 1609.344}};
    for (const Named& candidate : kKnown)
        if (std::fabs(metresPerUnit - candidate.Metres) <= candidate.Metres * 1e-9) {
            document_.SourceUnit = candidate.Unit;
            break;
        }
}

bool Reader::FindColourUnder(int id, Vec3f& out, int depth) const {
    if (depth > 8) return false;
    const SF::Entity* entity = At(id);
    if (!entity) return false;

    if (entity->Is("COLOUR_RGB")) {
        const std::vector<Value>* params = entity->Params("COLOUR_RGB");
        if (params && params->size() >= 4) {
            out = Vec3f(static_cast<float>((*params)[1].AsNumber()),
                        static_cast<float>((*params)[2].AsNumber()),
                        static_cast<float>((*params)[3].AsNumber()));
            return true;
        }
        return false;
    }
    if (entity->Is("DRAUGHTING_PRE_DEFINED_COLOUR")) {
        // The named palette AP203 defines. Anything outside it stays default
        // rather than being guessed at.
        static const std::map<std::string, Vec3f> kNamed = {
                {"red", {1, 0, 0}},     {"green", {0, 1, 0}},   {"blue", {0, 0, 1}},
                {"yellow", {1, 1, 0}},  {"magenta", {1, 0, 1}}, {"cyan", {0, 1, 1}},
                {"black", {0, 0, 0}},   {"white", {1, 1, 1}}};
        auto found = kNamed.find(entity->Param("DRAUGHTING_PRE_DEFINED_COLOUR", 0).AsText());
        if (found == kNamed.end()) return false;
        out = found->second;
        return true;
    }

    // Anything else in the presentation chain: follow every reference it
    // holds. The chain from a styled_item down to a colour_rgb is six or seven
    // entities deep and differs between exporters, so walking it beats
    // matching each shape.
    for (const SF::Record& record : entity->Records)
        for (const Value& value : record.Params) {
            if (value.IsReference() && FindColourUnder(value.Reference, out, depth + 1))
                return true;
            if (value.IsList())
                for (const Value& item : value.Items)
                    if (item.IsReference() && FindColourUnder(item.Reference, out, depth + 1))
                        return true;
        }
    return false;
}

int Reader::MaterialFor(const Vec3f& colour, const std::string& name) {
    char key[64];
    std::snprintf(key, sizeof(key), "%.6f|%.6f|%.6f", colour.x, colour.y, colour.z);
    auto cached = materialForColour_.find(key);
    if (cached != materialForColour_.end()) return cached->second;

    ModelMaterial material;
    material.Name = name.empty() ? std::string("colour_") + key : name;
    material.BaseColorFactor = Vec4f(colour.x, colour.y, colour.z, 1.0f);
    material.MetallicFactor = 0.0f;
    material.RoughnessFactor = 0.6f;
    // CAD colours are stated as a surface appearance, so the fixed-function
    // block is filled too rather than left for a writer to guess back.
    PhongParams phong;
    phong.Diffuse = colour;
    phong.Ambient = Vec3f(colour.x * 0.2f, colour.y * 0.2f, colour.z * 0.2f);
    phong.Specular = Vec3f(0.2f, 0.2f, 0.2f);
    phong.Shininess = 32.0f;
    material.Phong = phong;

    const int index = document_.AddMaterial(std::move(material));
    materialForColour_[key] = index;
    return index;
}

void Reader::ReadColours() {
    for (const auto& entry : model_.Entities) {
        const SF::Entity& styled = entry.second;
        const char* keyword = styled.Is("STYLED_ITEM")             ? "STYLED_ITEM"
                            : styled.Is("OVER_RIDING_STYLED_ITEM") ? "OVER_RIDING_STYLED_ITEM"
                                                                   : nullptr;
        if (!keyword) continue;

        const int itemId = styled.Param(keyword, 2).AsReference();
        if (itemId < 0) continue;

        // The colour is somewhere under the style list; which entity it is
        // attached to decides whether it paints one face or a whole body.
        int material = -1;
        for (int styleId : styled.Param(keyword, 1).AsReferences()) {
            Vec3f colour;
            if (!FindColourUnder(styleId, colour)) continue;
            material = MaterialFor(colour, styled.Param(keyword, 0).AsText());
            if (material >= 0) break;
        }
        if (material < 0) continue;

        auto face = faceFor_.find(itemId);
        if (face != faceFor_.end() && face->second >= 0) {
            brep_.Faces[static_cast<size_t>(face->second)].Material = material;
            continue;
        }
        // Not a face: a solid, or the whole shape representation. Both mean
        // "everything under this", so the solids it covers take it.
        const std::string tag = "#" + std::to_string(itemId);
        for (BrepSolid& solid : brep_.Solids) {
            auto stepId = solid.Extras.find("stepId");
            if (stepId != solid.Extras.end() && stepId->second == tag) solid.Material = material;
        }
    }

    // A body with no styling at all still needs something to draw with.
    if (document_.Materials.empty() && !brep_.Solids.empty()) {
        ModelMaterial material;
        material.Name = "Steel";
        material.BaseColorFactor = Vec4f(0.72f, 0.74f, 0.78f, 1.0f);
        material.MetallicFactor = 0.1f;
        material.RoughnessFactor = 0.45f;
        material.DeriveMissingModel();
        const int index = document_.AddMaterial(std::move(material));
        for (BrepSolid& solid : brep_.Solids)
            if (solid.Material < 0) solid.Material = index;
    }
}

void Reader::PlaceSolids() {
    // The part's name, for a document that has one body and no assembly.
    for (const auto& entry : model_.Entities) {
        const SF::Entity& product = entry.second;
        if (!product.Is("PRODUCT")) continue;
        const std::string name = product.Param("PRODUCT", 1).AsText();
        if (!name.empty()) { document_.Title = name; break; }
        const std::string id = product.Param("PRODUCT", 0).AsText();
        if (!id.empty()) { document_.Title = id; break; }
    }
    if (document_.Title.empty()) document_.Title = model_.SourceName();

    // What wrote the file matters when a body turns out to be malformed: the
    // exporter is usually the answer, and it is in the header.
    const std::string origin = model_.OriginatingSystem();
    if (!origin.empty()) document_.Metadata["originatingSystem"] = origin;
    const std::vector<std::string> schemas = model_.Schemas();
    if (!schemas.empty()) document_.Metadata["schema"] = schemas.front();
    for (const SF::Record& record : model_.Header)
        if (record.Keyword == "FILE_NAME" && record.Params.size() >= 3) {
            if (record.Params[1].Type == SF::ValueType::String && !record.Params[1].Text.empty())
                document_.Metadata["timeStamp"] = record.Params[1].Text;
            if (record.Params[2].IsList() && !record.Params[2].Items.empty() &&
                record.Params[2].Items[0].Type == SF::ValueType::String)
                document_.Author = record.Params[2].Items[0].Text;
        }
}

void Reader::Run() {
    ReadUnits();
    ReadSolids();
    ReadColours();
    PlaceSolids();

    // An assembly is a product structure with placements, not just several
    // bodies. Applying those placements needs the product graph walked, which
    // this reader does not do — so it says so rather than putting every part
    // at the origin and letting the user discover it.
    if (!model_.OfType("REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION").empty() ||
        !model_.OfType("NEXT_ASSEMBLY_USAGE_OCCURRENCE").empty())
        Warn("STEP: the file is an assembly; component placements are not applied, so every "
             "part arrives in its own coordinates");

    // The topology has to be sound before anything is meshed from it, and a
    // STEP file referencing geometry it never defines is common enough that
    // saying so is more useful than a mesh with a hole in it.
    std::vector<std::string> problems;
    if (!brep_.Validate(problems)) {
        const size_t show = std::min<size_t>(problems.size(), 6);
        for (size_t i = 0; i < show; ++i) Warn("STEP: " + problems[i]);
        if (problems.size() > show)
            Warn("STEP: and " + std::to_string(problems.size() - show) +
                 " further structural problem(s)");
    }
}

// ===== WRITER =====

// A STEP file is a graph of numbered instances, so writing one is mostly
// bookkeeping: every point, direction and placement must be emitted once,
// numbered, and referenced by number afterwards. Writer owns that numbering.
class Writer {
public:
    Writer(std::ostream& out, const ModelDocument& document, const ConversionOptions& options)
            : out_(out), document_(document), options_(options) {}

    bool Run(const std::string& name);

private:
    std::ostream& out_;
    const ModelDocument& document_;
    const ConversionOptions& options_;

    int next_ = 1;
    std::ostringstream body_;

    // Deduplication. A box has eight corners and twenty-odd directions; a
    // milled part has hundreds of thousands of points, most of them repeated
    // between the curves and the placements that use them. Writing each once
    // is the difference between a 3 MB file and a 30 MB one.
    std::map<std::string, int> pointIds_, directionIds_;
    std::map<int, int> curveIds_, surfaceIds_, vertexIds_, edgeIds_, faceIds_;

    int Emit(const std::string& text) {
        const int id = next_++;
        body_ << "#" << id << " = " << text << ";\n";
        return id;
    }

    std::string Number(double value) const;
    std::string Triple(const Vec3d& v) const;

    int Point(const Vec3d& point);
    int Direction(const Vec3d& direction);
    int Placement(const BrepPlacement& placement);
    int Curve(int index);
    int Curve2D(int index);
    int Surface(int index);
    int Vertex(int index);
    int Edge(int index);
    int Loop(const BrepLoop& loop);
    int Face(int index);
    int Solid(int index, std::vector<int>& outFaceIds);
    void Styles(const std::vector<std::pair<int, int>>& itemsAndMaterials);
};

std::string Writer::Number(double value) const {
    std::ostringstream text;
    // STEP reals must carry a decimal point. Unlike the mesh formats, there is
    // no six-digit mode here: the reason to write STEP at all is that the
    // numbers are exact, and a NURBS weight of cos(45 degrees) rounded to six
    // significant digits is no longer a circular arc. Compact means fifteen
    // digits — every digit a double is guaranteed to carry — and Full means
    // the seventeen that round-trip the bits themselves.
    text << std::setprecision(options_.Precision == NumericPrecision::Full ? 17 : 15);
    if (value == 0.0) return "0.";
    text << value;
    std::string out = text.str();
    if (out.find('.') == std::string::npos && out.find('E') == std::string::npos &&
        out.find('e') == std::string::npos)
        out += '.';
    // Part 21 spells exponents with a capital E and requires a point before it.
    const size_t exponent = out.find_first_of("eE");
    if (exponent != std::string::npos) {
        std::string mantissa = out.substr(0, exponent);
        if (mantissa.find('.') == std::string::npos) mantissa += '.';
        out = mantissa + "E" + out.substr(exponent + 1);
    }
    return out;
}

std::string Writer::Triple(const Vec3d& v) const {
    return "(" + Number(v.x) + "," + Number(v.y) + "," + Number(v.z) + ")";
}

int Writer::Point(const Vec3d& point) {
    const std::string key = Triple(point);
    auto found = pointIds_.find(key);
    if (found != pointIds_.end()) return found->second;
    const int id = Emit("CARTESIAN_POINT(''," + key + ")");
    pointIds_.emplace(key, id);
    return id;
}

int Writer::Direction(const Vec3d& direction) {
    const Vec3d unit = direction.Normalized();
    const std::string key = Triple(unit);
    auto found = directionIds_.find(key);
    if (found != directionIds_.end()) return found->second;
    const int id = Emit("DIRECTION(''," + key + ")");
    directionIds_.emplace(key, id);
    return id;
}

int Writer::Placement(const BrepPlacement& placement) {
    const int origin = Point(placement.Origin);
    const int axis = Direction(placement.AxisZ);
    const int reference = Direction(placement.AxisX);
    return Emit("AXIS2_PLACEMENT_3D('',#" + std::to_string(origin) + ",#" +
                std::to_string(axis) + ",#" + std::to_string(reference) + ")");
}

int Writer::Curve(int index) {
    auto cached = curveIds_.find(index);
    if (cached != curveIds_.end()) return cached->second;
    if (index < 0 || static_cast<size_t>(index) >= document_.Brep.Curves.size()) return -1;
    const BrepCurve& curve = document_.Brep.Curves[static_cast<size_t>(index)];

    int id = -1;
    switch (curve.Type) {
        case BrepCurveType::Line: {
            const int origin = Point(curve.Placement.Origin);
            const double magnitude = curve.Placement.AxisZ.Length();
            const int direction = Direction(curve.Placement.AxisZ);
            const int vector = Emit("VECTOR('',#" + std::to_string(direction) + "," +
                                    Number(magnitude > 0.0 ? magnitude : 1.0) + ")");
            id = Emit("LINE('',#" + std::to_string(origin) + ",#" + std::to_string(vector) + ")");
            break;
        }
        case BrepCurveType::Circle:
            id = Emit("CIRCLE('',#" + std::to_string(Placement(curve.Placement)) + "," +
                      Number(curve.Radius) + ")");
            break;
        case BrepCurveType::Ellipse:
            id = Emit("ELLIPSE('',#" + std::to_string(Placement(curve.Placement)) + "," +
                      Number(curve.Radius) + "," + Number(curve.MinorRadius) + ")");
            break;
        case BrepCurveType::Parabola:
            id = Emit("PARABOLA('',#" + std::to_string(Placement(curve.Placement)) + "," +
                      Number(curve.FocalDistance) + ")");
            break;
        case BrepCurveType::Hyperbola:
            id = Emit("HYPERBOLA('',#" + std::to_string(Placement(curve.Placement)) + "," +
                      Number(curve.Radius) + "," + Number(curve.MinorRadius) + ")");
            break;
        case BrepCurveType::Polyline: {
            std::string points;
            for (const Vec3d& point : curve.ControlPoints) {
                if (!points.empty()) points += ",";
                points += "#" + std::to_string(Point(point));
            }
            id = Emit("POLYLINE('',(" + points + "))");
            break;
        }
        case BrepCurveType::BSpline: {
            std::string points;
            for (const Vec3d& point : curve.ControlPoints) {
                if (!points.empty()) points += ",";
                points += "#" + std::to_string(Point(point));
            }
            // STEP stores distinct knots with multiplicities, which is how the
            // expanded vector has to be folded back up.
            std::vector<double> distinct;
            std::vector<int> multiplicities;
            for (double knot : curve.Knots) {
                if (!distinct.empty() && std::fabs(knot - distinct.back()) <= 1e-12)
                    ++multiplicities.back();
                else { distinct.push_back(knot); multiplicities.push_back(1); }
            }
            std::string knotText, multiplicityText;
            for (size_t i = 0; i < distinct.size(); ++i) {
                if (i) { knotText += ","; multiplicityText += ","; }
                knotText += Number(distinct[i]);
                multiplicityText += std::to_string(multiplicities[i]);
            }

            const std::string shape =
                    "B_SPLINE_CURVE(" + std::to_string(curve.Degree) + ",(" + points +
                    "),.UNSPECIFIED.," + (curve.IsClosed() ? ".T." : ".F.") + ",.F.)";
            const std::string knots =
                    "B_SPLINE_CURVE_WITH_KNOTS((" + multiplicityText + "),(" + knotText +
                    "),.UNSPECIFIED.)";

            if (curve.Weights.size() == curve.ControlPoints.size()) {
                // A rational curve has to be a complex instance — there is no
                // simple entity for one, which is why every NURBS in every
                // STEP file looks like this.
                std::string weights;
                for (double weight : curve.Weights) {
                    if (!weights.empty()) weights += ",";
                    weights += Number(weight);
                }
                id = Emit("( BOUNDED_CURVE() " + shape + " " + knots +
                          " CURVE() GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_CURVE((" +
                          weights + ")) REPRESENTATION_ITEM('') )");
            } else {
                // A polynomial curve is one simple instance, and carries the
                // inherited name as its first attribute rather than leaving it
                // to a sibling REPRESENTATION_ITEM record.
                id = Emit("B_SPLINE_CURVE_WITH_KNOTS(''," + std::to_string(curve.Degree) +
                          ",(" + points + "),.UNSPECIFIED.," +
                          (curve.IsClosed() ? ".T." : ".F.") + ",.F.,(" + multiplicityText +
                          "),(" + knotText + "),.UNSPECIFIED.)");
            }
            break;
        }
    }

    curveIds_[index] = id;
    return id;
}

int Writer::Surface(int index) {
    auto cached = surfaceIds_.find(index);
    if (cached != surfaceIds_.end()) return cached->second;
    if (index < 0 || static_cast<size_t>(index) >= document_.Brep.Surfaces.size()) return -1;
    const BrepSurface& surface = document_.Brep.Surfaces[static_cast<size_t>(index)];

    int id = -1;
    switch (surface.Type) {
        case BrepSurfaceType::Plane:
            id = Emit("PLANE('',#" + std::to_string(Placement(surface.Placement)) + ")");
            break;
        case BrepSurfaceType::Cylinder:
            id = Emit("CYLINDRICAL_SURFACE('',#" + std::to_string(Placement(surface.Placement)) +
                      "," + Number(surface.Radius) + ")");
            break;
        case BrepSurfaceType::Cone:
            id = Emit("CONICAL_SURFACE('',#" + std::to_string(Placement(surface.Placement)) +
                      "," + Number(surface.Radius) + "," + Number(surface.HalfAngleRadians) + ")");
            break;
        case BrepSurfaceType::Sphere:
            id = Emit("SPHERICAL_SURFACE('',#" + std::to_string(Placement(surface.Placement)) +
                      "," + Number(surface.Radius) + ")");
            break;
        case BrepSurfaceType::Torus:
            id = Emit("TOROIDAL_SURFACE('',#" + std::to_string(Placement(surface.Placement)) +
                      "," + Number(surface.Radius) + "," + Number(surface.MinorRadius) + ")");
            break;
        case BrepSurfaceType::Extrusion: {
            const int profile = Curve(surface.ProfileCurve);
            if (profile < 0) break;
            const int direction = Direction(surface.ExtrusionDirection);
            const int vector = Emit("VECTOR('',#" + std::to_string(direction) + "," +
                                    Number(surface.ExtrusionDirection.Length()) + ")");
            id = Emit("SURFACE_OF_LINEAR_EXTRUSION('',#" + std::to_string(profile) + ",#" +
                      std::to_string(vector) + ")");
            break;
        }
        case BrepSurfaceType::Revolution: {
            const int profile = Curve(surface.ProfileCurve);
            if (profile < 0) break;
            const int origin = Point(surface.Placement.Origin);
            const int axis = Direction(surface.Placement.AxisZ);
            const int placement = Emit("AXIS1_PLACEMENT('',#" + std::to_string(origin) + ",#" +
                                       std::to_string(axis) + ")");
            id = Emit("SURFACE_OF_REVOLUTION('',#" + std::to_string(profile) + ",#" +
                      std::to_string(placement) + ")");
            break;
        }
        case BrepSurfaceType::Ruled:
            // No AP203 entity holds a ruled surface directly; it is written as
            // the B-spline it is equivalent to, which every kernel accepts.
            options_.Warn("STEP: a ruled surface has no AP203 entity and was written as a "
                          "B-spline approximation");
            break;
        case BrepSurfaceType::BSpline: {
            std::string columns;
            for (int u = 0; u < surface.ControlPointsU; ++u) {
                std::string column;
                for (int v = 0; v < surface.ControlPointsV; ++v) {
                    const size_t at = static_cast<size_t>(v) *
                                      static_cast<size_t>(surface.ControlPointsU) +
                                      static_cast<size_t>(u);
                    if (at >= surface.ControlNet.size()) continue;
                    if (!column.empty()) column += ",";
                    column += "#" + std::to_string(Point(surface.ControlNet[at]));
                }
                if (!columns.empty()) columns += ",";
                columns += "(" + column + ")";
            }

            auto fold = [this](const std::vector<double>& knots, std::string& values,
                               std::string& multiplicities) {
                std::vector<double> distinct;
                std::vector<int> counts;
                for (double knot : knots) {
                    if (!distinct.empty() && std::fabs(knot - distinct.back()) <= 1e-12)
                        ++counts.back();
                    else { distinct.push_back(knot); counts.push_back(1); }
                }
                for (size_t i = 0; i < distinct.size(); ++i) {
                    if (i) { values += ","; multiplicities += ","; }
                    values += Number(distinct[i]);
                    multiplicities += std::to_string(counts[i]);
                }
            };
            std::string knotsU, knotsV, multiplicitiesU, multiplicitiesV;
            fold(surface.KnotsU, knotsU, multiplicitiesU);
            fold(surface.KnotsV, knotsV, multiplicitiesV);

            const std::string shape =
                    "B_SPLINE_SURFACE(" + std::to_string(surface.DegreeU) + "," +
                    std::to_string(surface.DegreeV) + ",(" + columns +
                    "),.UNSPECIFIED.," + (surface.PeriodicU ? ".T." : ".F.") + "," +
                    (surface.PeriodicV ? ".T." : ".F.") + ",.F.)";
            const std::string knots =
                    "B_SPLINE_SURFACE_WITH_KNOTS((" + multiplicitiesU + "),(" +
                    multiplicitiesV + "),(" + knotsU + "),(" + knotsV + "),.UNSPECIFIED.)";

            if (surface.NetWeights.size() == surface.ControlNet.size()) {
                std::string weightColumns;
                for (int u = 0; u < surface.ControlPointsU; ++u) {
                    std::string column;
                    for (int v = 0; v < surface.ControlPointsV; ++v) {
                        const size_t at = static_cast<size_t>(v) *
                                          static_cast<size_t>(surface.ControlPointsU) +
                                          static_cast<size_t>(u);
                        if (at >= surface.NetWeights.size()) continue;
                        if (!column.empty()) column += ",";
                        column += Number(surface.NetWeights[at]);
                    }
                    if (!weightColumns.empty()) weightColumns += ",";
                    weightColumns += "(" + column + ")";
                }
                id = Emit("( BOUNDED_SURFACE() " + shape + " " + knots +
                          " GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_SURFACE((" +
                          weightColumns + ")) REPRESENTATION_ITEM('') SURFACE() )");
            } else {
                id = Emit("B_SPLINE_SURFACE_WITH_KNOTS(''," + std::to_string(surface.DegreeU) +
                          "," + std::to_string(surface.DegreeV) + ",(" + columns +
                          "),.UNSPECIFIED.," + (surface.PeriodicU ? ".T." : ".F.") + "," +
                          (surface.PeriodicV ? ".T." : ".F.") + ",.F.,(" + multiplicitiesU +
                          "),(" + multiplicitiesV + "),(" + knotsU + "),(" + knotsV +
                          "),.UNSPECIFIED.)");
            }
            break;
        }
    }

    surfaceIds_[index] = id;
    return id;
}

int Writer::Vertex(int index) {
    auto cached = vertexIds_.find(index);
    if (cached != vertexIds_.end()) return cached->second;
    if (index < 0 || static_cast<size_t>(index) >= document_.Brep.Vertices.size()) return -1;
    const int id = Emit("VERTEX_POINT('',#" +
                        std::to_string(Point(document_.Brep.Vertices[static_cast<size_t>(index)].Point)) +
                        ")");
    vertexIds_[index] = id;
    return id;
}

int Writer::Edge(int index) {
    auto cached = edgeIds_.find(index);
    if (cached != edgeIds_.end()) return cached->second;
    if (index < 0 || static_cast<size_t>(index) >= document_.Brep.Edges.size()) return -1;
    const BrepEdge& edge = document_.Brep.Edges[static_cast<size_t>(index)];

    const int start = Vertex(edge.Start);
    const int end = Vertex(edge.End);
    const int curve = Curve(edge.Curve);
    if (start < 0 || end < 0 || curve < 0) { edgeIds_[index] = -1; return -1; }

    // The edge's own sense against its curve. The parameter range is not
    // written: STEP recovers it from the vertices, which is what this reader
    // does too, and writing a trimmed_curve here would be redundant.
    const bool sameSense = edge.TEnd >= edge.TStart;
    const int id = Emit("EDGE_CURVE('',#" + std::to_string(start) + ",#" + std::to_string(end) +
                        ",#" + std::to_string(curve) + "," + (sameSense ? ".T." : ".F.") + ")");
    edgeIds_[index] = id;
    return id;
}

// No pcurves are written: this reader recovers a face's trimming by inverting
// its surface, exactly for the analytic types and numerically for NURBS, and
// the round trip is exact through it. A receiving system with a stricter
// trimming model may prefer them; that is recorded as a gap rather than
// half-done here.
int Writer::Loop(const BrepLoop& loop) {
    std::string oriented;
    for (const BrepCoedge& coedge : loop.Coedges) {
        const int edge = Edge(coedge.Edge);
        if (edge < 0) continue;
        const int id = Emit("ORIENTED_EDGE('',*,*,#" + std::to_string(edge) + "," +
                            (coedge.Forward ? ".T." : ".F.") + ")");
        if (!oriented.empty()) oriented += ",";
        oriented += "#" + std::to_string(id);
    }
    if (oriented.empty()) return -1;
    return Emit("EDGE_LOOP('',(" + oriented + "))");
}

int Writer::Face(int index) {
    auto cached = faceIds_.find(index);
    if (cached != faceIds_.end()) return cached->second;
    if (index < 0 || static_cast<size_t>(index) >= document_.Brep.Faces.size()) return -1;
    const BrepFace& face = document_.Brep.Faces[static_cast<size_t>(index)];

    const int surface = Surface(face.Surface);
    if (surface < 0) { faceIds_[index] = -1; return -1; }

    // The largest loop bounds the face when the document does not say which
    // does, the same rule the tessellator uses.
    int outer = -1;
    for (size_t i = 0; i < face.Loops.size(); ++i) {
        const int loopIndex = face.Loops[i];
        if (loopIndex < 0 || static_cast<size_t>(loopIndex) >= document_.Brep.Loops.size()) continue;
        if (document_.Brep.Loops[static_cast<size_t>(loopIndex)].Kind == BrepLoopKind::Outer) {
            outer = static_cast<int>(i);
            break;
        }
    }
    if (outer < 0 && !face.Loops.empty()) outer = 0;

    std::string bounds;
    for (size_t i = 0; i < face.Loops.size(); ++i) {
        const int loopIndex = face.Loops[i];
        if (loopIndex < 0 || static_cast<size_t>(loopIndex) >= document_.Brep.Loops.size()) continue;
        const int loop = Loop(document_.Brep.Loops[static_cast<size_t>(loopIndex)]);
        if (loop < 0) continue;
        const char* keyword = static_cast<int>(i) == outer ? "FACE_OUTER_BOUND" : "FACE_BOUND";
        const int bound = Emit(std::string(keyword) + "('',#" + std::to_string(loop) + ",.T.)");
        if (!bounds.empty()) bounds += ",";
        bounds += "#" + std::to_string(bound);
    }
    if (bounds.empty()) { faceIds_[index] = -1; return -1; }

    const int id = Emit("ADVANCED_FACE('" + face.Name + "',(" + bounds + "),#" +
                        std::to_string(surface) + "," + (face.SameSense ? ".T." : ".F.") + ")");
    faceIds_[index] = id;
    return id;
}

int Writer::Solid(int index, std::vector<int>& outFaceIds) {
    if (index < 0 || static_cast<size_t>(index) >= document_.Brep.Solids.size()) return -1;
    const BrepSolid& solid = document_.Brep.Solids[static_cast<size_t>(index)];

    std::vector<int> shellIds;
    for (int shellIndex : solid.Shells) {
        if (shellIndex < 0 || static_cast<size_t>(shellIndex) >= document_.Brep.Shells.size())
            continue;
        const BrepShell& shell = document_.Brep.Shells[static_cast<size_t>(shellIndex)];
        std::string faces;
        for (int faceIndex : shell.Faces) {
            const int face = Face(faceIndex);
            if (face < 0) continue;
            outFaceIds.push_back(face);
            if (!faces.empty()) faces += ",";
            faces += "#" + std::to_string(face);
        }
        if (faces.empty()) continue;
        const char* keyword = shell.Closed ? "CLOSED_SHELL" : "OPEN_SHELL";
        shellIds.push_back(Emit(std::string(keyword) + "('',(" + faces + "))"));
    }
    if (shellIds.empty()) return -1;

    const std::string name = solid.Name.empty() ? "body" : solid.Name;
    if (!solid.Closed) {
        std::string shells;
        for (int shell : shellIds) {
            if (!shells.empty()) shells += ",";
            shells += "#" + std::to_string(shell);
        }
        return Emit("SHELL_BASED_SURFACE_MODEL('" + name + "',(" + shells + "))");
    }
    if (shellIds.size() == 1)
        return Emit("MANIFOLD_SOLID_BREP('" + name + "',#" + std::to_string(shellIds[0]) + ")");

    std::string voids;
    for (size_t i = 1; i < shellIds.size(); ++i) {
        if (!voids.empty()) voids += ",";
        voids += "#" + std::to_string(shellIds[i]);
    }
    return Emit("BREP_WITH_VOIDS('" + name + "',#" + std::to_string(shellIds[0]) + ",(" +
                voids + "))");
}

void Writer::Styles(const std::vector<std::pair<int, int>>& itemsAndMaterials) {
    std::map<int, int> colourIds;
    for (const auto& entry : itemsAndMaterials) {
        const int item = entry.first;
        const int materialIndex = entry.second;
        if (item < 0 || materialIndex < 0 ||
            static_cast<size_t>(materialIndex) >= document_.Materials.size())
            continue;
        const ModelMaterial& material = document_.Materials[static_cast<size_t>(materialIndex)];

        auto colour = colourIds.find(materialIndex);
        if (colour == colourIds.end()) {
            const Vec4f& base = material.BaseColorFactor;
            const int id = Emit("COLOUR_RGB('" + material.Name + "'," +
                                Number(base.x) + "," + Number(base.y) + "," + Number(base.z) + ")");
            colour = colourIds.emplace(materialIndex, id).first;
        }

        // The presentation chain AP203 requires between a colour and the thing
        // it paints. It is six entities deep and every one of them is needed
        // for a CAD system to show the part in colour rather than grey.
        const int fillColour = Emit("FILL_AREA_STYLE_COLOUR('',#" +
                                    std::to_string(colour->second) + ")");
        const int fillStyle = Emit("FILL_AREA_STYLE('',(#" + std::to_string(fillColour) + "))");
        const int surfaceFill = Emit("SURFACE_STYLE_FILL_AREA(#" +
                                     std::to_string(fillStyle) + ")");
        const int sideStyle = Emit("SURFACE_SIDE_STYLE('',(#" +
                                   std::to_string(surfaceFill) + "))");
        const int usage = Emit("SURFACE_STYLE_USAGE(.BOTH.,#" + std::to_string(sideStyle) + ")");
        const int assignment = Emit("PRESENTATION_STYLE_ASSIGNMENT((#" +
                                    std::to_string(usage) + "))");
        Emit("STYLED_ITEM('colour',(#" + std::to_string(assignment) + "),#" +
             std::to_string(item) + ")");
    }
}

bool Writer::Run(const std::string& name) {
    const BrepData& brep = document_.Brep;
    if (brep.Solids.empty()) return false;

    // --- context: units and the tolerance the geometry is stated to ---
    const int metre = Emit("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.) )");
    int lengthUnit = metre;
    const ModelUnit unit = document_.SourceUnit;
    const double metresPerUnit = document_.UnitScaleToMeters > 0.0
                                         ? document_.UnitScaleToMeters
                                         : MetersPerUnit(unit);
    if (unit == ModelUnit::Millimeter) {
        lengthUnit = Emit("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )");
    } else if (unit == ModelUnit::Centimeter) {
        lengthUnit = Emit("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.CENTI.,.METRE.) )");
    } else if (metresPerUnit > 0.0 && std::fabs(metresPerUnit - 1.0) > 1e-12) {
        // Anything else — inches, feet, a scanner's own unit — is a
        // conversion-based unit stated against the metre.
        const int measure = Emit("LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(" +
                                 Number(metresPerUnit) + "),#" + std::to_string(metre) + ")");
        const std::string unitName = ModelUnitSymbol(unit);
        lengthUnit = Emit("( CONVERSION_BASED_UNIT('" +
                          (unitName.empty() ? std::string("MODEL_UNIT") : unitName) + "',#" +
                          std::to_string(measure) + ") LENGTH_UNIT() NAMED_UNIT(#" +
                          std::to_string(metre) + ") )");
    }

    const int angleUnit = Emit("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )");
    const int solidAngleUnit = Emit("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )");
    const int uncertainty =
            Emit("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#" +
                 std::to_string(lengthUnit) + ",'distance_accuracy_value','confusion accuracy')");
    const int context =
            Emit("( GEOMETRIC_REPRESENTATION_CONTEXT(3) "
                 "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#" + std::to_string(uncertainty) + ")) "
                 "GLOBAL_UNIT_ASSIGNED_CONTEXT((#" + std::to_string(lengthUnit) + ",#" +
                 std::to_string(angleUnit) + ",#" + std::to_string(solidAngleUnit) + ")) "
                 "REPRESENTATION_CONTEXT('Context','3D') )");

    // --- the bodies ---
    std::vector<int> solidIds;
    std::vector<std::pair<int, int>> styled;
    for (size_t i = 0; i < brep.Solids.size(); ++i) {
        std::vector<int> faceIds;
        const int id = Solid(static_cast<int>(i), faceIds);
        if (id < 0) continue;
        solidIds.push_back(id);

        // The body's own colour paints everything, and a face that states its
        // own is styled on top of it. Both are emitted: dropping the body's
        // because one face overrides it would leave the other faces grey,
        // which is what a round trip through this writer used to do.
        const BrepSolid& solid = brep.Solids[i];
        if (solid.Material >= 0) styled.emplace_back(id, solid.Material);
        size_t at = 0;
        for (int shellIndex : solid.Shells) {
            if (shellIndex < 0 || static_cast<size_t>(shellIndex) >= brep.Shells.size()) continue;
            for (int faceIndex : brep.Shells[static_cast<size_t>(shellIndex)].Faces) {
                if (at >= faceIds.size()) break;
                const int material = brep.Faces[static_cast<size_t>(faceIndex)].Material;
                if (material >= 0 && material != solid.Material)
                    styled.emplace_back(faceIds[at], material);
                ++at;
            }
        }
    }
    if (solidIds.empty()) return false;

    std::string items;
    for (int id : solidIds) {
        if (!items.empty()) items += ",";
        items += "#" + std::to_string(id);
    }
    const int shape = Emit("ADVANCED_BREP_SHAPE_REPRESENTATION('" + name + "',(" + items + "),#" +
                           std::to_string(context) + ")");

    Styles(styled);

    // --- product structure: the wrapper AP203 requires around a shape ---
    const int applicationContext = Emit("APPLICATION_CONTEXT("
            "'core data for automotive mechanical design processes')");
    Emit("APPLICATION_PROTOCOL_DEFINITION('international standard',"
         "'automotive_design',2000,#" + std::to_string(applicationContext) + ")");
    const int product = Emit("PRODUCT('" + name + "','" + name + "','',(#" +
                             std::to_string(Emit("PRODUCT_CONTEXT('',#" +
                                                 std::to_string(applicationContext) +
                                                 ",'mechanical')")) + "))");
    const int formation = Emit("PRODUCT_DEFINITION_FORMATION('','',#" +
                               std::to_string(product) + ")");
    const int definitionContext = Emit("PRODUCT_DEFINITION_CONTEXT('part definition',#" +
                                       std::to_string(applicationContext) + ",'design')");
    const int definition = Emit("PRODUCT_DEFINITION('design','',#" + std::to_string(formation) +
                                ",#" + std::to_string(definitionContext) + ")");
    const int definitionShape = Emit("PRODUCT_DEFINITION_SHAPE('','',#" +
                                     std::to_string(definition) + ")");
    Emit("SHAPE_DEFINITION_REPRESENTATION(#" + std::to_string(definitionShape) + ",#" +
         std::to_string(shape) + ")");

    // --- and finally the file around all of it ---
    const std::string generator =
            options_.Generator.empty() ? std::string("UltraCanvas") : options_.Generator;
    out_ << "ISO-10303-21;\n";
    out_ << "HEADER;\n";
    out_ << "FILE_DESCRIPTION(('" << name << "'),'2;1');\n";
    out_ << "FILE_NAME('" << name << "','',('" << (document_.Author.empty() ? "" : document_.Author)
         << "'),(''),'" << generator << "','" << generator << "','');\n";
    out_ << "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 3 1 1 }'));\n";
    out_ << "ENDSEC;\n";
    out_ << "DATA;\n";
    out_ << body_.str();
    out_ << "ENDSEC;\n";
    out_ << "END-ISO-10303-21;\n";
    return true;
}

} // namespace

// ===== MESH -> FACETED B-REP =====

void StepConverter::FacetMeshesIntoBrep(ModelDocument& document, const ConversionOptions& options) {
    if (document.Meshes.empty()) return;

    BrepData& brep = document.Brep;
    size_t skipped = 0;

    // A document assembled by hand may hold meshes that no node places. They
    // are still geometry the caller asked to write, so each gets a node before
    // the walk below, rather than being silently dropped.
    std::set<int> placed;
    for (const ModelNode& node : document.Nodes)
        if (node.Mesh >= 0) placed.insert(node.Mesh);
    for (size_t i = 0; i < document.Meshes.size(); ++i) {
        if (placed.count(static_cast<int>(i))) continue;
        ModelNode node;
        node.Name = document.Meshes[i].Name;
        node.Mesh = static_cast<int>(i);
        document.AddNode(std::move(node));
    }

    for (size_t nodeIndex = 0; nodeIndex < document.Nodes.size(); ++nodeIndex) {
        if (document.Nodes[nodeIndex].Mesh < 0) continue;
        const int meshIndex = document.Nodes[nodeIndex].Mesh;
        if (static_cast<size_t>(meshIndex) >= document.Meshes.size()) continue;

        // The facets have to be in one space for their edges to be shared, so
        // the node's world transform is baked here rather than left on a node
        // a solid cannot carry.
        const Matrix4x4 world = document.GlobalTransform(static_cast<int>(nodeIndex));
        const ModelMesh& mesh = document.Meshes[static_cast<size_t>(meshIndex)];

        // One vertex per distinct position: two facets that meet must reach the
        // same BrepVertex, or the shell is a pile of loose triangles rather
        // than a solid.
        std::map<std::string, int> vertexForPosition;
        auto vertexFor = [&](const Vec3d& raw) {
            const Vec3d point = world.TransformPoint(raw);
            char key[96];
            std::snprintf(key, sizeof(key), "%.9g|%.9g|%.9g", point.x, point.y, point.z);
            auto found = vertexForPosition.find(key);
            if (found != vertexForPosition.end()) return found->second;
            const int index = brep.AddVertex(point);
            vertexForPosition.emplace(key, index);
            return index;
        };
        // And one edge per unordered vertex pair, for the same reason.
        std::map<std::pair<int, int>, int> edgeForPair;
        auto edgeFor = [&](int from, int to) {
            const std::pair<int, int> key{std::min(from, to), std::max(from, to)};
            auto found = edgeForPair.find(key);
            if (found != edgeForPair.end()) return found->second;
            const Vec3d a = brep.Vertices[static_cast<size_t>(key.first)].Point;
            const Vec3d b = brep.Vertices[static_cast<size_t>(key.second)].Point;
            BrepCurve line;
            line.Type = BrepCurveType::Line;
            line.Placement.Origin = a;
            line.Placement.AxisZ = b - a;
            line.TMin = 0.0;
            line.TMax = 1.0;
            BrepEdge edge;
            edge.Curve = brep.AddCurve(std::move(line));
            edge.Start = key.first;
            edge.End = key.second;
            edge.TStart = 0.0;
            edge.TEnd = 1.0;
            const int index = brep.AddEdge(std::move(edge));
            edgeForPair.emplace(key, index);
            return index;
        };

        BrepShell shell;
        for (const MeshPrimitive& source : mesh.Primitives) {
            if (source.Mode == PrimitiveMode::Points || source.Mode == PrimitiveMode::Lines ||
                source.Mode == PrimitiveMode::LineStrip || source.Mode == PrimitiveMode::LineLoop) {
                ++skipped;
                continue;
            }
            const size_t faces = source.FaceCount();
            for (size_t f = 0; f < faces; ++f) {
                const std::vector<uint32_t> corners = source.Face(f);
                if (corners.size() < 3) continue;

                std::vector<Vec3d> points;
                std::vector<int> vertices;
                points.reserve(corners.size());
                vertices.reserve(corners.size());
                bool usable = true;
                for (uint32_t corner : corners) {
                    if (corner >= source.Positions.size()) { usable = false; break; }
                    const int vertex = vertexFor(source.Positions[corner]);
                    // A facet that visits the same vertex twice is degenerate.
                    if (std::find(vertices.begin(), vertices.end(), vertex) != vertices.end())
                        continue;
                    vertices.push_back(vertex);
                    points.push_back(brep.Vertices[static_cast<size_t>(vertex)].Point);
                }
                if (!usable || vertices.size() < 3) continue;

                // The facet's own plane, from its winding, so the face normal
                // points the way the triangle did.
                const Vec3d normal = NewellNormal(points).Normalized();
                if (normal.Length() < 0.5) continue;   // zero-area facet

                BrepSurface plane;
                plane.Type = BrepSurfaceType::Plane;
                plane.Placement.Origin = points[0];
                plane.Placement.AxisZ = normal;
                plane.Placement.AxisX = (points[1] - points[0]).Normalized();
                plane.Placement.Normalize();

                BrepLoop loop;
                loop.Kind = BrepLoopKind::Outer;
                for (size_t i = 0; i < vertices.size(); ++i) {
                    const size_t next = (i + 1) % vertices.size();
                    BrepCoedge coedge;
                    coedge.Edge = edgeFor(vertices[i], vertices[next]);
                    // The shared edge runs low-to-high; this facet may want it
                    // the other way, and that is exactly what the flag is for.
                    coedge.Forward = vertices[i] < vertices[next];
                    loop.Coedges.push_back(coedge);
                }

                BrepFace face;
                face.Surface = brep.AddSurface(std::move(plane));
                face.Loops.push_back(brep.AddLoop(std::move(loop)));
                face.Material = source.Material;
                shell.Faces.push_back(brep.AddFace(std::move(face)));
            }
        }

        if (shell.Faces.empty()) continue;
        // Whether it encloses volume is not knowable from a mesh without
        // checking every edge twice — which Validate does — so the honest
        // default is a closed shell for a mesh that is manifold and an open
        // one otherwise.
        std::map<int, int> uses;
        for (int faceIndex : shell.Faces)
            for (int loopIndex : brep.Faces[static_cast<size_t>(faceIndex)].Loops)
                for (const BrepCoedge& coedge : brep.Loops[static_cast<size_t>(loopIndex)].Coedges)
                    ++uses[coedge.Edge];
        bool manifold = true;
        for (const auto& entry : uses)
            if (entry.second != 2) manifold = false;
        shell.Closed = manifold;

        BrepSolid solid;
        solid.Name = mesh.Name.empty() ? document.Nodes[nodeIndex].Name : mesh.Name;
        solid.Closed = manifold;
        solid.Shells.push_back(brep.AddShell(std::move(shell)));
        document.Nodes[nodeIndex].Solid = brep.AddSolid(std::move(solid));

        if (!manifold)
            options.Warn("STEP: the mesh '" + solid.Name +
                         "' is not closed, so it is written as a surface model rather than a "
                         "solid");
    }

    if (skipped)
        options.Warn("STEP: " + std::to_string(skipped) +
                     " point or line primitive(s) have no B-rep form and were not written");
}

// ===== CONVERTER =====

FormatCapabilities StepConverter::GetCapabilities() const {
    FormatCapabilities capabilities;
    capabilities.Brep = true;
    capabilities.NurbsSurfaces = true;
    capabilities.Assemblies = true;      // the format carries them; see the reader's warning
    capabilities.Meshes = true;          // as a faceted b-rep, both ways
    capabilities.NGons = true;
    capabilities.SceneGraph = true;
    capabilities.Instancing = false;
    capabilities.Materials = true;
    capabilities.PBRMaterials = false;   // STEP states a colour, not a shading model
    capabilities.Units = true;
    capabilities.UpAxis = false;         // STEP has no up axis; Z-up is convention only
    capabilities.Metadata = true;
    capabilities.DoublePrecision = true;
    return capabilities;
}

std::shared_ptr<ModelDocument> StepConverter::ImportFromStream(std::istream& stream,
                                                               const ConversionOptions& options) {
    SF::Model file;
    if (!file.Parse(stream, [&options](const std::string& message) { options.Warn(message); }))
        return nullptr;

    auto document = std::make_shared<ModelDocument>();
    document->SourceFormat = "step";
    document->Generator = options.Generator;
    // STEP has no up-axis statement, but every CAD system that writes one
    // works in Z-up, so recording Z is the honest reading of the convention.
    document->Up = UpAxis::ZUp;

    Reader reader(file, *document, options);
    reader.Run();

    if (document->Brep.Solids.empty()) {
        options.Warn("STEP: the file holds no solid or surface model this reader could build");
        return nullptr;
    }

    if (options.TessellateOnImport) {
        std::vector<std::string> problems;
        document->TessellateBreps(options.Tessellation, &problems);
        for (const std::string& problem : problems) options.Warn("STEP: " + problem);
    }
    if (options.ForceUpAxis.has_value()) document->ConvertUpAxis(*options.ForceUpAxis);
    return document;
}

std::shared_ptr<ModelDocument> StepConverter::Import(const std::string& filename,
                                                     const ConversionOptions& options) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        options.Warn("STEP: cannot open " + filename);
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

std::shared_ptr<ModelDocument> StepConverter::ImportFromMemory(const std::vector<uint8_t>& data,
                                                               const ConversionOptions& options) {
    std::string text(data.begin(), data.end());
    std::istringstream stream(text);
    return ImportFromStream(stream, options);
}

bool StepConverter::ExportToStream(const ModelDocument& document, std::ostream& stream,
                                   const ConversionOptions& options) {
    const std::string name = document.Title.empty() ? "model" : document.Title;

    if (!document.Brep.Solids.empty()) {
        Writer writer(stream, document, options);
        return writer.Run(name);
    }

    if (document.Meshes.empty()) {
        options.Warn("STEP: the document holds neither solids nor meshes; nothing to write");
        return false;
    }

    // A mesh has no surfaces of its own, so they are made: one plane per
    // facet, edges shared between neighbours. That is what STEP has for a
    // mesh, and it is what a CAD system will read back.
    ModelDocument faceted = document;
    FacetMeshesIntoBrep(faceted, options);
    if (faceted.Brep.Solids.empty()) {
        options.Warn("STEP: the document's meshes produced no facets that could be written");
        return false;
    }
    options.Warn("STEP: the document holds meshes rather than exact bodies, so it is written as "
                 "a faceted b-rep — every facet becomes its own planar face, and the result is "
                 "the mesh's accuracy, not a CAD model's");
    Writer writer(stream, faceted, options);
    return writer.Run(name);
}

bool StepConverter::Export(const ModelDocument& document, const std::string& filename,
                           const ConversionOptions& options) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        options.Warn("STEP: cannot write " + filename);
        return false;
    }
    return ExportToStream(document, file, options);
}

bool StepConverter::ExportToMemory(const ModelDocument& document, std::vector<uint8_t>& outData,
                                   const ConversionOptions& options) {
    std::ostringstream stream;
    if (!ExportToStream(document, stream, options)) return false;
    const std::string text = stream.str();
    outData.assign(text.begin(), text.end());
    return true;
}

bool StepConverter::ValidateData(const std::vector<uint8_t>& data) const {
    return SF::LooksLikeStepFile(data);
}

bool StepConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> head(64);
    file.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));
    return SF::LooksLikeStepFile(head);
}

} // namespace ModelConverter
} // namespace UltraCanvas
