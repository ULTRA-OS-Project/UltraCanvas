// Tests/ModelStepTest.cpp
// The STEP reader and writer: ISO 10303-21 syntax, AP203/214 entity meanings,
// and the round trip back out.
//
// Two kinds of assertion, deliberately. The syntax cases are unit tests on
// text written inline, because the Part 21 grammar has corners — doubled
// quotes, \X2\ escapes, comments, complex instances — that a sample file will
// not reliably contain. The geometry cases run against the hand-authored
// samples in media/models/STEP and assert *measurements*: the volume of the
// meshed box is 6000 or the reader got a face's orientation wrong; the area of
// the NURBS sheet is a quarter cylinder's or the rational weights were
// ignored. Those numbers cannot be right by accident.
//
// argv[1] is media/models/STEP. Without it only the syntax cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/STEP/UltraCanvasStepConverter.h"
#include "Models/STEP/UltraCanvasStepFile.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelStorage;
using namespace UltraCanvas::ModelConverter;
namespace SF = UltraCanvas::StepFile;

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

static std::vector<uint8_t> Bytes(const std::string& text) {
    return std::vector<uint8_t>(text.begin(), text.end());
}

static double MeshVolume(const ModelDocument& document) {
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

static double MeshArea(const ModelDocument& document) {
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

// ===== SYNTAX =====

static void TestParsing() {
    std::printf("Part 21 syntax\n");

    const std::string text =
            "ISO-10303-21;\n"
            "HEADER;\n"
            "FILE_DESCRIPTION(('a test'),'2;1');\n"
            "FILE_NAME('bracket.stp','2026-01-01T00:00:00',('Ada Lovelace'),(''),"
            "'preprocessor','Some CAD 2026','');\n"
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 3 1 1 }'));\n"
            "ENDSEC;\n"
            "DATA;\n"
            "/* a comment, which may sit anywhere between tokens */\n"
            "#20 = CARTESIAN_POINT('',(1.,-2.5,3.0E1));\n"
            "#10 = ADVANCED_FACE('it''s mine',(#20),#30,.T.);\n"
            "#30 = ORIENTED_EDGE('',*,*,#20,.F.);\n"
            "#40 = LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(2.54E-02),#20);\n"
            "#50 = PRODUCT('caf\\X2\\00E9\\X0\\','',$,(#20));\n"
            "#60 = ( BOUNDED_CURVE() B_SPLINE_CURVE(3,(#20,#20,#20,#20),.UNSPECIFIED.,.F.,.F.)\n"
            "        RATIONAL_B_SPLINE_CURVE((1.,1.,0.5,1.)) REPRESENTATION_ITEM('') );\n"
            "ENDSEC;\n"
            "END-ISO-10303-21;\n";

    SF::Model model;
    Check(model.Parse(text, nullptr), "a Part 21 file parses");
    Check(model.Entities.size() == 6, "every instance is in the table, out of order or not");

    const SF::Entity* point = model.Find(20);
    Check(point && point->Is("CARTESIAN_POINT"), "an instance is found by its id");
    if (point) {
        const std::vector<double> coordinates = point->Param("CARTESIAN_POINT", 1).AsNumbers();
        Check(coordinates.size() == 3, "a nested list is a list");
        if (coordinates.size() == 3) {
            CheckNear(coordinates[0], 1.0, 1e-12, "reading a plain real");
            CheckNear(coordinates[1], -2.5, 1e-12, "a negative one");
            CheckNear(coordinates[2], 30.0, 1e-12, "and one in exponent form");
        }
    }

    const SF::Entity* face = model.Find(10);
    Check(face && face->Param("ADVANCED_FACE", 0).AsText() == "it's mine",
          "a doubled quote inside a string is one quote, not the end of it");
    Check(face && face->Param("ADVANCED_FACE", 3).AsBoolean() == true, ".T. reads as true");

    const SF::Entity* oriented = model.Find(30);
    Check(oriented && oriented->Param("ORIENTED_EDGE", 1).IsUnset(),
          "a derived attribute (*) reads as unset rather than as a value");
    Check(oriented && oriented->Param("ORIENTED_EDGE", 4).AsBoolean() == false,
          ".F. reads as false");

    const SF::Entity* measure = model.Find(40);
    Check(measure && measure->Param("LENGTH_MEASURE_WITH_UNIT", 0).Type == SF::ValueType::Typed,
          "a typed parameter keeps its keyword");
    if (measure && !measure->Param("LENGTH_MEASURE_WITH_UNIT", 0).Items.empty())
        CheckNear(measure->Param("LENGTH_MEASURE_WITH_UNIT", 0).Items[0].AsNumber(), 0.0254, 1e-12,
                  "and its value");

    const SF::Entity* product = model.Find(50);
    Check(product && product->Param("PRODUCT", 0).AsText() == "caf\xC3\xA9",
          "a \\X2\\ escape decodes to UTF-8, so an accented part name survives");
    Check(product && product->Param("PRODUCT", 2).IsUnset(), "$ reads as unset");

    // The complex instance: the reason a reader that handles only simple ones
    // cannot read curved geometry at all.
    const SF::Entity* spline = model.Find(60);
    Check(spline && spline->IsComplex(), "a complex instance is recognised as one");
    Check(spline && spline->Keyword().empty(),
          "and has no single keyword, because it is several types at once");
    Check(spline && spline->Is("B_SPLINE_CURVE") && spline->Is("RATIONAL_B_SPLINE_CURVE"),
          "each of its records is findable by name");
    if (spline) {
        Check(spline->Param("B_SPLINE_CURVE", 0).AsInteger() == 3,
              "a sub-record's attributes are its own, without the inherited name in front");
        const std::vector<double> weights =
                spline->Param("RATIONAL_B_SPLINE_CURVE", 0).AsNumbers();
        Check(weights.size() == 4 && std::fabs(weights[2] - 0.5) < 1e-12,
              "so the weights come from the record that actually holds them");
    }

    Check(model.SourceName() == "bracket.stp", "the header names the source file");
    Check(model.OriginatingSystem() == "Some CAD 2026", "and what wrote it");
    Check(!model.Schemas().empty() && model.Schemas()[0].find("AUTOMOTIVE_DESIGN") == 0,
          "and the schema it claims");
}

static void TestMalformed() {
    std::printf("Malformed input\n");

    StepConverter converter;
    Check(!converter.ValidateData({}), "empty data is rejected");
    Check(!converter.ValidateData(Bytes("solid teapot\nfacet normal 0 0 1\n")),
          "an ASCII STL is rejected");
    Check(!converter.ValidateData(Bytes("<html><body>not a model</body></html>")),
          "HTML is rejected");
    Check(converter.ValidateData(Bytes("ISO-10303-21;\nHEADER;\n")),
          "the magic line is what makes it a STEP file");
    Check(converter.ValidateData(Bytes("\xEF\xBB\xBF" "ISO-10303-21;\n")),
          "and a byte-order mark in front of it does not change that");

    ConversionOptions quiet;
    int warnings = 0;
    quiet.WarningCallback = [&warnings](const std::string&) { ++warnings; };

    // A file that stops mid-instance: the instances before it must survive.
    SF::Model truncated;
    Check(truncated.Parse(std::string("ISO-10303-21;\nDATA;\n"
                                      "#1 = CARTESIAN_POINT('',(0.,0.,0.));\n"
                                      "#2 = CARTESIAN_POINT('',(1.,0.,"),
                          nullptr),
          "a file truncated mid-instance still yields what came before it");
    Check(truncated.Entities.size() >= 1, "and does not lose the whole table");

    // A body referring to geometry the file never defines is common enough
    // that it has to be reported rather than crash or silently mesh wrong.
    warnings = 0;
    auto dangling = converter.ImportFromMemory(
            Bytes("ISO-10303-21;\nDATA;\n"
                  "#1 = MANIFOLD_SOLID_BREP('ghost',#2);\n"
                  "#2 = CLOSED_SHELL('',(#3));\n"
                  "#3 = ADVANCED_FACE('',(#4),#99,.T.);\n"
                  "ENDSEC;\nEND-ISO-10303-21;\n"),
            quiet);
    Check(dangling == nullptr, "a body whose surfaces do not exist yields no document");
    Check(warnings > 0, "and says why");

    Check(converter.ImportFromMemory(Bytes("not a step file at all"), quiet) == nullptr,
          "and something that is not STEP at all yields nothing");
}

// ===== SAMPLES =====

static std::shared_ptr<ModelDocument> Load(const std::string& path, ConversionOptions& options) {
    StepConverter converter;
    return converter.Import(path, options);
}

static void TestBox(const std::string& root) {
    std::printf("A hand-authored block\n");
    const std::string path = (std::filesystem::path(root) / "Box.step").string();

    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& m) { warnings.push_back(m); };
    auto document = Load(path, options);
    Check(document != nullptr, "the block reads");
    if (!document) return;
    for (const std::string& warning : warnings) std::printf("      warn: %s\n", warning.c_str());
    Check(warnings.empty(), "with nothing to report — every entity in it is understood");

    const BrepData& brep = document->Brep;
    Check(brep.Solids.size() == 1, "one solid");
    Check(brep.Faces.size() == 6, "six faces");
    Check(brep.Edges.size() == 12, "twelve edges");
    Check(brep.Vertices.size() == 8, "eight vertices");
    Check(brep.Surfaces.size() == 6, "six planes");
    Check(document->Meshes.empty(), "and no mesh, because nothing asked for one");

    Check(document->SourceUnit == ModelUnit::Millimeter,
          "the file's millimetre unit is recorded");
    CheckNear(document->UnitScaleToMeters, 1e-3, 1e-15, "as a scale to metres");
    Check(document->Up == UpAxis::ZUp, "and STEP is read as Z-up, which is CAD's convention");
    Check(document->Title == "Block \xC3\x97 hand authored",
          "the product name comes through, escapes and all");

    std::vector<std::string> problems;
    Check(brep.Validate(problems), "the topology is sound");
    for (const std::string& problem : problems) std::printf("      %s\n", problem.c_str());

    const Bounds3D bounds = brep.ComputeBounds();
    CheckNear(bounds.Size().x, 10.0, 1e-9, "the block is 10 across");
    CheckNear(bounds.Size().y, 20.0, 1e-9, "20 deep");
    CheckNear(bounds.Size().z, 30.0, 1e-9, "and 30 tall");

    Check(document->TessellateBreps() == 1, "it tessellates");
    Check(document->TotalFaceCount() == 12,
          "into exactly twelve triangles — a plane needs no more, at any tolerance");
    // The block's left face states its loop backwards with a .F. bound
    // orientation. A reader that ignores that flag builds that face inside
    // out, and the signed volume is the only measure that notices.
    CheckNear(MeshVolume(*document), 6000.0, 1e-6,
              "the signed volume is the block's, so every face — including the one whose "
              "bound orientation is .F. — is wound outward");
    CheckNear(MeshArea(*document), 2200.0, 1e-6, "and the surface area is the block's");
}

