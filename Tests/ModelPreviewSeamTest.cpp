// Tests/ModelPreviewSeamTest.cpp
// The seam between core's viewers and the 3D formats it cannot read.
//
// The Filer's thumbnails and the media viewer's 3D pane live in core; every
// format but STL lives in the Models plugin, which links against core. So
// core cannot call the plugin, and for a long time both viewers simply said
// `ext == "stl"` and stayed blind to nine formats the framework could already
// read. `UltraCanvasModelPreview.h` is the inversion: core declares what it
// wants, `RegisterModelFormatsPlugin()` fills it in.
//
// These assertions are about that contract rather than about any one format:
// that a build with no provider behaves exactly as it did before, that a
// provider widens both questions core asks, that clearing it narrows them
// back, and that .stl never depends on a provider at all. The sample half
// then proves the real provider — the same LoadModelDocument +
// ModelDocumentToMesh3D pair the plugin installs — turns each sample into
// triangles a preview can draw.
//
// argv[1] is media/models. Without it only the contract cases run.
//
// Version: 1.0.0
// Last Modified: 2026-09-11
// Author: UltraCanvas Framework

#include "UltraCanvasModelPreview.h"

#include "Models/UltraCanvasModelFormatsPlugin.h"
#include "Models/UltraCanvasModelMesh3D.h"

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

static bool Claims(const std::vector<std::string>& list, const std::string& ext) {
    return std::find(list.begin(), list.end(), ext) != list.end();
}

// Exactly what RegisterModelFormatsPlugin() installs, built here rather than
// called, so this suite needs the dispatch and the bridge but not the UI stack
// the IGraphicsPlugin façade drags in.
static ModelPreviewProvider RealProvider() {
    ModelPreviewProvider provider;
    provider.Extensions = [] {
        return UltraCanvasModelFormatsPlugin::SupportedLoadExtensions();
    };
    provider.Load = [](const std::string& path, Mesh3D& out) {
        ConversionOptions options;
        options.TriangulateOnImport = true;
        options.TessellateOnImport = true;
        auto document = UltraCanvasModelFormatsPlugin::LoadModelDocument(path, options);
        if (!document || document->Empty()) return false;
        out = ModelDocumentToMesh3D(*document);
        return !out.Empty();
    };
    return provider;
}

// ===== THE CONTRACT =====

static void TestWithoutProvider() {
    std::printf("No provider: core knows STL and nothing else\n");
    SetModelPreviewProvider(ModelPreviewProvider{});

    Check(CanPreviewModelExtension("stl"), ".stl previews with no plugin registered");
    Check(!CanPreviewModelExtension("obj"), "and .obj does not");
    Check(!CanPreviewModelExtension("fbx"), "nor .fbx");

    const std::vector<std::string> extensions = PreviewableModelExtensions();
    Check(extensions.size() == 1 && extensions[0] == "stl",
          "the previewable list is exactly {stl}");

    // The one thing a thumbnail worker must never do is hand a path to a
    // loader that cannot read it and wait to find out.
    Mesh3D mesh;
    Check(!LoadModelPreviewMesh("aircraft.obj", mesh),
          "loading an unclaimed extension fails without touching the file");
    Check(mesh.Empty(), "and leaves the mesh empty rather than half-filled");
}

