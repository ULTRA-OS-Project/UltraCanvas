// Tests/ModelX3DTest.cpp
// The X3D reader.
//
// The sample is the same E-45 aircraft every other model suite reads, which
// makes it a cross-format check as much as a reader check: the X3D export
// carries exactly the OBJ export's 8110 quads, and unlike the .dae it has the
// mirror modifier applied. Asserting both stops a later change quietly
// altering either.
//
// The classic encoding gets its own section, and its central assertion is the
// one that says the split was worth making: the *same scene* written as XML and
// as classic VRML must produce the same document, field for field. Two
// encodings of one node set is a claim a reader can either honour or quietly
// break, and only reading both proves which.
//
// The synthetic cases cover what one export cannot reach: DEF/USE instancing,
// the transform composition the spec defines (which is not a TRS triple),
// per-face colours, reversed winding, the Immersive profile's geometric
// primitives, and the ROUTE plumbing that is X3D's whole animation model.
//
// The reader takes both of the standard's text encodings, so the suite does
// too: argv[1] is the .x3d and argv[2] the .wrl. Without them only the
// synthetic cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/X3D/UltraCanvasX3DConverter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelStorage;
using namespace UltraCanvas::ModelConverter;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
static bool Near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

static std::vector<uint8_t> Bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

// An X3D document around the caller's <Scene> body.
static std::string Scene(const std::string& body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<X3D version=\"3.0\" profile=\"Immersive\"><head/><Scene>" + body +
           "</Scene></X3D>";
}

// One unit square as an IndexedFaceSet, with the caller's extra fields.
static std::string Square(const std::string& fields = "", const std::string& extra = "") {
    return "<Shape><IndexedFaceSet coordIndex=\"0 1 2 3 -1\" " + fields + ">"
           "<Coordinate point=\"0 0 0, 1 0 0, 1 1 0, 0 1 0\"/>" + extra +
           "</IndexedFaceSet></Shape>";
}

static std::shared_ptr<ModelDocument> Read(const std::string& text,
                                           std::vector<std::string>* warnings = nullptr) {
    X3DConverter converter;
    ConversionOptions options;
    if (warnings)
        options.WarningCallback = [warnings](const std::string& w) { warnings->push_back(w); };
    return converter.ImportFromMemory(Bytes(text), options);
}

static void TestValidation() {
    std::printf("Validation\n");
    X3DConverter converter;
    Check(!converter.ValidateData({}), "empty data is rejected");
    Check(!converter.ValidateData(Bytes("<svg xmlns=\"http://www.w3.org/2000/svg\"/>")),
          "an SVG is rejected");
    Check(!converter.ValidateData(Bytes("<COLLADA version=\"1.4.1\"/>")),
          "a COLLADA document is rejected");
    Check(converter.ValidateData(Bytes(Scene(Square()))), "an X3D document is accepted");
    Check(converter.ValidateData(Bytes(
                  "<!DOCTYPE X3D PUBLIC \"ISO//Web3D//DTD X3D 3.0//EN\" "
                  "\"http://www.web3d.org/specifications/x3d-3.0.dtd\">")),
          "the DOCTYPE alone identifies it");
    // The classic encoding is the same node set in a different syntax, and both
    // are read. Recognition is by content: the mandatory header line names it.
    Check(converter.ValidateData(Bytes("#VRML V2.0 utf8\nShape { geometry Box {} }")),
          "the VRML97 classic encoding is accepted");
    Check(converter.ValidateData(Bytes("#X3D V3.3 utf8\nShape { geometry Box {} }")),
          "and so is X3D's own classic encoding");
    Check(!converter.ValidateData(Bytes("# just a shell script\nexit 0\n")),
          "a file that merely starts with a comment is not");

    std::vector<std::string> warnings;
    Check(Read("<X3D><head/></X3D>", &warnings) == nullptr, "an X3D with no <Scene> reads as nothing");
    Check(Read("<Scene><Shape/></Scene>", &warnings) == nullptr,
          "a document whose root is not <X3D> reads as nothing");
    Check(Read("<X3D><Scene><Shape>", &warnings) == nullptr, "malformed XML reads as nothing");
    Check(warnings.size() >= 3, "and each says why");
}

// The property most likely to be got wrong: an IndexedFaceSet's index streams
// are parallel and independent, so a corner is the tuple of all of them.
static void TestIndexedFaceSet() {
    std::printf("IndexedFaceSet\n");

    // A quad and a triangle sharing vertices. The n-gon must survive as one
    // face rather than becoming two triangles.
    auto mixed = Read(Scene(
            "<Shape><IndexedFaceSet coordIndex=\"0 1 2 3 -1 1 4 2 -1\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 1 1 0, 0 1 0, 2 0 0\"/>"
            "</IndexedFaceSet></Shape>"));
    Check(mixed != nullptr, "a mixed quad and triangle parses");
    if (mixed) {
        const MeshPrimitive& prim = mixed->Meshes[0].Primitives[0];
        Check(prim.Mode == PrimitiveMode::Polygons, "it uses Polygons mode");
        Check(prim.FaceCount() == 2 && prim.Face(0).size() == 4 && prim.Face(1).size() == 3,
              "the quad stays a quad and the triangle a triangle");
        Check(prim.VertexCount() == 5, "with no index stream to disagree, vertices are shared");
    }

    // The same quad with a texCoordIndex that does not match coordIndex. Every
    // corner is now a distinct (position, uv) pair, so the four shared corners
    // become four vertices - and a fifth position that two faces shared has to
    // split.
    auto split = Read(Scene(
            "<Shape><IndexedFaceSet coordIndex=\"0 1 2 3 -1 1 4 2 -1\" "
            "texCoordIndex=\"0 1 2 3 -1 4 5 6 -1\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 1 1 0, 0 1 0, 2 0 0\"/>"
            "<TextureCoordinate point=\"0 0, 1 0, 1 1, 0 1, 0 0, 1 0, 1 1\"/>"
            "</IndexedFaceSet></Shape>"));
    Check(split != nullptr, "a disagreeing texCoordIndex parses");
    if (split) {
        const MeshPrimitive& prim = split->Meshes[0].Primitives[0];
        Check(prim.VertexCount() == 7, "each corner tuple becomes its own vertex (4 + 3)");
        const VertexAttribute* uv = prim.FindAttribute(AttributeSemantic::TexCoord, 0);
        Check(uv && uv->Count() == prim.VertexCount(),
              "and the texture coordinates stay parallel to the vertices");
    }

    // ccw="false" says the corners are wound clockwise. The document assumes
    // counter-clockwise, so they have to be reversed - which is visible as the
    // face normal flipping, not as any change in the vertex set.
    auto clockwise = Read(Scene(Square("ccw=\"false\"")));
    auto counter = Read(Scene(Square()));
    Check(clockwise && counter, "both windings parse");
    if (clockwise && counter) {
        const Vec3f a = clockwise->Meshes[0].Primitives[0].Normals[0];
        const Vec3f b = counter->Meshes[0].Primitives[0].Normals[0];
        Check(Near(a.z, -b.z, 1e-6) && std::fabs(b.z) > 0.9f,
              "ccw=\"false\" reverses the winding, so the generated normal flips");
    }

    // Colours declared per face rather than per vertex: the stream is indexed
    // by the face counter, not by the corner.
    auto perFace = Read(Scene(
            "<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1 1 3 2 -1\" colorPerVertex=\"false\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 0 1 0, 1 1 0\"/>"
            "<Color color=\"1 0 0, 0 1 0\"/>"
            "</IndexedFaceSet></Shape>"));
    Check(perFace != nullptr, "colorPerVertex=\"false\" parses");
    if (perFace) {
        const MeshPrimitive& prim = perFace->Meshes[0].Primitives[0];
        const VertexAttribute* colors = prim.FindAttribute(AttributeSemantic::Color, 0);
        Check(colors && colors->Components == 3, "a Color stream arrives");
        if (colors) {
            const std::vector<uint32_t> first = prim.Face(0);
            const std::vector<uint32_t> second = prim.Face(1);
            bool firstRed = true, secondGreen = true;
            for (uint32_t v : first)
                if (!Near(colors->Values[v * 3], 1.0, 1e-6)) firstRed = false;
            for (uint32_t v : second)
                if (!Near(colors->Values[v * 3 + 1], 1.0, 1e-6)) secondGreen = false;
            Check(firstRed && secondGreen,
                  "every corner of face 0 is red and every corner of face 1 is green");
        }
    }

    // An index past the end of the Coordinate is a corrupt file, not a reason
    // to read a truncated face.
    std::vector<std::string> warnings;
    auto broken = Read(Scene(
            "<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1 0 1 9 -1\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 0 1 0\"/></IndexedFaceSet></Shape>"), &warnings);
    Check(broken && broken->Meshes[0].Primitives[0].FaceCount() == 1,
          "a face indexing past the coordinates is dropped, not truncated");
    Check(!warnings.empty(), "and the reader says so");
}

