// Apps/DemoApp/UltraCanvasModelFormatsExamples.cpp
// "3D Model Formats" demo page - the Models plugin's whole converter matrix,
// in the 3D Graphics section beside the STL page and the OpenGL showcase.
//
// Until now the 3D section showed exactly two readers: STL, and the demo's own
// Wavefront OBJ parser inside the GL tab. Everything the plugin gained since -
// 3DS, COLLADA, FBX, Alembic, DirectX .x, MilkShape, STEP - was reachable
// through FileLoader and the Filer but appeared nowhere a visitor would look
// for 3D support.
//
// Each sample is read by UltraCanvasModelFormatsPlugin::LoadModelDocument into
// a ModelStorage::ModelDocument - the universal structure, not a per-format
// one - and the page reports what that document turned out to hold. The
// interesting part of a format survey is precisely where the formats differ:
// a .3ds has no scene graph to speak of, a .dae arrives as a node hierarchy
// with materials, an .abc is a sampled cache, and a .step carries no triangles
// at all until something asks for them.
//
// The same document is then flattened to a Mesh3D and shown in an
// UltraCanvasSTLElement, which orbits it on GL builds and draws a shaded still
// otherwise - so this page is useful in a build with no OpenGL at all.
//
// Samples are the SMALL ones on purpose: every file here is under 600 kB, and
// the page names the large ones it skips rather than pretending they are
// unsupported. See kSamples below.
//
// Version: 1.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#ifdef ULTRACANVAS_HAS_MODELS_PLUGIN

#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"

#include "Models/STL/UltraCanvasSTLElement.h"
#include "Models/UltraCanvasModelFormatsPlugin.h"
#include "Models/UltraCanvasModelMesh3D.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace UltraCanvas {

namespace {

    using ModelConverter::ConversionOptions;
    using ModelConverter::FormatCapabilities;
    using ModelStorage::ModelDocument;

    // ===== THE SAMPLES =====

    struct SampleSpec {
        const char* relativePath;   // under media/models/
        const char* extension;      // what the dispatch is asked for
        const char* note;           // why this one is worth looking at
    };

    // Ordered smallest file first, which also happens to run from the most
    // compact binary format to the most verbose. Everything here is under
    // 600 kB so the page stays responsive even though each sample is parsed
    // on demand - the larger samples in media/models (the 18 MB .blend, the
    // 6.9 MB VRML, the 3.9 MB PLY) are read by the same dispatch and are
    // deliberately not listed; see kOmittedNote.
    const std::vector<SampleSpec>& Samples() {
        static const std::vector<SampleSpec> kSamples = {
                {"STEP/Pin.step", "step",
                 "Exact B-rep: analytic surfaces closed into a solid. No mesh\n"
                 "exists in the file - the triangles below were tessellated\n"
                 "on import, which is what TessellateOnImport asks for."},
                {"STEP/NurbsSheet.step", "step",
                 "A trimmed NURBS sheet rather than a solid. Same reader, and\n"
                 "the tessellation tolerance decides the triangle count."},
                {"MS3D/E-45-Aircraft.ms3d", "ms3d",
                 "A game format: a fixed sequence of packed little-endian\n"
                 "structs, no chunks or offsets. Groups become meshes, and\n"
                 "smoothing groups arrive as a bitmask."},
                {"FBX/E-45-Aircraft.fbx", "fbx",
                 "Binary FBX - the same model as the 2.2 MB ASCII variant in\n"
                 "media/models/FBX, an order of magnitude smaller. What the\n"
                 "header declared lands in ModelDocument::Metadata as\n"
                 "fbx.version, fbx.encoding and fbx.application."},
                {"Alembic/E-45-Aircraft.abc", "abc",
                 "A sampled cache rather than a scene description: geometry is\n"
                 "stored per time sample. This reader takes the first sample."},
                {"COLLADA/E-45-Aircraft.dae", "dae",
                 "XML interchange with a real scene graph and materials, so\n"
                 "the node and material counts below are non-trivial.\n"
                 "Compile-time optional - it needs tinyxml2."},
                {"3DS/E-45-Aircraft.3ds", "3ds",
                 "Chunked binary from 1990s 3D Studio. Sixteen-bit indices and\n"
                 "a flat object list, so a mesh per object and no hierarchy."},
                {"XFile/E-45-Aircraft.x", "x",
                 "DirectX retained-mode .x, text encoding. Templates declare\n"
                 "their own layout, so the parser is driven by the file."},
        };
        return kSamples;
    }