static void TestPin(const std::string& root) {
    std::printf("A cylindrical pin, in inches\n");
    const std::string path = (std::filesystem::path(root) / "Pin.step").string();

    ConversionOptions options;
    auto document = Load(path, options);
    Check(document != nullptr, "the pin reads");
    if (!document) return;

    const BrepData& brep = document->Brep;
    Check(brep.Faces.size() == 3, "three faces: a cylinder and two caps");
    Check(brep.Edges.size() == 3, "three edges: two circles and a seam");
    Check(brep.Vertices.size() == 2, "and two vertices, both on the seam");

    Check(document->SourceUnit == ModelUnit::Inch,
          "a conversion_based_unit of 0.0254 metres is recognised as the inch");
    CheckNear(document->UnitScaleToMeters, 0.0254, 1e-15, "with the scale it stated");

    // The seam edge is used twice by the side face's single loop, once each
    // way. Getting that wrong is what turns a cylinder into a slit tube.
    bool seamFound = false;
    for (const BrepLoop& loop : brep.Loops) {
        std::map<int, int> uses;
        for (const BrepCoedge& coedge : loop.Coedges) ++uses[coedge.Edge];
        for (const auto& entry : uses) if (entry.second == 2) seamFound = true;
    }
    Check(seamFound, "the seam edge appears twice in one loop, as a closed surface needs");

    std::vector<std::string> problems;
    Check(brep.Validate(problems), "and the shell still closes");
    for (const std::string& problem : problems) std::printf("      %s\n", problem.c_str());

    Check(document->Materials.size() == 2,
          "the presentation chain yields two colours, not one and not none");
    bool brass = false, red = false;
    for (const ModelMaterial& material : document->Materials) {
        if (material.Name == "brass") brass = true;
        if (material.Name == "red") red = true;
    }
    Check(brass && red, "named as the file named them");
    int overridden = 0;
    for (const BrepFace& face : brep.Faces)
        if (face.Material >= 0 && face.Material != brep.Solids[0].Material) ++overridden;
    Check(overridden == 1, "one face overrides the body's colour, and only one");

    // A chord approximation of a cylinder is inscribed, so the meshed volume
    // is under the true one and closes on it as the tolerance tightens.
    const double exact = kPi * 8.0 * 8.0 * 25.0;
    double previous = 0.0;
    for (double tolerance : {0.1, 0.01, 0.001}) {
        ModelDocument copy = *document;
        BrepTessellationOptions tessellation;
        tessellation.ChordTolerance = tolerance;
        copy.TessellateBreps(tessellation);
        const double volume = MeshVolume(copy);
        Check(volume < exact && volume > previous,
              "at tolerance " + std::to_string(tolerance) +
              " the meshed volume is under the exact one and closer than the last");
        std::printf("      tolerance %g: %zu triangles, volume %.4f of %.4f\n",
                    tolerance, copy.TotalFaceCount(), volume, exact);
        previous = volume;
    }
    Check(previous > exact * 0.9999,
          "and at the tightest tolerance it is within a hundredth of a percent");
}

