// Tests/BrepStorageTest.cpp
// ModelStorage::BrepData — exact boundary representation, and the tessellator
// that turns it into triangles.
//
// The assertions here are geometric rather than structural, because a B-rep
// that holds the right numbers and meshes them wrongly is worse than one that
// fails: it produces a model that looks plausible and is not the part. So the
// checks are quantities that can only come out right if everything did — the
// signed volume of a meshed box (which is also a test that every face's
// winding points outward), the radial error of a meshed cylinder and sphere
// against their exact radii, and the area of a plate with a hole in it.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasModelStorage.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas::ModelStorage;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
static void CheckNear(double actual, double expected, double tolerance,
                      const std::string& what) {
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("  [%s] %s (%.10g vs %.10g)\n", ok ? "PASS" : "FAIL", what.c_str(),
                actual, expected);
    if (!ok) ++failures;
}

static constexpr double kPi = 3.14159265358979323846;

// ===== BUILDERS =====

// A closed box: 8 vertices, 12 edges, 6 planar faces, each loop wound so the
// face normal points out of the solid.
static int BuildBox(BrepData& brep, double sx, double sy, double sz) {
    const double hx = sx / 2, hy = sy / 2, hz = sz / 2;
    const double corners[8][3] = {
            {-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, hy, -hz}, {-hx, hy, -hz},
            {-hx, -hy,  hz}, {hx, -hy,  hz}, {hx, hy,  hz}, {-hx, hy,  hz}};
    for (const auto& corner : corners) brep.AddVertex(Vec3d(corner[0], corner[1], corner[2]));

    const int ends[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
                             {0,4},{1,5},{2,6},{3,7}};
    int edges[12];
    for (int i = 0; i < 12; ++i) {
        const Vec3d from = brep.Vertices[static_cast<size_t>(ends[i][0])].Point;
        const Vec3d to = brep.Vertices[static_cast<size_t>(ends[i][1])].Point;
        BrepCurve line;
        line.Type = BrepCurveType::Line;
        line.Placement.Origin = from;
        line.Placement.AxisZ = to - from;
        line.TMin = 0.0; line.TMax = 1.0;

        BrepEdge edge;
        edge.Curve = brep.AddCurve(line);
        edge.Start = ends[i][0];
        edge.End = ends[i][1];
        edge.TStart = 0.0; edge.TEnd = 1.0;
        edges[i] = brep.AddEdge(edge);
    }

    struct FaceDef { Vec3d Origin, Normal, RefX; int Edges[4]; bool Forward[4]; };
    const FaceDef definitions[6] = {
            {{0, 0, -hz}, {0, 0, -1}, {1, 0, 0},  {3, 2, 1, 0},   {false,false,false,false}},
            {{0, 0,  hz}, {0, 0,  1}, {1, 0, 0},  {4, 5, 6, 7},   {true, true, true, true}},
            {{0, -hy, 0}, {0, -1, 0}, {1, 0, 0},  {0, 9, 4, 8},   {true, true, false,false}},
            {{ hx, 0, 0}, {1, 0, 0},  {0, 1, 0},  {1, 10, 5, 9},  {true, true, false,false}},
            {{0,  hy, 0}, {0, 1, 0},  {-1, 0, 0}, {2, 11, 6, 10}, {true, true, false,false}},
            {{-hx, 0, 0}, {-1, 0, 0}, {0, -1, 0}, {3, 8, 7, 11},  {true, true, false,false}}};

    BrepShell shell;
    for (const FaceDef& definition : definitions) {
        BrepSurface plane;
        plane.Type = BrepSurfaceType::Plane;
        plane.Placement.Origin = definition.Origin;
        plane.Placement.AxisZ = definition.Normal;
        plane.Placement.AxisX = definition.RefX;
        plane.Placement.Normalize();

        BrepLoop loop;
        loop.Kind = BrepLoopKind::Outer;
        for (int k = 0; k < 4; ++k) {
            BrepCoedge coedge;
            coedge.Edge = edges[definition.Edges[k]];
            coedge.Forward = definition.Forward[k];
            loop.Coedges.push_back(coedge);
        }

        BrepFace face;
        face.Surface = brep.AddSurface(plane);
        face.Loops.push_back(brep.AddLoop(loop));
        shell.Faces.push_back(brep.AddFace(face));
    }
    shell.Closed = true;

    BrepSolid solid;
    solid.Name = "Box";
    solid.Closed = true;
    solid.Shells.push_back(brep.AddShell(shell));
    return brep.AddSolid(solid);
}

