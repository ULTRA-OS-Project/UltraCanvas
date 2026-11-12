// Tests/ModelIgesTest.cpp
// The IGES reader and writer: the 80-column card syntax, IGES 5.3 entity
// meanings, and the round trip back out.
//
// As for STEP, two kinds of assertion. The syntax cases are unit tests on
// decks written inline, because the corners of the format - a Hollerith
// string holding the delimiters, a D exponent, delimiters the global section
// redefines, a deck with no line ends - will not all turn up in one sample.
// The geometry cases run against media/3D/IGES, whose samples
// make_samples.py builds entity by entity without the C++ writer, and assert
// measurements: the block's meshed volume is 6000 or a face is inside out;
// the plate's area is short by exactly its hole or the trimming was wrong.
//
// argv[1] is media/3D. Without it only the syntax cases run.
//
// Version: 1.0.0
// Last Modified: 2026-11-12
// Author: UltraCanvas Framework

#include "Models/IGES/UltraCanvasIgesConverter.h"
#include "Models/IGES/UltraCanvasIgesFile.h"
#include "Models/STEP/UltraCanvasStepConverter.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "../UltraCanvas/include/UltraCanvasPathUtf8.h"

using namespace UltraCanvas;
using namespace UltraCanvas::ModelStorage;
using namespace UltraCanvas::ModelConverter;
namespace IF = UltraCanvas::IgesFile;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
static void CheckNear(double actual, double expected, double tolerance, const std::string& what) {
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("  [%s] %s (%.10g vs %.10g)\n", ok ? "PASS" : "FAIL", what.c_str(), actual,
                expected);
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

static std::string ReadText(const std::string& path) {
    std::ifstream file(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// One 80-column card.
static std::string Card(const std::string& text, char section, int number) {
    std::string line = text;
    line.resize(72, ' ');
    std::string sequence = std::to_string(number);
    return line + section + std::string(7 - sequence.size(), ' ') + sequence;
}

// A directory line from its nine fields, each right-justified in eight columns.
static std::string Fields(const std::vector<std::string>& fields) {
    std::string line;
    for (const std::string& field : fields) line += std::string(8 - field.size(), ' ') + field;
    return line;
}

// ===== SYNTAX =====

static void TestParsing() {
    std::printf("Card syntax\n");

    // A deck with its own delimiters ('/' and '#'), a Hollerith string that
    // contains both, a D exponent and an empty field.
    std::string deck;
    deck += Card("Inline deck", 'S', 1) + "\n";
    deck += Card("1H//1H#/6HPart/1/8Hpart.igs#", 'G', 1) + "\n";
    // Directory: a point, entity 116, parameters on line 1.
    deck += Card(Fields({"116", "1", "0", "0", "0", "0", "0", "0", "00010000"}), 'D', 1) + "\n";
    deck += Card(Fields({"116", "0", "3", "1", "0", "", "", "LABEL", "7"}), 'D', 2) + "\n";
    std::string parameters = "116/1.5D1/  -2.25 //3H/#,#";
    parameters.resize(64, ' ');
    deck += parameters + "       1" + "P" + "      1" + "\n";
    deck += Card("S      1G      1D      2P      1", 'T', 1) + "\n";

    IF::Model model;
    std::vector<std::string> warnings;
    const bool parsed = model.ParseText(deck, [&warnings](const std::string& w) { warnings.push_back(w); });
    Check(parsed, "a deck with its own delimiters parses");
    Check(model.ParameterDelimiter == '/' && model.RecordDelimiter == '#',
          "and the global section's first two fields redefine the delimiters");
    Check(model.GlobalText(3) == "Part/1", "a Hollerith string keeps the delimiter it contains");
    Check(model.Start == "Inline deck", "the start section is kept as text");

    const IF::Entity* point = model.At(1);
    Check(point != nullptr, "an entity is addressed by its first directory line");
    if (point) {
        Check(point->Type == 116, "with the type its directory gives");
        CheckNear(point->Real(0), 15.0, 0.0, "1.5D1 is a Fortran double: fifteen");
        CheckNear(point->Real(1), -2.25, 0.0, "a number may sit among blanks");
        Check(point->Param(2).IsEmpty(), "two delimiters in a row are an empty field");
        Check(point->Text(3) == "/#,", "a string may hold the delimiters, counted by its length");
        Check(point->Color == 3 && point->Label == "LABEL" && point->Subscript == 7,
              "colour, label and subscript come from the second directory line");
        Check(point->SubordinateSwitch() == 1 && point->EntityUseFlag() == 0,
              "and the status number splits into its two-digit fields");
    }

    // The same deck without line ends, as some older systems write it.
    std::string flat;
    std::istringstream lines(deck);
    std::string line;
    while (std::getline(lines, line)) flat += line;
    IF::Model unbroken;
    Check(unbroken.ParseText(flat) && unbroken.At(1) && unbroken.At(1)->Real(0) == 15.0,
          "a deck of 80-column cards with no line ends at all reads the same");

    // And with Windows line ends.
    std::string crlf;
    std::istringstream again(deck);
    while (std::getline(again, line)) crlf += line + "\r\n";
    IF::Model windows;
    Check(windows.ParseText(crlf) && windows.At(1) && windows.At(1)->Real(1) == -2.25,
          "and so does one with CR LF line ends");

    Check(IF::LooksLikeIgesFile(Bytes(deck)), "the sniffer recognises the deck");
    Check(!IF::LooksLikeIgesFile(Bytes("ISO-10303-21;\nHEADER;\n")), "and not a STEP file");

    // Number spelling: always a decimal point, never the locale's.
    Check(IF::FormatReal(5.0, 15) == "5.", "a whole real keeps its point: 5.");
    Check(IF::FormatReal(1e-7, 15) == "1.E-07", "an exponent is upper case and has its point");
    Check(IF::FormatReal(-2.5, 15) == "-2.5", "a fraction is written as it is");
    Check(IF::FormatReal(0.0, 15) == "0.", "and zero is 0.");
    Check(IF::FormatString("Steel") == "5HSteel", "a string is its length, H, and itself");
}

static void TestWriterLayout() {
    std::printf("Card layout\n");
    IF::FileWriter writer;
    IF::FileWriter::EntitySpec name;
    name.Type = 406;
    name.Form = 15;
    // Longer than a card's parameter field, so it has to be split.
    const std::string longName(150, 'x');
    name.Params = {"1", IF::FormatString(longName)};
    const int pointer = writer.Add(name);
    Check(pointer == 1, "the first entity is directory line 1");

    std::ostringstream out;
    writer.Write(out, "layout test", {IF::FormatString("p"), IF::FormatString("f")});
    const std::string text = out.str();

    bool all80 = true;
    std::istringstream lines(text);
    std::string line;
    size_t count = 0;
    while (std::getline(lines, line)) {
        ++count;
        if (line.size() != 80) all80 = false;
    }
    Check(all80 && count > 5, "every line is an 80-column card");

    IF::Model model;
    Check(model.ParseText(text), "the deck reads back");
    const IF::Entity* entity = model.At(pointer);
    Check(entity && entity->Type == 406 && entity->Form == 15,
          "with the entity at the pointer it was given");
    Check(entity && entity->Text(1) == longName,
          "and a string split across three cards comes back whole");
}

static void TestMalformed() {
    std::printf("What is not IGES\n");
    IgesConverter converter;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    Check(converter.ImportFromMemory(Bytes("hello\nworld\n"), options) == nullptr,
          "plain text is refused");
    Check(!warnings.empty(), "with a reason rather than silence");

    std::string compressed = Card("compressed", 'C', 1) + "\n";
    warnings.clear();
    Check(converter.ImportFromMemory(Bytes(compressed), options) == nullptr,
          "the compressed form is refused");
    bool named = false;
    for (const std::string& w : warnings)
        if (w.find("compressed") != std::string::npos) named = true;
    Check(named, "and the warning names it, so the user knows to export again");

    // Well-formed, but holding nothing a model can be built from.
    std::string pointsOnly;
    pointsOnly += Card("points", 'S', 1) + "\n";
    pointsOnly += Card("1H,,1H;;", 'G', 1) + "\n";
    pointsOnly += Card(Fields({"116", "1", "0", "0", "0", "0", "0", "0", "00000000"}), 'D', 1) + "\n";
    pointsOnly += Card(Fields({"116", "0", "0", "1", "0", "", "", "", "0"}), 'D', 2) + "\n";
    std::string parameters = "116,1.,2.,3.;";
    parameters.resize(64, ' ');
    pointsOnly += parameters + "       1P      1\n";
    pointsOnly += Card("S      1G      1D      2P      1", 'T', 1) + "\n";
    warnings.clear();
    Check(converter.ImportFromMemory(Bytes(pointsOnly), options) == nullptr,
          "a file of loose points is not a model");
    Check(!warnings.empty(), "and says so");
}

// ===== THE SAMPLES =====

static void TestBlock(const std::string& root) {
    std::printf("A block as a manifold solid B-rep object\n");
    IgesConverter converter;
    ConversionOptions options;
    std::vector<std::string> warnings;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
    const std::string path = (UltraCanvas::PathFromUtf8(root) / "IGES/Block.igs").string();
    Check(converter.ValidateFile(path), "the file is recognised as IGES");
    auto document = converter.Import(path, options);
    Check(document != nullptr, "the block reads");
    if (!document) return;
    for (const std::string& w : warnings) std::printf("      warning: %s\n", w.c_str());
    Check(warnings.empty(), "without a single warning");

    const BrepData& brep = document->Brep;
    Check(brep.Solids.size() == 1 && brep.Solids[0].Closed, "one closed solid");
    Check(!brep.Solids.empty() && brep.Solids[0].Name == "Block",
          "named by its name property, not its eight-character label");
    Check(brep.Faces.size() == 6, "six faces");
    Check(brep.Vertices.size() == 8, "eight vertices, shared through the vertex list");
    Check(brep.Edges.size() == 12, "twelve edges, each shared by two faces");
    std::vector<std::string> problems;
    Check(brep.Validate(problems), "and every edge is used once each way: it is a solid");
    for (const std::string& problem : problems) std::printf("      %s\n", problem.c_str());

    Check(document->SourceUnit == ModelUnit::Millimeter, "the global section's unit flag 2 is mm");
    Check(document->Title == "Block", "the sender's product id is the title");
    Check(document->Metadata["originatingSystem"] == "make_samples.py",
          "and the native system is kept");

    int blue = 0;
    for (const BrepFace& face : brep.Faces)
        if (face.Material >= 0 && document->Materials[static_cast<size_t>(face.Material)].Name ==
                                          "Blue anodised")
            ++blue;
    Check(blue == 1, "one face carries the colour definition, by name");

    ModelDocument meshed = *document;
    meshed.TessellateBreps();
    CheckNear(MeshVolume(meshed), 6000.0, 1e-9,
              "the meshed volume is 10 x 20 x 30: every face is the right way out");
}

static void TestBracket(const std::string& root) {
    std::printf("Trimmed surfaces: a plate with a hole, and a boss\n");
    IgesConverter converter;
    ConversionOptions options;
    std::vector<std::string> warnings;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
    const std::string path = (UltraCanvas::PathFromUtf8(root) / "IGES/Bracket.igs").string();
    auto document = converter.Import(path, options);
    Check(document != nullptr, "the bracket reads, though its global section uses the "
                               "default delimiters");
    if (!document) return;
    for (const std::string& w : warnings) std::printf("      warning: %s\n", w.c_str());
    Check(warnings.empty(), "without a warning");

    const BrepData& brep = document->Brep;
    Check(brep.Solids.size() == 1 && !brep.Solids[0].Closed,
          "trimmed surfaces with no topology are one open body");
    Check(brep.Faces.size() == 2, "two faces");
    if (brep.Faces.size() != 2) return;
    Check(brep.Faces[0].Loops.size() == 2, "the plate has an outer loop and a hole");
    Check(brep.Faces[1].Loops.size() == 1, "the boss's side is one loop");
    Check(brep.Surfaces[static_cast<size_t>(brep.Faces[1].Surface)].Type == BrepSurfaceType::Cylinder,
          "on a cylinder");

    size_t parameterCurves = 0;
    for (int loop : brep.Faces[0].Loops)
        for (const BrepCoedge& coedge : brep.Loops[static_cast<size_t>(loop)].Coedges)
            if (coedge.ParameterCurve >= 0) ++parameterCurves;
    Check(parameterCurves == 5, "the plate's five trimming curves in (u, v) are kept: they lie "
                                "on their edges");

    auto materialName = [&](int face) {
        const int material = brep.Faces[static_cast<size_t>(face)].Material;
        return material >= 0 ? document->Materials[static_cast<size_t>(material)].Name
                             : std::string();
    };
    Check(materialName(0) == "Aluminium", "the plate's colour is its colour definition");
    Check(materialName(1) == "Red", "and the boss's is standard colour 2");

    // The top circle is placed by a matrix that mirrors it, so it runs the
    // other way round; read wrong, the side folds over itself and comes up
    // short.
    ModelDocument meshed = *document;
    BrepTessellationOptions tessellation;
    tessellation.ChordTolerance = 0.0005;
    meshed.TessellateBreps(tessellation);
    const double plate = 40.0 * 30.0 - kPi * 25.0;
    const double side = 2.0 * kPi * 5.0 * 10.0;
    CheckNear(MeshArea(meshed), plate + side, (plate + side) * 1e-4,
              "the area is the plate less its hole, plus the boss's side");

    // A parameter curve that does not lie on its edge - here the hole's,
    // moved - is dropped, and the edge is projected instead.
    std::string text = ReadText(path);
    const std::string arc = "100,0.,20.,15.,25.,15.,25.,15.;";
    const size_t first = text.find(arc);
    const size_t second = first == std::string::npos ? first : text.find(arc, first + 1);
    Check(second != std::string::npos, "the sample has the hole twice, in space and in (u, v)");
    if (second == std::string::npos) return;
    text.replace(second, arc.size(), "100,0.,10.,15.,15.,15.,15.,15.;");
    warnings.clear();
    auto moved = converter.ImportFromMemory(Bytes(text), options);
    Check(moved != nullptr, "a file whose hole is misplaced in (u, v) still reads");
    if (!moved) return;
    bool said = false;
    for (const std::string& w : warnings)
        if (w.find("did not lie on their edges") != std::string::npos) said = true;
    Check(said, "and says the parameter curve was replaced");
    moved->TessellateBreps(tessellation);
    CheckNear(MeshArea(*moved), plate + side, (plate + side) * 1e-4,
              "with the hole still in the right place");
}

// ===== WRITING =====

static void TestRoundTrip(const std::string& root) {
    std::printf("Writing IGES back out\n");
    IgesConverter iges;
    StepConverter step;

    struct Case { const char* path; bool fromStep; };
    for (const Case& sample : {Case{"IGES/Block.igs", false}, Case{"IGES/Bracket.igs", false},
                               Case{"STEP/Box.step", true}, Case{"STEP/Pin.step", true},
                               Case{"STEP/NurbsSheet.step", true}}) {
        const std::string path = (UltraCanvas::PathFromUtf8(root) / sample.path).string();
        ConversionOptions options;
        auto original = sample.fromStep ? step.Import(path, options) : iges.Import(path, options);
        if (!original) { Check(false, std::string(sample.path) + " reads"); continue; }

        std::vector<uint8_t> written;
        Check(iges.ExportToMemory(*original, written, options),
              std::string(sample.path) + " writes as IGES");
        Check(iges.ValidateData(written), "which is recognisably IGES");
        const std::string text(written.begin(), written.end());
        bool all80 = true;
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line))
            if (line.size() != 80) all80 = false;
        Check(all80, "in 80-column cards");

        std::vector<std::string> warnings;
        options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
        auto again = iges.ImportFromMemory(written, options);
        Check(again != nullptr, "and reads back");
        if (!again) continue;
        for (const std::string& w : warnings) std::printf("      warning: %s\n", w.c_str());
        Check(warnings.empty(), "without a warning");

        Check(again->Brep.Solids.size() == original->Brep.Solids.size(), "same bodies");
        Check(again->Brep.Faces.size() == original->Brep.Faces.size(), "same faces");
        bool sameClosure = again->Brep.Solids.size() == original->Brep.Solids.size();
        for (size_t i = 0; sameClosure && i < again->Brep.Solids.size(); ++i)
            sameClosure = again->Brep.Solids[i].Closed == original->Brep.Solids[i].Closed;
        Check(sameClosure, "a solid is still a solid and a sheet still a sheet");
        Check(again->SourceUnit == original->SourceUnit, "the same unit");
        std::vector<std::string> problems;
        Check(again->Brep.Validate(problems) || !again->Brep.Solids[0].Closed,
              "and a closed body's topology still closes");
        for (const std::string& problem : problems) std::printf("      %s\n", problem.c_str());

        bool coloursKept = true;
        for (size_t i = 0; i < original->Brep.Faces.size() && i < again->Brep.Faces.size(); ++i) {
            auto nameOf = [](const ModelDocument& d, const BrepFace& face, int solidMaterial) {
                const int m = face.Material >= 0 ? face.Material : solidMaterial;
                return m >= 0 ? d.Materials[static_cast<size_t>(m)].Name : std::string();
            };
            const int before = original->Brep.Solids.empty() ? -1 : original->Brep.Solids[0].Material;
            const int after = again->Brep.Solids.empty() ? -1 : again->Brep.Solids[0].Material;
            if (nameOf(*original, original->Brep.Faces[i], before) !=
                nameOf(*again, again->Brep.Faces[i], after))
                coloursKept = false;
        }
        Check(coloursKept, "every face keeps its colour's name");

        BrepTessellationOptions tessellation;
        tessellation.ChordTolerance = 0.01;
        ModelDocument before = *original, after = *again;
        before.TessellateBreps(tessellation);
        after.TessellateBreps(tessellation);
        // Exact geometry both sides: the same tolerance gives the same mesh.
        CheckNear(MeshArea(after), MeshArea(before), MeshArea(before) * 1e-9,
                  std::string(sample.path) + " meshes to the same area after a round trip");
        CheckNear(MeshVolume(after), MeshVolume(before), std::fabs(MeshVolume(before)) * 1e-9 + 1e-9,
                  "and the same volume");
    }
}

static void TestFullPrecision(const std::string& root) {
    std::printf("Full precision\n");
    StepConverter step;
    IgesConverter iges;
    ConversionOptions options;
    options.Precision = NumericPrecision::Full;
    auto pin = step.Import((UltraCanvas::PathFromUtf8(root) / "STEP/Pin.step").string(), options);
    if (!pin) { Check(false, "the pin reads"); return; }
    // Move it far from the origin, where six or even fifteen digits are not
    // enough to say where it is.
    for (BrepVertex& vertex : pin->Brep.Vertices) vertex.Point = vertex.Point + Vec3d(1234567.8912345678, 0, 0);
    for (BrepCurve& curve : pin->Brep.Curves) curve.Placement.Origin = curve.Placement.Origin + Vec3d(1234567.8912345678, 0, 0);
    for (BrepSurface& surface : pin->Brep.Surfaces) surface.Placement.Origin = surface.Placement.Origin + Vec3d(1234567.8912345678, 0, 0);
    std::vector<uint8_t> written;
    Check(iges.ExportToMemory(*pin, written, options), "a part far from the origin writes");
    auto back = iges.ImportFromMemory(written, options);
    Check(back != nullptr, "and reads back");
    if (!back) return;
    bool exact = back->Brep.Vertices.size() == pin->Brep.Vertices.size();
    for (size_t i = 0; exact && i < back->Brep.Vertices.size(); ++i)
        exact = back->Brep.Vertices[i].Point.x == pin->Brep.Vertices[i].Point.x;
    Check(exact, "with every vertex bit for bit where it was");
}

static void TestMeshToIges() {
    std::printf("A mesh written as a faceted solid\n");
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

    IgesConverter converter;
    ConversionOptions options;
    int warnings = 0;
    options.WarningCallback = [&warnings](const std::string&) { ++warnings; };
    std::vector<uint8_t> written;
    Check(converter.ExportToMemory(document, written, options), "a document of triangles writes");
    Check(warnings > 0, "and says a faceted solid is not a CAD model");

    auto back = converter.ImportFromMemory(written, options);
    Check(back != nullptr, "which reads back as a solid");
    if (!back) return;
    Check(back->Brep.Faces.size() == 12, "one planar face per triangle");
    Check(back->Brep.Vertices.size() == 8, "eight shared vertices");
    Check(back->Brep.Edges.size() == 18, "and eighteen shared edges");
    std::vector<std::string> problems;
    Check(back->Brep.Validate(problems), "the shell closes");
    for (const std::string& problem : problems) std::printf("      %s\n", problem.c_str());
    back->TessellateBreps();
    CheckNear(MeshVolume(*back), 1.0, 1e-9, "and it encloses the cube's volume");
}

int main(int argc, char** argv) {
    TestParsing();
    TestWriterLayout();
    TestMalformed();
    TestMeshToIges();
    if (argc > 1) {
        const std::string root = argv[1];
        TestBlock(root);
        TestBracket(root);
        TestRoundTrip(root);
        TestFullPrecision(root);
    } else {
        std::printf("Samples: skipped (pass the media/3D path to run them)\n");
    }
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