static void TestNurbsSheet(const std::string& root) {
    std::printf("A rational NURBS sheet\n");
    const std::string path = (std::filesystem::path(root) / "NurbsSheet.step").string();

    ConversionOptions options;
    auto document = Load(path, options);
    Check(document != nullptr, "the sheet reads");
    if (!document) return;

    const BrepData& brep = document->Brep;
    Check(brep.Surfaces.size() == 1, "one surface");
    if (brep.Surfaces.empty()) return;
    const BrepSurface& patch = brep.Surfaces[0];
    Check(patch.Type == BrepSurfaceType::BSpline,
          "read from a complex instance as a B-spline surface");
    Check(patch.DegreeU == 2 && patch.DegreeV == 1, "with the degrees the file stated");
    Check(patch.ControlPointsU == 3 && patch.ControlPointsV == 2,
          "and a 3 by 2 control net, transposed out of STEP's u-major lists");
    Check(patch.NetWeights.size() == patch.ControlNet.size(),
          "the weights are read, which is what makes it rational");
    Check(patch.KnotsU.size() == 6 && patch.KnotsV.size() == 4,
          "and the knot multiplicities are expanded into full vectors");

    // The weights make this an exact quarter circle rather than a parabola, so
    // every point of the patch is exactly the radius from the axis. Nothing
    // but a correct rational evaluation gives that.
    double worst = 0.0;
    for (int i = 0; i <= 20; ++i)
        for (int j = 0; j <= 4; ++j) {
            const Vec3d point = patch.Evaluate(i / 20.0, j / 4.0, brep.Curves);
            worst = std::max(worst, std::fabs(std::sqrt(point.x * point.x + point.y * point.y) - 5.0));
        }
    CheckNear(worst, 0.0, 1e-12,
              "every point of the patch is exactly 5 from the axis — the weights are being used");

    Check(brep.Solids.size() == 1 && !brep.Solids[0].Closed,
          "a shell_based_surface_model is an open body, not a solid");

    BrepTessellationOptions tessellation;
    tessellation.ChordTolerance = 0.005;
    std::vector<std::string> problems;
    Check(document->TessellateBreps(tessellation, &problems) == 1, "and it meshes");
    for (const std::string& problem : problems) std::printf("      %s\n", problem.c_str());

    // The boundary carries no pcurves, so the reader had to invert the patch
    // numerically to find where the arcs run in (u, v). If that went wrong the
    // trimmed region is the wrong shape and the area says so.
    const double exact = 2.0 * kPi * 5.0 / 4.0 * 10.0;
    const double area = MeshArea(*document);
    Check(area < exact && area > exact * 0.999,
          "the meshed area is a quarter cylinder's, so the boundary was projected correctly");
    std::printf("      %zu triangles, area %.5f of %.5f\n",
                document->TotalFaceCount(), area, exact);
}

