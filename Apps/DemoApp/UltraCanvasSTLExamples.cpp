// Apps/DemoApp/UltraCanvasSTLExamples.cpp
// STL (stereolithography) 3D model demo - the Models/STL plugin's self-contained
// mesh reader, in the 3D Graphics section beside the OpenGL showcase.
// Every .stl file in media/3D/STL is parsed by UltraCanvasSTLLoader (ASCII and
// binary, auto-detected) and displayed in an UltraCanvasSTLElement: a shaded,
// mouse-orbited 3D view on GL builds, a mesh info placeholder otherwise. The stats
// panel reports what the parser found - triangles, vertices, bounds, encoding and
// parse time - and the sample can be opened in the media viewer window.
// Version: 1.3.0 - the shared page look (UltraCanvasDemoPageKit): viewer and
//                  report cards that wrap, the reports as tall as their text,
//                  buttons as wide as their text
// Version: 1.2.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "Models/STL/UltraCanvasSTLElement.h"
#include "Models/STL/UltraCanvasSTLLoader.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include "UltraCanvasPathUtf8.h"

namespace UltraCanvas {

namespace {

    // ===== SAMPLE LOADING =====

    struct StlSample {
        std::string path;
        std::string fileName;
        Mesh3D mesh;
        bool loaded = false;
        bool binary = false;
        std::uintmax_t bytes = 0;
        double milliseconds = 0;
        std::string error;
    };

    // Every .stl file in media/3D/STL, sorted by name, so a newly dropped-in
    // sample shows up without touching this file.
    std::vector<std::string> CollectSTLFiles(const std::string& dir) {
        std::vector<std::string> paths;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(UltraCanvas::PathFromUtf8(dir), ec)) {
            if (!entry.is_regular_file()) continue;
            if (!UltraCanvasSTLLoader::HasSTLExtension(PathToUtf8(entry.path()))) continue;
            paths.push_back(PathToUtf8(entry.path()));
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }

    StlSample LoadSample(const std::string& path) {
        StlSample sample;
        sample.path = path;
        sample.fileName = PathToUtf8(PathFromUtf8(path).filename());

        std::error_code ec;
        sample.bytes = std::filesystem::file_size(UltraCanvas::PathFromUtf8(path), ec);
        if (ec) sample.bytes = 0;

        auto start = std::chrono::steady_clock::now();
        sample.loaded = UltraCanvasSTLLoader::Load(path, sample.mesh, &sample.error);
        sample.milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();

        // The canonical binary-STL size identity: an 80-byte header, a 4-byte
        // facet count, then 50 bytes per facet. ASCII files never match it.
        if (sample.loaded && sample.bytes > 0) {
            sample.binary = sample.bytes == 84 + 50 * static_cast<std::uintmax_t>(
                    sample.mesh.TriangleCount());
        }
        return sample;
    }

    std::string HumanSize(std::uintmax_t bytes) {
        char buf[48];
        if (bytes >= 1024ull * 1024ull) {
            std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024ull) {
            std::snprintf(buf, sizeof(buf), "%.1f kB", static_cast<double>(bytes) / 1024.0);
        } else {
            std::snprintf(buf, sizeof(buf), "%llu bytes", static_cast<unsigned long long>(bytes));
        }
        return buf;
    }

    // Multi-line mesh report for the stats panel.
    std::string DescribeSample(const StlSample& sample) {
        if (!sample.loaded) {
            return "Failed to load " + sample.fileName + "\n" +
                   (sample.error.empty() ? std::string("(unknown parse error)") : sample.error);
        }

        const Mesh3D& mesh = sample.mesh;
        const Vec3 size = mesh.bounds.IsValid()
                ? Vec3(mesh.bounds.max.x - mesh.bounds.min.x,
                       mesh.bounds.max.y - mesh.bounds.min.y,
                       mesh.bounds.max.z - mesh.bounds.min.z)
                : Vec3();

        char buf[512];
        std::snprintf(buf, sizeof(buf),
                      "File      %s (%s)\n"
                      "Encoding  %s STL\n"
                      "Solid     %s\n"
                      "Triangles %zu\n"
                      "Vertices  %zu\n"
                      "Extent    %.2f x %.2f x %.2f units\n"
                      "Centre    %.2f, %.2f, %.2f\n"
                      "Parsed in %.0f ms",
                      sample.fileName.c_str(), HumanSize(sample.bytes).c_str(),
                      sample.binary ? "binary" : "ASCII",
                      mesh.name.empty() ? "(unnamed)" : mesh.name.c_str(),
                      mesh.TriangleCount(), mesh.VertexCount(),
                      size.x, size.y, size.z,
                      mesh.bounds.Center().x, mesh.bounds.Center().y, mesh.bounds.Center().z,
                      sample.milliseconds);
        return buf;
    }