    const char* kOmittedNote =
            "Also read, but omitted here for size: .blend (18 MB), VRML .wrl\n"
            "(6.9 MB), PLY (3.9 MB), DXF (2.6 MB), ASCII FBX (2.2 MB), X3D\n"
            "(1.4 MB) and OBJ (1.3 MB). Same dispatch, same document.";

    // ===== LOADING =====

    struct FormatSample {
        std::string path;
        std::string fileName;
        std::string extension;
        std::string note;

        // What the dispatch says about the format, whether or not the file loads.
        bool converterAvailable = false;
        std::string formatName;
        std::string formatVersion;
        bool canExport = false;
        FormatCapabilities capabilities;

        // What the file turned out to hold.
        bool attempted = false;
        bool loaded = false;
        std::shared_ptr<ModelDocument> document;
        Mesh3D mesh;
        std::uintmax_t bytes = 0;
        double milliseconds = 0;
        std::vector<std::string> warnings;
        std::string error;
    };

    void DescribeConverter(FormatSample& sample) {
        auto converter = UltraCanvasModelFormatsPlugin::CreateConverterForExtension(
                sample.extension);
        if (!converter) {
            sample.formatName = "." + sample.extension + " (no converter in this build)";
            return;
        }
        sample.converterAvailable = true;
        sample.formatName = converter->GetFormatName();
        sample.formatVersion = converter->GetFormatVersion();
        sample.canExport = converter->CanExport();
        sample.capabilities = converter->GetCapabilities();
    }

    void LoadSample(FormatSample& sample) {
        sample.attempted = true;

        std::error_code ec;
        sample.bytes = std::filesystem::file_size(sample.path, ec);
        if (ec) sample.bytes = 0;

        if (!std::filesystem::exists(sample.path, ec)) {
            sample.error = "Sample not found: " + sample.path;
            return;
        }
        if (!sample.converterAvailable) {
            sample.error = "This build has no converter for ." + sample.extension;
            return;
        }

        ConversionOptions options;
        // The viewer draws triangles and nothing else, so ask for them: n-gons
        // reduced, and the exact bodies in a STEP file turned into a mesh. A
        // converter writing STEP to IGES would want both of these off.
        options.TriangulateOnImport = true;
        options.TessellateOnImport = true;
        options.WarningCallback = [&sample](const std::string& message) {
            // Approximations and dropped features are part of what this page is
            // for, so they are shown rather than logged and forgotten.
            if (sample.warnings.size() < 6) sample.warnings.push_back(message);
        };

        const auto start = std::chrono::steady_clock::now();
        sample.document = UltraCanvasModelFormatsPlugin::LoadModelDocument(sample.path, options);
        if (sample.document) sample.mesh = ModelDocumentToMesh3D(*sample.document);
        sample.milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();

        if (!sample.document) {
            sample.error = "The reader returned no document";
            return;
        }
        // A document with exact bodies and no triangles is a successful read,
        // not a failure - it just has nothing for this viewer to draw.
        sample.loaded = true;
    }

    // ===== REPORTING =====

