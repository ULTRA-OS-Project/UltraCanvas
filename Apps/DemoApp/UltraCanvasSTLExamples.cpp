// Apps/DemoApp/UltraCanvasSTLExamples.cpp
// STL (stereolithography) 3D model demo - the Models/STL plugin's self-contained
// mesh reader shown next to the other vector formats.
// Every .stl file in media/vector/STL is parsed by UltraCanvasSTLLoader (ASCII and
// binary, auto-detected) and displayed in an UltraCanvasSTLElement: a shaded,
// mouse-orbited 3D view on GL builds, a mesh info placeholder otherwise. The stats
// panel reports what the parser found - triangles, vertices, bounds, encoding and
// parse time - and the sample can be opened fullscreen.
// Version: 1.0.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
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

    // Every .stl file in media/vector/STL, sorted by name, so a newly dropped-in
    // sample shows up without touching this file.
    std::vector<std::string> CollectSTLFiles(const std::string& dir) {
        std::vector<std::string> paths;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (!UltraCanvasSTLLoader::HasSTLExtension(entry.path().string())) continue;
            paths.push_back(entry.path().string());
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }

    StlSample LoadSample(const std::string& path) {
        StlSample sample;
        sample.path = path;
        sample.fileName = std::filesystem::path(path).filename().string();

        std::error_code ec;
        sample.bytes = std::filesystem::file_size(path, ec);
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

// ===== FULLSCREEN VIEWER =====
    // Holds the parsed samples so opening the fullscreen viewer re-uses the mesh
    // already in memory instead of copying or re-parsing it.
    class STLDemoHandler {
    private:
        std::shared_ptr<UltraCanvasWindow> fullscreenWindow;
        std::shared_ptr<std::vector<StlSample>> samples;
        std::shared_ptr<size_t> currentIndex;
        size_t colorIndex = 0;
        bool autoRotate = true;

    public:
        STLDemoHandler(std::shared_ptr<std::vector<StlSample>> loadedSamples,
                       std::shared_ptr<size_t> index)
                : samples(std::move(loadedSamples)), currentIndex(std::move(index)) {}

        void OnClick() {
            if (fullscreenWindow) return;
            if (!samples || !currentIndex || *currentIndex >= samples->size()) return;
            const StlSample& sample = (*samples)[*currentIndex];
            if (!sample.loaded) return;
            CreateFullscreenWindow(sample);
        }

        void CreateFullscreenWindow(const StlSample& sample) {
            const int screenWidth = 1920;
            const int screenHeight = 1080;

            WindowConfig config;
            config.title = "STL Viewer - " + sample.fileName;
            config.width = screenWidth;
            config.height = screenHeight;
            config.x = 0;
            config.y = 0;
            config.type = WindowType::Fullscreen;
            config.resizable = false;

            fullscreenWindow = CreateWindow(config);
            fullscreenWindow->SetBackgroundColor(Color(32, 32, 32, 255));

            auto viewer = std::make_shared<UltraCanvasSTLElement>(
                    "FullscreenSTL", 0, 50, static_cast<float>(screenWidth),
                    static_cast<float>(screenHeight - 50));
            viewer->SetMesh(sample.mesh);
            viewer->SetModelColor(ModelColors()[colorIndex].second);
            viewer->SetAutoRotate(autoRotate);
            fullscreenWindow->AddChild(viewer);

            auto rotateBtn = MakeToolButton("STLFsRotate", 10, 10, 130, "Auto-rotate: on", nullptr);
            rotateBtn->onClick = [this, viewer, rotateBtn]() {
                autoRotate = !autoRotate;
                viewer->SetAutoRotate(autoRotate);
                rotateBtn->SetText(autoRotate ? "Auto-rotate: on" : "Auto-rotate: off");
            };
            rotateBtn->SetText(autoRotate ? "Auto-rotate: on" : "Auto-rotate: off");
            fullscreenWindow->AddChild(rotateBtn);

            auto materialBtn = MakeToolButton("STLFsMaterial", 150, 10, 140,
                                              "Material: " + ModelColors()[colorIndex].first, nullptr);
            materialBtn->onClick = [this, viewer, materialBtn]() {
                colorIndex = (colorIndex + 1) % ModelColors().size();
                viewer->SetModelColor(ModelColors()[colorIndex].second);
                materialBtn->SetText("Material: " + ModelColors()[colorIndex].first);
            };
            fullscreenWindow->AddChild(materialBtn);

            auto closeBtn = MakeToolButton("STLFsClose", 300, 10, 90, "Close",
                                           [this]() { CloseFullscreenWindow(); });
            fullscreenWindow->AddChild(closeBtn);

            auto hint = std::make_shared<UltraCanvasLabel>("STLFsHint", 410, 10, 900, 30);
            hint->SetText(sample.fileName + " - drag to orbit, wheel to zoom, ESC to close");
            hint->SetTextColor(Color(200, 200, 200, 255));
            hint->SetFontSize(12);
            fullscreenWindow->AddChild(hint);

            fullscreenWindow->SetEventCallback([this](const UCEvent& event) {
                if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                    CloseFullscreenWindow();
                    return true;
                }
                return false;
            });

            fullscreenWindow->Show();
        }

        void CloseFullscreenWindow() {
            if (!fullscreenWindow) return;
            fullscreenWindow->Close();
            fullscreenWindow.reset();
        }
    };

} // namespace

