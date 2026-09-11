// Tests/ModelBlendTest.cpp
// The .blend inspector, and the converter that deliberately declines.
//
// The assertions here are as much about the decision as the code: a .blend is
// recognised, described, and refused with a reason. If someone later adds a
// geometry reader for it, these tests should be the first thing they read —
// the E-45 sample stores 1147 vertices behind Mirror, Subsurf and EdgeSplit
// modifiers, while the same model exported to OBJ is 11749, so importing the
// stored mesh would deliver a tenth of the aircraft.
//
// argv[1] is the .blend. Without it only the synthetic cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "Models/Blend/UltraCanvasBlendConverter.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelConverter;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

static bool Contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

static void TestRecognition() {
    std::printf("Recognition\n");
    BlendConverter conv;

    Check(!conv.ValidateData({}), "empty data is not a .blend");
    Check(!conv.ValidateData(std::vector<uint8_t>{'v', ' ', '0', ' ', '0', ' ', '0'}),
          "an OBJ is not a .blend");
    const std::vector<uint8_t> uncompressed = {'B','L','E','N','D','E','R','-','v','2','7','8'};
    Check(conv.ValidateData(uncompressed), "an uncompressed .blend is recognised by its magic");
    Check(conv.ValidateData(std::vector<uint8_t>{0x1f, 0x8b, 0x08, 0x00}),
          "a gzip wrapper is accepted — Blender writes compressed files by default");

    // Everything false: the report describes what this converter does, not
    // what the format could hold.
    const FormatCapabilities caps = conv.GetCapabilities();
    Check(!caps.Meshes && !caps.Materials && !caps.SceneGraph && !caps.Animations,
          "the capability report claims nothing, because nothing is imported");
    Check(conv.CanImport() && !conv.CanExport(),
          "it is an import-side converter that always declines");
}

static void TestDeclining() {
    std::printf("Declining\n");
    BlendConverter conv;
    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };

    // A zstd-compressed .blend — what Blender 3.0 and later write by default.
    const std::vector<uint8_t> zstd = {0x28, 0xb5, 0x2f, 0xfd, 0x00, 0x00};
    Check(conv.ImportFromMemory(zstd, options) == nullptr, "a zstd .blend imports as nothing");
    bool saidZstd = false;
    for (const auto& w : warnings)
        if (w.find("zstd") != std::string::npos) saidZstd = true;
    Check(saidZstd, "and the warning names zstd rather than failing obscurely");

    warnings.clear();
    const std::vector<uint8_t> notBlend = {'n', 'o', 'p', 'e'};
    Check(conv.ImportFromMemory(notBlend, options) == nullptr, "non-Blender data imports as nothing");
    Check(!warnings.empty(), "and says so");
}

static void TestSample(const char* path) {
    std::printf("Sample: %s\n", path);
    BlendConverter conv;
    Check(conv.ValidateFile(path), "the file is recognised as a .blend");

    const BlendFileInfo info = BlendConverter::Inspect(path);
    if (!info.Valid) {
        std::printf("  [FAIL] the file did not inspect: %s\n", info.Error.c_str());
        ++failures;
        return;
    }

    Check(info.Version == "2.78", "the header version is read");
    Check(info.PointerSize == 8 && !info.BigEndian, "pointer size and endianness are read");
    Check(info.Compression == "gzip", "the gzip wrapper is inflated");
    Check(info.BlockCount > 2000, "the whole block index is walked");

    // Datablock names come from the ID at the head of each struct, at an
    // offset computed from the SDNA rather than assumed.
    Check(info.ObjectNames.size() == 5, "all five objects are named");
    Check(Contains(info.ObjectNames, "Armature") && Contains(info.ObjectNames, "Cube.021"),
          "the names are the artist's, with Blender's type prefix stripped");
    Check(info.MeshNames.size() == 2 && Contains(info.MeshNames, "Cube.048"),
          "both meshes are named");
    Check(info.MaterialNames.size() == 3 && Contains(info.MaterialNames, "ship"),
          "all three materials are named");

    // The whole reason this is not a converter.
    Check(info.HasUnappliedModifiers(), "the file has unapplied modifiers");
    Check(Contains(info.Modifiers, "Mirror") && Contains(info.Modifiers, "Subsurf") &&
          Contains(info.Modifiers, "EdgeSplit"),
          "Mirror, Subsurf and EdgeSplit are all found, by SDNA struct name");
    Check(info.StoredVertexCount == 1147,
          "the stored cage is 1147 vertices — a tenth of the 11749 the OBJ export holds");
    Check(info.StoredPolygonCount == 1030, "and 1030 polygons");

    const std::string summary = info.Summary();
    Check(summary.find("2.78") != std::string::npos &&
          summary.find("Mirror") != std::string::npos,
          "the summary names the version and the modifiers");
    Check(summary.find("Export to glTF") != std::string::npos,
          "and tells the user what would actually work");

    std::vector<std::string> warnings;
    ConversionOptions options;
    options.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
    Check(conv.Import(path, options) == nullptr,
          "importing it as geometry yields nothing, on purpose");
    Check(warnings.size() == 1 && warnings[0].find("Mirror") != std::string::npos,
          "and the single warning carries the whole explanation");
}

int main(int argc, char** argv) {
    TestRecognition();
    TestDeclining();
    if (argc > 1) TestSample(argv[1]);
    else std::printf("Sample: skipped (pass a .blend path to run it)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