    std::string HumanSize(std::uintmax_t bytes) {
        char buf[48];
        if (bytes >= 1024ull * 1024ull) {
            std::snprintf(buf, sizeof(buf), "%.1f MB",
                          static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024ull) {
            std::snprintf(buf, sizeof(buf), "%.1f kB", static_cast<double>(bytes) / 1024.0);
        } else {
            std::snprintf(buf, sizeof(buf), "%llu bytes",
                          static_cast<unsigned long long>(bytes));
        }
        return buf;
    }

    const char* UpAxisName(ModelStorage::UpAxis axis) {
        return axis == ModelStorage::UpAxis::ZUp ? "Z-up" : "Y-up";
    }

    std::string UnitText(const ModelDocument& doc) {
        const char* symbol = ModelStorage::ModelUnitSymbol(doc.SourceUnit);
        if (!symbol || !*symbol) return "unitless (the format does not state one)";
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s (1 unit = %g m)", symbol, doc.UnitScaleToMeters);
        return buf;
    }

    // What the reader found, which is the whole point of the page.
    std::string DescribeDocument(const FormatSample& sample) {
        if (!sample.loaded) {
            return "Could not read " + sample.fileName + "\n" +
                   (sample.error.empty() ? std::string("(unknown error)") : sample.error);
        }

        const ModelDocument& doc = *sample.document;
        const ModelStorage::Bounds3D bounds = doc.ComputeBounds();
        const ModelStorage::Vec3d size = bounds.IsValid() ? bounds.Size()
                                                          : ModelStorage::Vec3d{0, 0, 0};

        char buf[1200];
        std::snprintf(buf, sizeof(buf),
                      "File        %s (%s)\n"
                      "Format      %s%s%s\n"
                      "Read in     %.0f ms\n"
                      "\n"
                      "Title       %s\n"
                      "Generator   %s\n"
                      "Space       %s, %s, %s\n"
                      "\n"
                      "Scenes      %zu       Nodes      %zu\n"
                      "Meshes      %zu       Materials  %zu\n"
                      "Images      %zu       Animations %zu\n"
                      "Cameras     %zu       Lights     %zu\n"
                      "B-rep solids %zu\n"
                      "\n"
                      "Vertices    %zu\n"
                      "Faces       %zu\n"
                      "Triangles drawn %zu\n"
                      "Extent      %.2f x %.2f x %.2f",
                      sample.fileName.c_str(), HumanSize(sample.bytes).c_str(),
                      sample.formatName.c_str(),
                      sample.formatVersion.empty() ? "" : " ",
                      sample.formatVersion.c_str(),
                      sample.milliseconds,
                      doc.Title.empty() ? "(none)" : doc.Title.c_str(),
                      doc.Generator.empty() ? "(none)" : doc.Generator.c_str(),
                      UpAxisName(doc.Up),
                      doc.Chirality == ModelStorage::Handedness::RightHanded
                              ? "right-handed" : "left-handed",
                      UnitText(doc).c_str(),
                      doc.Scenes.size(), doc.Nodes.size(),
                      doc.Meshes.size(), doc.Materials.size(),
                      doc.Images.size(), doc.Animations.size(),
                      doc.Cameras.size(), doc.Lights.size(),
                      doc.Brep.Solids.size(),
                      doc.TotalVertexCount(), doc.TotalFaceCount(),
                      sample.mesh.TriangleCount(),
                      size.x, size.y, size.z);
        return buf;
    }

    // The converter's own capability flags, so the page states what the format
    // can carry rather than what this one file happens to use.
    std::string DescribeCapabilities(const FormatSample& sample) {
        if (!sample.converterAvailable) {
            return "No converter for ." + sample.extension + " in this build.\n\n"
                   "COLLADA needs tinyxml2 and the .blend and FBX readers need\n"
                   "zlib, so each is a separate build option. Ask\n"
                   "SupportedLoadExtensions() rather than assuming.";
        }

        const FormatCapabilities& c = sample.capabilities;
        auto tick = [](bool on) { return on ? "yes" : " - "; };

        char buf[900];
        std::snprintf(buf, sizeof(buf),
                      "Read  yes        Write %s\n"
                      "\n"
                      "Meshes        %s    N-gons        %s\n"
                      "Scene graph   %s    Instancing    %s\n"
                      "Materials     %s    PBR materials %s\n"
                      "Textures      %s    Embedded      %s\n"
                      "Vertex colors %s    Point clouds  %s\n"
                      "B-rep         %s    NURBS         %s\n"
                      "\n"
                      "%s",
                      sample.canExport ? "yes" : "read-only",
                      tick(c.Meshes), tick(c.NGons),
                      tick(c.SceneGraph), tick(c.Instancing),
                      tick(c.Materials), tick(c.PBRMaterials),
                      tick(c.Textures), tick(c.EmbeddedTextures),
                      tick(c.VertexColors), tick(c.PointClouds),
                      tick(c.Brep), tick(c.NurbsSurfaces),
                      sample.note.c_str());
        return buf;
    }

    // One line for the status bar, plus any warnings the import reported.
    std::string SummariseSample(const FormatSample& sample) {
        if (!sample.loaded) {
            return "Failed: " + sample.fileName + " - " + sample.error;
        }
        char buf[320];
        std::snprintf(buf, sizeof(buf), "%s - %s, %zu triangles, read in %.0f ms",
                      sample.fileName.c_str(), sample.formatName.c_str(),
                      sample.mesh.TriangleCount(), sample.milliseconds);
        std::string line = buf;
        if (!sample.warnings.empty()) {
            line += "   |   warning: " + sample.warnings.front();
            if (sample.warnings.size() > 1) {
                line += " (+" + std::to_string(sample.warnings.size() - 1) + " more)";
            }
        }
        return line;
    }

    const std::vector<std::pair<std::string, Vec3>>& ModelColors() {
        static const std::vector<std::pair<std::string, Vec3>> colors = {
                {"Steel",  Vec3(0.78f, 0.80f, 0.85f)},
                {"Brass",  Vec3(0.85f, 0.68f, 0.32f)},
                {"Copper", Vec3(0.80f, 0.45f, 0.30f)},
                {"Jade",   Vec3(0.40f, 0.75f, 0.60f)},
                {"Resin",  Vec3(0.90f, 0.90f, 0.94f)},
        };
        return colors;
    }

    std::shared_ptr<UltraCanvasButton> MakeToolButton(const std::string& id, int x, int y, int w,
                                                      const std::string& text,
                                                      std::function<void()> action) {
        auto btn = std::make_shared<UltraCanvasButton>(id, x, y, w, 30);
        btn->SetText(text);
        btn->SetColors(Color(60, 60, 65, 255));
        btn->SetTextColors(Colors::White);
        btn->onClick = std::move(action);
        return btn;
    }

} // namespace

// ===== MODEL FORMATS DEMO PAGE =====
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateModelFormatsExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("ModelFormatsExamples", 0, 0, 1000, 780);
    container->SetBackgroundColor(Color(245, 245, 245, 255));

