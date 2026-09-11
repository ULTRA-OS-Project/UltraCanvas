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
// argv[1] is media/models, whose per-format subdirectories hold the samples.
// Without it only the dispatch-table cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-10
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
    bool ImportsGeometry;   // .blend deliberately does not
};

static const std::vector<Sample>& Samples() {
    static const std::vector<Sample> samples = {
            {"3ds", "3DS/E-45-Aircraft.3ds", true},
            {"obj", "OBJ/E-45-Aircraft.obj", true},
            {"dxf", "DXF/E-45-Aircraft.dxf", true},
            {"step", "STEP/Box.step", true},
            {"abc", "Alembic/E-45-Aircraft.abc", true},
#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
            {"dae", "COLLADA/E-45-Aircraft.dae", true},
#endif
#ifdef ULTRACANVAS_HAS_FBX_CONVERTER
            {"fbx", "FBX/E-45-Aircraft.fbx", true},
#endif
#ifdef ULTRACANVAS_HAS_BLEND_CONVERTER
            {"blend", "Blend/E-45-Aircraft.blend", false},
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
    Check(UltraCanvasModelFormatsPlugin::CreateConverterForExtension("notaformat") == nullptr,
          "a format this build has no converter for resolves to nothing");

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
    if (argc > 1) TestSamples(argv[1]);
    else std::printf("Samples: skipped (pass the media/models path to run them)\n");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