// The spec composes a Transform as T * C * R * SR * S * -SR * -C. A reader
// that copies translation, rotation and scale into three slots puts anything
// with a `center` in the wrong place.
static void TestTransform() {
    std::printf("Transform composition\n");

    auto plain = Read(Scene("<Transform translation=\"1 2 3\" scale=\"2 2 2\" "
                            "rotation=\"0 1 0 1.5707963\">" + Square() + "</Transform>"));
    Check(plain != nullptr, "a plain Transform parses");
    if (plain) {
        const ModelNode& node = plain->Nodes[0];
        Check(Near(node.Translation.x, 1.0, 1e-9) && Near(node.Translation.z, 3.0, 1e-9),
              "translation survives as translation");
        Check(Near(node.Scale.y, 2.0, 1e-9), "and scale as scale");
        const Vec3d turned = node.Rotation.Rotate(Vec3d(1.0, 0.0, 0.0));
        Check(Near(turned.z, -1.0, 1e-6),
              "the axis-angle rotation becomes the quaternion that turns +X onto -Z");
    }

    // A rotation about a centre one unit away moves the geometry; a reader
    // with fixed slots would leave it at the origin.
    auto centered = Read(Scene("<Transform center=\"1 0 0\" rotation=\"0 0 1 3.1415927\">"
                               "<Shape><IndexedFaceSet coordIndex=\"0 1 2 -1\">"
                               "<Coordinate point=\"0 0 0, 1 0 0, 0 1 0\"/>"
                               "</IndexedFaceSet></Shape></Transform>"));
    Check(centered != nullptr, "a Transform with a centre parses");
    if (centered) {
        const Bounds3D bounds = centered->ComputeBounds();
        // Turning the triangle half a turn about (1,0,0) puts its origin
        // corner at (2,0,0) and its (1,0,0) corner at (1,0,0).
        Check(Near(bounds.Max.x, 2.0, 1e-6) && Near(bounds.Min.x, 1.0, 1e-6),
              "the rotation happens about the stated centre, not about the origin");
    }
}

// DEF/USE is the whole instancing mechanism, and it works on geometry and on
// whole subtrees alike.
static void TestInstancing() {
    std::printf("DEF/USE instancing\n");

    auto shared = Read(Scene(
            "<Transform translation=\"0 0 0\"><Shape>"
            "<IndexedFaceSet DEF=\"Face\" coordIndex=\"0 1 2 -1\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 0 1 0\"/></IndexedFaceSet></Shape></Transform>"
            "<Transform translation=\"5 0 0\"><Shape>"
            "<IndexedFaceSet USE=\"Face\"/></Shape></Transform>"));
    Check(shared != nullptr, "a USE'd geometry parses");
    if (shared) {
        Check(shared->Meshes.size() == 1, "one geometry USE'd twice is one mesh");
        int placements = 0;
        for (const ModelNode& node : shared->Nodes)
            if (node.Mesh == 0) ++placements;
        Check(placements == 2, "placed by two nodes");
        const Bounds3D bounds = shared->ComputeBounds();
        Check(Near(bounds.Max.x, 6.0, 1e-6), "and the second placement carries its transform");
    }

    // A USE of a whole Transform re-expands its subtree.
    auto subtree = Read(Scene(
            "<Transform DEF=\"Arm\" translation=\"1 0 0\"><Shape>"
            "<IndexedFaceSet coordIndex=\"0 1 2 -1\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 0 1 0\"/></IndexedFaceSet></Shape></Transform>"
            "<Transform translation=\"0 10 0\"><Transform USE=\"Arm\"/></Transform>"));
    Check(subtree != nullptr, "a USE'd subtree parses");
    if (subtree) {
        const Bounds3D bounds = subtree->ComputeBounds();
        Check(Near(bounds.Max.y, 11.0, 1e-6),
              "the reused subtree is placed under its new parent, not at its original position");
    }

    // A USE naming nothing, and a USE of the node it sits inside, are both
    // file errors that must not take the reader with them.
    std::vector<std::string> warnings;
    auto dangling = Read(Scene(Square() + "<Transform USE=\"Nothing\"/>"), &warnings);
    Check(dangling != nullptr && !warnings.empty(),
          "a dangling USE is reported and the rest of the file still reads");
    warnings.clear();
    auto cyclic = Read(Scene("<Transform DEF=\"Loop\">" + Square() +
                             "<Transform USE=\"Loop\"/></Transform>"), &warnings);
    Check(cyclic != nullptr && !warnings.empty(),
          "a self-referential USE is reported rather than expanded forever");
}