// A one-face open body: the side wall of a cylinder, bounded below and above
// by full circles and closed across the seam.
static int BuildTube(BrepData& brep, double radius, double height) {
    BrepSurface side;
    side.Type = BrepSurfaceType::Cylinder;
    side.Radius = radius;
    side.VMin = 0.0; side.VMax = height;
    const int surface = brep.AddSurface(side);

    BrepCurve bottom;
    bottom.Type = BrepCurveType::Circle;
    bottom.Radius = radius;
    BrepCurve top = bottom;
    top.Placement.Origin = Vec3d(0, 0, height);

    const int seamBottom = brep.AddVertex(Vec3d(radius, 0, 0));
    const int seamTop = brep.AddVertex(Vec3d(radius, 0, height));

    BrepEdge lower;
    lower.Curve = brep.AddCurve(bottom);
    lower.Start = seamBottom; lower.End = seamBottom;
    lower.TStart = 0.0; lower.TEnd = 2 * kPi;
    const int lowerEdge = brep.AddEdge(lower);

    BrepEdge upper;
    upper.Curve = brep.AddCurve(top);
    upper.Start = seamTop; upper.End = seamTop;
    upper.TStart = 0.0; upper.TEnd = 2 * kPi;
    const int upperEdge = brep.AddEdge(upper);

    BrepCurve seamLine;
    seamLine.Type = BrepCurveType::Line;
    seamLine.Placement.Origin = Vec3d(radius, 0, 0);
    seamLine.Placement.AxisZ = Vec3d(0, 0, height);
    seamLine.TMin = 0.0; seamLine.TMax = 1.0;
    BrepEdge seam;
    seam.Curve = brep.AddCurve(seamLine);
    seam.Start = seamBottom; seam.End = seamTop;
    seam.TStart = 0.0; seam.TEnd = 1.0;
    const int seamEdge = brep.AddEdge(seam);

    BrepLoop loop;
    loop.Kind = BrepLoopKind::Outer;
    const int useEdges[4] = {lowerEdge, seamEdge, upperEdge, seamEdge};
    const bool forward[4] = {true, true, false, false};
    for (int k = 0; k < 4; ++k) {
        BrepCoedge coedge;
        coedge.Edge = useEdges[k];
        coedge.Forward = forward[k];
        loop.Coedges.push_back(coedge);
    }

    BrepFace face;
    face.Surface = surface;
    face.Loops.push_back(brep.AddLoop(loop));

    BrepShell shell;
    shell.Closed = false;
    shell.Faces.push_back(brep.AddFace(face));

    BrepSolid solid;
    solid.Name = "Tube";
    solid.Closed = false;
    solid.Shells.push_back(brep.AddShell(shell));
    return brep.AddSolid(solid);
}

// ===== MEASURES =====

static double SignedVolume(const ModelDocument& document) {
    double volume = 0.0;
    for (const ModelMesh& mesh : document.Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives)
            for (size_t i = 0; i + 2 < prim.Indices.size(); i += 3) {
                const Vec3d& a = prim.Positions[prim.Indices[i]];
                const Vec3d& b = prim.Positions[prim.Indices[i + 1]];
                const Vec3d& c = prim.Positions[prim.Indices[i + 2]];
                volume += a.Dot(b.Cross(c)) / 6.0;
            }
    return volume;
}

static double SurfaceArea(const ModelDocument& document) {
    double area = 0.0;
    for (const ModelMesh& mesh : document.Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives)
            for (size_t i = 0; i + 2 < prim.Indices.size(); i += 3) {
                const Vec3d& a = prim.Positions[prim.Indices[i]];
                const Vec3d& b = prim.Positions[prim.Indices[i + 1]];
                const Vec3d& c = prim.Positions[prim.Indices[i + 2]];
                area += (b - a).Cross(c - a).Length() * 0.5;
            }
    return area;
}

// ===== TESTS =====