    auto title = std::make_shared<UltraCanvasLabel>("MFTitle", 10, 10, 800, 30);
    title->SetText("3D Model Formats - One Document Structure, Nine Readers");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    container->AddChild(title);

    auto description = std::make_shared<UltraCanvasLabel>("MFDescription", 10, 45, 960, 44);
    description->SetText(
            "Every sample is read into the same ModelStorage::ModelDocument, so the panels on the right show where the\n"
            "formats genuinely differ - scene graph, materials, units, up-axis, exact solids - rather than nine of the same row.");
    description->SetFontSize(12);
    description->SetTextColor(Color(80, 80, 80, 255));
    container->AddChild(description);

    auto statusLabel = std::make_shared<UltraCanvasLabel>("MFStatus", 10, 740, 980, 30);
    statusLabel->SetFontSize(11);
    statusLabel->SetTextColor(Color(60, 60, 60, 255));
    statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
    container->AddChild(statusLabel);

    // ===== SAMPLE TABLE =====
    const std::string modelsDir = NormalizePath(GetResourcesDir() + "media/models/");
    auto samples = std::make_shared<std::vector<FormatSample>>();
    for (const SampleSpec& spec : Samples()) {
        FormatSample sample;
        sample.path = NormalizePath(modelsDir + spec.relativePath);
        sample.fileName = std::filesystem::path(sample.path).filename().string();
        sample.extension = spec.extension;
        sample.note = spec.note;
        // Cheap, and it decides what the capability panel can say even for a
        // sample the user never selects.
        DescribeConverter(sample);
        samples->push_back(std::move(sample));
    }

    // ===== STATS PANEL =====
    auto statsContainer = std::make_shared<UltraCanvasContainer>("MFStatsPanel", 660, 100, 320, 390);
    statsContainer->SetBackgroundColor(Colors::White);
    statsContainer->SetBorders(2, Color(180, 180, 180, 255));

    auto statsTitle = std::make_shared<UltraCanvasLabel>("MFStatsTitle", 10, 10, 300, 25);
    statsTitle->SetText("What the reader found");
    statsTitle->SetFontWeight(FontWeight::Bold);
    statsTitle->SetFontSize(13);
    statsContainer->AddChild(statsTitle);

    auto statsText = std::make_shared<UltraCanvasLabel>("MFStatsText", 10, 38, 300, 344);
    statsText->SetFontSize(11);
    statsText->SetTextColor(Color(50, 50, 50, 255));
    statsContainer->AddChild(statsText);
    container->AddChild(statsContainer);

    // ===== CAPABILITY PANEL =====
    auto capsContainer = std::make_shared<UltraCanvasContainer>("MFCapsPanel", 660, 500, 320, 230);
    capsContainer->SetBackgroundColor(Color(240, 248, 255, 255));
    capsContainer->SetBorders(2, Color(100, 149, 237, 255));

    auto capsTitle = std::make_shared<UltraCanvasLabel>("MFCapsTitle", 10, 8, 300, 22);
    capsTitle->SetText("What the format can carry");
    capsTitle->SetFontWeight(FontWeight::Bold);
    capsTitle->SetFontSize(13);
    capsContainer->AddChild(capsTitle);

    auto capsText = std::make_shared<UltraCanvasLabel>("MFCapsText", 10, 34, 300, 190);
    capsText->SetFontSize(11);
    capsText->SetTextColor(Color(50, 50, 50, 255));
    capsContainer->AddChild(capsText);
    container->AddChild(capsContainer);

    // ===== VIEWER PANEL =====
    auto viewerPanel = std::make_shared<UltraCanvasContainer>("MFViewerPanel", 20, 100, 620, 520);
    viewerPanel->SetBackgroundColor(Colors::White);
    viewerPanel->SetBorders(2, Color(180, 180, 180, 255));
    container->AddChild(viewerPanel);