// ===== STL DEMO PAGE =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateSTLModelExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("STLExamples", 0, 0, 1000, 780);
        container->SetBackgroundColor(Color(245, 245, 245, 255));

        auto title = std::make_shared<UltraCanvasLabel>("STLTitle", 10, 10, 700, 30);
        title->SetText("STL 3D Models - Drag to Orbit, Wheel to Zoom");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto description = std::make_shared<UltraCanvasLabel>("STLDescription", 10, 45, 900, 40);
        description->SetText("Self-contained stereolithography reader and writer - ASCII and binary, auto-detected,\n"
                             "no external dependencies. Samples come from media/vector/STL.");
        description->SetFontSize(12);
        description->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(description);

        auto statusLabel = std::make_shared<UltraCanvasLabel>("STLStatus", 10, 740, 980, 30);
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        container->AddChild(statusLabel);

        const std::string sampleDir = NormalizePath(GetResourcesDir() + "media/vector/STL");
        const std::vector<std::string> files = CollectSTLFiles(sampleDir);

        // ===== STATS PANEL =====
        auto statsContainer = std::make_shared<UltraCanvasContainer>("STLStatsPanel", 660, 100, 320, 250);
        statsContainer->SetBackgroundColor(Colors::White);
        statsContainer->SetBorders(2, Color(180, 180, 180, 255));

        auto statsTitle = std::make_shared<UltraCanvasLabel>("STLStatsTitle", 10, 10, 300, 25);
        statsTitle->SetText("What the parser found");
        statsTitle->SetFontWeight(FontWeight::Bold);
        statsTitle->SetFontSize(13);
        statsContainer->AddChild(statsTitle);

        auto statsText = std::make_shared<UltraCanvasLabel>("STLStatsText", 10, 40, 300, 200);
        statsText->SetFontSize(11);
        statsText->SetTextColor(Color(50, 50, 50, 255));
        statsContainer->AddChild(statsText);
        container->AddChild(statsContainer);

        // ===== VIEWER PANEL =====
        auto viewerPanel = std::make_shared<UltraCanvasContainer>("STLViewerPanel", 20, 100, 620, 520);
        viewerPanel->SetBackgroundColor(Colors::White);
        viewerPanel->SetBorders(2, Color(180, 180, 180, 255));
        container->AddChild(viewerPanel);

        auto viewer = std::make_shared<UltraCanvasSTLElement>("STLViewer", 10, 10, 600, 430);
        viewerPanel->AddChild(viewer);

        auto nameLabel = std::make_shared<UltraCanvasLabel>("STLName", 10, 486, 600, 24);
        nameLabel->SetFontSize(11);
        nameLabel->SetAlignment(TextAlignment::Center);
        nameLabel->SetTextColor(Color(60, 60, 60, 255));
        viewerPanel->AddChild(nameLabel);

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
        auto handler = std::make_shared<STLDemoHandler>(samples, currentIndex);

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
        auto rotateBtn = MakeToolButton("STLRotate", 10, 450, 130, "Auto-rotate: on", nullptr);
        rotateBtn->onClick = [viewer, rotateBtn, autoRotate]() {
            *autoRotate = !*autoRotate;
            viewer->SetAutoRotate(*autoRotate);
            rotateBtn->SetText(*autoRotate ? "Auto-rotate: on" : "Auto-rotate: off");
        };
        viewerPanel->AddChild(rotateBtn);

        auto materialBtn = MakeToolButton("STLMaterial", 150, 450, 130,
                                          "Material: " + ModelColors()[0].first, nullptr);
        materialBtn->onClick = [viewer, materialBtn, colorIndex]() {
            *colorIndex = (*colorIndex + 1) % ModelColors().size();
            viewer->SetModelColor(ModelColors()[*colorIndex].second);
            materialBtn->SetText("Material: " + ModelColors()[*colorIndex].first);
        };
        viewerPanel->AddChild(materialBtn);

        auto fullscreenBtn = MakeToolButton("STLFullscreen", 290, 450, 150, "View Fullscreen",
                                            [handler]() { handler->OnClick(); });
        viewerPanel->AddChild(fullscreenBtn);

        if (files.size() > 1) {
            auto prevBtn = MakeToolButton("STLPrev", 450, 450, 70, "◀ Prev",
                                          [showSample, currentIndex, files]() {
                                              showSample((*currentIndex + files.size() - 1) % files.size());
                                          });
            viewerPanel->AddChild(prevBtn);

            auto nextBtn = MakeToolButton("STLNext", 530, 450, 70, "Next ▶",
                                          [showSample, currentIndex, files]() {
                                              showSample((*currentIndex + 1) % files.size());
                                          });
            viewerPanel->AddChild(nextBtn);
        }

        // ===== INFO PANEL =====
        auto infoContainer = std::make_shared<UltraCanvasContainer>("STLInfoPanel", 660, 370, 320, 250);
        infoContainer->SetBackgroundColor(Color(240, 248, 255, 255));
        infoContainer->SetBorders(2, Color(100, 149, 237, 255));

        auto infoTitle = std::make_shared<UltraCanvasLabel>("STLInfoTitle", 10, 10, 300, 25);
        infoTitle->SetText("STL support");
        infoTitle->SetFontWeight(FontWeight::Bold);
        infoTitle->SetFontSize(13);
        infoContainer->AddChild(infoTitle);

        auto infoText = std::make_shared<UltraCanvasLabel>("STLInfoText", 10, 40, 300, 200);
        infoText->SetText(
                "✓ Read ASCII STL and binary STL (auto-detected)\n"
                "✓ Write ASCII STL and binary STL\n"
                "✓ Per-vertex normals, recomputed when a file\n"
                "   ships degenerate facet normals\n"
                "✓ Bounding box, triangle and vertex counts\n"
                "✓ Shaded 3D view with mouse orbit and dolly\n"
                "   zoom on GL builds (ULTRACANVAS_ENABLE_GL)\n"
                "✓ Mesh info placeholder without GL\n"
                "✓ Registered with FileLoader: LoadGraphicsFile\n"
                "   opens .stl like any other graphics file"
        );
        infoText->SetFontSize(11);
        infoText->SetTextColor(Color(50, 50, 50, 255));
        infoContainer->AddChild(infoText);
        container->AddChild(infoContainer);

        // ===== HOW IT WORKS =====
        auto howContainer = std::make_shared<UltraCanvasContainer>("STLHowPanel", 20, 630, 960, 100);
        howContainer->SetBackgroundColor(Color(255, 250, 240, 255));
        howContainer->SetBorders(2, Color(222, 184, 135, 255));

        auto howText = std::make_shared<UltraCanvasLabel>("STLHowText", 10, 8, 940, 88);
        howText->SetText(
                "UltraCanvasSTLLoader::Load parses the file into a Mesh3D (positions, normals, indices, bounds);\n"
                "UltraCanvasSTLElement uploads that mesh to an UltraCanvasGLSurface and shades it, or draws the\n"
                "mesh summary when the build has no GL. UltraCanvasSTLPlugin registers both with FileLoader.\n"
                "\n"
                "Mesh3D mesh; UltraCanvasSTLLoader::Load(\"model.stl\", mesh);   "
                "UltraCanvasSTLLoader::Save(\"copy.stl\", mesh, STLFormat::Binary);"
        );
        howText->SetFontSize(11);
        howText->SetTextColor(Color(50, 50, 50, 255));
        howContainer->AddChild(howText);
        container->AddChild(howContainer);

        return container;
    }

} // namespace UltraCanvas
