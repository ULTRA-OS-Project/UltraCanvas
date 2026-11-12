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
// Version: 1.2.0 - the shared page look (UltraCanvasDemoPageKit): the viewer
//                  and the two report panels as cards that wrap under each
//                  other, panels that grow with their text instead of
//                  scrolling, buttons as wide as their text
// Version: 1.1.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasPathUtf8.h"

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
        const char* relativePath;   // under media/3D/
        const char* extension;      // what the dispatch is asked for
        std::string note;           // why this one is worth looking at (shown)
    };

    // Ordered smallest file first, which also happens to run from the most
    // compact binary format to the most verbose. Everything here is under a
    // megabyte, bar the 2.6 MB DXF whose 8110 flat triangles cost less to read
    // than their size suggests, and the 1.1 MB .x, so the page stays responsive
    // even though each sample is parsed on demand - the larger samples in
    // media/3D (the 18 MB .blend, the 6.9 MB VRML, the 3.9 MB PLY) are read by
    // the same dispatch and are deliberately not listed; see kOmittedNote.
    //
    // Five of the aircraft exports used to be incomplete, which looked like a
    // reader dropping geometry and was not: both meshes in the source
    // media/3D/Blend/E-45-Aircraft.blend carry a Mirror modifier about X=0,
    // and the .dae, .x, .abc and binary .fbx had been exported without
    // applying modifiers, so the files held half an aeroplane - while the
    // .ms3d held only the glass canopy. They were regenerated from that
    // .blend with the modifier applied (the .fbx by Blender's exporter, the
    // .abc by rewriting its Ogawa archive around the evaluated hull, the rest
    // by mirroring the file's own geometry), so every sample now carries the
    // whole model.
    //
    // The .x needed two more repairs, made 2026-10-05. Its mesh frames were
    // relative to the armature bones both meshes hang from, which Blender's
    // exporter did not write, so the hull stood on its head with the canopy
    // buried inside it; the frames now hold the placements the .blend gives
    // the objects. And the mirroring had doubled the vertices and faces but
    // not the UVs or the per-face material indices; both are now complete.
    // Built on each call, so the notes are in the language of the moment.
    std::vector<SampleSpec> Samples() {
        return {
                {"STEP/Pin.step", "step",
                 Tr("Exact B-rep: analytic surfaces closed into a solid. No mesh\n"
                 "exists in the file - the triangles below were tessellated\n"
                 "on import, which is what TessellateOnImport asks for.")},
                {"STEP/NurbsSheet.step", "step",
                 Tr("A trimmed NURBS sheet rather than a solid. Same reader, and\n"
                 "the tessellation tolerance decides the triangle count.")},
                {"MS3D/E-45-Aircraft.ms3d", "ms3d",
                 Tr("A game format: a fixed sequence of packed little-endian\n"
                 "structs, no chunks or offsets. Groups become meshes - here\n"
                 "the hull and the canopy - and smoothing groups arrive as a\n"
                 "bitmask. Sixteen-bit indices cap it at 65535 vertices.")},
                {"FBX/E-45-Aircraft.fbx", "fbx",
                 Tr("Binary FBX - vertex for vertex the same mesh as the 2.2 MB\n"
                 "ASCII variant in media/3D/FBX, in a quarter of the space.\n"
                 "What the header declared lands in ModelDocument::Metadata as\n"
                 "fbx.version, fbx.encoding and fbx.application.")},
                {"Alembic/E-45-Aircraft.abc", "abc",
                 Tr("A sampled cache rather than a scene description: geometry is\n"
                 "stored per time sample. This reader takes the first sample.\n"
                 "Its matrices are row-vector, the opposite of the document's,\n"
                 "which is why the canopy sits on the hull rather than in it.")},
                {"COLLADA/E-45-Aircraft.dae", "dae",
                 Tr("XML interchange with a real scene graph and materials, so\n"
                 "the node and material counts below are non-trivial.\n"
                 "Compile-time optional - it needs tinyxml2.")},
                {"3DS/E-45-Aircraft.3ds", "3ds",
                 Tr("Chunked binary from 1990s 3D Studio. Sixteen-bit indices and\n"
                 "a flat object list, so a mesh per object and no hierarchy.")},
                {"XFile/E-45-Aircraft.x", "x",
                 Tr("DirectX retained-mode .x, text encoding. Templates declare\n"
                 "their own layout, so the parser is driven by the file.")},
                {"DXF/E-45-Aircraft.dxf", "dxf",
                 Tr("The same CAD format the drawings page reads, this time\n"
                 "carrying geometry: 8110 3DFACE entities rather than the\n"
                 "LWPOLYLINEs and SPLINEs of a flat drawing. A DXF holding\n"
                 "only 2D entities is refused here with an explanation, and\n"
                 "belongs on that page instead - see media/vector/DXF.")},
        };
    }

    std::string OmittedNote() {
        return Tr("Also read, but omitted here for size: .blend (18 MB), VRML .wrl\n"
                  "(6.9 MB), PLY (3.9 MB), ASCII FBX (2.2 MB), X3D (1.4 MB) and OBJ\n"
                  "(1.3 MB). Same dispatch, same document.");
    }

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
            sample.formatName = TrF(".{0} (no converter in this build)", sample.extension);
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
        sample.bytes = std::filesystem::file_size(UltraCanvas::PathFromUtf8(sample.path), ec);
        if (ec) sample.bytes = 0;

        if (!std::filesystem::exists(UltraCanvas::PathFromUtf8(sample.path), ec)) {
            sample.error = TrF("Sample not found: {0}", sample.path);
            return;
        }
        if (!sample.converterAvailable) {
            sample.error = TrF("This build has no converter for .{0}", sample.extension);
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
            sample.error = Tr("The reader returned no document");
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
            return TrN("{n} byte", "{n} bytes", static_cast<long long>(bytes));
        }
        return buf;
    }

    std::string UpAxisName(ModelStorage::UpAxis axis) {
        return axis == ModelStorage::UpAxis::ZUp ? Tr("Z-up") : Tr("Y-up");
    }

    // A label padded with spaces to `width` characters (one space when it is
    // longer), so the report's values line up in whatever language it is in.
    std::string Pad(std::string label, size_t width) {
        size_t chars = 0;
        for (unsigned char c : label) if ((c & 0xC0) != 0x80) ++chars;
        label.append(chars < width ? width - chars : 1, ' ');
        return label;
    }

    std::string Number(const char* format, double value) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), format, value);
        return buf;
    }

    std::string UnitText(const ModelDocument& doc) {
        const char* symbol = ModelStorage::ModelUnitSymbol(doc.SourceUnit);
        if (!symbol || !*symbol) return Tr("unitless (the format does not state one)");
        return TrF("{0} (1 unit = {1} m)", symbol, Number("%g", doc.UnitScaleToMeters));
    }

    // What the reader found, which is the whole point of the page.
    std::string DescribeDocument(const FormatSample& sample) {
        if (!sample.loaded) {
            return TrF("Could not read {0}", sample.fileName) + "\n" +
                   (sample.error.empty() ? Tr("(unknown error)") : sample.error);
        }

        const ModelDocument& doc = *sample.document;
        const ModelStorage::Bounds3D bounds = doc.ComputeBounds();
        const ModelStorage::Vec3d size = bounds.IsValid() ? bounds.Size()
                                                          : ModelStorage::Vec3d{0, 0, 0};
        const std::string none = Tr("(none)");
        auto count = [](size_t n) { return std::to_string(n); };
        // Two counts on a line: the left column's label 12 wide, then the right's 11.
        auto pair = [&](const std::string& left, size_t a, const std::string& right, size_t b) {
            return Pad(left, 12) + count(a) + "       " + Pad(right, 11) + count(b) + "\n";
        };

        std::string text;
        text += Pad(Tr("File"), 12) + sample.fileName + " (" + HumanSize(sample.bytes) + ")\n";
        text += Pad(Tr("Format"), 12) + sample.formatName +
                (sample.formatVersion.empty() ? "" : " ") + sample.formatVersion + "\n";
        text += Pad(Tr("Read in"), 12) + TrF("{0} ms", Number("%.0f", sample.milliseconds)) + "\n";
        text += "\n";
        text += Pad(Tr("Title"), 12) + (doc.Title.empty() ? none : doc.Title) + "\n";
        text += Pad(Tr("Generator"), 12) + (doc.Generator.empty() ? none : doc.Generator) + "\n";
        text += Pad(Tr("Space"), 12) + UpAxisName(doc.Up) + ", " +
                (doc.Chirality == ModelStorage::Handedness::RightHanded
                         ? Tr("right-handed") : Tr("left-handed")) + ", " +
                UnitText(doc) + "\n";
        text += "\n";
        text += pair(Tr("Scenes"), doc.Scenes.size(), Tr("Nodes"), doc.Nodes.size());
        text += pair(Tr("Meshes"), doc.Meshes.size(), Tr("Materials"), doc.Materials.size());
        text += pair(Tr("Images"), doc.Images.size(), Tr("Animations"), doc.Animations.size());
        text += pair(Tr("Cameras"), doc.Cameras.size(), Tr("Lights"), doc.Lights.size());
        text += Pad(Tr("B-rep solids"), 12) + count(doc.Brep.Solids.size()) + "\n";
        text += "\n";
        text += Pad(Tr("Vertices"), 12) + count(doc.TotalVertexCount()) + "\n";
        text += Pad(Tr("Faces"), 12) + count(doc.TotalFaceCount()) + "\n";
        text += Pad(Tr("Triangles drawn"), 12) + count(sample.mesh.TriangleCount()) + "\n";
        text += Pad(Tr("Extent"), 12) + Number("%.2f", size.x) + " x " + Number("%.2f", size.y) +
                " x " + Number("%.2f", size.z);
        return text;
    }

    // The converter's own capability flags, so the page states what the format
    // can carry rather than what this one file happens to use.
    std::string DescribeCapabilities(const FormatSample& sample) {
        if (!sample.converterAvailable) {
            return TrF("No converter for .{0} in this build.", sample.extension) + "\n\n" +
                   Tr("COLLADA needs tinyxml2 and the .blend and FBX readers need\n"
                   "zlib, so each is a separate build option. Ask\n"
                   "SupportedLoadExtensions() rather than assuming.");
        }

        const FormatCapabilities& c = sample.capabilities;
        const std::string yes = Tr("yes");
        auto tick = [&yes](bool on) { return on ? yes : std::string(" - "); };
        // Two flags on a line, each label 14 wide.
        auto pair = [&](const std::string& left, bool a, const std::string& right, bool b) {
            return Pad(left, 14) + tick(a) + "    " + Pad(right, 14) + tick(b) + "\n";
        };

        std::string text;
        text += Pad(Tr("Read"), 6) + yes + "        " + Pad(Tr("Write"), 6) +
                (sample.canExport ? yes : Tr("read-only")) + "\n";
        text += "\n";
        text += pair(Tr("Meshes"), c.Meshes, Tr("N-gons"), c.NGons);
        text += pair(Tr("Scene graph"), c.SceneGraph, Tr("Instancing"), c.Instancing);
        text += pair(Tr("Materials"), c.Materials, Tr("PBR materials"), c.PBRMaterials);
        text += pair(Tr("Textures"), c.Textures, Tr("Embedded"), c.EmbeddedTextures);
        text += pair(Tr("Vertex colors"), c.VertexColors, Tr("Point clouds"), c.PointClouds);
        text += pair(Tr("B-rep"), c.Brep, Tr("NURBS"), c.NurbsSurfaces);
        text += "\n";
        text += sample.note;
        return text;
    }

    // One line for the status bar, plus any warnings the import reported.
    std::string SummariseSample(const FormatSample& sample) {
        if (!sample.loaded) {
            return TrF("Failed: {0} - {1}", sample.fileName, sample.error);
        }
        std::string line = LocalisationDetail::Substitute(
                TrN("{0} - {1}, {n} triangle, read in {2} ms",
                    "{0} - {1}, {n} triangles, read in {2} ms",
                    static_cast<long long>(sample.mesh.TriangleCount())),
                {sample.fileName, sample.formatName, Number("%.0f", sample.milliseconds)});
        if (!sample.warnings.empty()) {
            line += "   |   " + TrF("warning: {0}", sample.warnings.front());
            if (sample.warnings.size() > 1) {
                line += " " + TrN("(+{n} more)", "(+{n} more)",
                                  static_cast<long long>(sample.warnings.size() - 1));
            }
        }
        return line;
    }

    // Built on each call, so the names are in the language of the moment.
    std::vector<std::pair<std::string, Vec3>> ModelColors() {
        return {
                {Tr("Steel"),  Vec3(0.78f, 0.80f, 0.85f)},
                {Tr("Brass"),  Vec3(0.85f, 0.68f, 0.32f)},
                {Tr("Copper"), Vec3(0.80f, 0.45f, 0.30f)},
                {Tr("Jade"),   Vec3(0.40f, 0.75f, 0.60f)},
                {Tr("Resin"),  Vec3(0.90f, 0.90f, 0.94f)},
        };
    }

    // The tree entry's own name, and the page's heading. Kept beside the
    // sample table so a new sample cannot arrive without a title for it.
    std::string FormatTitle(const std::string& extension) {
        struct Named { const char* extension; std::string title; };
        const Named kNames[] = {
                {"step", Tr("STEP 3D Models - Exact B-rep Solids")},
                {"ms3d", Tr("MilkShape 3D Models")},
                {"fbx",  Tr("FBX 3D Models")},
                {"abc",  Tr("Alembic 3D Models - A Sampled Cache")},
                {"dae",  Tr("COLLADA 3D Models - A Scene Graph in XML")},
                {"3ds",  Tr("3D Studio Models")},
                {"x",    Tr("DirectX .x 3D Models")},
                {"dxf",  Tr("DXF 3D Models - Geometry, Not a Drawing")},
        };
        for (const Named& named : kNames)
            if (extension == named.extension) return named.title;
        return TrF(".{0} 3D Models", extension);
    }

    // As wide as its text, at least `minWidth` (the width it had).
    std::shared_ptr<UltraCanvasButton> MakeToolButton(const std::string& id, float minWidth,
                                                      const std::string& text,
                                                      std::function<void()> action) {
        auto btn = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, 30, text);
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(minWidth);
        btn->boxConstraints = limits;
        btn->SetColors(Color(60, 60, 65, 255));
        btn->SetTextColors(Colors::White);
        btn->onClick = std::move(action);
        return btn;
    }

} // namespace

