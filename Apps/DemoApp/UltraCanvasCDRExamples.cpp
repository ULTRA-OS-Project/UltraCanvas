// Apps/DemoApp/UltraCanvasCDRExamples.cpp
// CDR vector graphics demo examples for UltraCanvas Framework
// Version: 1.5.0 - one drawing at a time, a tab per file on the left; each is
//                  read when its tab is first shown, on a worker thread
//                  (LoadFromFileAsync) behind a busy indicator, so opening the
//                  page no longer freezes the window while all three load
// Version: 1.4.0 - the shared page look (UltraCanvasDemoPageKit): each drawing
//                  in a card with its controls in a row, the two panels as
//                  cards, the status line at the bottom sized to its text
// Version: 1.3.0 - Save as XAR works: the Vector plugin's CDRConverter reads
//                  the drawing into its document model and XARConverter
//                  writes that (the CDR plugin alone cannot reach either)
// Version: 1.2.1
// Last Modified: 2026-10-27
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasBusyIndicator.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasFileLoader.h"
#include "../Plugins/Vector/CDR/UltraCanvasCDRPlugin.h"
#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN
#include "UltraCanvasCDRConverter.h"
#include "UltraCanvasXARConverter.h"
#endif
#include <algorithm>
#include <chrono>
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
    namespace {
        // The drawings shipped under media/vector/CDR, one tab each. No
        // .cmx / .ccx / .cdt sample ships yet, though the plugin reads those too.
        struct CDRSample {
            const char* file;
            const char* about;
        };
        const CDRSample kCDRSamples[] = {
            {"detailed.cdr",
             "3.3 MB, one 614 x 384 pt page: 67 draw calls, but 10 gradients and 6 embedded "
             "bitmaps - the fill and image paths, and the one worth zooming into. The slowest of "
             "the three to read: every pixel of its bitmaps is colour-converted."},
            {"door-panel.cdr",
             "237 KB, one Letter page, 400 draw calls of flat-filled line work: no gradients, "
             "no bitmaps."},
            {"dubai-atlantis.cdr",
             "97 KB, one Letter page and the busiest of the three at 725 draw calls."},
        };
    }

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateCDRVectorExamples() {
        auto page = DemoKit::CreatePage("CDRExamples",
                "CorelDRAW CDR Graphics Demo",
                "One drawing at a time: pick a file on the left. A drawing is read the first time "
                "its tab is shown, on a worker thread, so the window stays responsive meanwhile. "
                "A click on the drawing opens it in the media viewer; \"Save as\" exports the shown "
                "page to SVG, or the first page to XAR. Supports CDR, CMX, CCX, CDT.");
        // The tabs take the height left under the texts.
        page->SetFillsArea(true);

        // Status line for feedback; it goes in at the bottom of the page.
        auto statusLabel = DemoKit::CreateText("CDRStatus", "Ready.");
        statusLabel->SetFontSize(11);
        statusLabel->SetBackgroundColor(Color(230, 230, 230, 255));
        statusLabel->SetPadding(6);

        auto tabs = std::make_shared<UltraCanvasTabbedContainer>("CDRTabs", 0, 0, 0, 0);
        tabs->SetTabPosition(TabPosition::Left);
        tabs->SetTabStyle(TabStyle::Modern);
        tabs->SetCloseMode(TabCloseMode::NoClose);
        tabs->SetTabHeight(28);

        // What starts a tab's drawing loading, by tab index; the plugin page
        // at the end has nothing to load.
        auto starters = std::make_shared<std::vector<std::function<void()>>>();

        for (const CDRSample& sample : kCDRSamples) {
            const std::string fileName = sample.file;
            const std::string id = "CDR_" + fileName.substr(0, fileName.find('.'));
            const std::string path = NormalizePath(GetResourcesDir() + "media/vector/CDR/" + fileName);

            auto tabPage = DemoKit::CreateColumn(id + "Page", 8);
            tabPage->SetPadding(10, 12, 10, 12);
            tabPage->SetBackgroundColor(Colors::White);

            // ----- the controls, above the drawing -----
            auto controls = DemoKit::CreateRow(id + "Controls", 4);
            DemoKit::Add(tabPage, controls);

            auto element = std::make_shared<UltraCanvasCDRElement>(id + "El", 0, 0, 0, 0);
            element->SetFitMode(CDRFitMode::FitPage);

            auto makeButton = [&](const std::string& suffix, int bw,
                                  const std::string& text, std::function<void()> action) -> UltraCanvasButton& {
                auto btn = std::make_shared<UltraCanvasButton>(id + suffix, 0, 0, bw, 26);
                btn->SetText(text);
                btn->SetFontSize(10);
                btn->onClick = std::move(action);
                DemoKit::Add(controls, btn);
                return *btn;
            };

            // Page navigation. The label doubles as the page indicator.
            auto pageLabel = DemoKit::CreateCaption(id + "PageNo", "-/-");
            pageLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
            pageLabel->SetTextColor(DemoKit::BodyText());
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
            makeButton("ZoomOut", 28, "\xE2\x88\x92", [element]() {  // U+2212
                element->SetFitMode(CDRFitMode::FitNone);
                element->SetZoom(element->GetZoom() / 1.25f);
            }).SetTooltip("Zoom out");
            makeButton("ZoomIn", 28, "+", [element]() {
                element->SetFitMode(CDRFitMode::FitNone);
                element->SetZoom(element->GetZoom() * 1.25f);
            }).SetTooltip("Zoom in");
            makeButton("Fit", 40, "Fit", [element]() {
                element->SetFitMode(CDRFitMode::FitPage);
            }).SetTooltip("Fit the page into the view");

            DemoKit::Add(controls, MakeCDRSaveAsButton(id + "Save", 0, 0, 64, 26,
                                                       element, path, statusLabel));
            makeButton("Open", 120, "Open in viewer", [path, statusLabel]() {
                ShowInMediaViewer(path);
                statusLabel->SetText("Opened in the media viewer: " + path);
            });

            // ----- the drawing, or what stands in for it while it is read -----
            auto view = DemoKit::CreateColumn(id + "View", 0);
            view->SetBackgroundColor(Color(246, 246, 248, 255));
            view->SetBorders(1, DemoKit::CardBorder());
            DemoKit::Add(tabPage, view, 1);
            view->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));

            element->SetVisible(false);
            element->SetMouseCursor(UCMouseCursor::Hand);
            element->SetTooltip("Click to open in the media viewer");
            DemoKit::Add(view, element, 1);
            element->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));

            // A turning ring and what it waits for, in the middle of the view.
            auto loadingPane = DemoKit::CreateColumn(id + "Loading", 10);
            loadingPane->layout.SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
                               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
            DemoKit::Add(view, loadingPane, 1);
            auto busy = CreateBusyIndicator(id + "Busy", 0, 0, 48.0f);
            loadingPane->AddChild(busy);
            auto loadingText = DemoKit::CreateCaption(id + "LoadingText", "Reading " + fileName + "\xE2\x80\xA6");
            loadingText->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
            DemoKit::Add(loadingPane, loadingText);

            DemoKit::AddCaption(tabPage, id + "About", fileName + " - " + sample.about);

            // Raw pointers in the element's own callbacks: it owns them, and
            // a shared_ptr there would keep it alive forever.
            UltraCanvasCDRElement* el = element.get();
            UltraCanvasContainer* pane = loadingPane.get();
            UltraCanvasBusyIndicator* ring = busy.get();
            UltraCanvasLabel* waiting = loadingText.get();
            UltraCanvasLabel* pageNo = pageLabel.get();
            const auto started = std::make_shared<std::chrono::steady_clock::time_point>();
            element->onPageChanged = [pageNo, el](int shown) {
                pageNo->SetText(std::to_string(shown + 1) + "/" + std::to_string(el->GetPageCount()));
            };
            element->onLoadComplete = [el, pane, ring, pageNo, statusLabel, path, started]() {
                ring->Stop();
                pane->SetVisible(false);
                el->SetVisible(true);
                pageNo->SetText("1/" + std::to_string(el->GetPageCount()));
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - *started).count();
                statusLabel->SetText("Loaded " + path + " (" + std::to_string(el->GetPageCount()) +
                                     (el->GetPageCount() == 1 ? " page" : " pages") + ") in " +
                                     std::to_string(ms) + " ms");
            };
            element->onLoadError = [ring, waiting, statusLabel](const std::string& error) {
                ring->Stop();
                waiting->SetText(error);
                waiting->SetTextColor(Color(170, 40, 40, 255));
                statusLabel->SetText("Failed: " + error);
            };
            element->SetEventCallback([statusLabel, path](const UCEvent& event) {
                if (event.type != UCEventType::MouseUp) return false;
                ShowInMediaViewer(path);
                statusLabel->SetText("Opened in the media viewer: " + path);
                return true;
            });

            // Read once, the first time the tab is shown.
            starters->push_back([element, busy, statusLabel, path, started, loaded = false]() mutable {
                if (loaded) return;
                loaded = true;
                *started = std::chrono::steady_clock::now();
                busy->Start();
                statusLabel->SetText("Reading " + path + "\xE2\x80\xA6");
                element->LoadFromFileAsync(path);
            });

            tabs->AddTab(fileName, tabPage);
        }

        // ===== THE PLUGIN =====
        {
            auto about = DemoKit::CreateColumn("CDRAboutPage", 10);
            about->SetPadding(10, 12, 10, 12);
            about->SetBackgroundColor(Colors::White);
            DemoKit::AddHeading(about, "CDRAboutHeading", "CDR Plugin Features");
            DemoKit::Add(about, DemoKit::CreateBullets("CDRAboutText", {
                    "CorelDRAW CDR format",
                    "Corel Presentation Exchange CMX",
                    "Multi-page document support",
                    "Vector paths and shapes",
                    "Text with styling",
                    "Transformations (rotate, scale)",
                    "Groups and layers",
                    "Stroke and fill styles, gradients, embedded bitmaps",
                    "Zoom and pan controls",
                    "Fit modes (page, width, height)",
                    "Save as SVG, and as XAR through the Vector plugin",
                    "LoadFromFileAsync: a drawing read on a worker thread"}));
            DemoKit::AddCaption(about, "CDRAboutLib",
                    "Uses libcdr for parsing (UltraCanvas/third_party/libcdr, patched).");
            starters->push_back([]() {});
            tabs->AddTab("About the plugin", about);
        }

        tabs->onTabChange = [starters](int, int index) {
            if (index >= 0 && index < static_cast<int>(starters->size())) (*starters)[static_cast<size_t>(index)]();
        };

        auto tabsCard = DemoKit::CreateCard("CDRTabsCard");
        tabsCard->SetPadding(4);
        DemoKit::Add(page, tabsCard, 1);
        DemoKit::Add(tabsCard, tabs, 1);
        tabs->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
        tabs->SetActiveTab(0);
        // SetActiveTab on the tab already active reports no change.
        (*starters)[0]();

        DemoKit::Add(page, statusLabel);

        return page;
    }

} // namespace UltraCanvas