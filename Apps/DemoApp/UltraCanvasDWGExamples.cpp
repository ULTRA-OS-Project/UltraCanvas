// Apps/DemoApp/UltraCanvasDWGExamples.cpp
// AutoCAD DWG and DXF drawings demo - the Vector plugin's native CAD import.
// Each sample in media/vector/DWG is decoded by the native DWG reader
// (UltraCanvasDWGDecoder) and each sample in media/vector/DXF is read
// directly; both are built into a VectorStorage::VectorDocument by the DXF
// reader and shown in an UltraCanvasVectorElement; a click opens the drawing
// fullscreen with pan and zoom. The status line reports what the reader
// found and any entity types it skipped.
// Version: 1.1.0
// Last Modified: 2026-09-10
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasVectorFormatsPlugin.h"
#include "UltraCanvasVectorElement.h"
#include "UltraCanvasCADConverters.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    // Decodes a CAD file through the converter matrix, collecting the
    // reader's warnings so the page can show them.
    struct CadLoadResult {
        std::shared_ptr<VectorStorage::VectorDocument> document;
        std::vector<std::string> warnings;
        double milliseconds = 0;
    };

    CadLoadResult LoadCadDocument(const std::string& path) {
        CadLoadResult result;
        auto converter = UltraCanvasVectorFormatsPlugin::CreateConverterForExtension(path);
        if (!converter || !converter->CanImport()) {
            result.warnings.push_back("no reader for " + path);
            return result;
        }
        VectorConverter::ConversionOptions options;
        options.WarningCallback = [&result](const std::string& w) { result.warnings.push_back(w); };
        auto start = std::chrono::steady_clock::now();
        result.document = converter->Import(path, options);
        result.milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        return result;
    }

    size_t CountDrawables(const VectorStorage::VectorElement& e) {
        if (const auto* g = dynamic_cast<const VectorStorage::VectorGroup*>(&e)) {
            size_t n = 0;
            for (const auto& child : g->Children) if (child) n += CountDrawables(*child);
            return n;
        }
        return 1;
    }

    std::string DescribeDocument(const VectorStorage::VectorDocument& doc, double ms) {
        size_t drawables = 0;
        for (const auto& layer : doc.Layers) if (layer) drawables += CountDrawables(*layer);
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%zu layers, %zu drawables, page %.0f x %.0f pt, decoded in %.0f ms",
                      doc.Layers.size(), drawables, doc.Size.width, doc.Size.height, ms);
        return buf;
    }

// ===== FULLSCREEN VIEWER =====
    class DWGDemoHandler {
    private:
        std::shared_ptr<UltraCanvasWindow> fullscreenWindow;
        std::string filePath;
        std::shared_ptr<VectorStorage::VectorDocument> document;

    public:
        DWGDemoHandler(std::string path, std::shared_ptr<VectorStorage::VectorDocument> doc)
                : filePath(std::move(path)), document(std::move(doc)) {}

        void OnClick() {
            if (!fullscreenWindow) CreateFullscreenWindow();
        }

        void CreateFullscreenWindow() {
            int screenWidth = 1920;
            int screenHeight = 1080;

            WindowConfig config;
            config.title = "CAD Viewer - " + filePath;
            config.width = screenWidth;
            config.height = screenHeight;
            config.x = 0;
            config.y = 0;
            config.type = WindowType::Fullscreen;
            config.resizable = false;

            fullscreenWindow = CreateWindow(config);
            fullscreenWindow->SetBackgroundColor(Color(32, 32, 32, 255));

            auto viewer = CreateVectorElement("FullscreenDWG", 0, 50, screenWidth, screenHeight - 100);
            VectorElementOptions opts = viewer->GetOptions();
            opts.InteractionMode = VectorInteractionMode::PanZoom;
            opts.BackgroundColor = Colors::White;
            opts.MinZoom = 0.05f;
            opts.MaxZoom = 200.0f;
            viewer->SetOptions(opts);
            if (document) viewer->SetDocument(document);
            fullscreenWindow->AddChild(viewer);

            auto makeButton = [&](const std::string& id, int x, int w, const std::string& text,
                                  std::function<void()> action) {
                auto btn = std::make_shared<UltraCanvasButton>(id, x, 10, w, 30);
                btn->SetText(text);
                btn->SetColors(Color(60, 60, 65, 255));
                btn->SetTextColors(Colors::White);
                btn->onClick = std::move(action);
                fullscreenWindow->AddChild(btn);
            };
            makeButton("BtnZoomOut", 400, 40, "−", [viewer]() { viewer->SetZoom(viewer->GetZoom() / 1.25f); });
            makeButton("BtnZoomIn", 450, 40, "+", [viewer]() { viewer->SetZoom(viewer->GetZoom() * 1.25f); });
            makeButton("BtnFit", 500, 80, "Fit Page", [viewer]() { viewer->ZoomToFit(); });

            auto hint = std::make_shared<UltraCanvasLabel>("Instructions", screenWidth - 420, 10, 410, 30);
            hint->SetText("Drag to pan, wheel to zoom, ESC to close");
            hint->SetTextColor(Color(200, 200, 200, 255));
            fullscreenWindow->AddChild(hint);

            fullscreenWindow->SetEventCallback([this](const UCEvent& event) {
                if (event.type == UCEventType::KeyUp && event.virtualKey == UCKeys::Escape) {
                    if (fullscreenWindow) {
                        fullscreenWindow->Close();
                        fullscreenWindow.reset();
                    }
                    return true;
                }
                return false;
            });

            fullscreenWindow->Show();
        }
    };

}   // namespace