static void TestAppearance() {
    std::printf("Appearance\n");

    auto textured = Read(Scene(
            "<Shape><Appearance>"
            "<ImageTexture DEF=\"Tex\" url='\"textures/hull.png\" \"hull.png\" "
            "\"C:/elsewhere/hull.png\"'/>"
            "<Material diffuseColor=\"0.2 0.4 0.6\" specularColor=\"1 1 1\" "
            "ambientIntensity=\"0.5\" shininess=\"0.25\" transparency=\"0.25\"/>"
            "</Appearance><IndexedFaceSet solid=\"false\" coordIndex=\"0 1 2 -1\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 0 1 0\"/></IndexedFaceSet></Shape>"));
    Check(textured != nullptr, "an Appearance parses");
    if (textured) {
        Check(textured->Materials.size() == 1, "one material");
        const ModelMaterial& material = textured->Materials[0];
        Check(material.Phong.has_value(), "stated in fixed-function terms");
        if (material.Phong) {
            Check(Near(material.Phong->Diffuse.x, 0.2, 1e-6) &&
                  Near(material.Phong->Diffuse.z, 0.6, 1e-6), "diffuseColor is the diffuse");
            // X3D states ambient as an intensity multiplying the diffuse.
            Check(Near(material.Phong->Ambient.z, 0.3, 1e-6),
                  "ambientIntensity times diffuseColor is the ambient colour");
            // shininess is the OpenGL exponent over 128; Ns is the exponent.
            Check(Near(material.Phong->Shininess, 32.0, 1e-4),
                  "shininess 0.25 becomes an Ns of 32");
        }
        Check(Near(material.BaseColorFactor.w, 0.75, 1e-6) &&
              material.Alpha == AlphaMode::Blend,
              "transparency becomes opacity, and the material blends");
        Check(material.DoubleSided, "solid=\"false\" on the geometry makes the material two-sided");
        Check(textured->Images.size() == 1 && textured->Images[0].Uri == "textures/hull.png" &&
              textured->Images[0].MimeType == "image/png",
              "the first url of the MFString fallback list is the texture");
        Check(material.BaseColorTexture.Image == 0, "and it reaches the base colour slot");
        auto alternates = textured->Metadata.find("x3d.imageTexture.Tex.alternateUrls");
        Check(alternates != textured->Metadata.end() &&
              alternates->second.find("C:/elsewhere/hull.png") != std::string::npos,
              "the remaining urls are kept rather than dropped");
    }

    // The same Appearance DEF'd once and USE'd twice is one material.
    auto reused = Read(Scene(
            "<Shape><Appearance DEF=\"A\"><Material diffuseColor=\"1 0 0\"/></Appearance>"
            "<IndexedFaceSet coordIndex=\"0 1 2 -1\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 0 1 0\"/></IndexedFaceSet></Shape>"
            "<Shape><Appearance USE=\"A\"/>"
            "<IndexedFaceSet coordIndex=\"0 1 2 -1\">"
            "<Coordinate point=\"0 0 0, 2 0 0, 0 2 0\"/></IndexedFaceSet></Shape>"));
    Check(reused && reused->Materials.size() == 1,
          "an Appearance USE'd twice is one material, not two");
}

// Box, Sphere, Cylinder and Cone are real geometry in X3D, and a hand-written
// scene is often nothing else.
static void TestPrimitives() {
    std::printf("Geometric primitives\n");

    auto box = Read(Scene("<Shape><Box size=\"2 4 6\"/></Shape>"));
    Check(box != nullptr, "a Box parses");
    if (box) {
        const MeshPrimitive& prim = box->Meshes[0].Primitives[0];
        Check(prim.FaceCount() == 6, "six faces");
        Check(prim.Face(0).size() == 4, "each a quad, not a pair of triangles");
        const Bounds3D bounds = box->ComputeBounds();
        Check(Near(bounds.Max.x, 1.0, 1e-9) && Near(bounds.Max.y, 2.0, 1e-9) &&
              Near(bounds.Max.z, 3.0, 1e-9), "size is the full extent, so the box is half of it");
        // Every face normal must point away from the centre, or the box is
        // inside out - the one property a winding mistake always breaks.
        bool outward = true;
        for (size_t f = 0; f < prim.FaceCount(); ++f) {
            const std::vector<uint32_t> corners = prim.Face(f);
            Vec3d centre(0.0, 0.0, 0.0);
            for (uint32_t c : corners) centre += prim.Positions[c];
            centre = centre * (1.0 / static_cast<double>(corners.size()));
            const Vec3f normal = prim.Normals[corners[0]];
            if (centre.Dot(Vec3d(normal.x, normal.y, normal.z)) <= 0.0) outward = false;
        }
        Check(outward, "and every face is wound so its normal points outward");
    }

    auto sphere = Read(Scene("<Shape><Sphere radius=\"3\"/></Shape>"));
    Check(sphere != nullptr, "a Sphere parses");
    if (sphere) {
        const MeshPrimitive& prim = sphere->Meshes[0].Primitives[0];
        Check(prim.FaceCount() == 288, "24 segments by 12 rings");
        Check(prim.Face(0).size() == 3 && prim.Face(prim.FaceCount() - 1).size() == 3,
              "the pole rings are triangles, not quads with a doubled corner");
        const Bounds3D bounds = sphere->ComputeBounds();
        Check(Near(bounds.Max.y, 3.0, 1e-9) && Near(bounds.Min.y, -3.0, 1e-9),
              "the radius is honoured on the axis, where the tessellation is exact");
        bool outward = true;
        for (size_t v = 0; v < prim.VertexCount(); ++v) {
            const Vec3f normal = prim.Normals[v];
            if (prim.Positions[v].Dot(Vec3d(normal.x, normal.y, normal.z)) < 0.0) outward = false;
        }
        Check(outward, "and every normal points away from the centre");
    }

    auto cylinder = Read(Scene("<Shape><Cylinder radius=\"1\" height=\"4\"/></Shape>"));
    Check(cylinder != nullptr, "a Cylinder parses");
    if (cylinder) {
        const MeshPrimitive& prim = cylinder->Meshes[0].Primitives[0];
        Check(prim.FaceCount() == 26, "24 side quads and two caps");
        size_t discs = 0;
        for (size_t f = 0; f < prim.FaceCount(); ++f)
            if (prim.Face(f).size() == 24) ++discs;
        Check(discs == 2, "the caps are 24-sided n-gons, not fans of triangles");
        const Bounds3D bounds = cylinder->ComputeBounds();
        Check(Near(bounds.Max.y, 2.0, 1e-9), "height is the full extent");
    }

    auto capless = Read(Scene("<Shape><Cylinder top=\"false\" bottom=\"false\"/></Shape>"));
    Check(capless && capless->Meshes[0].Primitives[0].FaceCount() == 24,
          "top=\"false\" bottom=\"false\" leaves only the side");

    auto cone = Read(Scene("<Shape><Cone bottomRadius=\"2\" height=\"5\"/></Shape>"));
    Check(cone != nullptr, "a Cone parses");
    if (cone) {
        const MeshPrimitive& prim = cone->Meshes[0].Primitives[0];
        Check(prim.FaceCount() == 25, "24 side triangles and one base");
        const Bounds3D bounds = cone->ComputeBounds();
        Check(Near(bounds.Max.y, 2.5, 1e-9) && Near(bounds.Max.x, 2.0, 1e-9),
              "the apex is at half the height and the base at bottomRadius");
    }
}

