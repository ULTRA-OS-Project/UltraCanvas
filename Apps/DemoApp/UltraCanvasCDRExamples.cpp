// Apps/DemoApp/UltraCanvasCDRExamples.cpp
// CDR vector graphics demo examples for UltraCanvas Framework
// Version: 1.4.0 - the shared page look (UltraCanvasDemoPageKit): each drawing
//                  in a card with its controls in a row, the two panels as
//                  cards, the status line at the bottom sized to its text
// Version: 1.3.0 - Save as XAR works: the Vector plugin's CDRConverter reads
//                  the drawing into its document model and XARConverter
//                  writes that (the CDR plugin alone cannot reach either)
// Version: 1.2.1
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasFileLoader.h"
#include "../Plugins/Vector/CDR/UltraCanvasCDRPlugin.h"
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
#include "UltraCanvasCDRConverter.h"
#include "UltraCanvasXARConverter.h"
#endif
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>

namespace UltraCanvas {

// ===== "SAVE AS" BUTTON =====
    // CDR to XAR goes through the Vector plugin's document model: its
    // CDRConverter reads the drawing (the first page, through the CDR
    // plugin and librevenge's SVG) and its XARConverter writes it. The CDR
    // plugin cannot do this itself - it sits below the Vector plugin - so
    // without the Vector plugin this is ExportToXAR's error.
    static CDRExportResult SaveCDRAsXAR(const std::string& cdrFile, const std::string& xarPath) {
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
        CDRExportResult result;
        VectorConverter::ConversionOptions options;
        std::string firstWarning;
        options.WarningCallback = [&firstWarning](const std::string& message) {
            if (firstWarning.empty()) firstWarning = message;
        };
        VectorConverter::CDRConverter reader;
        auto document = reader.Import(cdrFile, options);
        if (!document) {
            result.error = firstWarning.empty() ? "could not read " + cdrFile : firstWarning;
            return result;
        }
        VectorConverter::XARConverter writer;
        if (!writer.Export(*document, xarPath, options)) {
            result.error = firstWarning.empty() ? "could not write " + xarPath : firstWarning;
            return result;
        }
        result.success = true;
        result.writtenFiles.push_back(xarPath);
        return result;
#else
        return UltraCanvasCDRPlugin::ExportToXAR(cdrFile, xarPath);
#endif
    }

