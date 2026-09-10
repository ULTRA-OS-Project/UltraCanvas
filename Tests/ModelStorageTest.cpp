// Tests/ModelStorageTest.cpp
// ModelStorage::ModelDocument - the framework's universal 3D scene structure.
// Covers the parts a format converter depends on and would otherwise discover
// the hard way: TRS composition and decomposition, double-precision positions,
// every primitive topology including n-gons and point clouds, custom vertex
// attributes, instancing and transform flattening, up-axis conversion, unit
// bookkeeping, and the Phong <-> PBR material derivation.
//
// Pure geometry and data - no UI stack, no GL, no file format beyond the
// optional STL sample passed as argv[1].
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "DataFormats/UltraCanvasModelStorage.h"
#include "Models/UltraCanvasModelMesh3D.h"
#include "Models/STL/UltraCanvasSTLLoader.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelStorage;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
static bool Near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
static void CheckNear(double actual, double expected, double eps, const std::string& what) {
    const bool ok = Near(actual, expected, eps);
    std::printf("  [%s] %s (%.10g vs %.10g)\n", ok ? "PASS" : "FAIL", what.c_str(),
                actual, expected);
    if (!ok) ++failures;
}

static void TestMath() {
    std::printf("Math\n");
    Quatd q = Quatd::FromAxisAngle(Vec3d(0,0,1), 1.5707963267948966);
    Vec3d r = q.Rotate(Vec3d(1,0,0));
    Check(Near(r.x,0,1e-12) && Near(r.y,1,1e-12) && Near(r.z,0,1e-12), "quaternion rotates X to Y about Z");

    Matrix4x4 m = Matrix4x4::FromTRS(Vec3d(5,6,7), q, Vec3d(2,2,2));
    Vec3d p = m.TransformPoint(Vec3d(1,0,0));
    Check(Near(p.x,5,1e-12) && Near(p.y,8,1e-12) && Near(p.z,7,1e-12), "TRS applies scale, then rotation, then translation");

    Vec3d t2, s2; Quatd r2;
    bool clean = m.DecomposeTRS(t2, r2, s2);
    Check(clean && Near(t2.x,5,1e-9) && Near(s2.x,2,1e-9), "DecomposeTRS recovers translation and scale");
    Matrix4x4 rebuilt = Matrix4x4::FromTRS(t2, r2, s2);
    double worst = 0; for (int i=0;i<16;++i) worst = std::max(worst, std::fabs(rebuilt.m[i]-m.m[i]));
    Check(worst < 1e-9, "recomposed matrix matches the original");

    Matrix4x4 shear; shear.m[4] = 0.5;   // x += 0.5y
    Vec3d st, ss; Quatd sr;
    Check(!shear.DecomposeTRS(st, sr, ss), "DecomposeTRS reports shear it cannot express");

    // Double precision is the reason positions are not float.
    MeshPrimitive far; far.Positions.push_back(Vec3d(1234567.891, 0, 0));
    Check(Near(far.ComputeBounds().Min.x, 1234567.891, 1e-6), "a far-from-origin coordinate keeps sub-micron precision");
}

static void TestTopology() {
    std::printf("Topology\n");
    MeshPrimitive quad;
    quad.Mode = PrimitiveMode::Polygons;
    quad.Positions = {Vec3d(0,0,0), Vec3d(1,0,0), Vec3d(1,1,0), Vec3d(0,1,0)};
    quad.Indices = {0,1,2,3};
    quad.FaceStarts = {0,4};
    Check(quad.FaceCount() == 1 && quad.Face(0).size() == 4, "an n-gon face survives as one 4-vertex face");
    quad.Triangulate();
    Check(quad.Mode == PrimitiveMode::Triangles && quad.FaceCount() == 2, "the quad triangulates into 2 triangles");
    quad.RecomputeNormals();
    Check(quad.Normals.size() == 4 && Near(quad.Normals[0].z, 1.0f, 1e-6), "recomputed normals face +Z");

    MeshPrimitive strip;
    strip.Mode = PrimitiveMode::TriangleStrip;
    strip.Positions = {Vec3d(0,0,0), Vec3d(1,0,0), Vec3d(0,1,0), Vec3d(1,1,0)};
    Check(strip.FaceCount() == 2, "a 4-vertex strip is 2 triangles");
    auto second = strip.Face(1);
    Check(second.size()==3 && second[0]==2 && second[1]==1 && second[2]==3, "odd strip triangles are wound back");

    MeshPrimitive cloud;
    cloud.Mode = PrimitiveMode::Points;
    cloud.Positions = {Vec3d(0,0,0), Vec3d(1,1,1)};
    VertexAttribute intensity; intensity.Semantic = AttributeSemantic::Custom;
    intensity.Name = "intensity"; intensity.Components = 1; intensity.Values = {0.25f, 0.75f};
    cloud.Attributes.push_back(intensity);
    Check(cloud.FaceCount() == 2, "a point cloud reports one face per point");
    Check(cloud.FindAttribute("intensity") != nullptr && cloud.FindAttribute("intensity")->Count() == 2,
          "a format-specific per-vertex property round-trips by name");
}

