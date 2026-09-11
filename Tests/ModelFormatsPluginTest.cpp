// Tests/ModelFormatsPluginTest.cpp
// The 3D format dispatch: one call, any format.
//
// Until this existed, five converters were reachable only by naming their
// classes — which is a library, not a plugin matrix. These assertions are
// about the seam rather than about any one format: that every claimed
// extension resolves to a converter, that every converter's own extension list
// agrees with the dispatch, that a document round-trips through
// LoadModelDocument / SaveModelDocument without the caller knowing which
// format it holds, and that .dxf is deliberately dispatchable but not claimed.
//
// One of them is about the seam *above* the dispatch rather than the dispatch
// itself: every claimed extension has to classify as a 3D model in
// GraphicsFormatDetector, because the registry and the FileLoader inventory
// ask that table, not the plugin, when deciding what a file is.
//
// argv[1] is media/models, whose per-format subdirectories hold the samples.
// Without it only the dispatch-table cases run.
//
// Version: 1.1.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "Models/UltraCanvasModelFormatsPlugin.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace UltraCanvas;
using namespace UltraCanvas::ModelConverter;

static int failures = 0;
static void Check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// Every format this build carries, and the sample that exercises it.
struct Sample {
    const char* Extension;
    const char* RelativePath;
    bool ImportsGeometry;   // false for a format that dispatches but declines
};

static const std::vector<Sample>& Samples() {
    static const std::vector<Sample> samples = {
            {"3ds", "3DS/E-45-Aircraft.3ds", true},
            {"obj", "OBJ/E-45-Aircraft.obj", true},
            {"dxf", "DXF/E-45-Aircraft.dxf", true},
            {"step", "STEP/Box.step", true},
            {"abc", "Alembic/E-45-Aircraft.abc", true},
            {"ply", "PLY/E-45-Aircraft.ply", true},
            {"x", "XFile/E-45-Aircraft.x", true},
            {"ms3d", "MS3D/E-45-Aircraft.ms3d", true},
#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
            {"dae", "COLLADA/E-45-Aircraft.dae", true},
#endif
#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
            {"x3d", "X3D/E-45-Aircraft.x3d", true},
#endif
#ifdef ULTRACANVAS_HAS_FBX_CONVERTER
            {"fbx", "FBX/E-45-Aircraft.fbx", true},
#endif
#ifdef ULTRACANVAS_HAS_BLEND_CONVERTER
            {"blend", "Blend/E-45-Aircraft.blend", true},
#endif
    };
    return samples;
}

static void TestDispatchTable() {
    std::printf("Dispatch table\n");

    for (const Sample& sample : Samples()) {
        auto converter =
                UltraCanvasModelFormatsPlugin::CreateConverterForExtension(sample.Extension);
        Check(converter != nullptr,
              std::string("'") + sample.Extension + "' resolves to a converter");
        if (!converter) continue;

        // The converter must agree that this is its extension — a dispatch
        // table that disagrees with its own converters is the classic way this
        // seam rots.
        const std::vector<std::string> own = converter->GetFileExtensions();
        const std::string dotted = std::string(".") + sample.Extension;
        Check(std::find(own.begin(), own.end(), dotted) != own.end(),
              std::string("and that converter claims ") + dotted + " itself");
    }

    // A path, a bare extension and a dotted extension all resolve the same,
    // and case does not matter.
    Check(UltraCanvasModelFormatsPlugin::CreateConverterForExtension("/models/ship.OBJ") != nullptr,
          "an upper-case path resolves");
    Check(UltraCanvasModelFormatsPlugin::CreateConverterForExtension(".obj") != nullptr,
          "a dotted extension resolves");
    Check(UltraCanvasModelFormatsPlugin::CreateConverterForExtension("obj") != nullptr,
          "a bare extension resolves");
    // Deliberately not a real format. This assertion has gone stale twice, once
    // when .abc gained a reader and once when .ply did, because it named a
    // format that was merely unsupported *yet*. An extension no one will ever
    // implement tests the same thing and cannot rot.
    Check(UltraCanvasModelFormatsPlugin::CreateConverterForExtension("notaformat") == nullptr,
          "an extension no converter claims resolves to nothing");

    // And says so rather than returning null in silence: a caller handed an
    // unreadable file has to be able to tell "unsupported" from "corrupt". The
    // example is an extension no one will ever implement, because naming a
    // format that is merely unsupported *yet* is how this assertion went stale
    // twice - once when .abc gained a reader, once when .fbx did.
    std::string reported;
    ConversionOptions listening;
    listening.WarningCallback = [&reported](const std::string& message) { reported = message; };
    Check(UltraCanvasModelFormatsPlugin::CreateConverterForExtension("notaformat") == nullptr,
          "an extension no format will ever use resolves to nothing, and cannot go stale");
    Check(UltraCanvasModelFormatsPlugin::LoadModelDocument("aircraft.notaformat", listening) == nullptr,
          "an unsupported extension loads nothing");
    Check(reported.find("notaformat") != std::string::npos &&
          reported.find("obj") != std::string::npos,
          "and the warning names both the format it cannot read and the ones it can");
    reported.clear();
    ModelStorage::ModelDocument empty;
    Check(!UltraCanvasModelFormatsPlugin::SaveModelDocument(empty, "out.notaformat", listening),
          "and saving to one writes nothing");
    Check(!reported.empty(), "with a warning rather than a silent false");

    Check(UltraCanvasModelFormatsPlugin::AvailableFormats().size() == Samples().size(),
          "AvailableFormats lists exactly what was built");
}

