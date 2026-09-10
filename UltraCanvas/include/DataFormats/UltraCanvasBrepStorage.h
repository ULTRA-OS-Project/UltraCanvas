// include/DataFormats/UltraCanvasBrepStorage.h
// ModelStorage::BrepData — exact boundary representation, held beside the mesh
// rather than instead of it.
//
// STEP (ISO 10303 AP203/214/242), IGES, ACIS/SAT, Parasolid XT, OpenNURBS
// (.3dm) and the 3DSOLID / REGION / BODY / SURFACE entities of DWG and DXF do
// not contain meshes. They contain trimmed parametric surfaces and the
// topology that stitches them into solids. A triangle mesh is a *view* of that
// — one chosen at a tolerance the file never stated — so a reader that
// tessellates and throws the surfaces away has decided, on the user's behalf,
// that the model is now approximate for ever. This header is the alternative:
// the exact form is stored, and tessellation becomes something the document
// does on demand, at a tolerance the caller picks.
//
// Shape and hierarchy, the one every B-rep kernel and every B-rep file format
// agrees on:
//
//   Solid  -> Shells   (the first is the outer boundary; the rest are voids)
//   Shell  -> Faces
//   Face   -> Surface + Loops     (the first bounds it; the rest are holes)
//   Loop   -> Coedges             (an ordered, closed circuit)
//   Coedge -> Edge + orientation + optional parameter-space curve
//   Edge   -> Curve + two Vertices + a parameter range
//   Vertex -> a point
//
// Everything is flat and index-addressed, exactly as the mesh side is, because
// sharing is the whole point: two faces of a solid share an edge, and an edge
// shared by two faces is what makes it a solid rather than a pile of patches.
//
// What each field maps to in the source formats is annotated inline. The
// survey behind those annotations is Docs/Research/UltraCanvas3DModelProposal.md
// (§2.5, which this header reverses, and records why).
//
// This is core, not plugin: per Masterfile_modules.md a framework-wide
// structure lives in UltraCanvas/{include,core} so that every format reader can
// fill it without depending on any other format's plugin.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BREP_STORAGE_H
#define ULTRACANVAS_BREP_STORAGE_H

#include "DataFormats/UltraCanvasModelMath.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace ModelStorage {

// ===== PLACEMENT =====

// A right-handed local frame: STEP's axis2_placement_3d, IGES's transformation
// matrix entity, the ACIS transform, OpenNURBS's ON_Plane. Every analytic
// curve and surface below is defined in its own frame and placed by one of
// these, which is what makes a cylinder two numbers rather than a control net.
//
// AxisZ is the primary direction (a cylinder's axis, a plane's normal, a
// line's direction); AxisX is the reference direction that fixes where the
// parameterisation starts. AxisY is derived, never stored, so the frame cannot
// go non-orthogonal in the file.
    struct BrepPlacement {
        Vec3d Origin{0.0, 0.0, 0.0};
        Vec3d AxisZ{0.0, 0.0, 1.0};
        Vec3d AxisX{1.0, 0.0, 0.0};

        Vec3d AxisY() const { return AxisZ.Cross(AxisX); }
        // Local (x, y, z) -> world.
        Vec3d ToWorld(double x, double y, double z) const;
        // World -> local (x, y, z). The frame is orthonormal, so this is the
        // transpose rather than an inverse.
        Vec3d ToLocal(const Vec3d& world) const;
        // Re-orthogonalise: AxisX is projected off AxisZ and both normalised.
        // Readers should call it, because files do not promise exactness.
        void Normalize();
    };

// ===== CURVE AND SURFACE GEOMETRY =====

    enum class BrepCurveType {
        Line,        // STEP line, IGES 110, SAT straight-curve
        Circle,      // STEP circle, IGES 100, SAT ellipse with equal radii
        Ellipse,     // STEP ellipse, IGES 104 (conic arc)
        Parabola,    // STEP parabola, IGES 104
        Hyperbola,   // STEP hyperbola, IGES 104
        Polyline,    // IGES 106 linear path, DXF 3D polyline read as exact
        BSpline      // STEP b_spline_curve_with_knots (+ the rational subtype),
                     // IGES 126, SAT intcurve/exactcurve, OpenNURBS ON_NurbsCurve
    };