// ===== MODEL FORMATS DEMO PAGE =====
// `extension` selects one format, which is how the tree lists them: a reader
// per entry, beside STL, rather than one page a visitor has to page through to
// discover that .step and .3ds are both supported. Empty shows them all.
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateModelFormatsExamples(
        const std::string& extension) {
    auto container = DemoKit::CreatePage("ModelFormatsExamples",
            extension.empty()
            ? Tr("3D Model Formats - One Document Structure, Nine Readers")
            : FormatTitle(extension),
            extension.empty()
            ? Tr(
                "Every sample is read into the same ModelStorage::ModelDocument, so the panels on the right show where the "
                "formats genuinely differ - scene graph, materials, units, up-axis, exact solids - rather than nine of the same row.")
            : Tr(
                "Read into a ModelStorage::ModelDocument by the Models plugin's dispatch - the same universal structure every "
                "other 3D format lands in, so the panels on the right say what this one carried and what the format can carry."));

    // The viewer beside the two report panels while both fit; the panels
    // move under it when they do not.
    auto mainRow = DemoKit::AddRow(container, "MFMainRow");
    auto viewerPanel = DemoKit::AddCard(mainRow, "MFViewerPanel", "", "",
                                        600 + 2 * DemoKit::kCardPadding + 2);
    auto infoColumn = DemoKit::CreateColumn("MFInfoColumn", DemoKit::kPageGap);
    DemoKit::Add(mainRow, infoColumn, 1);
    infoColumn->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(320));

    // ===== SAMPLE TABLE =====
    const std::string modelsDir = NormalizePath(GetResourcesDir() + "media/3D/");
    auto samples = std::make_shared<std::vector<FormatSample>>();
    for (const SampleSpec& spec : Samples()) {
        if (!extension.empty() && extension != spec.extension) continue;
        FormatSample sample;
        sample.path = NormalizePath(modelsDir + spec.relativePath);
        sample.fileName = PathToUtf8(PathFromUtf8(sample.path).filename());
        sample.extension = spec.extension;
        sample.note = spec.note;
        // Cheap, and it decides what the capability panel can say even for a
        // sample the user never selects.
        DescribeConverter(sample);
        samples->push_back(std::move(sample));
    }

    // ===== STATS PANEL =====
    // The panel grows with the report: a COLLADA scene says a great deal
    // more than a STEP solid.
    auto statsContainer = DemoKit::AddCard(infoColumn, "MFStatsPanel", Tr("What the reader found"));
    auto statsText = DemoKit::AddText(statsContainer, "MFStatsText", "");
    statsText->SetFontSize(11);
    statsText->SetTextColor(Color(50, 50, 50, 255));

    // ===== CAPABILITY PANEL =====
    // The capability grid is a fixed twelve lines, but the per-sample note
    // under it is not - and it is a paragraph when the build has no converter.
    auto capsContainer = DemoKit::AddCard(infoColumn, "MFCapsPanel", Tr("What the format can carry"));
    auto capsText = DemoKit::AddText(capsContainer, "MFCapsText", "");
    capsText->SetFontSize(11);
    capsText->SetTextColor(Color(50, 50, 50, 255));

    // ===== VIEWER PANEL =====
    // Named for STL, but it takes a Mesh3D and knows nothing about where the
    // mesh came from - a GL-orbited view where there is GL, a shaded software
    // still where there is not.
    auto viewer = std::make_shared<UltraCanvasSTLElement>("MFViewer", 0, 0, 600, 430);
    DemoKit::AddFixed(viewerPanel, viewer);

    auto toolRow = DemoKit::AddRow(viewerPanel, "MFToolRow", 10);

    auto nameLabel = DemoKit::AddText(viewerPanel, "MFName", "");
    nameLabel->SetFontSize(11);
    nameLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
    nameLabel->SetTextColor(Color(60, 60, 60, 255));

    // ===== HOW IT WORKS =====
    auto howContainer = DemoKit::AddCard(container, "MFHowPanel");
    auto howText = DemoKit::AddText(howContainer, "MFHowText",
            std::string("ConversionOptions o; o.TriangulateOnImport = o.TessellateOnImport = true;\n") +
            "auto doc = UltraCanvasModelFormatsPlugin::LoadModelDocument(path, o);\n"
            "Mesh3D mesh = ModelDocumentToMesh3D(*doc);\n"
            "\n" + OmittedNote());
    howText->SetFontSize(11);
    howText->SetTextColor(Color(50, 50, 50, 255));

    auto statusLabel = DemoKit::AddText(container, "MFStatus", "");
    statusLabel->SetFontSize(11);
    statusLabel->SetTextColor(Color(60, 60, 60, 255));
    statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
    statusLabel->SetPadding(4.0f);

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

        nameLabel->SetText(TrF("{0} - {1}  ({2} of {3})", sample.formatName, sample.fileName,
                               index + 1, samples->size()));
        statsText->SetText(DescribeDocument(sample));
        capsText->SetText(DescribeCapabilities(sample));
        statusLabel->SetText(SummariseSample(sample));
    };

    showSample(0);
    viewer->SetModelColor(ModelColors()[0].second);
    viewer->SetAutoRotate(*autoRotate);

    // ===== VIEWER CONTROLS =====
    auto rotateBtn = MakeToolButton("MFRotate", 130, Tr("Auto-rotate: on"), nullptr);
    rotateBtn->onClick = [viewer, rotateBtn = rotateBtn.get(), autoRotate]() {
        *autoRotate = !*autoRotate;
        viewer->SetAutoRotate(*autoRotate);
        rotateBtn->SetText(*autoRotate ? Tr("Auto-rotate: on") : Tr("Auto-rotate: off"));
    };
    DemoKit::Add(toolRow, rotateBtn);

    auto materialBtn = MakeToolButton("MFMaterial", 130,
                                      TrF("Material: {0}", ModelColors()[0].first), nullptr);
    materialBtn->onClick = [viewer, materialBtn = materialBtn.get(), colorIndex]() {
        *colorIndex = (*colorIndex + 1) % ModelColors().size();
        viewer->SetModelColor(ModelColors()[*colorIndex].second);
        materialBtn->SetText(TrF("Material: {0}", ModelColors()[*colorIndex].first));
    };
    DemoKit::Add(toolRow, materialBtn);

    // A format page with one sample has nothing to page through, so it says so
    // by not offering the buttons at all rather than by two that do nothing.
    const size_t sampleCount = samples->size();
    if (sampleCount > 1) {
        auto prevBtn = MakeToolButton("MFPrev", 100, Tr("Prev"),
                                      [showSample, currentIndex, sampleCount]() {
                                          showSample((*currentIndex + sampleCount - 1) % sampleCount);
                                      });
        SetFlatButtonIcon(*prevBtn, "menu/chevron-left.svg");
        DemoKit::Add(toolRow, prevBtn);

        auto nextBtn = MakeToolButton("MFNext", 100, Tr("Next"),
                                      [showSample, currentIndex, sampleCount]() {
                                          showSample((*currentIndex + 1) % sampleCount);
                                      });
        SetFlatButtonIcon(*nextBtn, "menu/chevron-right.svg", ButtonIconPosition::Right);
        DemoKit::Add(toolRow, nextBtn);
    }

    return container;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_MODELS_PLUGIN