static void TestOtherGeometry() {
    std::printf("Other geometry nodes\n");

    auto triangles = Read(Scene(
            "<Shape><IndexedTriangleSet index=\"0 1 2 1 3 2\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 0 1 0, 1 1 0\"/></IndexedTriangleSet></Shape>"));
    Check(triangles != nullptr, "an IndexedTriangleSet parses");
    if (triangles) {
        const MeshPrimitive& prim = triangles->Meshes[0].Primitives[0];
        Check(prim.Mode == PrimitiveMode::Triangles && prim.FaceCount() == 2,
              "two triangles sharing one index stream");
        Check(prim.VertexCount() == 4, "and sharing their vertices, because the streams agree");
    }

    auto strip = Read(Scene(
            "<Shape><IndexedTriangleStripSet index=\"0 1 2 3\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 0 1 0, 1 1 0\"/>"
            "</IndexedTriangleStripSet></Shape>"));
    Check(strip && strip->Meshes[0].Primitives[0].FaceCount() == 2,
          "a four-vertex strip becomes two triangles");

    auto lines = Read(Scene(
            "<Shape><IndexedLineSet coordIndex=\"0 1 2 -1 2 3 -1\">"
            "<Coordinate point=\"0 0 0, 1 0 0, 2 0 0, 3 0 0\"/></IndexedLineSet></Shape>"));
    Check(lines != nullptr, "an IndexedLineSet parses");
    if (lines) {
        const MeshPrimitive& prim = lines->Meshes[0].Primitives[0];
        Check(prim.Mode == PrimitiveMode::Lines && prim.FaceCount() == 3,
              "two polylines become three segments");
    }

    auto points = Read(Scene(
            "<Shape><PointSet><Coordinate point=\"0 0 0, 1 1 1\"/>"
            "<Color color=\"1 0 0, 0 0 1\"/></PointSet></Shape>"));
    Check(points != nullptr, "a PointSet parses");
    if (points) {
        const MeshPrimitive& prim = points->Meshes[0].Primitives[0];
        Check(prim.Mode == PrimitiveMode::Points && prim.VertexCount() == 2,
              "with its points");
        Check(prim.FindAttribute(AttributeSemantic::Color, 0) != nullptr, "and its colours");
    }

    // Switch draws one child, and -1 - its default - draws none.
    auto chosen = Read(Scene("<Switch whichChoice=\"1\">"
                             "<Shape><Box size=\"1 1 1\"/></Shape>"
                             "<Shape><Box size=\"8 8 8\"/></Shape></Switch>"));
    Check(chosen != nullptr, "a Switch parses");
    if (chosen) {
        Check(chosen->Meshes.size() == 1, "only the chosen child is read");
        Check(Near(chosen->ComputeBounds().Max.x, 4.0, 1e-9), "and it is the one selected");
    }
    std::vector<std::string> warnings;
    auto none = Read(Scene("<Switch>" + Square() + "</Switch>"), &warnings);
    Check(none == nullptr && !warnings.empty(),
          "the default whichChoice of -1 draws nothing, and the reader says so");

    warnings.clear();
    auto unknown = Read(Scene("<Shape><ElevationGrid xDimension=\"2\" zDimension=\"2\" "
                              "height=\"0 0 0 0\"/></Shape>"), &warnings);
    Check(unknown == nullptr && !warnings.empty(),
          "a geometry node this reader does not implement is named in a warning");
}

// X3D holds no keyframes on the node: a TimeSensor drives an interpolator
// through one ROUTE, and the interpolator drives a node field through another.
static void TestAnimation() {
    std::printf("Animation through ROUTEs\n");

    auto animated = Read(Scene(
            "<Transform DEF=\"Ship\">" + Square() + "</Transform>"
            "<TimeSensor DEF=\"Clock\" cycleInterval=\"2\" loop=\"true\"/>"
            "<PositionInterpolator DEF=\"Move\" key=\"0 0.5 1\" "
            "keyValue=\"0 0 0, 2 0 0, 4 0 0\"/>"
            "<OrientationInterpolator DEF=\"Turn\" key=\"0 1\" "
            "keyValue=\"0 1 0 0, 0 1 0 3.1415927\"/>"
            "<ROUTE fromNode=\"Clock\" fromField=\"fraction_changed\" toNode=\"Move\" "
            "toField=\"set_fraction\"/>"
            "<ROUTE fromNode=\"Move\" fromField=\"value_changed\" toNode=\"Ship\" "
            "toField=\"set_translation\"/>"
            "<ROUTE fromNode=\"Clock\" fromField=\"fraction_changed\" toNode=\"Turn\" "
            "toField=\"set_fraction\"/>"
            "<ROUTE fromNode=\"Turn\" fromField=\"value_changed\" toNode=\"Ship\" "
            "toField=\"set_rotation\"/>"));
    Check(animated != nullptr, "a routed scene parses");
    if (animated) {
        Check(animated->Animations.size() == 1 && animated->Animations[0].Channels.size() == 2,
              "two interpolators become two channels of one animation");
        if (animated->Animations.size() == 1 && animated->Animations[0].Channels.size() == 2) {
            const ModelAnimation& track = animated->Animations[0];
            Check(Near(track.Duration(), 2.0f, 1e-6),
                  "the TimeSensor's cycleInterval scales the 0..1 keys into seconds");
            for (const AnimationChannel& channel : track.Channels) {
                const AnimationSampler& sampler =
                        track.Samplers[static_cast<size_t>(channel.Sampler)];
                if (channel.Path == AnimationPath::Translation) {
                    Check(sampler.Values.size() == 9 && Near(sampler.Values[6], 4.0f, 1e-6),
                          "the position keys arrive as three-float translations");
                } else if (channel.Path == AnimationPath::Rotation) {
                    // A half turn about +Y is the quaternion (0, 1, 0, 0).
                    Check(sampler.Values.size() == 8 && Near(sampler.Values[5], 1.0f, 1e-6) &&
                          Near(sampler.Values[7], 0.0f, 1e-6),
                          "and the axis-angle keys become quaternions");
                }
            }
        }
        Check(animated->Animations[0].Channels[0].TargetNode == 0,
              "both target the Transform the ROUTE names");
    }
}