// ===== DWG VECTOR EXAMPLES IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateDWGVectorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("DWGExamples", 0, 0, 1000, 780);
        container->SetBackgroundColor(Color(245, 245, 245, 255));

        auto title = std::make_shared<UltraCanvasLabel>("DWGTitle", 10, 10, 700, 30);
        title->SetText("AutoCAD DWG and DXF Drawings - Click to View Fullscreen");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        auto description = std::make_shared<UltraCanvasLabel>("DWGDescription", 10, 45, 900, 40);
        description->SetText("Native DWG decoding (R13 to R2018) with no external tools, and the DXF reader it shares: blocks,\n"
                             "layers, hatches, splines, dimensions and 3D meshes projected to plan view. Click a drawing to pan and zoom.");
        description->SetFontSize(12);
        description->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(description);

        auto statusLabel = std::make_shared<UltraCanvasLabel>("DWGStatus", 10, 712, 980, 60);
        statusLabel->SetText("Ready. Click a drawing to view it fullscreen.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        container->AddChild(statusLabel);

        // One tile per sample drawing in media/vector/DWG/ and media/vector/DXF/.
        auto makeTile = [&](const std::string& id, int x, int y, const std::string& folder,
                            const std::string& fileName, const std::string& caption) {
            auto tile = std::make_shared<UltraCanvasContainer>(id, x, y, 300, 240);
            tile->SetBackgroundColor(Colors::White);
            tile->SetBorders(2, Color(180, 180, 180, 255));

            std::string path = NormalizePath(GetResourcesDir() + "media/vector/" + folder + "/" + fileName);
            CadLoadResult loaded = LoadCadDocument(path);

            auto element = CreateVectorElement(id + "El", 10, 10, 280, 190);
            VectorElementOptions opts = element->GetOptions();
            opts.BackgroundColor = Colors::White;
            element->SetOptions(opts);

            std::string summary;
            if (loaded.document) {
                element->SetDocument(loaded.document);
                summary = fileName + ": " + DescribeDocument(*loaded.document, loaded.milliseconds);
            } else {
                summary = "Failed to load " + path;
            }
            for (const auto& w : loaded.warnings) summary += "\n" + w;

            auto label = std::make_shared<UltraCanvasLabel>(id + "Label", 10, 204, 280, 28);
            label->SetText(caption);
            label->SetAlignment(TextAlignment::Center);
            label->SetFontSize(11);
            tile->AddChild(label);

            auto handler = std::make_shared<DWGDemoHandler>(path, loaded.document);
            element->SetEventCallback([handler, tile, statusLabel, summary](const UCEvent& event) {
                switch (event.type) {
                    case UCEventType::MouseUp:
                        handler->OnClick();
                        return true;
                    case UCEventType::MouseEnter:
                        tile->SetBordersColor(Color(0, 122, 204, 255));
                        statusLabel->SetText(summary);
                        return true;
                    case UCEventType::MouseLeave:
                        tile->SetBordersColor(Color(180, 180, 180, 255));
                        return true;
                    default:
                        return false;
                }
            });

            tile->AddChild(element);
            container->AddChild(tile);
        };

        makeTile("DWGContainer1", 20, 95, "DWG", "Audi-Q5-DWGFree.com_.dwg",
                 "DWG: Audi Q5 (R2013, 2D block)");
        makeTile("DWGContainer2", 340, 95, "DWG", "womans hostel.dwg",
                 "DWG: hostel plans (R2007, blocks, hatches)");
        makeTile("DWGContainer3", 660, 95, "DWG", "bagno_3d_1.dwg",
                 "DWG: bathroom (R2013, 3D polyface meshes)");
        makeTile("DXFContainer1", 20, 340, "DXF", "millennium-falcon.dxf",
                 "DXF: Millennium Falcon (1015 LWPOLYLINEs, 507 LINEs)");
        makeTile("DXFContainer2", 340, 340, "DXF", "women-body.dxf",
                 "DXF: figure study (74 NURBS SPLINEs)");

        // ===== INFO PANEL =====
        auto infoContainer = std::make_shared<UltraCanvasContainer>("InfoPanel", 660, 340, 320, 240);
        infoContainer->SetBackgroundColor(Color(240, 248, 255, 255));
        infoContainer->SetBorders(2, Color(100, 149, 237, 255));

        auto infoTitle = std::make_shared<UltraCanvasLabel>("InfoTitle", 10, 10, 300, 25);
        infoTitle->SetText("Native CAD import");
        infoTitle->SetFontWeight(FontWeight::Bold);
        infoTitle->SetFontSize(13);
        infoContainer->AddChild(infoTitle);

        auto infoText = std::make_shared<UltraCanvasLabel>("InfoText", 10, 40, 300, 190);
        infoText->SetText(
                "✓ DWG R13, R14, 2000, 2004, 2007, 2010, 2013, 2018\n"
                "✓ DXF R12 and later (read and write)\n"
                "✓ Lines, arcs, circles, ellipses, splines\n"
                "✓ Polylines, LW polylines, 3D and mesh polylines\n"
                "✓ Hatches (solid, pattern, gradient boundaries)\n"
                "✓ Text, MTEXT, attributes, leaders, dimensions\n"
                "✓ Blocks and nested inserts, mirrored and arrayed\n"
                "✓ Layers, true colours, lineweights, linetypes\n"
                "✓ DWG to DXF conversion: DWGConverter::DecodeToDxf\n"
                "• DWG writing needs LibreDWG's dxf2dwg (optional)\n"
        );
        infoText->SetFontSize(11);
        infoText->SetTextColor(Color(50, 50, 50, 255));
        infoContainer->AddChild(infoText);
        container->AddChild(infoContainer);

        // ===== HOW IT WORKS =====
        auto howContainer = std::make_shared<UltraCanvasContainer>("HowPanel", 20, 586, 960, 122);
        howContainer->SetBackgroundColor(Color(255, 250, 240, 255));
        howContainer->SetBorders(2, Color(222, 184, 135, 255));

        auto howTitle = std::make_shared<UltraCanvasLabel>("HowTitle", 10, 6, 940, 22);
        howTitle->SetText("How a .dwg becomes a VectorDocument");
        howTitle->SetFontWeight(FontWeight::Bold);
        howTitle->SetFontSize(13);
        howContainer->AddChild(howTitle);

        auto howText = std::make_shared<UltraCanvasLabel>("HowText", 10, 30, 940, 88);
        howText->SetText(
                "1. UltraCanvasDWGDecoder decodes the binary drawing database - bit-coded values, the R2004+ compressed\n"
                "   pages, the R2007 Reed-Solomon pages, the object map, CLASSES and the block definitions - and renders\n"
                "   it as tagged DXF text (DWGConverter::DecodeToDxf).\n"
                "2. The DXF reader builds the VectorStorage::VectorDocument, the same path a .dxf file takes directly:\n"
                "   blocks expand into transformed groups, layers keep their names, colours and visibility, and the page\n"
                "   follows the drawing's real extents.   auto doc = DWGConverter().Import(\"plan.dwg\");"
        );
        howText->SetFontSize(11);
        howText->SetTextColor(Color(50, 50, 50, 255));
        howContainer->AddChild(howText);
        container->AddChild(howContainer);

        return container;
    }

} // namespace UltraCanvas