// .dxf is the one extension the plugin dispatches but does not claim: a DXF is
// a drawing far more often than a model, so the Vector plugin's reader stays
// the default for LoadGraphicsFile.
static void TestDxfIsNotClaimed() {
    std::printf("DXF ownership\n");
    const std::vector<std::string> claimed =
            UltraCanvasModelFormatsPlugin::SupportedLoadExtensions();

    Check(std::find(claimed.begin(), claimed.end(), "dxf") == claimed.end(),
          "the plugin does not claim .dxf, so it cannot steal it from the 2D reader");
    Check(UltraCanvasModelFormatsPlugin::CreateConverterForExtension("plan.dxf") != nullptr,
          "but an explicit caller still gets the 3D DXF converter");

    // Everything the plugin does claim must actually dispatch.
    bool allResolve = true;
    for (const std::string& extension : claimed)
        if (!UltraCanvasModelFormatsPlugin::CreateConverterForExtension(extension))
            allResolve = false;
    Check(allResolve, "every claimed extension resolves to a converter");

    // And everything it claims to write must have a writer.
    bool allWrite = true;
    for (const std::string& extension : UltraCanvasModelFormatsPlugin::SupportedSaveExtensions()) {
        auto converter = UltraCanvasModelFormatsPlugin::CreateConverterForExtension(extension);
        if (!converter || !converter->CanExport()) allWrite = false;
    }
    Check(allWrite, "every save extension has a converter that can actually export");
}

// Every extension the plugin claims has to survive the trip out to the rest of
// the framework, and that trip runs through GraphicsFormatDetector rather than
// through the plugin. A format the detector does not classify gets a
// GraphicsFileInfo whose formatType is Unknown, which makes IsValid() false -
// and CanHandle() used to ask IsValid() before asking whether any plugin had
// claimed the extension, so .step, .abc, .x, .blend and .ms3d were all refused
// by a registry that would have loaded them. Nothing in the dispatch tests
// could catch that: the dispatch was right and the gate in front of it was not.
static void TestEveryClaimedExtensionReachesTheFramework() {
    std::printf("Reach: the detector agrees with what the plugin claims\n");

    std::vector<std::string> claimed = UltraCanvasModelFormatsPlugin::SupportedLoadExtensions();
    for (const std::string& extension : UltraCanvasModelFormatsPlugin::SupportedSaveExtensions())
        if (std::find(claimed.begin(), claimed.end(), extension) == claimed.end())
            claimed.push_back(extension);

    bool allThreeD = true;
    std::string missed;
    for (const std::string& extension : claimed) {
        if (GraphicsFormatDetector::DetectFromExtension(extension) !=
            GraphicsFormatType::ThreeD) {
            allThreeD = false;
            missed += (missed.empty() ? "" : ", ") + extension;
        }
    }
    Check(allThreeD, "every claimed extension classifies as a 3D model" +
                     (missed.empty() ? std::string() : " (missed: " + missed + ")"));

    // The consequence, stated directly rather than inferred: a file named with
    // any claimed extension must describe itself as a valid 3D file that needs
    // a plugin, because that is what a file browser gates its preview on.
    bool allValid = true;
    for (const std::string& extension : claimed) {
        GraphicsFileInfo info("E-45-Aircraft." + extension);
        if (!info.IsValid() || !info.RequiresPlugin()) allValid = false;
    }
    Check(allValid, "and describes itself as a 3D file that needs a plugin");

    // .dxf stays Vector on purpose - the same decision TestDxfIsNotClaimed
    // pins from the plugin side, asserted here from the detector's.
    Check(GraphicsFormatDetector::DetectFromExtension("dxf") == GraphicsFormatType::Vector,
          "and .dxf is still classified as a drawing");
}