static void TestLightsAndViewpoints() {
    std::printf("Lights and viewpoints\n");

    auto lit = Read(Scene(
            "<DirectionalLight DEF=\"Sun\" direction=\"0 -1 0\" color=\"1 0.9 0.8\" "
            "intensity=\"0.7\"/>"
            "<SpotLight location=\"1 2 3\" direction=\"0 0 -1\" cutOffAngle=\"0.5\" "
            "beamWidth=\"1.5\" radius=\"20\"/>" + Square()));
    Check(lit != nullptr, "a lit scene parses");
    if (lit) {
        Check(lit->Lights.size() == 2, "both lights are read");
        const ModelLight& sun = lit->Lights[0];
        Check(sun.Type == LightType::Directional && Near(sun.Intensity, 0.7, 1e-6) &&
              Near(sun.Color.z, 0.8, 1e-6), "the directional light keeps its colour and intensity");
        // X3D states direction on the light; the document states it as the
        // node's orientation, pointing -Z.
        int lightNode = -1;
        for (size_t i = 0; i < lit->Nodes.size(); ++i)
            if (lit->Nodes[i].Light == 0) lightNode = static_cast<int>(i);
        Check(lightNode >= 0, "and it is placed by a node");
        if (lightNode >= 0) {
            const Vec3d aimed =
                    lit->Nodes[static_cast<size_t>(lightNode)].Rotation.Rotate(
                            Vec3d(0.0, 0.0, -1.0));
            Check(Near(aimed.y, -1.0, 1e-6),
                  "whose rotation carries -Z onto the direction the light stated");
        }
        const ModelLight& spot = lit->Lights[1];
        Check(spot.Type == LightType::Spot && Near(spot.OuterConeRadians, 0.5, 1e-6) &&
              Near(spot.InnerConeRadians, 0.5, 1e-6),
              "a beamWidth wider than cutOffAngle clamps to the outer cone");
        Check(Near(spot.Range, 20.0, 1e-6), "and radius becomes the range");
    }

    auto viewed = Read(Scene("<Viewpoint description=\"Front\" position=\"1 2 30\" "
                             "fieldOfView=\"0.9\"/>" + Square()));
    Check(viewed != nullptr, "a Viewpoint parses");
    if (viewed) {
        Check(viewed->Cameras.size() == 1 && viewed->Cameras[0].Name == "Front" &&
              Near(viewed->Cameras[0].YFovRadians, 0.9, 1e-6), "with its description and fov");
        bool placed = false;
        for (const ModelNode& node : viewed->Nodes)
            if (node.Camera == 0 && Near(node.Translation.z, 30.0, 1e-9)) placed = true;
        Check(placed, "and its position on the node that carries it");
    }
}

static void TestHead() {
    std::printf("Head and units\n");

    auto plain = Read(Scene(Square()));
    Check(plain && plain->SourceUnit == ModelUnit::Meter &&
          Near(plain->UnitScaleToMeters, 1.0, 1e-12),
          "X3D is metres by definition when nothing says otherwise");
    Check(plain && plain->Up == UpAxis::YUp, "and Y-up, which no field can contradict");

    auto described = Read(
            "<?xml version=\"1.0\"?><X3D version=\"3.3\" profile=\"Immersive\"><head>"
            "<meta name=\"generator\" content=\"Blender 2.78 (sub 0)\"/>"
            "<meta name=\"title\" content=\"Hangar\"/>"
            "<meta name=\"filename\" content=\"hangar.x3d\"/>"
            "<unit category=\"length\" name=\"millimetre\" conversionFactor=\"0.001\"/>"
            "</head><Scene>" + Square() + "</Scene></X3D>");
    Check(described != nullptr, "a head with meta and unit parses");
    if (described) {
        Check(described->Generator == "Blender 2.78 (sub 0)", "generator reaches Generator");
        Check(described->Title == "Hangar", "title reaches Title");
        Check(described->SourceUnit == ModelUnit::Millimeter &&
              Near(described->UnitScaleToMeters, 0.001, 1e-12),
              "the <unit> statement is the only place X3D names a physical size, and it survives");
        auto filename = described->Metadata.find("x3d.meta.filename");
        Check(filename != described->Metadata.end() && filename->second == "hangar.x3d",
              "a meta the document has no field for is kept as metadata");
        Check(described->Metadata["x3d.version"] == "3.3" &&
              described->Metadata["x3d.profile"] == "Immersive",
              "and the version and profile with it");
    }
}

// ===== THE CLASSIC VRML ENCODING =====
//
// X3D has three encodings and this reader takes both text ones. VRML97 writes
// the same syntax as X3D's classic encoding, one revision earlier, which is why
// a .wrl and a .x3dv arrive at the same place.

// A classic-encoding document around the caller's body.
static std::string Classic(const std::string& body, const char* header = "#VRML V2.0 utf8\n") {
    return std::string(header) + body + "\n";
}