static void TestSceneGraph() {
    std::printf("Scene graph\n");
    ModelMesh unit;
    MeshPrimitive tri;
    tri.Positions = {Vec3d(0,0,0), Vec3d(1,0,0), Vec3d(0,1,0)};
    tri.Indices = {0,1,2};
    unit.Primitives.push_back(tri);

    ModelDocument doc;
    int mesh = doc.AddMesh(unit);
    ModelNode root; root.Name = "root"; root.Translation = Vec3d(10,0,0);
    int rootIndex = doc.AddNode(root);
    // Same mesh, two nodes: instancing is the normal case in 3D.
    ModelNode a; a.Name = "a"; a.Mesh = mesh; doc.AddNode(a, rootIndex);
    ModelNode b; b.Name = "b"; b.Mesh = mesh; b.Translation = Vec3d(0,5,0); doc.AddNode(b, rootIndex);

    Check(doc.Meshes.size() == 1 && doc.Nodes.size() == 3, "one mesh instanced by two nodes");
    Bounds3D bounds = doc.ComputeBounds();
    Check(Near(bounds.Min.x,10,1e-9) && Near(bounds.Max.y,6,1e-9), "world bounds compose the node hierarchy");

    size_t dropped = 0;
    doc.FlattenTransforms(&dropped);
    Check(doc.Meshes.size() == 2 && doc.Nodes.size() == 2, "flattening resolves the instances into 2 baked meshes");
    Bounds3D flatBounds = doc.ComputeBounds();
    Check(Near(flatBounds.Min.x, bounds.Min.x, 1e-9) && Near(flatBounds.Max.y, bounds.Max.y, 1e-9),
          "flattening preserves world bounds");
}

static void TestUpAxis() {
    std::printf("Up axis and units\n");
    ModelMesh m;
    MeshPrimitive p;
    p.Positions = {Vec3d(0,0,0), Vec3d(0,0,1)};   // a metre "up" in Z-up
    m.Primitives.push_back(p);
    ModelDocument doc = ModelDocument::FromSingleMesh(m, "zup");
    doc.Up = UpAxis::ZUp;
    doc.SourceUnit = ModelUnit::Millimeter;
    doc.UnitScaleToMeters = MetersPerUnit(ModelUnit::Millimeter);

    doc.ConvertUpAxis(UpAxis::YUp);
    Bounds3D b = doc.ComputeBounds();
    Check(doc.Up == UpAxis::YUp && Near(b.Max.y, 1.0, 1e-9), "Z-up geometry becomes Y-up without touching vertices");
    Check(Near(doc.UnitScaleToMeters, 0.001, 1e-12) && std::string(ModelUnitSymbol(doc.SourceUnit)) == "mm",
          "the file's declared unit is recorded, not silently normalised");
}