// A curve in space. Analytic types use Placement and the radius fields;
// BSpline and Polyline use the control net.
//
// A NURBS curve here is the general form: degree, a clamped or periodic knot
// vector, control points and — when Weights is non-empty — per-point weights
// that make it rational. That is exactly what STEP, IGES, ACIS and OpenNURBS
// each store, under four different spellings.
    struct BrepCurve {
        BrepCurveType Type = BrepCurveType::Line;
        std::string Name;

        BrepPlacement Placement;    // Line: Origin + AxisZ is the direction
        double Radius = 0.0;        // Circle radius / Ellipse+Hyperbola major
        double MinorRadius = 0.0;   // Ellipse, Hyperbola
        double FocalDistance = 0.0; // Parabola

        int Degree = 3;                       // BSpline
        std::vector<Vec3d> ControlPoints;     // BSpline, Polyline
        std::vector<double> Weights;          // BSpline; empty = non-rational
        std::vector<double> Knots;            // BSpline; ControlPoints + Degree + 1 entries
        bool Periodic = false;                // closed without duplicated points

        // The parameter interval the curve is defined over. Analytic curves
        // default to their natural range (0..2pi for a circle); a BSpline's is
        // taken from the knot vector when this is left empty.
        double TMin = 0.0, TMax = 0.0;
        bool HasRange() const { return TMax > TMin; }

        // Natural parameter range: the stored one when set, else the type's.
        void ParameterRange(double& tMin, double& tMax) const;
        Vec3d Evaluate(double t) const;
        // d/dt. Falls back to a central difference when the analytic form is
        // not worth writing out.
        Vec3d Tangent(double t) const;
        bool IsClosed() const;
    };

    enum class BrepCurve2DType {
        Line,        // STEP line in parameter space
        Circle,
        Ellipse,
        Polyline,    // the form a tessellating reader produces directly
        BSpline
    };

// A trimming curve in a surface's (u, v) parameter space. STEP calls it a
// pcurve, IGES entity 142 a "curve on a parametric surface", ACIS stores it on
// the coedge. Without it a trimmed face cannot be meshed, because the 3D edge
// curve alone does not say which side of itself is inside.
    struct BrepCurve2D {
        BrepCurve2DType Type = BrepCurve2DType::Line;

        Vec2d Origin;                  // Line: a point on it; Circle: the centre
        Vec2d Direction{1.0, 0.0};     // Line
        Vec2d RefDirection{1.0, 0.0};  // Circle/Ellipse: where the angle starts
        double Radius = 0.0;
        double MinorRadius = 0.0;

        int Degree = 3;
        std::vector<Vec2d> ControlPoints;
        std::vector<double> Weights;
        std::vector<double> Knots;

        double TMin = 0.0, TMax = 0.0;
        bool HasRange() const { return TMax > TMin; }

        void ParameterRange(double& tMin, double& tMax) const;
        Vec2d Evaluate(double t) const;
    };

    enum class BrepSurfaceType {
        Plane,         // STEP plane, IGES 108/190, SAT plane-surface
        Cylinder,      // STEP cylindrical_surface, IGES 192, SAT cone with zero angle
        Cone,          // STEP conical_surface, IGES 194
        Sphere,        // STEP spherical_surface, IGES 196
        Torus,         // STEP toroidal_surface, IGES 198
        Extrusion,     // STEP surface_of_linear_extrusion, IGES 122
        Revolution,    // STEP surface_of_revolution, IGES 120
        Ruled,         // IGES 118
        BSpline        // STEP b_spline_surface_with_knots (+ rational), IGES 128,
                       // SAT spline surface, OpenNURBS ON_NurbsSurface
    };