static void TestClassicEncoding() {
    std::printf("The classic VRML encoding\n");

    // The assertion this whole layer exists for: one scene, two spellings, one
    // document. If these ever disagree, the node set has been implemented
    // twice and one of the copies has drifted.
    const std::string xml = Scene(
            "<Transform translation='1 2 3' scale='2 2 2'>"
            "<Shape><Appearance><Material diffuseColor='1 0 0' transparency='0.25'/></Appearance>"
            "<IndexedFaceSet coordIndex='0 1 2 -1' solid='false'>"
            "<Coordinate point='0 0 0 1 0 0 0 1 0'/>"
            "</IndexedFaceSet></Shape></Transform>");
    const std::string classic = Classic(
            "Transform {\n"
            "  translation 1 2 3\n"
            "  scale 2 2 2\n"
            "  children [\n"
            "    Shape {\n"
            "      appearance Appearance {\n"
            "        material Material { diffuseColor 1 0 0  transparency 0.25 }\n"
            "      }\n"
            "      geometry IndexedFaceSet {\n"
            "        coordIndex [ 0, 1, 2, -1 ]\n"
            "        solid FALSE\n"
            "        coord Coordinate { point [ 0 0 0, 1 0 0, 0 1 0 ] }\n"
            "      }\n"
            "    }\n"
            "  ]\n"
            "}");

    auto fromXml = Read(xml);
    auto fromClassic = Read(classic);
    Check(fromXml != nullptr && fromClassic != nullptr, "the same scene reads in both encodings");
    if (!fromXml || !fromClassic) return;

    Check(fromXml->Meshes.size() == fromClassic->Meshes.size() &&
          fromXml->Nodes.size() == fromClassic->Nodes.size() &&
          fromXml->Materials.size() == fromClassic->Materials.size(),
          "and yields the same counts of meshes, nodes and materials");
    Check(fromXml->TotalFaceCount() == fromClassic->TotalFaceCount() &&
          fromXml->TotalVertexCount() == fromClassic->TotalVertexCount(),
          "the same geometry, face for face and vertex for vertex");

    const Bounds3D a = fromXml->ComputeBounds();
    const Bounds3D b = fromClassic->ComputeBounds();
    Check(Near(a.Min.x, b.Min.x, 1e-9) && Near(a.Max.x, b.Max.x, 1e-9) &&
          Near(a.Min.y, b.Min.y, 1e-9) && Near(a.Max.y, b.Max.y, 1e-9) &&
          Near(a.Min.z, b.Min.z, 1e-9) && Near(a.Max.z, b.Max.z, 1e-9),
          "and the transform composes identically - the bounds agree to the last bit");
    Check(!fromClassic->Materials.empty() &&
          Near(fromClassic->Materials[0].BaseColorFactor.w, 0.75, 1e-6),
          "transparency 0.25 becomes an alpha of 0.75, as it does in the XML encoding");
    Check(fromClassic->SourceFormat == "vrml" && fromXml->SourceFormat == "x3d",
          "the document says which of the two it came from");
    Check(fromClassic->Metadata.count("x3d.encoding") == 1 &&
          fromClassic->Metadata.at("x3d.encoding") == "classic-vrml",
          "and names the encoding rather than leaving a caller to guess");

    // A bare word is a node when a brace follows it and a literal otherwise.
    // That one token of lookahead is the whole disambiguation, so it gets a
    // case where both appear in the same node body.
    auto both = Read(Classic(
            "Shape {\n"
            "  geometry IndexedFaceSet {\n"
            "    solid TRUE\n"
            "    ccw FALSE\n"
            "    coordIndex [ 0 1 2 -1 ]\n"
            "    coord Coordinate { point [ 0 0 0, 1 0 0, 0 1 0 ] }\n"
            "  }\n"
            "}"));
    Check(both != nullptr && both->TotalFaceCount() == 1,
          "a field whose value is a word and one whose value is a node sit side by side");

    // `ccw FALSE` reverses the winding, which flips the generated normal - the
    // same check the XML encoding's case makes, and the only way to prove the
    // bare word reached the reader as a value rather than being taken for the
    // name of the next field.
    auto counter = Read(Classic(
            "Shape { geometry IndexedFaceSet { coordIndex [ 0 1 2 -1 ]\n"
            "        coord Coordinate { point [ 0 0 0, 1 0 0, 0 1 0 ] } } }"));
    if (both && counter && !both->Meshes.empty() && !counter->Meshes.empty()) {
        const Vec3f clockwise = both->Meshes[0].Primitives[0].Normals[0];
        const Vec3f other = counter->Meshes[0].Primitives[0].Normals[0];
        Check(Near(clockwise.z, -other.z, 1e-6) && std::fabs(other.z) > 0.9f,
              "and `ccw FALSE` flips the generated normal, so the word was read as a value");
    }

    // An empty MFNode is brackets with nothing between them, and must not be
    // mistaken for an empty *value* list - or for a missing field.
    std::vector<std::string> warnings;
    auto empty = Read(Classic("Group { children [ ] }"), &warnings);
    Check(empty != nullptr && empty->Meshes.empty() && empty->Nodes.size() == 1,
          "an empty children list gives a group with no children, not a parse error");

    // Comments run to end of line, and commas are whitespace - both inside a
    // number list, which is where a mistake would corrupt geometry silently.
    auto commented = Read(Classic(
            "# a leading comment\n"
            "Shape {   # and a trailing one\n"
            "  geometry IndexedFaceSet {\n"
            "    coordIndex [ 0, 1, 2, -1 ]   # the only face\n"
            "    coord Coordinate { point [ 0 0 0,\n"
            "                               1 0 0,   # a comment mid-array\n"
            "                               0 1 0 ] }\n"
            "  }\n"
            "}"));
    Check(commented != nullptr && commented->TotalFaceCount() == 1 &&
          commented->TotalVertexCount() == 3,
          "comments and commas inside a number list are whitespace");
}

static void TestClassicInstancingAndRoutes() {
    std::printf("DEF, USE and ROUTE in the classic encoding\n");

    // DEF/USE is a syntax here rather than an attribute, but it means exactly
    // what it means in XML: one geometry, drawn twice.
    auto shared = Read(Classic(
            "Group {\n"
            "  children [\n"
            "    Transform { translation 0 0 0 children [ Shape {\n"
            "      geometry DEF Tri IndexedFaceSet {\n"
            "        coordIndex [ 0 1 2 -1 ]\n"
            "        coord Coordinate { point [ 0 0 0, 1 0 0, 0 1 0 ] }\n"
            "      } } ] }\n"
            "    Transform { translation 5 0 0 children [ Shape {\n"
            "      geometry USE Tri\n"
            "    } ] }\n"
            "  ]\n"
            "}"));
    Check(shared != nullptr, "a DEF'd geometry and a USE of it read");
    if (shared) {
        Check(shared->Meshes.size() == 1,
              "and share one mesh rather than duplicating it - USE is instancing");
        const Bounds3D bounds = shared->ComputeBounds();
        Check(Near(bounds.Max.x, 6.0, 1e-6),
              "with the second instance placed by its own Transform");
    }

    // A USE that names nothing must be reported, not silently dropped: a
    // missing instance is a hole in the model a caller needs to hear about.
    std::vector<std::string> warnings;
    auto dangling = Read(Classic(
            "Shape { geometry USE Nothing }\n"
            "Shape { geometry IndexedFaceSet { coordIndex [ 0 1 2 -1 ]\n"
            "        coord Coordinate { point [ 0 0 0, 1 0 0, 0 1 0 ] } } }"), &warnings);
    bool named = false;
    for (const std::string& w : warnings)
        if (w.find("Nothing") != std::string::npos) named = true;
    Check(dangling != nullptr && named, "a USE naming nothing is reported by name");

    // ROUTE is `a.b TO c.d` here and four attributes in XML. Both have to reach
    // the same channel, or animation works in one encoding only.
    auto animated = Read(Classic(
            "DEF Clock TimeSensor { cycleInterval 2.0 loop TRUE }\n"
            "DEF Path PositionInterpolator {\n"
            "  key [ 0 1 ]\n"
            "  keyValue [ 0 0 0, 0 10 0 ]\n"
            "}\n"
            "DEF Mover Transform {\n"
            "  children [ Shape { geometry IndexedFaceSet {\n"
            "    coordIndex [ 0 1 2 -1 ]\n"
            "    coord Coordinate { point [ 0 0 0, 1 0 0, 0 1 0 ] } } } ]\n"
            "}\n"
            "ROUTE Clock.fraction_changed TO Path.set_fraction\n"
            "ROUTE Path.value_changed TO Mover.set_translation"));
    Check(animated != nullptr && animated->Animations.size() == 1,
          "a TimeSensor, an interpolator and two ROUTEs become one animation");
    if (animated && animated->Animations.size() == 1) {
        const ModelAnimation& animation = animated->Animations[0];
        Check(animation.Channels.size() == 1 &&
              animation.Channels[0].Path == AnimationPath::Translation,
              "driving the translation of the Transform the second ROUTE names");
        Check(!animation.Samplers.empty() && animation.Samplers[0].Times.size() == 2 &&
              Near(animation.Samplers[0].Times.back(), 2.0, 1e-9),
              "with the key times scaled by the TimeSensor's cycleInterval");
    }

    // A prototype is a node type declared in the file. Instantiating one means
    // running its body, which this reader does not do - so it says so.
    warnings.clear();
    auto proto = Read(Classic(
            "PROTO Widget [ field SFVec3f where 0 0 0 ] {\n"
            "  Shape { geometry Box { size 1 1 1 } }\n"
            "}\n"
            "Shape { geometry IndexedFaceSet { coordIndex [ 0 1 2 -1 ]\n"
            "        coord Coordinate { point [ 0 0 0, 1 0 0, 0 1 0 ] } } }"), &warnings);
    bool saidProto = false;
    for (const std::string& w : warnings)
        if (w.find("PROTO") != std::string::npos || w.find("prototype") != std::string::npos)
            saidProto = true;
    Check(proto != nullptr && proto->TotalFaceCount() == 1,
          "a PROTO declaration is skipped whole, and the scene after it still reads");
    Check(saidProto, "and the skip is reported rather than silent");
}