static void TestRoundTrip(const std::string& root) {
    std::printf("Writing STEP back out\n");
    StepConverter converter;

    for (const char* sample : {"Box.step", "Pin.step", "NurbsSheet.step"}) {
        const std::string path = (std::filesystem::path(root) / sample).string();
        ConversionOptions options;
        auto original = converter.Import(path, options);
        if (!original) { Check(false, std::string(sample) + " reads"); continue; }

        std::vector<uint8_t> written;
        Check(converter.ExportToMemory(*original, written, options),
              std::string(sample) + " writes back out");
        Check(!written.empty(), "producing a file");
        Check(converter.ValidateData(written), "which is recognisably STEP");

        auto again = converter.ImportFromMemory(written, options);
        Check(again != nullptr, "and reads back");
        if (!again) continue;

        Check(again->Brep.Solids.size() == original->Brep.Solids.size(), "same solids");
        Check(again->Brep.Faces.size() == original->Brep.Faces.size(), "same faces");
        Check(again->Brep.Edges.size() == original->Brep.Edges.size(), "same edges");
        Check(again->Brep.Surfaces.size() == original->Brep.Surfaces.size(), "same surfaces");
        Check(again->SourceUnit == original->SourceUnit,
              "and the same unit — a written inch part is still in inches");
        Check(again->Materials.size() == original->Materials.size(),
              "with every colour still attached");

        std::vector<std::string> problems;
        Check(again->Brep.Validate(problems), "the written topology is sound");
        for (const std::string& problem : problems) std::printf("      %s\n", problem.c_str());

        BrepTessellationOptions tessellation;
        tessellation.ChordTolerance = 0.01;
        ModelDocument before = *original, after = *again;
        before.TessellateBreps(tessellation);
        after.TessellateBreps(tessellation);
        // The surfaces are exact on both sides, so this is not "close enough":
        // the same tolerance on the same geometry must give the same mesh.
        CheckNear(MeshVolume(after), MeshVolume(before), 1e-9,
                  std::string(sample) + " meshes to the same volume after a round trip");
        Check(after.TotalFaceCount() == before.TotalFaceCount(), "and the same triangle count");
    }
}