static void TestGeometry() {
    std::printf("Curve and surface evaluation\n");

    BrepCurve circle;
    circle.Type = BrepCurveType::Circle;
    circle.Radius = 4.0;
    CheckNear(circle.Evaluate(0.0).x, 4.0, 1e-12, "a circle starts on its reference axis");
    CheckNear(circle.Evaluate(kPi / 2).y, 4.0, 1e-12, "and is a quarter turn along at pi/2");
    Check(circle.IsClosed(), "a full-range circle reports itself closed");

    // A rational quadratic B-spline with weight 1/sqrt(2) at the middle point
    // is an exact quarter circle. It is the standard test of the rational
    // evaluator, because a non-rational one gets it visibly wrong.
    BrepCurve arc;
    arc.Type = BrepCurveType::BSpline;
    arc.Degree = 2;
    arc.ControlPoints = {Vec3d(4, 0, 0), Vec3d(4, 4, 0), Vec3d(0, 4, 0)};
    arc.Weights = {1.0, std::sqrt(0.5), 1.0};
    arc.Knots = {0, 0, 0, 1, 1, 1};
    double worst = 0.0;
    for (int i = 0; i <= 20; ++i)
        worst = std::max(worst, std::fabs(arc.Evaluate(i / 20.0).Length() - 4.0));
    CheckNear(worst, 0.0, 1e-12,
              "a rational quadratic B-spline reproduces a circular arc exactly");

    // The same control points without weights are a parabola, not an arc —
    // proof the weights are being used rather than ignored.
    BrepCurve polynomial = arc;
    polynomial.Weights.clear();
    Check(std::fabs(polynomial.Evaluate(0.5).Length() - 4.0) > 0.1,
          "and dropping the weights visibly is not an arc, so they are not ignored");

    BrepSurface sphere;
    sphere.Type = BrepSurfaceType::Sphere;
    sphere.Radius = 2.5;
    const std::vector<BrepCurve> noCurves;
    CheckNear(sphere.Evaluate(0.7, 0.3, noCurves).Length(), 2.5, 1e-12,
              "every point of a spherical surface is its radius from the centre");
    const Vec3d normal = sphere.Normal(0.7, 0.3, noCurves);
    CheckNear(normal.Dot(sphere.Evaluate(0.7, 0.3, noCurves).Normalized()), 1.0, 1e-9,
              "and its normal is the outward radius");

    // Projection is the inverse the trimming needs when a file carries no
    // pcurve, so it has to round-trip.
    double u = 0.0, v = 0.0;
    Check(sphere.Project(sphere.Evaluate(1.1, -0.4, noCurves), noCurves, u, v),
          "a point on the sphere projects back into parameter space");
    CheckNear(u, 1.1, 1e-9, "recovering u");
    CheckNear(v, -0.4, 1e-9, "and v");

    BrepSurface torus;
    torus.Type = BrepSurfaceType::Torus;
    torus.Radius = 10.0;
    torus.MinorRadius = 2.0;
    Check(torus.Project(torus.Evaluate(2.0, 1.0, noCurves), noCurves, u, v),
          "a toroidal surface inverts too");
    CheckNear(u, 2.0, 1e-9, "recovering u around the ring");
    CheckNear(v, 1.0, 1e-9, "and v around the tube");
}

static void TestValidation() {
    std::printf("Validation\n");

    BrepData brep;
    BuildBox(brep, 2, 4, 6);
    std::vector<std::string> problems;
    const bool valid = brep.Validate(problems);
    Check(valid, "a well-formed box validates");
    Check(problems.empty(), "with nothing to report");
    for (const std::string& problem : problems) std::printf("      %s\n", problem.c_str());

    // Each of these is a corruption a real file produces, and each has to be
    // named rather than meshed.
    {
        BrepData broken;
        BuildBox(broken, 2, 2, 2);
        broken.Faces[0].Surface = 99;
        std::vector<std::string> found;
        Check(!broken.Validate(found), "a face pointing at a surface that does not exist fails");
    }
    {
        BrepData broken;
        BuildBox(broken, 2, 2, 2);
        // Break the circuit: the first loop's second coedge now starts
        // somewhere its predecessor does not end.
        broken.Loops[0].Coedges[1].Edge = broken.Loops[1].Coedges[0].Edge;
        std::vector<std::string> found;
        Check(!broken.Validate(found), "a loop that does not close fails");
    }
    {
        BrepData broken;
        BuildBox(broken, 2, 2, 2);
        // Reverse one coedge: the edge is now traversed the same way twice, so
        // the two faces sharing it disagree about which side is outside.
        broken.Loops[1].Coedges[0].Forward = !broken.Loops[1].Coedges[0].Forward;
        std::vector<std::string> found;
        Check(!broken.Validate(found),
              "an edge used twice in the same direction fails the closed-shell check");
    }
    {
        BrepData sheet;
        BuildTube(sheet, 3, 5);
        std::vector<std::string> found;
        Check(sheet.Validate(found),
              "an open shell is not held to that rule — a sheet body is legitimate");
    }
}