static void TestMaterials() {
    std::printf("Materials\n");
    ModelMaterial mtl;
    PhongParams phong;
    phong.Diffuse = Vec3f(0.8f, 0.2f, 0.2f);
    phong.Specular = Vec3f(0.05f, 0.05f, 0.05f);
    phong.Shininess = 60.0f;
    mtl.Phong = phong;
    mtl.DeriveMissingModel();
    Check(Near(mtl.BaseColorFactor.x, 0.8f, 1e-6) && mtl.MetallicFactor == 0.0f,
          "an OBJ/MTL Phong material derives a plausible PBR base colour");
    Check(mtl.RoughnessFactor > 0.1f && mtl.RoughnessFactor < 0.3f, "shininess 60 maps to a mid-low roughness");

    // Regression: a white specular means "shiny", not "metal", and it is the
    // most common value in MTL/3DS/COLLADA files. Deriving metalness from the
    // specular alone made every painted surface in the E-45 3DS sample import
    // as raw metal.
    ModelMaterial painted;
    PhongParams shiny;
    shiny.Diffuse = Vec3f(0.8f, 0.8f, 0.8f);
    shiny.Specular = Vec3f(1.0f, 1.0f, 1.0f);
    painted.Phong = shiny;
    painted.DeriveMissingModel();
    Check(painted.MetallicFactor == 0.0f, "a bright diffuse with a white specular is not metal");

    ModelMaterial chrome;
    PhongParams metal;
    metal.Diffuse = Vec3f(0.02f, 0.02f, 0.02f);
    metal.Specular = Vec3f(0.95f, 0.93f, 0.88f);
    chrome.Phong = metal;
    chrome.DeriveMissingModel();
    Check(chrome.MetallicFactor == 1.0f, "a dark diffuse with a bright specular is metal");
    Check(Near(chrome.BaseColorFactor.x, 0.95f, 1e-6),
          "a metal takes its base colour from the specular, not the black diffuse");

    ModelMaterial pbr;
    pbr.BaseColorFactor = Vec4f(0.9f, 0.9f, 0.9f, 1.0f);
    pbr.MetallicFactor = 1.0f;
    pbr.RoughnessFactor = 0.2f;
    pbr.DeriveMissingModel();
    Check(pbr.Phong.has_value() && pbr.Phong->Specular.x > 0.8f,
          "a metal derives a bright specular for the fixed-function writers");
}

static void TestRealFile(const char* path) {
    std::printf("Real file: %s\n", path);
    Mesh3D mesh; std::string err;
    if (!UltraCanvasSTLLoader::Load(path, mesh, &err)) {
        std::printf("  [FAIL] could not load: %s\n", err.c_str());
        ++failures; return;
    }
    const size_t triangles = mesh.TriangleCount();
    const size_t vertices = mesh.VertexCount();
    std::printf("  loaded %zu triangles, %zu vertices\n", triangles, vertices);

    ModelDocument doc = Mesh3DToModelDocument(mesh);
    Check(doc.TotalFaceCount() == triangles, "ModelDocument holds every triangle");
    Check(doc.TotalVertexCount() == vertices, "ModelDocument holds every vertex");

    Bounds3D b = doc.ComputeBounds();
    Check(Near(b.Min.x, mesh.bounds.min.x, 1e-3) && Near(b.Max.z, mesh.bounds.max.z, 1e-3),
          "document bounds match the source mesh bounds");

    const size_t removed = doc.WeldVertices(1e-6);
    std::printf("  welded away %zu duplicate vertices (%.1f%%)\n",
                removed, 100.0 * double(removed) / double(vertices));
    Check(doc.TotalFaceCount() == triangles, "welding preserves the triangle count");
    Bounds3D wb = doc.ComputeBounds();
    Check(Near(wb.Min.x, b.Min.x, 1e-6) && Near(wb.Max.y, b.Max.y, 1e-6), "welding preserves the bounds");

    // Welding is attribute-aware: an STL carries one facet normal per corner,
    // so corners that share a position but not a normal must NOT merge, or the
    // model loses its faceting. Dropping the normals first is what a caller
    // asking for topology (simplification, subdivision) wants, and merges far
    // more.
    ModelDocument positionsOnly = Mesh3DToModelDocument(mesh);
    for (auto& m : positionsOnly.Meshes)
        for (auto& pr : m.Primitives) pr.Normals.clear();
    const size_t removedPositionsOnly = positionsOnly.WeldVertices(1e-6);
    std::printf("  position-only weld removes %zu (%.1f%%)\n", removedPositionsOnly,
                100.0 * double(removedPositionsOnly) / double(vertices));
    Check(removedPositionsOnly > removed * 5,
          "welding respects differing normals; ignoring them merges far more");
    Check(positionsOnly.TotalFaceCount() == triangles, "position-only welding preserves the triangle count");

    Mesh3D back = ModelDocumentToMesh3D(doc);
    Check(back.TriangleCount() == triangles, "converting back yields the same triangle count");
    Check(Near(back.bounds.min.x, mesh.bounds.min.x, 1e-3), "converting back preserves the bounds");
}