static void TestMeshToStep() {
    std::printf("A mesh written as a faceted b-rep\n");

    // A unit cube as twelve triangles, wound outward.
    ModelMesh mesh;
    mesh.Name = "Cube";
    MeshPrimitive prim;
    prim.Mode = PrimitiveMode::Triangles;
    const double corners[8][3] = {{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    for (const auto& corner : corners) prim.Positions.emplace_back(corner[0], corner[1], corner[2]);
    const int triangles[12][3] = {{0,3,2},{0,2,1},{4,5,6},{4,6,7},{0,1,5},{0,5,4},
                                  {1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
    for (const auto& triangle : triangles)
        for (int corner : triangle) prim.Indices.push_back(static_cast<uint32_t>(corner));
    mesh.Primitives.push_back(std::move(prim));
    ModelDocument document = ModelDocument::FromSingleMesh(std::move(mesh), "Cube");

    StepConverter converter;
    ConversionOptions options;
    int warnings = 0;
    options.WarningCallback = [&warnings](const std::string&) { ++warnings; };

    std::vector<uint8_t> written;
    const bool wrote = converter.ExportToMemory(document, written, options);
    Check(wrote, "a document of triangles writes to STEP");
    Check(!written.empty(), "producing a file");
    Check(warnings > 0, "and says that a faceted b-rep is not a CAD model");

    auto back = converter.ImportFromMemory(written, options);
    Check(back != nullptr, "which reads back as a solid");
    if (!back) return;

    Check(back->Brep.Faces.size() == 12, "one planar face per triangle");
    Check(back->Brep.Vertices.size() == 8,
          "eight vertices, because the facets share them rather than each carrying its own");
    Check(back->Brep.Edges.size() == 18,
          "and eighteen edges — Euler's formula for a triangulated cube, which only holds "
          "if neighbouring facets share an edge");

    std::vector<std::string> problems;
    Check(back->Brep.Validate(problems),
          "the shell closes, so it is a solid rather than a pile of loose triangles");
    for (const std::string& problem : problems) std::printf("      %s\n", problem.c_str());

    back->TessellateBreps();
    CheckNear(MeshVolume(*back), 1.0, 1e-9, "and it encloses the cube's volume");
}

int main(int argc, char** argv) {
    TestParsing();
    TestMalformed();
    TestMeshToStep();
    if (argc > 1) {
        const std::string root = argv[1];
        TestBox(root);
        TestPin(root);
        TestNurbsSheet(root);
        TestRoundTrip(root);
    } else {
        std::printf("Samples: skipped (pass the media/models/STEP path to run them)\n");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