static void TestBoxTessellation() {
    std::printf("Tessellating a box\n");

    ModelDocument document;
    BuildBox(document.Brep, 2, 4, 6);

    Check(!document.Empty(),
          "a document with only B-rep bodies is not empty — it is exact, not blank");
    Check(document.Meshes.empty(), "and holds no mesh until one is asked for");

    const Bounds3D bounds = document.Brep.ComputeBounds();
    CheckNear(bounds.Size().x, 2.0, 1e-12, "the body's bounds come from its geometry");
    CheckNear(bounds.Size().z, 6.0, 1e-12, "in every axis");

    const size_t meshed = document.TessellateBreps();
    Check(meshed == 1, "one solid tessellates");
    Check(document.Meshes.size() == 1, "into one mesh");

    // A box is flat everywhere, so no tolerance can justify more than two
    // triangles a face.
    Check(document.TotalFaceCount() == 12,
          "a six-faced box needs exactly twelve triangles, whatever the tolerance");
    CheckNear(SignedVolume(document), 48.0, 1e-9,
              "the signed volume is the box's, so every face is wound outward");
    CheckNear(SurfaceArea(document), 2 * (2 * 4 + 2 * 6 + 4 * 6), 1e-9,
              "and the area is the box's");

    Check(!document.Nodes.empty() && document.Nodes[0].Solid == 0,
          "the node placing the mesh still points at the solid it came from");
    Check(document.Brep.Solids.size() == 1,
          "and the exact body is still there — tessellation adds, it does not replace");

    // Re-running must not double the geometry: the node already has its mesh.
    const size_t again = document.TessellateBreps();
    Check(again == 0 && document.Meshes.size() == 1,
          "tessellating twice does not mesh the same solid twice");
}

static void TestCurvedTessellation() {
    std::printf("Tessellating curved faces to a tolerance\n");

    for (double tolerance : {0.1, 0.01, 0.001}) {
        ModelDocument document;
        BuildTube(document.Brep, 5.0, 10.0);
        BrepTessellationOptions options;
        options.ChordTolerance = tolerance;
        std::vector<std::string> problems;
        document.TessellateBreps(options, &problems);

        double worst = 0.0;
        for (const ModelMesh& mesh : document.Meshes)
            for (const MeshPrimitive& prim : mesh.Primitives)
                for (const Vec3d& p : prim.Positions)
                    worst = std::max(worst, std::fabs(std::sqrt(p.x * p.x + p.y * p.y) - 5.0));

        // Every vertex is evaluated on the exact surface, so it is on the
        // cylinder to the last bit; the tolerance governs the chords between
        // them, which is what the area below measures.
        CheckNear(worst, 0.0, 1e-9,
                  "at tolerance " + std::to_string(tolerance) +
                  ", every vertex is exactly on the cylinder");
        Check(problems.empty(),
              "and the tolerance is reached without hitting a refinement limit");

        // A chord approximation of a circle is always inside it, so the meshed
        // area is under the true one and approaches it as the tolerance tightens.
        const double exact = 2 * kPi * 5.0 * 10.0;
        const double meshed = SurfaceArea(document);
        Check(meshed <= exact + 1e-9 && meshed >= exact * (1.0 - 20.0 * tolerance),
              "the meshed area brackets the exact one from below");
        std::printf("      tolerance %g: %zu triangles, area %.4f of %.4f\n",
                    tolerance, document.TotalFaceCount(), meshed, exact);
    }

    // Finer must mean more triangles, or the tolerance is not doing anything.
    size_t counts[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        ModelDocument document;
        BuildTube(document.Brep, 5.0, 10.0);
        BrepTessellationOptions options;
        options.ChordTolerance = i == 0 ? 0.1 : 0.005;
        document.TessellateBreps(options);
        counts[i] = document.TotalFaceCount();
    }
    Check(counts[1] > counts[0] * 2,
          "halving the tolerance twenty-fold more than doubles the triangle count");
}