// A surface. The analytic types are parameterised the way STEP defines them,
// so a reader copies numbers rather than converting:
//
//   Plane       (u, v) -> Origin + u*AxisX + v*AxisY
//   Cylinder    (u, v) -> Origin + R*(cos u * AxisX + sin u * AxisY) + v*AxisZ
//   Cone        as Cylinder with R replaced by (R + v*tan(HalfAngle))
//   Sphere      (u, v) -> Origin + R*(cos v*(cos u*X + sin u*Y) + sin v*Z)
//   Torus       (u, v) -> Origin + (MajorR + MinorR*cos v)*(cos u*X + sin u*Y)
//                                + MinorR*sin v*AxisZ
//
// u is an angle in radians for every rotational type; v is a length for
// Cylinder and Cone and an angle for Sphere and Torus. Extrusion, Revolution
// and Ruled are defined by the curves they reference, so they carry curve
// indices into BrepData::Curves.
    struct BrepSurface {
        BrepSurfaceType Type = BrepSurfaceType::Plane;
        std::string Name;

        BrepPlacement Placement;
        double Radius = 0.0;             // Cylinder, Cone (at v = 0), Sphere, Torus major
        double MinorRadius = 0.0;        // Torus minor
        double HalfAngleRadians = 0.0;   // Cone

        int ProfileCurve = -1;           // Extrusion, Revolution, Ruled: first curve
        int SecondCurve = -1;            // Ruled: second curve
        Vec3d ExtrusionDirection{0.0, 0.0, 1.0};   // Extrusion; Revolution uses Placement

        // BSpline: a (ControlPointsV x ControlPointsU) net stored row-major in
        // u, i.e. index = row * ControlPointsU + column.
        int DegreeU = 3, DegreeV = 3;
        int ControlPointsU = 0, ControlPointsV = 0;
        std::vector<Vec3d> ControlNet;
        std::vector<double> NetWeights;    // empty = non-rational
        std::vector<double> KnotsU, KnotsV;
        bool PeriodicU = false, PeriodicV = false;

        // The (u, v) rectangle the surface is defined over. Left empty for the
        // analytic types, whose natural range the evaluator supplies.
        double UMin = 0.0, UMax = 0.0, VMin = 0.0, VMax = 0.0;
        bool HasURange() const { return UMax > UMin; }
        bool HasVRange() const { return VMax > VMin; }

        void ParameterRange(double& uMin, double& uMax, double& vMin, double& vMax) const;
        // Evaluation needs the curve pool for Extrusion / Revolution / Ruled,
        // which are defined in terms of other curves.
        Vec3d Evaluate(double u, double v, const std::vector<BrepCurve>& curves) const;
        // Unit normal, from the cross product of the partial derivatives.
        // Zero-length at a pole (a sphere's, a cone's apex), where it is
        // genuinely undefined.
        Vec3d Normal(double u, double v, const std::vector<BrepCurve>& curves) const;
        bool IsPeriodicInU() const;
        bool IsPeriodicInV() const;
        // Closed-form for the analytic types, a seeded Newton descent for the
        // rest. Returns false when it does not converge.
        bool Project(const Vec3d& point, const std::vector<BrepCurve>& curves,
                     double& u, double& v) const;
    };

// ===== TOPOLOGY =====

    struct BrepVertex {
        Vec3d Point;
        std::string Name;
    };

// A bounded piece of a curve between two vertices. Start and End index
// BrepData::Vertices; TStart/TEnd are the curve parameters at them, which
// matters because a circle's two arcs share both endpoints.
//
// SameSense is STEP's edge_curve.same_sense: false when the edge runs against
// the curve's own direction.
    struct BrepEdge {
        int Curve = -1;             // index into BrepData::Curves; -1 = degenerate
        int Start = -1, End = -1;   // indices into BrepData::Vertices
        double TStart = 0.0, TEnd = 0.0;
        bool SameSense = true;
        // A seam or pole edge: a sphere's meridian, the apex of a cone. It has
        // no length in space but is needed to close the loop in parameter
        // space, and tessellation must not emit triangles for it.
        bool Degenerate = false;
        std::string Name;

        bool HasRange() const { return TEnd != TStart; }
    };

// One use of an edge by one loop: STEP's oriented_edge, ACIS's coedge. An edge
// in a closed solid is used exactly twice, once in each direction — which is
// the invariant Validate() checks.
    struct BrepCoedge {
        int Edge = -1;              // index into BrepData::Edges
        bool Forward = true;        // traverse the edge from Start to End
        // The edge as a curve in this face's parameter space. -1 when the file
        // did not carry one; tessellation then projects the 3D curve onto the
        // surface, which is exact for the analytic types and iterative for
        // NURBS.
        int ParameterCurve = -1;    // index into BrepData::Curves2D
    };

    enum class BrepLoopKind {
        Outer,       // bounds the face
        Inner,       // a hole in it
        Unspecified  // the file did not distinguish; the largest is taken as outer
    };

    struct BrepLoop {
        std::vector<BrepCoedge> Coedges;   // ordered, forming a closed circuit
        BrepLoopKind Kind = BrepLoopKind::Unspecified;
    };

// A trimmed patch: a surface, plus the loops that say which part of it is
// material.
//
// SameSense is STEP's advanced_face.same_sense — false when the face's outward
// normal is opposite to the surface's own. Getting it wrong turns a solid
// inside out, so it is stored rather than guessed.
    struct BrepFace {
        int Surface = -1;                  // index into BrepData::Surfaces
        std::vector<int> Loops;            // indices into BrepData::Loops
        bool SameSense = true;
        int Material = -1;                 // index into ModelDocument::Materials
        std::string Name;
        std::map<std::string, std::string> Extras;   // PMI, colours, layer names
    };

    struct BrepShell {
        std::vector<int> Faces;            // indices into BrepData::Faces
        bool Closed = true;                // false for STEP's open_shell / a surface model
        std::string Name;
    };

// A body. Closed means it encloses volume — STEP's manifold_solid_brep, DWG's
// 3DSOLID; open means a sheet or surface model — STEP's
// shell_based_surface_model, DWG's REGION and SURFACE.
    struct BrepSolid {
        std::string Name;
        // The first shell is the outer boundary; any others are internal voids,
        // which is how STEP, ACIS and Parasolid all state a hollow part.
        std::vector<int> Shells;           // indices into BrepData::Shells
        bool Closed = true;
        int Material = -1;                 // document material; face materials win
        std::map<std::string, std::string> Extras;   // product id, PMI, assembly path
    };

