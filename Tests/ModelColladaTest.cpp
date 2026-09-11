// Tests/ModelColladaTest.cpp
// The COLLADA reader.
//
// COLLADA is the first format that states everything the structure was built
// to hold — unit, up axis, a node hierarchy, materials, animation — so this is
// where those paths get exercised against a real file rather than a synthetic
// one. The synthetic cases cover the parts a single sample cannot: transform
// element ordering, n-gons, material binding by symbol, and a file with no
// visual scene.
//
// argv[1] is the .dae. Without it only the synthetic cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/COLLADA/UltraCanvasColladaConverter.h"

#include <cmath>
#include <cstdio>
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

// A one-triangle COLLADA with the caller's asset block and node body.
static std::string Scene(const std::string& asset, const std::string& nodeBody,
                         const std::string& extra = "") {
    return
        "<?xml version=\"1.0\"?>\n"
        "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">"
        + asset +
        "<library_geometries><geometry id=\"g\" name=\"tri\"><mesh>"
        "<source id=\"pos\"><float_array id=\"pa\" count=\"9\">0 0 0 1 0 0 0 1 0</float_array>"
        "<technique_common><accessor source=\"#pa\" count=\"3\" stride=\"3\"/></technique_common></source>"
        "<vertices id=\"v\"><input semantic=\"POSITION\" source=\"#pos\"/></vertices>"
        "<triangles material=\"sym\" count=\"1\">"
        "<input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/><p>0 1 2</p>"
        "</triangles></mesh></geometry></library_geometries>"
        + extra +
        "<library_visual_scenes><visual_scene id=\"s\" name=\"s\"><node id=\"n\" name=\"n\">"
        + nodeBody +
        "<instance_geometry url=\"#g\"/></node></visual_scene></library_visual_scenes>"
        "<scene><instance_visual_scene url=\"#s\"/></scene></COLLADA>";
}

static void TestValidation() {
    std::printf("Validation\n");
    ColladaConverter conv;
    Check(!conv.ValidateData({}), "empty data is rejected");
    Check(!conv.ValidateData(Bytes("<svg xmlns=\"http://www.w3.org/2000/svg\"/>")),
          "an SVG is rejected");
    Check(conv.ValidateData(Bytes(Scene("", ""))), "a COLLADA document is accepted");

    ConversionOptions options;
    std::vector<std::string> warnings;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
    Check(conv.ImportFromMemory(Bytes("<not-collada/>"), options) == nullptr,
          "a non-COLLADA root imports as nothing");
    Check(conv.ImportFromMemory(Bytes("<COLLADA><unclosed>"), options) == nullptr,
          "malformed XML imports as nothing");
    Check(!warnings.empty(), "and both say why");
}

// The property most likely to be got wrong: a node's transform is an ordered
// sequence, not a TRS triple. Translate-then-rotate and rotate-then-translate
// put the geometry in different places, and a reader with fixed slots cannot
// tell them apart.
static void TestTransformOrder() {
    std::printf("Transform composition\n");
    ColladaConverter conv;
    ConversionOptions quiet;

    const std::string rotateThenTranslate =
            "<rotate>0 0 1 90</rotate><translate>1 0 0</translate>";
    const std::string translateThenRotate =
            "<translate>1 0 0</translate><rotate>0 0 1 90</rotate>";

    auto first = conv.ImportFromMemory(Bytes(Scene("", rotateThenTranslate)), quiet);
    auto second = conv.ImportFromMemory(Bytes(Scene("", translateThenRotate)), quiet);
    Check(first && second, "both orderings parse");
    if (!first || !second) return;

    // Rotate 90 about Z, then translate (1,0,0) in the rotated frame, puts the
    // origin at (0,1,0). The other order puts it at (1,0,0).
    const Vec3d a = first->GlobalTransform(0).TransformPoint(Vec3d(0, 0, 0));
    const Vec3d b = second->GlobalTransform(0).TransformPoint(Vec3d(0, 0, 0));
    Check(Near(a.x, 0.0, 1e-9) && Near(a.y, 1.0, 1e-9),
          "rotate then translate moves along the rotated axis");
    Check(Near(b.x, 1.0, 1e-9) && Near(b.y, 0.0, 1e-9),
          "translate then rotate moves along the original axis");

    // Blender writes three separate <rotate> elements, one per axis.
    auto perAxis = conv.ImportFromMemory(Bytes(Scene("",
            "<rotate sid=\"rotationZ\">0 0 1 0</rotate>"
            "<rotate sid=\"rotationY\">0 1 0 0</rotate>"
            "<rotate sid=\"rotationX\">1 0 0 180</rotate>")), quiet);
    Check(perAxis != nullptr, "three per-axis rotate elements parse");
    if (perAxis) {
        const Vec3d up = perAxis->GlobalTransform(0).TransformDirection(Vec3d(0, 1, 0));
        Check(Near(up.y, -1.0, 1e-9), "a 180 degree X rotation flips Y, and the angle is degrees");
    }

    // A <matrix> is row-major in the file and column-major in the document.
    auto matrix = conv.ImportFromMemory(Bytes(Scene("",
            "<matrix>1 0 0 5  0 1 0 6  0 0 1 7  0 0 0 1</matrix>")), quiet);
    Check(matrix != nullptr, "a matrix transform parses");
    if (matrix) {
        const Vec3d origin = matrix->GlobalTransform(0).TransformPoint(Vec3d(0, 0, 0));
        Check(Near(origin.x, 5.0, 1e-9) && Near(origin.y, 6.0, 1e-9) && Near(origin.z, 7.0, 1e-9),
              "the row-major matrix transposes into the document correctly");
    }
}