// A concave n-gon is the case a fan triangulation gets wrong: fanning about
// the first vertex emits triangles that lie outside the polygon, filling the
// notch in. Ear clipping only ever emits triangles inside it, so the test is
// the area — which a fan overstates by exactly the notch.
static void TestConcaveTriangulation() {
    std::printf("Concave n-gons\n");

    // An L: a 3x3 square with a 2x2 bite out of its top-right corner.
    MeshPrimitive ell;
    ell.Mode = PrimitiveMode::Polygons;
    ell.Positions = {{0, 0, 0}, {3, 0, 0}, {3, 1, 0}, {1, 1, 0}, {1, 3, 0}, {0, 3, 0}};
    ell.Indices = {0, 1, 2, 3, 4, 5};
    ell.FaceStarts = {0, 6};

    Check(ell.FaceCount() == 1, "one six-cornered face");
    Check(ell.Triangulate(), "which triangulates");
    Check(ell.FaceCount() == 4, "into four triangles, as any n-gon of six corners does");

    auto area = [](const MeshPrimitive& prim) {
        double total = 0.0;
        for (size_t i = 0; i + 2 < prim.Indices.size(); i += 3) {
            const Vec3d& a = prim.Positions[prim.Indices[i]];
            const Vec3d& b = prim.Positions[prim.Indices[i + 1]];
            const Vec3d& c = prim.Positions[prim.Indices[i + 2]];
            total += (b - a).Cross(c - a).Length() * 0.5;
        }
        return total;
    };
    // The L's area is 5. A fan about vertex 0 gives 7 — the 3x3 square's 9
    // minus the two triangles it happens to miss — so this number alone
    // separates ear clipping from what was there before.
    CheckNear(area(ell), 5.0, 1e-9, "covering the L's own area, not its convex hull");

    // No triangle may sit in the bite.
    bool inNotch = false;
    for (size_t i = 0; i + 2 < ell.Indices.size(); i += 3) {
        const Vec3d centroid = (ell.Positions[ell.Indices[i]] +
                                ell.Positions[ell.Indices[i + 1]] +
                                ell.Positions[ell.Indices[i + 2]]) * (1.0 / 3.0);
        if (centroid.x > 1.0 && centroid.y > 1.0) inNotch = true;
    }
    Check(!inNotch, "and no triangle in the notch");

    // A convex quad must still come out as the obvious two triangles: the fast
    // path has to agree with the slow one.
    MeshPrimitive quad;
    quad.Mode = PrimitiveMode::Polygons;
    quad.Positions = {{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}};
    quad.Indices = {0, 1, 2, 3};
    quad.FaceStarts = {0, 4};
    quad.Triangulate();
    Check(quad.FaceCount() == 2, "a convex quad still becomes two triangles");
    CheckNear(area(quad), 4.0, 1e-9, "covering its area");

    // Winding must survive: a clockwise face stays clockwise, or a mesh comes
    // back inside out.
    MeshPrimitive clockwise;
    clockwise.Mode = PrimitiveMode::Polygons;
    clockwise.Positions = {{0, 3, 0}, {1, 3, 0}, {1, 1, 0}, {3, 1, 0}, {3, 0, 0}, {0, 0, 0}};
    clockwise.Indices = {0, 1, 2, 3, 4, 5};
    clockwise.FaceStarts = {0, 6};
    clockwise.Triangulate();
    double signedArea = 0.0;
    for (size_t i = 0; i + 2 < clockwise.Indices.size(); i += 3) {
        const Vec3d& a = clockwise.Positions[clockwise.Indices[i]];
        const Vec3d& b = clockwise.Positions[clockwise.Indices[i + 1]];
        const Vec3d& c = clockwise.Positions[clockwise.Indices[i + 2]];
        signedArea += (b - a).Cross(c - a).z * 0.5;
    }
    CheckNear(signedArea, -5.0, 1e-9,
              "a clockwise concave face keeps its winding, so normals do not flip");
}