static void TestProviderWidensBothQuestions() {
    std::printf("A provider widens what core will preview\n");

    // A synthetic provider, because the contract is not about which formats
    // the real one happens to carry: whatever it claims, core must accept.
    ModelPreviewProvider fake;
    fake.Extensions = [] { return std::vector<std::string>{"obj", "xyzzy"}; };
    fake.Load = [](const std::string&, Mesh3D& out) {
        out.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        out.indices = {0, 1, 2};
        return true;
    };
    SetModelPreviewProvider(fake);

    Check(CanPreviewModelExtension("obj"), "a claimed extension previews");
    Check(CanPreviewModelExtension("xyzzy"),
          "even one no format in the survey uses - core does not second-guess the list");
    Check(CanPreviewModelExtension("stl"), "and .stl still does, without the provider");
    Check(!CanPreviewModelExtension("fbx"), "an unclaimed one still does not");

    const std::vector<std::string> extensions = PreviewableModelExtensions();
    Check(Claims(extensions, "stl") && Claims(extensions, "obj") &&
                  Claims(extensions, "xyzzy"),
          "the previewable list is core's STL plus everything the provider claims");
    Check(std::is_sorted(extensions.begin(), extensions.end()) &&
                  std::adjacent_find(extensions.begin(), extensions.end()) == extensions.end(),
          "sorted and deduplicated, so a provider that also claims stl cannot double it");

    // Spelling. A file browser has a path, a dialog has ".OBJ", a format
    // switch has "obj", and all three have to reach the same answer.
    Check(CanPreviewModelExtension("/models/E-45.OBJ"), "a full path resolves");
    Check(CanPreviewModelExtension(".Obj"), "a dotted mixed-case extension resolves");
    Check(CanPreviewModelExtension("obj"), "a bare extension resolves");

    Mesh3D mesh;
    Check(LoadModelPreviewMesh("anything.obj", mesh) && mesh.TriangleCount() == 1,
          "and a load goes through the provider");
    Check(mesh.bounds.IsValid(),
          "with bounds computed for it, which the preview needs and a provider may skip");

    // A provider that reads the file but finds no triangles is a failure, not
    // an empty preview: the tile keeps its type glyph instead of showing a
    // blank square.
    ModelPreviewProvider empty;
    empty.Extensions = [] { return std::vector<std::string>{"obj"}; };
    empty.Load = [](const std::string&, Mesh3D&) { return true; };
    SetModelPreviewProvider(empty);
    Mesh3D nothing;
    Check(!LoadModelPreviewMesh("anything.obj", nothing),
          "a provider that returns no triangles reports failure, not an empty preview");

    // Half a provider is not a provider. Either call missing and core must
    // behave as though none was installed rather than calling through a null
    // std::function.
    ModelPreviewProvider halfBuilt;
    halfBuilt.Extensions = [] { return std::vector<std::string>{"obj"}; };
    SetModelPreviewProvider(halfBuilt);
    Check(!CanPreviewModelExtension("obj"),
          "a provider missing its loader is refused whole");

    SetModelPreviewProvider(ModelPreviewProvider{});
    Check(!CanPreviewModelExtension("obj") && CanPreviewModelExtension("stl"),
          "and clearing the provider narrows core back to STL");
}

static void TestDxfStaysADrawing() {
    std::printf("DXF ownership, from the preview side\n");
    SetModelPreviewProvider(RealProvider());

    // The Models plugin dispatches .dxf but deliberately does not claim it,
    // because a DXF is a drawing far more often than a model and the Vector
    // reader is the right default. That decision has to survive out here too,
    // or the Filer would start drawing wireframe thumbnails for floor plans.
    Check(!CanPreviewModelExtension("plan.dxf"),
          "a .dxf does not preview as a model, matching the dispatch's own refusal to claim it");
    Check(!Claims(PreviewableModelExtensions(), "dxf"),
          "and does not appear in the previewable list");
}

static void TestRealProviderClaimsWhatItReads() {
    std::printf("The real provider claims exactly what the dispatch reads\n");
    SetModelPreviewProvider(RealProvider());

    const std::vector<std::string> previewable = PreviewableModelExtensions();
    bool allClaimed = true;
    std::string missing;
    for (const std::string& extension :
         UltraCanvasModelFormatsPlugin::SupportedLoadExtensions()) {
        if (!Claims(previewable, extension)) {
            allClaimed = false;
            missing += (missing.empty() ? "" : ", ") + extension;
        }
    }
    Check(allClaimed, "every extension the dispatch reads is previewable" +
                              (missing.empty() ? std::string()
                                               : " (missing: " + missing + ")"));
    Check(Claims(previewable, "stl"),
          "and .stl is there too, which no converter in the dispatch provides");
}