static void TestAssetAndMaterials() {
    std::printf("Asset and materials\n");
    ColladaConverter conv;
    ConversionOptions quiet;

    auto metres = conv.ImportFromMemory(Bytes(Scene(
            "<asset><unit name=\"meter\" meter=\"1\"/><up_axis>Z_UP</up_axis></asset>", "")), quiet);
    Check(metres && metres->Up == UpAxis::ZUp && metres->SourceUnit == ModelUnit::Meter &&
          Near(metres->UnitScaleToMeters, 1.0, 1e-12),
          "the declared unit and up axis reach the document");

    auto millimetres = conv.ImportFromMemory(Bytes(Scene(
            "<asset><unit name=\"millimeter\" meter=\"0.001\"/><up_axis>Y_UP</up_axis></asset>",
            "")), quiet);
    Check(millimetres && millimetres->SourceUnit == ModelUnit::Millimeter &&
          Near(millimetres->UnitScaleToMeters, 0.001, 1e-15),
          "a millimetre unit is recognised from its scale, not its name");

    // An odd scale keeps the number and declines to name it.
    auto odd = conv.ImportFromMemory(Bytes(Scene(
            "<asset><unit name=\"furlong\" meter=\"201.168\"/></asset>", "")), quiet);
    Check(odd && odd->SourceUnit == ModelUnit::Unspecified &&
          Near(odd->UnitScaleToMeters, 201.168, 1e-9),
          "an unnamed unit keeps its scale rather than being forced to a near match");

    // A phong effect with transparency, bound through material -> effect and
    // instance_material symbol -> material.
    const std::string materials =
            "<library_effects><effect id=\"e\"><profile_COMMON><technique sid=\"c\"><phong>"
            "<diffuse><color>0.8 0.2 0.1 1</color></diffuse>"
            "<specular><color>1 1 1 1</color></specular>"
            "<shininess><float>50</float></shininess>"
            "<transparent opaque=\"A_ONE\"><color>1 1 1 0.25</color></transparent>"
            "<transparency><float>1</float></transparency>"
            "</phong></technique></profile_COMMON></effect></library_effects>"
            "<library_materials><material id=\"m\" name=\"Glass\">"
            "<instance_effect url=\"#e\"/></material></library_materials>";
    auto shaded = conv.ImportFromMemory(Bytes(Scene("", "", materials)), quiet);
    Check(shaded && shaded->Materials.size() == 1, "the material library is read");
    if (shaded && !shaded->Materials.empty()) {
        const ModelMaterial& material = shaded->Materials[0];
        Check(material.Name == "Glass", "the material keeps its name, not its id");
        Check(material.Phong.has_value() && Near(material.Phong->Diffuse.x, 0.8f, 1e-6),
              "the phong diffuse colour is read");
        Check(Near(material.BaseColorFactor.w, 0.25f, 1e-6) && material.Alpha == AlphaMode::Blend,
              "A_ONE transparency becomes opacity, and the material blends");
        // A white specular over a bright diffuse is not metal - the rule the
        // 3DS sample taught.
        Check(material.MetallicFactor == 0.0f, "a white specular is still not metal");
    }
}