// ===== TESSELLATION =====

// How finely to approximate. Both tolerances are honoured; whichever demands
// more subdivision wins, which is the same contract OpenCASCADE, Parasolid and
// every CAD viewer offer.
    struct BrepTessellationOptions {
        // Maximum distance between the mesh and the true surface, in the
        // document's own units. 0 means "derive one from the body's size",
        // which is what a viewer wants and what a mm-vs-metre mix-up needs.
        double ChordTolerance = 0.0;
        // Maximum angle between adjacent facet normals, radians. Bounds the
        // faceting a chord tolerance alone would allow on a large cylinder.
        double AngularTolerance = 0.35;    // ~20 degrees
        // Hard limits, so a pathological body cannot exhaust memory.
        int MinSegmentsPerCurve = 2;
        int MaxSegmentsPerCurve = 256;
        // Emit per-vertex normals from the surface rather than from the
        // triangles: the reason to keep the exact form at all.
        bool GenerateNormals = true;
        // Emit the surface's (u, v) as a texture-coordinate attribute.
        bool GenerateUVs = false;
        // One primitive per face rather than one per material. Larger output,
        // but a face's identity survives, which a CAD viewer's selection needs.
        bool SplitByFace = false;
    };

// ===== THE POOL =====

// Every B-rep body in a document, and the geometry they share.
//
// A ModelDocument owns one of these; a ModelNode points at a solid in it, the
// same way it points at a mesh. So a STEP file that also carries a tessellated
// presentation, or a document that has been tessellated once and kept both,
// holds the exact bodies and the meshes side by side rather than choosing.
    class BrepData {
    public:
        std::vector<BrepCurve> Curves;
        std::vector<BrepCurve2D> Curves2D;
        std::vector<BrepSurface> Surfaces;
        std::vector<BrepVertex> Vertices;
        std::vector<BrepEdge> Edges;
        std::vector<BrepLoop> Loops;
        std::vector<BrepFace> Faces;
        std::vector<BrepShell> Shells;
        std::vector<BrepSolid> Solids;

        bool Empty() const { return Solids.empty() && Faces.empty(); }
        void Clear();

        // --- building ---
        int AddCurve(BrepCurve curve);
        int AddCurve2D(BrepCurve2D curve);
        int AddSurface(BrepSurface surface);
        int AddVertex(const Vec3d& point);
        int AddEdge(BrepEdge edge);
        int AddLoop(BrepLoop loop);
        int AddFace(BrepFace face);
        int AddShell(BrepShell shell);
        int AddSolid(BrepSolid solid);

        // --- queries ---
        Bounds3D ComputeBounds() const;
        Bounds3D SolidBounds(int solidIndex) const;
        size_t FaceCountOfSolid(int solidIndex) const;
        // Curve length by adaptive sampling; the measure a tessellator needs
        // to decide how many segments an edge deserves.
        double EdgeLength(int edgeIndex) const;

        // --- validation ---
        // Structural soundness: every index in range, every loop closed, and —
        // for a solid whose shells are closed — every edge used exactly twice
        // and in opposite directions. Messages are appended to `problems`;
        // returns true when none were.
        //
        // This is the check a B-rep reader owes its caller. A STEP file that
        // references a face's surface by an id the file never defines is not
        // rare, and the failure it causes without this is a wrong mesh rather
        // than an error.
        bool Validate(std::vector<std::string>& problems) const;

        // --- tessellation ---
        // The mesh a face's trimmed surface approximates to, in the units the
        // B-rep is stated in. Positions, normals and (u, v) go into the
        // out-parameters as flat arrays; indices are triangles.
        //
        // Returns false when the face cannot be meshed — an unresolvable
        // surface index, a loop that does not close, a trimming region that
        // degenerates — and appends the reason to `problems` when non-null.
        bool TessellateFace(int faceIndex, const BrepTessellationOptions& options,
                            std::vector<Vec3d>& outPositions,
                            std::vector<Vec3f>& outNormals,
                            std::vector<float>& outUVs,
                            std::vector<uint32_t>& outIndices,
                            std::vector<std::string>* problems = nullptr) const;

        // A working chord tolerance for a body: the option's, or one derived
        // from its bounding box when the option is 0.
        double ResolveChordTolerance(int solidIndex,
                                     const BrepTessellationOptions& options) const;
    };

} // namespace ModelStorage
} // namespace UltraCanvas

#endif // ULTRACANVAS_BREP_STORAGE_H