static void TestClassicStringsAndVersions() {
    std::printf("Strings and versions in the classic encoding\n");

    // The one escaping rule that matters in practice. An exporter writes a
    // Windows path into an ImageTexture url, and a reader that treats every
    // backslash as an escape deletes the separators out of it.
    auto textured = Read(Classic(
            "Shape {\n"
            "  appearance Appearance {\n"
            "    texture ImageTexture { url [ \"textures\\E-45.jpg\" \"E-45.jpg\" ] }\n"
            "  }\n"
            "  geometry IndexedFaceSet { coordIndex [ 0 1 2 -1 ]\n"
            "    coord Coordinate { point [ 0 0 0, 1 0 0, 0 1 0 ] } }\n"
            "}"));
    Check(textured != nullptr && textured->Images.size() == 1,
          "an ImageTexture url list gives one image");
    if (textured && textured->Images.size() == 1)
        Check(textured->Images[0].Uri == "textures\\E-45.jpg",
              "and its backslashes survive - only a quote or a backslash is escapable");

    // An embedded quote, which *is* escapable, must not end the string.
    auto quoted = Read(Classic(
            "WorldInfo { title \"the \\\"E-45\\\" aircraft\" }\n"
            "Shape { geometry IndexedFaceSet { coordIndex [ 0 1 2 -1 ]\n"
            "        coord Coordinate { point [ 0 0 0, 1 0 0, 0 1 0 ] } } }"));
    Check(quoted != nullptr && quoted->Title == "the \"E-45\" aircraft",
          "an escaped quote stays inside the string it was written in");

    // VRML 1.0 is a different node set under the same file extension -
    // Separator, Coordinate3, a different IndexedFaceSet. Read as VRML97 it
    // would come out empty, so it is refused by name instead.
    std::vector<std::string> warnings;
    auto old = Read("#VRML V1.0 ascii\nSeparator { Coordinate3 { point [ 0 0 0 ] } }", &warnings);
    bool saidOne = false;
    for (const std::string& w : warnings)
        if (w.find("1.0") != std::string::npos) saidOne = true;
    Check(old == nullptr && saidOne,
          "VRML 1.0 is refused by name rather than read as an empty VRML97 scene");

    // A file that never states its encoding is not one of these. The header
    // line is mandatory in both revisions.
    warnings.clear();
    Check(Read("Shape { geometry Box {} }", &warnings) == nullptr,
          "classic syntax with no header line reads as nothing");

    // Truncation must stop at the end of the buffer, not run past it.
    warnings.clear();
    Check(Read(Classic("Shape { geometry IndexedFaceSet { coordIndex [ 0 1 2"), &warnings) ==
                  nullptr,
          "a file cut mid-array reads as nothing");
    Check(!warnings.empty(), "and says the file ends inside a node");
}

// The same aircraft again, exported through Blender's VRML97 writer. This is
// the cross-format half: the .wrl and the .x3d are the same model out of the
// same .blend, so what they agree and disagree on is a fact about the exports
// rather than about either reader.
static void TestVrmlSample(const char* path) {
    std::printf("Sample (VRML97): %s\n", path);
    X3DConverter converter;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    auto doc = converter.Import(path, options);
    if (!doc) { std::printf("  [FAIL] import returned nothing\n"); ++failures; return; }

    Check(doc->SourceFormat == "vrml" && doc->Up == UpAxis::YUp, "read as Y-up VRML97");
    Check(doc->Metadata.count("vrml.version") == 1 && doc->Metadata.at("vrml.version") == "2.0",
          "at the version its header line states");
    Check(doc->Metadata.count("x3d.encoding") == 1 &&
          doc->Metadata.at("x3d.encoding") == "classic-vrml",
          "through the classic encoding, which is what the extension did not say");

    // The VRML exporter writes one Shape with the transform chain already
    // applied to the coordinates, where the X3D exporter writes a four-deep
    // Transform chain and leaves them in local space. Same model, and the
    // reader has to arrive at the same place from both.
    Check(doc->Meshes.size() == 1 && doc->Nodes.size() == 1,
          "one shape under one node - this exporter bakes the chain the .x3d writes out");

    Check(doc->TotalFaceCount() == 64880, "64880 faces");
    bool allTriangles = true;
    for (const ModelMesh& mesh : doc->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives)
            for (size_t f = 0; f < prim.FaceCount(); ++f)
                if (prim.Face(f).size() != 3) allTriangles = false;
    Check(allTriangles, "every one of them a triangle, where the .x3d holds quads");

    // 64880 is 8 * 8110 exactly: the .x3d's 8110 quads, subdivided one further
    // level into four quads each and then split into two triangles apiece. The
    // two exporters ran the modifier stack to different depths.
    Check(doc->TotalFaceCount() == 8110 * 8,
          "which is exactly eight times the .x3d's 8110 - one more subdivision, then triangulated");

    // Per-corner texture indices split every corner into its own vertex, the
    // same way they do in the .x3d export.
    Check(doc->TotalVertexCount() == 194640,
          "and its per-corner texture indices split every corner into its own vertex");

    Check(doc->Materials.size() == 1 && doc->Images.size() == 1,
          "one material and the texture it names");
    Check(!doc->Images.empty() && doc->Images[0].Uri == "textures\\E-45 _col.jpg",
          "whose url keeps the backslashes of the path the exporter wrote");

    bool vertexColors = false, texCoords = false;
    for (const ModelMesh& mesh : doc->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives) {
            if (prim.FindAttribute(AttributeSemantic::Color, 0)) vertexColors = true;
            if (prim.FindAttribute(AttributeSemantic::TexCoord, 0)) texCoords = true;
        }
    Check(vertexColors && texCoords, "with both the colour and the texture coordinate stream");

    const Bounds3D bounds = doc->ComputeBounds();
    Check(Near(bounds.Min.x, -0.9732, 1e-3) && Near(bounds.Max.x, 0.9732, 1e-3) &&
          Near(bounds.Min.y, -1.3447, 1e-3) && Near(bounds.Max.y, 2.8458, 1e-3) &&
          Near(bounds.Min.z, -3.2033, 1e-3) && Near(bounds.Max.z, 2.9306, 1e-3),
          "world bounds match an independent walk of the file");

    // The same three axes as the .x3d, to a few thousandths - which is what
    // says the baked coordinates and the Transform chain describe one model.
    Check(Near(bounds.Min.x, -bounds.Max.x, 1e-6),
          "symmetric in X: this export had its mirror modifier applied, as the .x3d did");

    Check(warnings.empty(), "and the whole file reads without a single warning");
}