static void TestGeometryAndAnimation() {
    std::printf("Geometry and animation\n");
    ColladaConverter conv;
    ConversionOptions quiet;

    // A polylist of one quad and one triangle: n-gons must survive.
    auto mixed = conv.ImportFromMemory(Bytes(
            "<?xml version=\"1.0\"?><COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">"
            "<library_geometries><geometry id=\"g\"><mesh>"
            "<source id=\"pos\"><float_array id=\"pa\" count=\"15\">0 0 0 1 0 0 1 1 0 0 1 0 2 0 0</float_array>"
            "<technique_common><accessor source=\"#pa\" count=\"5\" stride=\"3\"/></technique_common></source>"
            "<vertices id=\"v\"><input semantic=\"POSITION\" source=\"#pos\"/></vertices>"
            "<polylist count=\"2\"><input semantic=\"VERTEX\" source=\"#v\" offset=\"0\"/>"
            "<vcount>4 3</vcount><p>0 1 2 3 1 4 2</p></polylist>"
            "</mesh></geometry></library_geometries>"
            "<library_visual_scenes><visual_scene id=\"s\"><node id=\"n\">"
            "<instance_geometry url=\"#g\"/></node></visual_scene></library_visual_scenes>"
            "<scene><instance_visual_scene url=\"#s\"/></scene></COLLADA>"), quiet);
    Check(mixed != nullptr, "a polylist parses");
    if (mixed) {
        const MeshPrimitive& prim = mixed->Meshes[0].Primitives[0];
        Check(prim.Mode == PrimitiveMode::Polygons, "a mixed quad/triangle list uses Polygons mode");
        Check(prim.FaceCount() == 2 && prim.Face(0).size() == 4 && prim.Face(1).size() == 3,
              "the quad stays a quad and the triangle a triangle");
    }

    // A matrix-valued animation channel must become translation, rotation and
    // scale channels, because that is what the document interpolates.
    const std::string animation =
            "<library_animations><animation id=\"a\">"
            "<source id=\"in\"><float_array id=\"ia\" count=\"2\">0 1</float_array>"
            "<technique_common><accessor source=\"#ia\" count=\"2\" stride=\"1\"/></technique_common></source>"
            "<source id=\"out\"><float_array id=\"oa\" count=\"32\">"
            "1 0 0 0  0 1 0 0  0 0 1 0  0 0 0 1  "
            "1 0 0 3  0 1 0 0  0 0 1 0  0 0 0 1</float_array>"
            "<technique_common><accessor source=\"#oa\" count=\"2\" stride=\"16\"/></technique_common></source>"
            "<source id=\"interp\"><Name_array id=\"na\" count=\"2\">LINEAR LINEAR</Name_array>"
            "<technique_common/></source>"
            "<sampler id=\"samp\">"
            "<input semantic=\"INPUT\" source=\"#in\"/>"
            "<input semantic=\"OUTPUT\" source=\"#out\"/>"
            "<input semantic=\"INTERPOLATION\" source=\"#interp\"/></sampler>"
            "<channel source=\"#samp\" target=\"n/transform\"/>"
            "</animation></library_animations>";
    auto animated = conv.ImportFromMemory(Bytes(Scene("", "", animation)), quiet);
    Check(animated && animated->Animations.size() == 1, "the animation library is read");
    if (animated && !animated->Animations.empty()) {
        const ModelAnimation& track = animated->Animations[0];
        Check(track.Channels.size() == 3,
              "one matrix channel becomes translation, rotation and scale channels");
        Check(Near(track.Duration(), 1.0f, 1e-6), "the duration comes from the key times");
        bool foundTranslation = false;
        for (const auto& channel : track.Channels) {
            if (channel.Path != AnimationPath::Translation) continue;
            foundTranslation = true;
            const AnimationSampler& sampler = track.Samplers[static_cast<size_t>(channel.Sampler)];
            Check(sampler.Values.size() == 6 && Near(sampler.Values[3], 3.0f, 1e-6),
                  "the decomposed translation carries the matrix's own offset");
        }
        Check(foundTranslation, "a translation channel is among them");
    }
}