    // A "Save as" button for one CDR tile: native save dialog offering SVG
    // (the tile's currently shown page, through the CDR plugin) and XAR (the
    // first page, through SaveCDRAsXAR). Outcome lands in the shared status
    // label.
    static std::shared_ptr<UltraCanvasButton> MakeCDRSaveAsButton(
            const std::string& id, int x, int y, int w, int h,
            std::shared_ptr<UltraCanvasCDRElement> cdrElement,
            const std::string& cdrFile,
            std::shared_ptr<UltraCanvasLabel> statusLabel) {
        auto btn = std::make_shared<UltraCanvasButton>(id, x, y, w, h);
        btn->SetText("Save as");
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
            opts.AddFilter("Xara drawing (first page)", "xar");

            const int page = cdrElement->GetCurrentPage();
            UltraCanvasFileLoader::SaveFileDialog(opts,
                [cdrFile, statusLabel, page](DialogResult res, const std::string& path) {
                    if (res != DialogResult::OK || path.empty()) return;

                    std::string ext;
                    size_t d = path.find_last_of('.');
                    if (d != std::string::npos) ext = path.substr(d + 1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    const bool xar = ext == "xar";
                    CDRExportResult r = xar ? SaveCDRAsXAR(cdrFile, path)
                                            : UltraCanvasCDRPlugin::ExportToSVG(cdrFile, path, page);
                    if (r.success) {
                        std::string files;
                        for (const auto& f : r.writtenFiles) {
                            files += (files.empty() ? "" : ", ") + f;
                        }
                        statusLabel->SetText("Saved page " + std::to_string(xar ? 1 : page + 1) +
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
        auto page = DemoKit::CreatePage("CDRExamples",
                "CorelDRAW CDR Graphics Demo - Click to Open in the Media Viewer",
                "Click on CDR images to open them in the media viewer. Use navigation buttons for multi-page files.\n\"Save as\" exports the shown page to SVG (XAR listed, writer not finished yet). Supports CDR, CMX, CCX, CDT.");

        // Status label for feedback; the tiles write to it while they load,
        // and it goes in at the bottom of the page.
        auto statusLabel = DemoKit::CreateText("CDRStatus", "Ready. Click on a CDR file to view.");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        statusLabel->SetPadding(6);

        // One tile per sample drawing in media/vector/CDR/. Each carries the
        // controls the element exposes: page navigation for multi-page
        // documents, zoom / fit, and "Save as" for the shown page.
        auto tiles = DemoKit::AddRow(page, "CDRTiles");
        auto makeTile = [&](const std::string& id, const std::string& fileName) {
            auto tile = DemoKit::AddCard(tiles, id);

            auto element = std::make_shared<UltraCanvasCDRElement>(id + "El", 0, 0, 280, 220);
            element->SetFitMode(CDRFitMode::FitPage);

            std::string path = NormalizePath(GetResourcesDir() + "media/vector/CDR/" + fileName);
            if (element->LoadFromFile(path)) {
                statusLabel->SetText("Loaded: " + path + " (" +
                                     std::to_string(element->GetPageCount()) + " pages)");
            } else {
                statusLabel->SetText("Failed to load " + path);
            }
            DemoKit::AddFixed(tile, element);

            auto nameLabel = DemoKit::AddText(tile, id + "Name", fileName);
            nameLabel->SetFontSize(11);

            auto controls = DemoKit::AddRow(tile, id + "Controls", 4);
            auto makeButton = [&](const std::string& suffix, int bw,
                                  const std::string& text, std::function<void()> action) -> UltraCanvasButton& {
                auto btn = std::make_shared<UltraCanvasButton>(id + suffix, 0, 0, bw, 26);
                btn->SetText(text);
                btn->SetFontSize(9);
                btn->onClick = std::move(action);
                DemoKit::Add(controls, btn);
                return *btn;
            };

            // Page navigation. The label doubles as the page indicator.
            auto pageLabel = DemoKit::CreateCaption(id + "Page", element->IsLoaded()
                                       ? "1/" + std::to_string(element->GetPageCount())
                                       : "-/-");
            pageLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
            pageLabel->SetTextColor(DemoKit::BodyText());
            element->onPageChanged = [pageLabel, element = element.get()](int page) {
                pageLabel->SetText(std::to_string(page + 1) + "/" +
                                   std::to_string(element->GetPageCount()));
            };

            // The page arrows are icons, not "◀" / "▶" glyphs, which the
            // bundled UI font does not have.
            UltraCanvasButton& prev = makeButton("Prev", 26, "", [element]() {
                if (element->IsLoaded() && element->GetCurrentPage() > 0) {
                    element->SetCurrentPage(element->GetCurrentPage() - 1);
                }
            });
            SetFlatButtonIcon(prev, "menu/chevron-left.svg", ButtonIconPosition::Center, 14);
            prev.SetTooltip("Previous page");
            DemoKit::Add(controls, pageLabel);
            UltraCanvasButton& next = makeButton("Next", 26, "", [element]() {
                if (element->IsLoaded() &&
                    element->GetCurrentPage() < element->GetPageCount() - 1) {
                    element->SetCurrentPage(element->GetCurrentPage() + 1);
                }
            });
            SetFlatButtonIcon(next, "menu/chevron-right.svg", ButtonIconPosition::Center, 14);
            next.SetTooltip("Next page");

            // Zoom / fit.
            makeButton("ZoomOut", 26, "\xE2\x88\x92", [element]() {  // U+2212
                element->SetFitMode(CDRFitMode::FitNone);
                element->SetZoom(element->GetZoom() / 1.25f);
            });
            makeButton("ZoomIn", 26, "+", [element]() {
                element->SetFitMode(CDRFitMode::FitNone);
                element->SetZoom(element->GetZoom() * 1.25f);
            });
            makeButton("Fit", 34, "Fit", [element]() {
                element->SetFitMode(CDRFitMode::FitPage);
            });

            DemoKit::Add(controls, MakeCDRSaveAsButton(id + "Save", 0, 0, 58, 26,
                                                       element, path, statusLabel));

            // Hovering the drawing highlights its card; leaving gives the
            // card its own border back.
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
                        tile->SetBordersColor(DemoKit::CardBorder());
                        return true;
                    default:
                        return false;
                }
            });
        };

        // Every .cdr file shipped under media/vector/CDR. No .cmx / .ccx / .cdt
        // sample ships yet, though the plugin reads those too.
        makeTile("CDRContainer1", "detailed.cdr");
        makeTile("CDRContainer2", "door-panel.cdr");
        makeTile("CDRContainer3", "dubai-atlantis.cdr");

        auto panels = DemoKit::AddRow(page, "CDRPanels");

        // ===== INFO PANEL =====
        auto infoContainer = DemoKit::AddCard(panels, "InfoPanel", "CDR Plugin Features");
        DemoKit::Add(infoContainer, DemoKit::CreateBullets("InfoText", {
                "CorelDRAW CDR format",
                "Corel Presentation Exchange CMX",
                "Multi-page document support",
                "Vector paths and shapes",
                "Text with styling",
                "Transformations (rotate, scale)",
                "Groups and layers",
                "Stroke and fill styles",
                "Zoom and pan controls",
                "Fit modes (page, width, height)",
                "Save as SVG (XAR planned)"}));
        DemoKit::AddCaption(infoContainer, "InfoLib", "Uses libcdr for parsing.");

        // ===== THE BUNDLED DRAWINGS =====
        auto howContainer = DemoKit::AddCard(panels, "HowPanel", "The bundled drawings");
        DemoKit::AddText(howContainer, "HowText",
                "detailed.cdr - 3.3 MB, one 614 x 384 pt page: 67 draw calls, "
                "but 10 gradients and 6 embedded bitmaps - the fill and image "
                "paths, and the one worth zooming into.\n\n"
                "door-panel.cdr - 237 KB, one Letter page, 400 draw calls of "
                "flat-filled line work: no gradients, no bitmaps.\n\n"
                "dubai-atlantis.cdr - 97 KB, one Letter page and the busiest "
                "of the three at 725 draw calls.\n\n"
                "All three are single-page, so the page arrows stay inert here; "
                "zoom and fit work in place, a click opens the drawing "
                "in the media viewer, and \"Save as\" writes the shown page out "
                "through the plugin's SVG exporter.");

        DemoKit::Add(page, statusLabel);

        return page;
    }

} // namespace UltraCanvas