static void TestSamples(const std::string& mediaRoot) {
    std::printf("Loading every format through one call\n");

    size_t loaded = 0;
    for (const Sample& sample : Samples()) {
        const std::filesystem::path path = std::filesystem::path(mediaRoot) / sample.RelativePath;
        if (!std::filesystem::exists(path)) {
            std::printf("  [FAIL] missing sample %s\n", path.string().c_str());
            ++failures;
            continue;
        }

        ConversionOptions quiet;
        auto document = UltraCanvasModelFormatsPlugin::LoadModelDocument(path.string(), quiet);

        if (!sample.ImportsGeometry) {
            Check(document == nullptr,
                  std::string(sample.Extension) + " dispatches, and declines to import — as designed");
            continue;
        }

        Check(document != nullptr && !document->Empty(),
              std::string(sample.Extension) + " loads through LoadModelDocument with no format "
              "knowledge in the caller");
        if (document && !document->Empty()) ++loaded;
    }

    const size_t expected = std::count_if(Samples().begin(), Samples().end(),
                                          [](const Sample& s) { return s.ImportsGeometry; });
    Check(loaded == expected, "every geometry format in this build loaded");

#ifdef ULTRACANVAS_HAS_X3D_CONVERTER
    // X3D is one node set in two text encodings, and dispatch is by extension,
    // so only a load proves the second one arrives anywhere. The table above
    // covers the XML encoding; this covers the classic VRML one, which reaches
    // the same reader under a different extension entirely.
    {
        const std::filesystem::path vrml = std::filesystem::path(mediaRoot) /
                                           "VRML/E-45-Aircraft.wrl";
        ConversionOptions quiet;
        auto document = UltraCanvasModelFormatsPlugin::LoadModelDocument(vrml.string(), quiet);
        Check(document != nullptr && !document->Empty(),
              "and .wrl loads its classic VRML encoding through the same call as .x3d's XML");
    }
#endif
#ifdef ULTRACANVAS_HAS_FBX_CONVERTER
    // One extension, two file formats: .fbx names both the 7.x binary the table
    // above loads and the 6.x ASCII text below, which share neither a byte
    // layout nor an object model. Dispatch is by extension, so nothing but a
    // load proves the second one arrives anywhere.
    {
        const std::filesystem::path ascii = std::filesystem::path(mediaRoot) /
                                            "FBX/E-45-Aircraft-6.1-ascii.fbx";
        ConversionOptions quiet;
        auto document = UltraCanvasModelFormatsPlugin::LoadModelDocument(ascii.string(), quiet);
        Check(document != nullptr && !document->Empty(),
              "and .fbx loads its 6.x ASCII encoding through the same call as its 7.x binary");
    }
#endif

    // A B-rep format holds no triangles until something asks, so it needs its
    // own check: the dispatch has to give back a document that is not empty
    // even though it has no mesh in it.
    {
        const std::filesystem::path path = std::filesystem::path(mediaRoot) / "STEP/Box.step";
        ConversionOptions quiet;
        auto exact = UltraCanvasModelFormatsPlugin::LoadModelDocument(path.string(), quiet);
        Check(exact != nullptr && exact->Meshes.empty() && !exact->Brep.Solids.empty(),
              "a STEP file arrives as exact bodies with no mesh, through the same one call");
    }

    // The seam that matters most: read one format, write another, read it
    // back — without the caller naming a converter at either end.
    std::printf("Converting between formats through the dispatch\n");
    const std::filesystem::path source =
            std::filesystem::path(mediaRoot) / "3DS/E-45-Aircraft.3ds";
    ConversionOptions quiet;
    auto document = UltraCanvasModelFormatsPlugin::LoadModelDocument(source.string(), quiet);
    Check(document != nullptr, "the 3DS sample loads");
    if (!document) return;

    const std::filesystem::path target =
            std::filesystem::temp_directory_path() / "ultracanvas-dispatch.obj";
    Check(UltraCanvasModelFormatsPlugin::SaveModelDocument(*document, target.string(), quiet),
          "and saves as OBJ, dispatched by the output extension alone");

    auto again = UltraCanvasModelFormatsPlugin::LoadModelDocument(target.string(), quiet);
    Check(again != nullptr, "the written OBJ reads back through the same dispatch");
    if (again) {
        Check(again->TotalFaceCount() == document->TotalFaceCount(),
              "3DS -> ModelDocument -> OBJ -> ModelDocument keeps every face");
        Check(again->TotalVertexCount() == document->TotalVertexCount(),
              "and every vertex");
    }
    std::filesystem::remove(target);
    std::filesystem::remove(target.parent_path() / "ultracanvas-dispatch.mtl");
}

int main(int argc, char** argv) {
    TestDispatchTable();
    TestDxfIsNotClaimed();
    TestEveryClaimedExtensionReachesTheFramework();
    if (argc > 1) TestSamples(argv[1]);
    else std::printf("Samples: skipped (pass the media/models path to run them)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