static void TestTrimmedFaceWithHole() {
    std::printf("A trimmed face with a hole\n");

    ModelDocument document;
    BrepData& brep = document.Brep;

    BrepSurface plane;
    plane.Type = BrepSurfaceType::Plane;
    const int surface = brep.AddSurface(plane);

    // Two rings on the same plane: a 10 x 10 outer square and a 2 x 2 hole,
    // wound the opposite way as a hole must be.
    auto addRing = [&brep](const std::vector<Vec3d>& points) {
        BrepLoop loop;
        std::vector<int> vertices;
        for (const Vec3d& point : points) vertices.push_back(brep.AddVertex(point));
        for (size_t i = 0; i < vertices.size(); ++i) {
            const size_t next = (i + 1) % vertices.size();
            BrepCurve line;
            line.Type = BrepCurveType::Line;
            line.Placement.Origin = points[i];
            line.Placement.AxisZ = points[next] - points[i];
            line.TMin = 0.0; line.TMax = 1.0;

            BrepEdge edge;
            edge.Curve = brep.AddCurve(line);
            edge.Start = vertices[i];
            edge.End = vertices[next];
            edge.TStart = 0.0; edge.TEnd = 1.0;

            BrepCoedge coedge;
            coedge.Edge = brep.AddEdge(edge);
            loop.Coedges.push_back(coedge);
        }
        return loop;
    };

    BrepLoop outer = addRing({{-5, -5, 0}, {5, -5, 0}, {5, 5, 0}, {-5, 5, 0}});
    outer.Kind = BrepLoopKind::Outer;
    BrepLoop inner = addRing({{-1, -1, 0}, {-1, 1, 0}, {1, 1, 0}, {1, -1, 0}});
    inner.Kind = BrepLoopKind::Inner;

    BrepFace face;
    face.Surface = surface;
    face.Loops.push_back(brep.AddLoop(outer));
    face.Loops.push_back(brep.AddLoop(inner));

    BrepShell shell;
    shell.Closed = false;
    shell.Faces.push_back(brep.AddFace(face));
    BrepSolid solid;
    solid.Closed = false;
    solid.Shells.push_back(brep.AddShell(shell));
    brep.AddSolid(solid);

    document.TessellateBreps();
    CheckNear(SurfaceArea(document), 100.0 - 4.0, 1e-9,
              "the hole is cut out of the plate rather than meshed over");
    Check(document.TotalFaceCount() >= 8,
          "and the region is triangulated, not covered by a single quad");

    // No triangle may have its centroid inside the hole. Area alone could be
    // right with overlapping triangles, which is exactly what a naive
    // triangulation of a bridged polygon produces.
    bool insideHole = false;
    for (const ModelMesh& mesh : document.Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives)
            for (size_t i = 0; i + 2 < prim.Indices.size(); i += 3) {
                const Vec3d centroid = (prim.Positions[prim.Indices[i]] +
                                        prim.Positions[prim.Indices[i + 1]] +
                                        prim.Positions[prim.Indices[i + 2]]) * (1.0 / 3.0);
                if (std::fabs(centroid.x) < 1.0 && std::fabs(centroid.y) < 1.0)
                    insideHole = true;
            }
    Check(!insideHole, "and no triangle lies inside the hole");
}

