// Apps/DemoApp/UltraCanvasCDRExamples.cpp
// CDR vector graphics demo examples for UltraCanvas Framework
// Version: 1.2.1
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasFileLoader.h"
#include "../Plugins/Vector/CDR/UltraCanvasCDRPlugin.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>

namespace UltraCanvas {

// ===== "SAVE AS" BUTTON =====
    // A "Save as…" button for one CDR tile: native save dialog offering SVG
    // (working) and XAR (writer not finished yet), then export of the tile's
    // currently shown page through the CDR plugin. Outcome lands in the
    // shared status label.
    static std::shared_ptr<UltraCanvasButton> MakeCDRSaveAsButton(
            const std::string& id, int x, int y, int w, int h,
            std::shared_ptr<UltraCanvasCDRElement> cdrElement,
            const std::string& cdrFile,
            std::shared_ptr<UltraCanvasLabel> statusLabel) {
        auto btn = std::make_shared<UltraCanvasButton>(id, x, y, w, h);
        btn->SetText("Save as…");
        btn->SetFontSize(10);
        btn->onClick = [cdrElement, cdrFile, statusLabel]() {
            if (!cdrElement->IsLoaded()) {
                statusLabel->SetText("Nothing to save: " + cdrFile + " is not loaded.");
                return;
            }

            // Default output name: source stem + .svg
            std::string stem = cdrFile;
            size_t slash = stem.find_last_of("/\\");
            if (slash != std::string::npos) stem = stem.substr(slash + 1);
            size_t dot = stem.find_last_of('.');
            if (dot != std::string::npos) stem = stem.substr(0, dot);

            FileDialogOptions opts;
            opts.title = "Save " + stem + " as SVG or XAR";
            opts.defaultFileName = stem + ".svg";
            opts.AddFilter("SVG vector graphics", "svg");
            opts.AddFilter("Xara drawing (not finished yet)", "xar");

            const int page = cdrElement->GetCurrentPage();
            UltraCanvasFileLoader::SaveFileDialog(opts,
                [cdrFile, statusLabel, page](DialogResult res, const std::string& path) {
                    if (res != DialogResult::OK || path.empty()) return;

                    std::string ext;
                    size_t d = path.find_last_of('.');
                    if (d != std::string::npos) ext = path.substr(d + 1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    CDRExportResult r = (ext == "xar")
                            ? UltraCanvasCDRPlugin::ExportToXAR(cdrFile, path)
                            : UltraCanvasCDRPlugin::ExportToSVG(cdrFile, path, page);
                    if (r.success) {
                        std::string files;
                        for (const auto& f : r.writtenFiles) {
                            files += (files.empty() ? "" : ", ") + f;
                        }
                        statusLabel->SetText("Saved page " + std::to_string(page + 1) +
                                             " of " + cdrFile + " to " + files);
                    } else {
                        statusLabel->SetText("Save failed: " + r.error);
                    }
                });
        };
        return btn;
    }

// ===== CDR VECTOR EXAMPLES IMPLEMENTATION =====
    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCDRVectorExamples() {
        auto container = std::make_shared<UltraCanvasContainer>("CDRExamples", 0, 0, 1000, 780);
        container->SetBackgroundColor(Color(245, 245, 245, 255));

        // Title
        auto title = std::make_shared<UltraCanvasLabel>("CDRTitle", 10, 10, 600, 30);
        title->SetText("CorelDRAW CDR Graphics Demo - Click to Open in the Media Viewer");
        title->SetFontSize(16);
        title->SetFontWeight(FontWeight::Bold);
        container->AddChild(title);

        // Description
        auto description = std::make_shared<UltraCanvasLabel>("CDRDescription", 10, 45, 700, 40);
        description->SetText("Click on CDR images to open them in the media viewer. Use navigation buttons for multi-page files.\n\"Save as…\" exports the shown page to SVG (XAR listed, writer not finished yet). Supports CDR, CMX, CCX, CDT.");
        description->SetFontSize(12);
        description->SetTextColor(Color(80, 80, 80, 255));
        container->AddChild(description);

        // Status label for feedback
        auto statusLabel = std::make_shared<UltraCanvasLabel>("CDRStatus", 10, 700, 980, 60);
        statusLabel->SetText("Ready. Click on a CDR file to view.");
        statusLabel->SetFontSize(11);
        statusLabel->SetTextColor(Color(60, 60, 60, 255));
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        container->AddChild(statusLabel);

        // One tile per sample drawing in media/vector/CDR/. Each carries the
        // controls the element exposes: page navigation for multi-page
        // documents, zoom / fit, and "Save as..." for the shown page.
        auto makeTile = [&](const std::string& id, int x, int y,
                            const std::string& fileName) {
            auto tile = std::make_shared<UltraCanvasContainer>(id, x, y, 300, 300);
            tile->SetBackgroundColor(Colors::White);
            tile->SetBorders(2, Color(180, 180, 180, 255));

            auto element = std::make_shared<UltraCanvasCDRElement>(id + "El", 10, 10, 280, 220);
            element->SetFitMode(CDRFitMode::FitPage);

            std::string path = NormalizePath(GetResourcesDir() + "media/vector/CDR/" + fileName);
            if (element->LoadFromFile(path)) {
                statusLabel->SetText("Loaded: " + path + " (" +
                                     std::to_string(element->GetPageCount()) + " pages)");
            } else {
                statusLabel->SetText("Failed to load " + path);
            }

            auto nameLabel = std::make_shared<UltraCanvasLabel>(id + "Name", 10, 234, 280, 22);
            nameLabel->SetText(fileName);
            nameLabel->SetAlignment(TextAlignment::Center);
            nameLabel->SetFontSize(11);
            tile->AddChild(nameLabel);

            auto makeButton = [&](const std::string& suffix, int bx, int bw,
                                  const std::string& text, std::function<void()> action) {
                auto btn = std::make_shared<UltraCanvasButton>(id + suffix, bx, 260, bw, 26);
                btn->SetText(text);
                btn->SetFontSize(9);
                btn->onClick = std::move(action);
                tile->AddChild(btn);
            };

            // Page navigation. The label doubles as the page indicator.
            auto pageLabel = std::make_shared<UltraCanvasLabel>(id + "Page", 40, 260, 58, 26);
            pageLabel->SetAlignment(TextAlignment::Center);
            pageLabel->SetFontSize(10);
            pageLabel->SetText(element->IsLoaded()
                                       ? "1/" + std::to_string(element->GetPageCount())
                                       : "-/-");
            element->onPageChanged = [pageLabel, element = element.get()](int page) {
                pageLabel->SetText(std::to_string(page + 1) + "/" +
                                   std::to_string(element->GetPageCount()));
            };

            makeButton("Prev", 10, 26, "\xE2\x97\x80", [element]() {   // U+25C0
                if (element->IsLoaded() && element->GetCurrentPage() > 0) {
                    element->SetCurrentPage(element->GetCurrentPage() - 1);
                }
            });
            tile->AddChild(pageLabel);
            makeButton("Next", 102, 26, "\xE2\x96\xB6", [element]() {  // U+25B6
                if (element->IsLoaded() &&
                    element->GetCurrentPage() < element->GetPageCount() - 1) {
                    element->SetCurrentPage(element->GetCurrentPage() + 1);
                }
            });

            // Zoom / fit.
            makeButton("ZoomOut", 134, 26, "\xE2\x88\x92", [element]() {  // U+2212
                element->SetFitMode(CDRFitMode::FitNone);
                element->SetZoom(element->GetZoom() / 1.25f);
            });
            makeButton("ZoomIn", 164, 26, "+", [element]() {
                element->SetFitMode(CDRFitMode::FitNone);
                element->SetZoom(element->GetZoom() * 1.25f);
            });
            makeButton("Fit", 194, 34, "Fit", [element]() {
                element->SetFitMode(CDRFitMode::FitPage);
            });

            tile->AddChild(MakeCDRSaveAsButton(id + "Save", 232, 260, 58, 26,
                                               element, path, statusLabel));

            element->SetMouseCursor(UCMouseCursor::Hand);
            element->SetEventCallback([tile = tile.get(), statusLabel, path](const UCEvent& event) {
                switch (event.type) {
                    case UCEventType::MouseUp:
                        ShowInMediaViewer(path);
                        statusLabel->SetText("Opened in the media viewer: " + path);
                        return true;
                    case UCEventType::MouseEnter:
                        tile->SetBordersColor(Color(0, 122, 204, 255));
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

        // Every .cdr file shipped under media/vector/CDR. No .cmx / .ccx / .cdt
        // sample ships yet, though the plugin reads those too.
        makeTile("CDRContainer1", 20, 95, "detailed.cdr");
        makeTile("CDRContainer2", 340, 95, "door-panel.cdr");
        makeTile("CDRContainer3", 660, 95, "dubai-atlantis.cdr");

        // ===== INFO PANEL =====
        auto infoContainer = std::make_shared<UltraCanvasContainer>("InfoPanel", 20, 410, 470, 280);
        infoContainer->SetBackgroundColor(Color(240, 248, 255, 255));
        infoContainer->SetBorders(2, Color(100, 149, 237, 255));

        auto infoTitle = std::make_shared<UltraCanvasLabel>("InfoTitle", 10, 10, 450, 25);
        infoTitle->SetText("CDR Plugin Features");
        infoTitle->SetFontWeight(FontWeight::Bold);
        infoTitle->SetFontSize(13);
        infoContainer->AddChild(infoTitle);

        auto infoText = std::make_shared<UltraCanvasLabel>("InfoText", 10, 42, 450, 230);
        infoText->SetText(
                "\xE2\x9C\x93 CorelDRAW CDR format\n"
                "\xE2\x9C\x93 Corel Presentation Exchange CMX\n"
                "\xE2\x9C\x93 Multi-page document support\n"
                "\xE2\x9C\x93 Vector paths and shapes\n"
                "\xE2\x9C\x93 Text with styling\n"
                "\xE2\x9C\x93 Transformations (rotate, scale)\n"
                "\xE2\x9C\x93 Groups and layers\n"
                "\xE2\x9C\x93 Stroke and fill styles\n"
                "\xE2\x9C\x93 Zoom and pan controls\n"
                "\xE2\x9C\x93 Fit modes (page, width, height)\n"
                "\xE2\x9C\x93 Save as SVG (XAR planned)\n"
                "\n"
                "Uses libcdr for parsing."
        );
        infoText->SetFontSize(11);
        infoText->SetTextColor(Color(50, 50, 50, 255));
        infoContainer->AddChild(infoText);

        container->AddChild(infoContainer);

        // ===== THE BUNDLED DRAWINGS =====
        auto howContainer = std::make_shared<UltraCanvasContainer>("HowPanel", 510, 410, 470, 280);
        howContainer->SetBackgroundColor(Color(255, 250, 240, 255));
        howContainer->SetBorders(2, Color(222, 184, 135, 255));

        auto howTitle = std::make_shared<UltraCanvasLabel>("HowTitle", 10, 10, 450, 25);
        howTitle->SetText("The bundled drawings");
        howTitle->SetFontWeight(FontWeight::Bold);
        howTitle->SetFontSize(13);
        howContainer->AddChild(howTitle);

        auto howText = std::make_shared<UltraCanvasLabel>("HowText", 10, 42, 450, 230);
        howText->SetText(
                "detailed.cdr - 3.3 MB, one 614 x 384 pt page: 67 draw calls,\n"
                "but 10 gradients and 6 embedded bitmaps - the fill and image\n"
                "paths, and the one worth zooming into.\n\n"
                "door-panel.cdr - 237 KB, one Letter page, 400 draw calls of\n"
                "flat-filled line work: no gradients, no bitmaps.\n\n"
                "dubai-atlantis.cdr - 97 KB, one Letter page and the busiest\n"
                "of the three at 725 draw calls.\n\n"
                "All three are single-page, so the page arrows stay inert here;\n"
                "zoom and fit work in place, a click opens the drawing\n"
                "in the media viewer, and \"Save as...\" writes the shown page out\n"
                "through the plugin's SVG exporter."
        );
        howText->SetFontSize(11);
        howText->SetTextColor(Color(50, 50, 50, 255));
        howContainer->AddChild(howText);

        container->AddChild(howContainer);

        return container;
    }

} // namespace UltraCanvas