static void TestSample(const char* path) {
    std::printf("Sample: %s\n", path);
    ColladaConverter conv;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    auto doc = conv.Import(path, options);
    if (!doc) { std::printf("  [FAIL] import returned nothing\n"); ++failures; return; }

    Check(doc->Title == "E-45_GLSL", "the visual scene's name titles the document");
    Check(doc->Author == "Blender User" && doc->Generator.rfind("Blender", 0) == 0,
          "the contributor block reaches Author and Generator");
    Check(doc->Up == UpAxis::ZUp, "up_axis Z_UP is read");
    Check(doc->SourceUnit == ModelUnit::Meter && Near(doc->UnitScaleToMeters, 1.0, 1e-12),
          "this is the first sample that states a real unit, and it survives");

    Check(doc->Meshes.size() == 2 && doc->Nodes.size() == 6, "two meshes under six nodes");
    Check(doc->TotalFaceCount() == 1995, "both polylists are read (186 + 1809 triangles)");

    // The node chain the file nests:
    //   Armature -> root -> Ship -> { Cube_021, Glass -> Cube_004 }
    int deepest = -1;
    for (size_t i = 0; i < doc->Nodes.size(); ++i)
        if (doc->Nodes[i].Name == "Cube_004") deepest = static_cast<int>(i);
    Check(deepest >= 0, "the deepest node is present");
    if (deepest >= 0) {
        std::vector<std::string> chain;
        for (int walk = doc->Nodes[static_cast<size_t>(deepest)].Parent; walk >= 0;
             walk = doc->Nodes[static_cast<size_t>(walk)].Parent)
            chain.push_back(doc->Nodes[static_cast<size_t>(walk)].Name);
        const std::vector<std::string> expected = {"Glass", "Ship", "root", "Armature"};
        Check(chain == expected,
              "its parent chain is Glass, Ship, root, Armature — four deep, as the file nests it");
    }

    Check(doc->Materials.size() == 2, "both materials are read");
    bool boundCorrectly = true;
    for (const auto& mesh : doc->Meshes)
        for (const auto& prim : mesh.Primitives)
            if (prim.Material < 0) boundCorrectly = false;
    Check(boundCorrectly, "every primitive is bound to a material through its symbol");

    bool foundTransparent = false;
    for (const auto& material : doc->Materials)
        if (material.BaseColorFactor.w < 0.9f && material.Alpha == AlphaMode::Blend)
            foundTransparent = true;
    Check(foundTransparent, "the canopy glass is read as transparent");

    bool foundVertexColors = false;
    for (const auto& mesh : doc->Meshes)
        for (const auto& prim : mesh.Primitives)
            if (prim.FindAttribute(AttributeSemantic::Color, 0)) foundVertexColors = true;
    Check(foundVertexColors, "the hull's vertex colour layer is read");

    Check(doc->Animations.size() == 1 && doc->Animations[0].Channels.size() == 9,
          "three matrix-valued animations become nine TRS channels");
    Check(Near(doc->Animations[0].Duration(), 0.8333333f, 1e-4),
          "the animation duration is read from the key times");

    // World bounds, checked against an independent computation of the same
    // file's node chain. This is the assertion that the ordered-transform
    // composition is right at depth.
    const Bounds3D bounds = doc->ComputeBounds();
    Check(Near(bounds.Min.x, -0.9732, 1e-3) && Near(bounds.Max.x, 0.0, 1e-3) &&
          Near(bounds.Min.y, -4.6097, 1e-3) && Near(bounds.Max.y, 6.8416, 1e-3) &&
          Near(bounds.Min.z, -1.3556, 1e-3) && Near(bounds.Max.z, 2.8555, 1e-3),
          "world bounds match an independent walk of the node chain");

    // This export is NOT the same geometry as the OBJ/3DS/DXF ones, and that
    // is a property of the file rather than of the reader. Asserting it stops
    // a later change "fixing" the reader to match the others.
    Check(bounds.Max.x <= 0.0001,
          "the DAE holds only half the hull in X — its mirror modifier was not applied");
    Check(doc->TotalFaceCount() < 2000,
          "and it is a far lower-poly export than the 16220-triangle 3DS");
}

int main(int argc, char** argv) {
    TestValidation();
    TestTransformOrder();
    TestAssetAndMaterials();
    TestGeometryAndAnimation();
    if (argc > 1) TestSample(argv[1]);
    else std::printf("Sample: skipped (pass a .dae path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