static void TestNurbsSurface() {
    std::printf("A NURBS surface\n");

    ModelDocument document;
    BrepData& brep = document.Brep;

    // A bicubic patch bowed up in the middle: the general case, with no
    // analytic inverse, so the trimming has to project numerically.
    BrepSurface patch;
    patch.Type = BrepSurfaceType::BSpline;
    patch.DegreeU = 3; patch.DegreeV = 3;
    patch.ControlPointsU = 4; patch.ControlPointsV = 4;
    patch.KnotsU = {0, 0, 0, 0, 1, 1, 1, 1};
    patch.KnotsV = patch.KnotsU;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) {
            const double x = column - 1.5, y = row - 1.5;
            const double z = (column == 1 || column == 2) && (row == 1 || row == 2) ? 2.0 : 0.0;
            patch.ControlNet.emplace_back(x, y, z);
        }

    const std::vector<BrepCurve> noCurves;
    // A clamped B-spline interpolates its corner control points exactly.
    const Vec3d corner = patch.Evaluate(0.0, 0.0, noCurves);
    CheckNear(corner.x, -1.5, 1e-12, "a clamped patch passes through its corner control point");
    CheckNear(corner.y, -1.5, 1e-12, "in both axes");
    Check(patch.Evaluate(0.5, 0.5, noCurves).z > 0.5,
          "and bows towards the raised interior control points");

    double u = 0.0, v = 0.0;
    const Vec3d onSurface = patch.Evaluate(0.3, 0.7, noCurves);
    Check(patch.Project(onSurface, noCurves, u, v),
          "a point on the patch projects back, without an analytic inverse");
    Check((patch.Evaluate(u, v, noCurves) - onSurface).Length() < 1e-4,
          "and the recovered parameters land back on the same point");

    BrepFace face;
    face.Surface = brep.AddSurface(patch);   // untrimmed: the whole patch
    BrepShell shell;
    shell.Closed = false;
    shell.Faces.push_back(brep.AddFace(face));
    BrepSolid solid;
    solid.Name = "Patch";
    solid.Closed = false;
    solid.Shells.push_back(brep.AddShell(shell));
    brep.AddSolid(solid);

    BrepTessellationOptions options;
    options.ChordTolerance = 0.01;
    options.GenerateUVs = true;
    std::vector<std::string> problems;
    Check(document.TessellateBreps(options, &problems) == 1,
          "an untrimmed NURBS patch meshes from its natural parameter range");

    double worst = 0.0;
    size_t vertices = 0;
    for (const ModelMesh& mesh : document.Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives) {
            vertices += prim.Positions.size();
            Check(prim.Normals.size() == prim.Positions.size(),
                  "every vertex gets a normal from the surface, not from its triangles");
            const VertexAttribute* uv = prim.FindAttribute(AttributeSemantic::TexCoord, 0);
            Check(uv != nullptr && uv->Count() == prim.Positions.size(),
                  "and its (u, v), when asked for");
            for (size_t i = 0; i + 2 < prim.Indices.size(); i += 3) {
                const Vec3d middle = (prim.Positions[prim.Indices[i]] +
                                      prim.Positions[prim.Indices[i + 1]]) * 0.5;
                double pu = 0.0, pv = 0.0;
                if (patch.Project(middle, noCurves, pu, pv))
                    worst = std::max(worst,
                                     (patch.Evaluate(pu, pv, noCurves) - middle).Length());
            }
        }
    std::printf("      %zu vertices, worst edge-midpoint deviation %.6f\n", vertices, worst);
    Check(worst <= 0.01 + 1e-6,
          "and every edge midpoint is within the chord tolerance of the true surface");
}

static void TestMaterialsAndInstancing() {
    std::printf("Materials and instancing\n");

    ModelDocument document;
    ModelMaterial red;
    red.Name = "Red";
    red.BaseColorFactor = Vec4f(1, 0, 0, 1);
    const int redIndex = document.AddMaterial(red);

    const int solid = BuildBox(document.Brep, 2, 2, 2);
    document.Brep.Solids[static_cast<size_t>(solid)].Material = redIndex;
    // One face overrides the body's material, which is how a CAD file colours
    // a single pocket differently.
    ModelMaterial blue;
    blue.Name = "Blue";
    const int blueIndex = document.AddMaterial(blue);
    document.Brep.Faces[0].Material = blueIndex;

    // Two nodes, one solid: the instancing case.
    ModelNode first;
    first.Name = "Left";
    first.Translation = Vec3d(-5, 0, 0);
    first.Solid = solid;
    document.AddNode(std::move(first));
    ModelNode second;
    second.Name = "Right";
    second.Translation = Vec3d(5, 0, 0);
    second.Solid = solid;
    document.AddNode(std::move(second));

    Check(document.TessellateBreps() == 2, "both nodes get a mesh");
    Check(document.Meshes.size() == 1,
          "but the solid is meshed once and shared, as an instanced mesh is");
    Check(document.Meshes[0].Primitives.size() == 2,
          "the face with its own material becomes its own primitive");

    bool sawBlue = false, sawRed = false;
    for (const MeshPrimitive& prim : document.Meshes[0].Primitives) {
        if (prim.Material == blueIndex) sawBlue = true;
        if (prim.Material == redIndex) sawRed = true;
    }
    Check(sawBlue && sawRed, "and each primitive carries the material it should");

    const Bounds3D bounds = document.ComputeBounds();
    CheckNear(bounds.Size().x, 12.0, 1e-9,
              "the two instances are placed by their nodes' transforms");
}

int main() {
    TestGeometry();
    TestValidation();
    TestBoxTessellation();
    TestCurvedTessellation();
    TestTrimmedFaceWithHole();
    TestNurbsSurface();
    TestMaterialsAndInstancing();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