// ===== THE SAMPLES =====

static void TestSamples(const std::string& mediaRoot) {
    std::printf("Every sample becomes triangles a preview can draw\n");
    SetModelPreviewProvider(RealProvider());

    struct Sample {
        const char* Extension;
        const char* RelativePath;
    };
    const std::vector<Sample> samples = {
            {"3ds", "3DS/E-45-Aircraft.3ds"},
            {"obj", "OBJ/E-45-Aircraft.obj"},
            {"abc", "Alembic/E-45-Aircraft.abc"},
            {"x", "XFile/E-45-Aircraft.x"},
#ifdef ULTRACANVAS_HAS_COLLADA_CONVERTER
            {"dae", "COLLADA/E-45-Aircraft.dae"},
#endif
#ifdef ULTRACANVAS_HAS_BLEND_CONVERTER
            {"blend", "Blend/E-45-Aircraft.blend"},
#endif
    };

    for (const Sample& sample : samples) {
        const std::filesystem::path path =
                std::filesystem::path(mediaRoot) / sample.RelativePath;
        if (!std::filesystem::exists(path)) {
            std::printf("  [SKIP] %s (sample not present)\n", sample.Extension);
            continue;
        }
        Mesh3D mesh;
        const bool loaded = LoadModelPreviewMesh(path.string(), mesh);
        Check(loaded && mesh.TriangleCount() > 0,
              std::string(sample.Extension) + " previews as triangles");
        if (!loaded) continue;
        // A preview projects the mesh into a square using its bounding radius,
        // so a degenerate bound divides by zero. Every sample must give one.
        Check(mesh.bounds.IsValid() && mesh.bounds.Radius() > 0.0f,
              std::string(sample.Extension) + " gives the preview a bounding sphere to frame");
        Check(mesh.normals.size() == mesh.positions.size(),
              std::string(sample.Extension) + " carries a normal per vertex, which the shading needs");
    }

    // STEP holds exact bodies and no mesh until something asks. The provider
    // asks - that is what TessellateOnImport is for - so a .step previews as
    // the solid it describes rather than as a blank tile.
    const std::filesystem::path box = std::filesystem::path(mediaRoot) / "STEP/Box.step";
    if (std::filesystem::exists(box)) {
        Mesh3D mesh;
        Check(LoadModelPreviewMesh(box.string(), mesh) && mesh.TriangleCount() > 0,
              "a STEP file of pure B-rep previews, because the provider tessellates on the way in");
    }

    // The cross-format figure this suite can pin without duplicating another:
    // the OBJ sample is the one every other suite measures at 8110 faces, and
    // a preview triangulates quads, so it must arrive as exactly twice that.
    const std::filesystem::path obj = std::filesystem::path(mediaRoot) / "OBJ/E-45-Aircraft.obj";
    if (std::filesystem::exists(obj)) {
        Mesh3D mesh;
        if (LoadModelPreviewMesh(obj.string(), mesh)) {
            Check(mesh.TriangleCount() == 8110 * 2,
                  "the OBJ's 8110 quads arrive as 16220 triangles, so nothing is dropped on the way");
        }
    }
}

int main(int argc, char** argv) {
    TestWithoutProvider();
    TestProviderWidensBothQuestions();
    TestDxfStaysADrawing();
    TestRealProviderClaimsWhatItReads();
    if (argc > 1) TestSamples(argv[1]);
    else std::printf("Samples: skipped (pass the media/models path to run them)\n");

    // Leave no provider behind: a suite that runs in the same process as
    // another must not decide what that one sees.
    SetModelPreviewProvider(ModelPreviewProvider{});

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