static void TestSample(const char* path) {
    std::printf("Sample: %s\n", path);
    X3DConverter converter;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    auto doc = converter.Import(path, options);
    if (!doc) { std::printf("  [FAIL] import returned nothing\n"); ++failures; return; }

    // The title is not in the file - X3D's <meta name="filename"> is the file
    // name, not a document title - so it comes from the path. Derive the
    // expectation the same way, or the assertion breaks on any copy.
    Check(doc->Title == std::filesystem::path(path).stem().string(),
          "with no title in the head, the file name titles the document");
    Check(doc->Generator == "Blender 2.78 (sub 0)", "the generator meta is read");
    Check(doc->SourceFormat == "x3d" && doc->Up == UpAxis::YUp, "read as Y-up X3D");
    Check(warnings.empty(), "and read without a single warning");

    Check(doc->Meshes.size() == 2 && doc->Nodes.size() == 9, "two meshes under nine nodes");
    Check(doc->TotalFaceCount() == 8110,
          "8110 faces - exactly the OBJ export's count, from the same .blend");

    bool allQuads = true;
    for (const ModelMesh& mesh : doc->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives) {
            if (prim.Mode != PrimitiveMode::Polygons) allQuads = false;
            for (size_t f = 0; f < prim.FaceCount(); ++f)
                if (prim.Face(f).size() != 4) allQuads = false;
        }
    Check(allQuads, "and every one of them is still a quad, not a fan of triangles");

    // Blender writes a texCoordIndex that disagrees with coordIndex, so every
    // corner is its own vertex. 8110 quads is 32440 corners.
    Check(doc->TotalVertexCount() == 32440,
          "the per-corner texture indices split every corner into its own vertex");

    Check(doc->Materials.size() == 2 && doc->Images.size() == 2,
          "both materials and both textures are read");
    bool bound = true;
    for (const ModelMesh& mesh : doc->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives)
            if (prim.Material < 0) bound = false;
    Check(bound, "every primitive is bound to its material");

    bool transparent = false;
    for (const ModelMaterial& material : doc->Materials)
        if (material.BaseColorFactor.w < 0.9f && material.Alpha == AlphaMode::Blend)
            transparent = true;
    Check(transparent, "the canopy glass is read as transparent");

    bool vertexColors = false;
    for (const ModelMesh& mesh : doc->Meshes)
        for (const MeshPrimitive& prim : mesh.Primitives)
            if (prim.FindAttribute(AttributeSemantic::Color, 0)) vertexColors = true;
    Check(vertexColors, "and the hull's vertex colour layer with it");

    // The node chain the file nests, and the root's Blender Z-up-to-Y-up turn.
    Check(doc->Nodes[0].Name == "Armature_TRANSFORM" && doc->Nodes[4].Parent == 3 &&
          doc->Nodes[3].Parent == 2 && doc->Nodes[2].Parent == 1 && doc->Nodes[1].Parent == 0,
          "the four-deep Transform chain under the armature survives");
    const Vec3d turned = doc->Nodes[0].Rotation.Rotate(Vec3d(0.0, 0.0, 1.0));
    Check(Near(turned.y, 1.0, 1e-5),
          "the root's half turn about (0, .7071, .7071) is Blender's Z-up to Y-up conversion");

    auto crease = doc->Nodes[4].Extras.find("x3d.creaseAngle");
    Check(crease != doc->Nodes[4].Extras.end(),
          "creaseAngle has no field in the document, so it is kept as a node extra");

    const Bounds3D bounds = doc->ComputeBounds();
    Check(Near(bounds.Min.x, -0.9732, 1e-3) && Near(bounds.Max.x, 0.9732, 1e-3) &&
          Near(bounds.Min.y, -1.3469, 1e-3) && Near(bounds.Max.y, 2.8478, 1e-3) &&
          Near(bounds.Min.z, -3.2049, 1e-3) && Near(bounds.Max.z, 2.9357, 1e-3),
          "world bounds match a walk of the node chain, Y-up after the root's turn");

    // This export has the mirror modifier applied and the .dae does not. That
    // is a property of the files, not of the readers, and asserting it stops a
    // later change "fixing" one to match the other.
    Check(Near(bounds.Min.x, -bounds.Max.x, 1e-6),
          "the X3D holds the whole hull in X - its mirror modifier was applied, unlike the .dae");
}

int main(int argc, char** argv) {
    TestValidation();
    TestIndexedFaceSet();
    TestTransform();
    TestInstancing();
    TestAppearance();
    TestPrimitives();
    TestOtherGeometry();
    TestAnimation();
    TestLightsAndViewpoints();
    TestHead();
    TestClassicEncoding();
    TestClassicInstancingAndRoutes();
    TestClassicStringsAndVersions();
    if (argc > 1) TestSample(argv[1]);
    else std::printf("Sample: skipped (pass an .x3d path to run it)\n");
    if (argc > 2) TestVrmlSample(argv[2]);
    else std::printf("Sample (VRML97): skipped (pass a .wrl path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