// Welding used to bucket a quantised coordinate and compare buckets, which
// fails for the one case that matters: two vertices within tolerance whose
// rounded coordinates land in different cells. That happens at every round
// number — which is where CAD geometry sits.
static void TestWeldingAcrossCellBoundaries() {
    std::printf("Welding\n");

    ModelMesh mesh;
    MeshPrimitive prim;
    prim.Mode = PrimitiveMode::Triangles;
    // Two triangles meeting at a seam on x = 1, with the second copy of each
    // shared vertex a nanometre away — on the other side of the lattice
    // boundary that any quantisation by 1e-6 puts exactly at 1.0.
    prim.Positions = {{0, 0, 0}, {1.0, 0, 0}, {1.0, 1, 0},
                      {2, 0, 0}, {1.0 - 1e-9, 0, 0}, {1.0 - 1e-9, 1, 0}};
    prim.Indices = {0, 1, 2, 3, 4, 5};

    ModelDocument document;
    mesh.Primitives.push_back(prim);
    document.AddMesh(mesh);

    const size_t removed = document.WeldVertices(1e-6);
    Check(removed == 2,
          "two vertices a nanometre apart merge even though they straddle a cell boundary");
    Check(document.Meshes[0].Primitives[0].Positions.size() == 4,
          "leaving four distinct vertices");
    Check(document.Meshes[0].Primitives[0].FaceCount() == 2,
          "and both triangles intact");

    // Attributes still gate the merge: a UV seam is a real discontinuity.
    ModelDocument seamed;
    MeshPrimitive withUVs = prim;
    VertexAttribute uv;
    uv.Semantic = AttributeSemantic::TexCoord;
    uv.Components = 2;
    uv.Values = {0, 0,  1, 0,  1, 1,  0, 0,  0.5f, 0,  0.5f, 1};
    withUVs.Attributes.push_back(uv);
    ModelMesh seamedMesh;
    seamedMesh.Primitives.push_back(withUVs);
    seamed.AddMesh(seamedMesh);
    Check(seamed.WeldVertices(1e-6) == 0,
          "but vertices whose texture coordinates differ do not merge");

    // A tolerance of zero must still mean exactly what it did: identical only.
    ModelDocument exact;
    ModelMesh exactMesh;
    exactMesh.Primitives.push_back(prim);
    exact.AddMesh(exactMesh);
    Check(exact.WeldVertices(0.0) == 0,
          "and a zero tolerance merges nothing that is not bit-identical");
}

// Smoothing groups are a per-face statement about which edges crease. Carrying
// them is what lets an OBJ round-trip its `s` statements; honouring them is
// what makes generated normals right.
static void TestSmoothingGroups() {
    std::printf("Smoothing groups\n");

    // Two quads meeting at a right angle along a shared edge — a folded sheet.
    MeshPrimitive fold;
    fold.Mode = PrimitiveMode::Polygons;
    fold.Positions = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                      {1, 0, 1}, {0, 1, 1}};
    // Second face reuses the shared edge's vertices 1 and 2.
    fold.Indices = {0, 1, 2, 3,  1, 4, 5, 2};
    fold.FaceStarts = {0, 4, 8};
    Check(fold.FaceCount() == 2, "a folded sheet of two quads");

    MeshPrimitive smoothed = fold;
    smoothed.SmoothingGroups = {1, 1};      // same group: smooth across the fold
    smoothed.RecomputeNormals();
    Check(smoothed.Positions.size() == 6,
          "faces in the same group share their vertices, so nothing is split");

    MeshPrimitive creased = fold;
    creased.SmoothingGroups = {1, 2};       // no shared bit: crease
    creased.RecomputeNormals();
    Check(creased.Positions.size() == 8,
          "faces in different groups split the two vertices they share, so the fold creases");
    Check(creased.Indices.size() == fold.Indices.size(),
          "the faces still have the same corners, now pointing at the split vertices");

    // The creased normals must be the two face normals, not their average.
    bool sawFaceNormal = false;
    for (const Vec3f& normal : creased.Normals)
        if (std::fabs(normal.z - 1.0f) < 1e-5f || std::fabs(normal.z + 1.0f) < 1e-5f)
            sawFaceNormal = true;
    Check(sawFaceNormal, "and each side keeps its own face normal rather than an average");

    // Triangulation has to carry the masks through, or the round trip loses
    // them the moment a consumer asks for triangles.
    MeshPrimitive triangulated = fold;
    triangulated.SmoothingGroups = {1, 4};
    triangulated.Triangulate();
    Check(triangulated.FaceCount() == 4, "two quads triangulate to four triangles");
    Check(triangulated.SmoothingGroups.size() == 4,
          "and every triangle inherits its face's smoothing group");
    Check(triangulated.SmoothingGroups[0] == 1 && triangulated.SmoothingGroups[1] == 1 &&
          triangulated.SmoothingGroups[2] == 4 && triangulated.SmoothingGroups[3] == 4,
          "in the right order");
}

int main(int argc, char** argv) {
    TestMath();
    TestTopology();
    TestSceneGraph();
    TestUpAxis();
    TestMaterials();
    TestConcaveTriangulation();
    TestWeldingAcrossCellBoundaries();
    TestSmoothingGroups();
    if (argc > 1) TestRealFile(argv[1]);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