    // Named for STL, but it takes a Mesh3D and knows nothing about where the
    // mesh came from - a GL-orbited view where there is GL, a shaded software
    // still where there is not.
    auto viewer = std::make_shared<UltraCanvasSTLElement>("MFViewer", 10, 10, 600, 430);
    viewerPanel->AddChild(viewer);

    auto nameLabel = std::make_shared<UltraCanvasLabel>("MFName", 10, 486, 600, 24);
    nameLabel->SetFontSize(11);
    nameLabel->SetAlignment(TextAlignment::Center);
    nameLabel->SetTextColor(Color(60, 60, 60, 255));
    viewerPanel->AddChild(nameLabel);

    auto currentIndex = std::make_shared<size_t>(0);
    auto colorIndex = std::make_shared<size_t>(0);
    auto autoRotate = std::make_shared<bool>(true);

    // Parsed on demand and cached: opening the page costs one file rather than
    // eight, and switching back to a sample already seen costs nothing.
    auto showSample = [samples, currentIndex, viewer, nameLabel, statsText, capsText,
                       statusLabel](size_t index) {
        if (index >= samples->size()) return;
        FormatSample& sample = (*samples)[index];
        if (!sample.attempted) LoadSample(sample);

        *currentIndex = index;
        // A STEP file of pure B-rep can read successfully and still hold no
        // triangles; leaving the previous mesh up would misreport that, so the
        // viewer is cleared rather than left stale.
        viewer->SetMesh(sample.mesh);

        char counter[64];
        std::snprintf(counter, sizeof(counter), "  (%zu of %zu)", index + 1, samples->size());
        nameLabel->SetText(sample.formatName + " - " + sample.fileName + counter);
        statsText->SetText(DescribeDocument(sample));
        capsText->SetText(DescribeCapabilities(sample));
        statusLabel->SetText(SummariseSample(sample));
    };

    showSample(0);
    viewer->SetModelColor(ModelColors()[0].second);
    viewer->SetAutoRotate(*autoRotate);

    // ===== VIEWER CONTROLS =====
    auto rotateBtn = MakeToolButton("MFRotate", 10, 450, 130, "Auto-rotate: on", nullptr);
    rotateBtn->onClick = [viewer, rotateBtn, autoRotate]() {
        *autoRotate = !*autoRotate;
        viewer->SetAutoRotate(*autoRotate);
        rotateBtn->SetText(*autoRotate ? "Auto-rotate: on" : "Auto-rotate: off");
    };
    viewerPanel->AddChild(rotateBtn);

    auto materialBtn = MakeToolButton("MFMaterial", 150, 450, 130,
                                      "Material: " + ModelColors()[0].first, nullptr);
    materialBtn->onClick = [viewer, materialBtn, colorIndex]() {
        *colorIndex = (*colorIndex + 1) % ModelColors().size();
        viewer->SetModelColor(ModelColors()[*colorIndex].second);
        materialBtn->SetText("Material: " + ModelColors()[*colorIndex].first);
    };
    viewerPanel->AddChild(materialBtn);

    const size_t sampleCount = samples->size();
    auto prevBtn = MakeToolButton("MFPrev", 300, 450, 100, "◀ Prev",
                                  [showSample, currentIndex, sampleCount]() {
                                      showSample((*currentIndex + sampleCount - 1) % sampleCount);
                                  });
    viewerPanel->AddChild(prevBtn);

    auto nextBtn = MakeToolButton("MFNext", 410, 450, 100, "Next ▶",
                                  [showSample, currentIndex, sampleCount]() {
                                      showSample((*currentIndex + 1) % sampleCount);
                                  });
    viewerPanel->AddChild(nextBtn);

    // ===== HOW IT WORKS =====
    auto howContainer = std::make_shared<UltraCanvasContainer>("MFHowPanel", 20, 630, 620, 100);
    howContainer->SetBackgroundColor(Color(255, 250, 240, 255));
    howContainer->SetBorders(2, Color(222, 184, 135, 255));

    auto howText = std::make_shared<UltraCanvasLabel>("MFHowText", 10, 8, 600, 88);
    howText->SetText(
            std::string("ConversionOptions o; o.TriangulateOnImport = o.TessellateOnImport = true;\n") +
            "auto doc = UltraCanvasModelFormatsPlugin::LoadModelDocument(path, o);\n"
            "Mesh3D mesh = ModelDocumentToMesh3D(*doc);\n"
            "\n" + kOmittedNote);
    howText->SetFontSize(11);
    howText->SetTextColor(Color(50, 50, 50, 255));
    howContainer->AddChild(howText);
    container->AddChild(howContainer);

    return container;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_MODELS_PLUGIN