    // One-line summary for the status bar.
    std::string SummariseSample(const StlSample& sample) {
        if (!sample.loaded) return "Failed to load " + sample.fileName + ": " + sample.error;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s - %zu triangles, %s STL, %s, parsed in %.0f ms",
                      sample.fileName.c_str(), sample.mesh.TriangleCount(),
                      sample.binary ? "binary" : "ASCII",
                      HumanSize(sample.bytes).c_str(), sample.milliseconds);
        return buf;
    }

    // The colours the "Material" button cycles through.
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

    // As wide as its text, at least minWidth (the width it used to have).
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

// ===== STL DEMO PAGE =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSTLModelExamples() {
        auto container = DemoKit::CreatePage("STLExamples",
                "STL 3D Models - Drag to Orbit, Wheel to Zoom",
                "Self-contained stereolithography reader and writer - ASCII and binary, auto-detected, "
                "no external dependencies. Samples come from media/3D/STL.");

        const std::string sampleDir = NormalizePath(GetResourcesDir() + "media/3D/STL");
        const std::vector<std::string> files = CollectSTLFiles(sampleDir);

        // The viewer beside the two report panels while both fit; the panels
        // move under it when they do not.
        auto mainRow = DemoKit::AddRow(container, "STLMainRow");
        auto viewerPanel = DemoKit::AddCard(mainRow, "STLViewerPanel", "", "",
                                            600 + 2 * DemoKit::kCardPadding + 2);
        auto infoColumn = DemoKit::CreateColumn("STLInfoColumn", DemoKit::kPageGap);
        DemoKit::Add(mainRow, infoColumn, 1);
        infoColumn->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(320));

        // ===== STATS PANEL =====
        // The panel grows with the report: a header comment, a long path and
        // a solid name all land in it.
        auto statsContainer = DemoKit::AddCard(infoColumn, "STLStatsPanel", "What the parser found");
        auto statsText = DemoKit::AddText(statsContainer, "STLStatsText", "");
        statsText->SetFontSize(11);
        statsText->SetTextColor(Color(50, 50, 50, 255));

        // ===== INFO PANEL =====
        auto infoContainer = DemoKit::AddCard(infoColumn, "STLInfoPanel", "STL support");
        DemoKit::Add(infoContainer, DemoKit::CreateBullets("STLInfoText", {
                "Read ASCII STL and binary STL (auto-detected)",
                "Write ASCII STL and binary STL",
                "Per-vertex normals, recomputed when a file ships degenerate facet normals",
                "Bounding box, triangle and vertex counts",
                "Shaded 3D view with mouse orbit and dolly zoom on GL builds (ULTRACANVAS_ENABLE_GL)",
                "Mesh info placeholder without GL",
                "Registered with FileLoader: LoadGraphicsFile opens .stl like any other graphics file"}));

        // ===== VIEWER PANEL =====
        auto viewer = std::make_shared<UltraCanvasSTLElement>("STLViewer", 0, 0, 600, 430);
        DemoKit::AddFixed(viewerPanel, viewer);

        auto toolRow = DemoKit::AddRow(viewerPanel, "STLToolRow", 10);

        auto nameLabel = DemoKit::AddText(viewerPanel, "STLName", "");
        nameLabel->SetFontSize(11);
        nameLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Top);
        nameLabel->SetTextColor(Color(60, 60, 60, 255));

        // ===== HOW IT WORKS =====
        auto howContainer = DemoKit::AddCard(container, "STLHowPanel");
        auto howText = DemoKit::AddText(howContainer, "STLHowText",
                "UltraCanvasSTLLoader::Load parses the file into a Mesh3D (positions, normals, indices, bounds); "
                "UltraCanvasSTLElement uploads that mesh to an UltraCanvasGLSurface and shades it, or draws the "
                "mesh summary when the build has no GL. UltraCanvasSTLPlugin registers both with FileLoader.\n"
                "\n"
                "Mesh3D mesh; UltraCanvasSTLLoader::Load(\"model.stl\", mesh);\n"
                "UltraCanvasSTLLoader::Save(\"copy.stl\", mesh, STLFormat::Binary);");
        howText->SetFontSize(11);
        howText->SetTextColor(Color(50, 50, 50, 255));

        auto statusLabel = DemoKit::AddText(container, "STLStatus", "");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        statusLabel->SetPadding(4.0f);

        if (files.empty()) {
            const std::string message = "No .stl files found in " + sampleDir;
            statusLabel->SetText(message);
            statsText->SetText(message);
            nameLabel->SetText("(no samples)");
            return container;
        }

        // Samples are parsed on demand and cached, so opening the page costs one
        // file - the airplane sample alone is 25 MB.
        auto samples = std::make_shared<std::vector<StlSample>>(files.size());
        auto currentIndex = std::make_shared<size_t>(0);
        auto colorIndex = std::make_shared<size_t>(0);
        auto autoRotate = std::make_shared<bool>(true);

        auto showSample = [samples, files, currentIndex, viewer, nameLabel, statsText,
                           statusLabel](size_t index) {
            if (index >= files.size()) return;
            StlSample& sample = (*samples)[index];
            if (sample.path.empty()) sample = LoadSample(files[index]);

            *currentIndex = index;
            if (sample.loaded) viewer->SetMesh(sample.mesh);

            char counter[64];
            std::snprintf(counter, sizeof(counter), "  (%zu of %zu)", index + 1, files.size());
            nameLabel->SetText(sample.fileName + counter);
            statsText->SetText(DescribeSample(sample));
            statusLabel->SetText(SummariseSample(sample));
        };

        showSample(0);
        viewer->SetModelColor(ModelColors()[0].second);
        viewer->SetAutoRotate(*autoRotate);

        // ===== VIEWER CONTROLS =====
        auto rotateBtn = MakeToolButton("STLRotate", 130, "Auto-rotate: on", nullptr);
        rotateBtn->onClick = [viewer, rotateBtn = rotateBtn.get(), autoRotate]() {
            *autoRotate = !*autoRotate;
            viewer->SetAutoRotate(*autoRotate);
            rotateBtn->SetText(*autoRotate ? "Auto-rotate: on" : "Auto-rotate: off");
        };
        DemoKit::Add(toolRow, rotateBtn);

        auto materialBtn = MakeToolButton("STLMaterial", 130,
                                          "Material: " + ModelColors()[0].first, nullptr);
        materialBtn->onClick = [viewer, materialBtn = materialBtn.get(), colorIndex]() {
            *colorIndex = (*colorIndex + 1) % ModelColors().size();
            viewer->SetModelColor(ModelColors()[*colorIndex].second);
            materialBtn->SetText("Material: " + ModelColors()[*colorIndex].first);
        };
        DemoKit::Add(toolRow, materialBtn);

        // The media viewer opens the file itself and walks the rest of
        // media/3D/STL with the arrow keys.
        auto fullscreenBtn = MakeToolButton("STLFullscreen", 150, "Open in Viewer",
                                            [files, currentIndex]() {
                                                if (*currentIndex < files.size())
                                                    ShowInMediaViewer(files[*currentIndex]);
                                            });
        DemoKit::Add(toolRow, fullscreenBtn);

        if (files.size() > 1) {
            auto prevBtn = MakeToolButton("STLPrev", 70, "Prev",
                                          [showSample, currentIndex, files]() {
                                              showSample((*currentIndex + files.size() - 1) % files.size());
                                          });
            SetFlatButtonIcon(*prevBtn, "menu/chevron-left.svg");
            DemoKit::Add(toolRow, prevBtn);

            auto nextBtn = MakeToolButton("STLNext", 70, "Next",
                                          [showSample, currentIndex, files]() {
                                              showSample((*currentIndex + 1) % files.size());
                                          });
            SetFlatButtonIcon(*nextBtn, "menu/chevron-right.svg", ButtonIconPosition::Right);
            DemoKit::Add(toolRow, nextBtn);
        }

        return container;
    }

} // namespace UltraCanvas
