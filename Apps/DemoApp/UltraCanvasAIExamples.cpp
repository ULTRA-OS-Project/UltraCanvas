// Apps/DemoApp/UltraCanvasAIExamples.cpp
// Adobe Illustrator (.ai) artwork demo for the UltraCanvas demo app.
//
// "Since Illustrator 9 a .ai file IS a PDF, so open it with the PDF engine"
// is only half the story, and this page used to tell only that half - which
// is why it showed a blank page for both of the samples under
// media/vector/AI. Illustrator's "Create PDF Compatible File" option decides
// whether the PDF page carries the artwork at all; with it off (and it is
// off in what CorelDRAW and several other exporters write) the PDF page
// draws nothing and every path lives in the private /AIPrivateData streams
// instead. Those are what the Vector plugin's AIConverter reads
// (UltraCanvasAIReader.cpp), building a VectorStorage::VectorDocument that
// this page shows in an UltraCanvasVectorElement.
//
// So the page tries the Vector plugin first and names the route it took. A
// .ai whose artwork really is in its PDF page carries no private data,
// AIConverter declines it, and the MuPDF-backed UltraCanvasPDFView takes
// over - which is what a file written by AIConverter itself (an export-only
// path that emits the PDF writer's output) needs.
// Version: 2.1.0 - the shared page look (UltraCanvasDemoPageKit): the viewer in a
//                  card that fills the area, a wrapping toolbar, every text sized to fit
// Version: 2.0.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"

#ifdef ULTRACANVAS_HAS_VECTOR_PLUGIN

#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasVectorElement.h"
#include "UltraCanvasVectorFormatsPlugin.h"
#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath

#ifdef ULTRACANVAS_PLUGIN_PDF
#include "Plugins/Documents/UltraCanvasPDFView.h"
#endif

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    struct AISample {
        std::string fileName;
        std::string caption;
    };

    // Every .ai file shipped under media/vector/AI. Both are CorelDRAW
    // exports: a valid PDF container whose page is empty, with the line art
    // in the Illustrator private data.
    const std::vector<AISample>& AISamples() {
        static const std::vector<AISample> samples = {
                {"turtle.ai", "turtle.ai - decorative line art"},
                {"mandalorian-star-wars.ai", "mandalorian-star-wars.ai - flat-colour line art"},
        };
        return samples;
    }

    // The page's two viewers. They are held together so the toolbar's
    // lambdas capture one thing rather than a capture list that changes
    // shape with the build's plugins.
    struct Viewers {
        std::weak_ptr<UltraCanvasVectorElement> vector;
#ifdef ULTRACANVAS_PLUGIN_PDF
        std::weak_ptr<UltraCanvasPDFView> pdf;
#endif
        // Which one the toolbar drives: the vector viewer whenever it is the
        // one on screen.
        std::shared_ptr<UltraCanvasVectorElement> ActiveVector() const {
            auto element = vector.lock();
            return (element && element->IsVisible()) ? element : nullptr;
        }
        void ShowVector() const {
            if (auto element = vector.lock()) element->SetVisible(true);
#ifdef ULTRACANVAS_PLUGIN_PDF
            if (auto element = pdf.lock()) element->SetVisible(false);
#endif
        }
#ifdef ULTRACANVAS_PLUGIN_PDF
        void ShowPdf() const {
            if (auto element = vector.lock()) element->SetVisible(false);
            if (auto element = pdf.lock()) element->SetVisible(true);
        }
#endif
    };

    struct AILoadResult {
        std::shared_ptr<VectorStorage::VectorDocument> document;
        std::vector<std::string> warnings;
    };

    AILoadResult LoadAIDocument(const std::string& path) {
        AILoadResult result;
        VectorConverter::AIConverter converter;
        VectorConverter::ConversionOptions options;
        options.WarningCallback = [&result](const std::string& message) {
            result.warnings.push_back(message);
        };
        result.document = converter.Import(path, options);
        return result;
    }

    size_t CountDrawables(const VectorStorage::VectorElement& element) {
        if (const auto* group = dynamic_cast<const VectorStorage::VectorGroup*>(&element)) {
            size_t total = 0;
            for (const auto& child : group->Children) if (child) total += CountDrawables(*child);
            return total;
        }
        return 1;
    }

    std::string DescribeDocument(const VectorStorage::VectorDocument& doc) {
        size_t drawables = 0;
        for (const auto& layer : doc.Layers) if (layer) drawables += CountDrawables(*layer);
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer),
                      "%zu object%s on %zu layer%s, page %.0f x %.0f pt",
                      drawables, drawables == 1 ? "" : "s",
                      doc.Layers.size(), doc.Layers.size() == 1 ? "" : "s",
                      doc.Size.width, doc.Size.height);
        return buffer;
    }

}   // namespace

// ===== AI VECTOR EXAMPLES IMPLEMENTATION =====
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAIVectorExamples() {
    auto page = DemoKit::CreatePage("AIExamples", "Adobe Illustrator (.ai) Artwork",
            "An .ai file is a PDF that also carries Illustrator's own artwork data. Saved without PDF "
            "compatibility, the PDF page draws nothing and that private data is the only copy of the "
            "drawing - which is what AIConverter reads.");
    // The viewers pan and zoom themselves: the page is the area's height.
    page->SetFillsArea(true);

    auto viewerCard = DemoKit::CreateCard("AIViewerCard");
    DemoKit::Add(page, viewerCard, 1);

    // ----- The two viewers: one per route, only one shown at a time -----
    auto stage = std::make_shared<UltraCanvasContainer>("AIStage", 0, 0, 0, 0);
    stage->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    stage->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                     .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto vectorView = CreateVectorElement("AIVectorView", 0, 0, 0, 0);
    VectorElementOptions vectorOptions = vectorView->GetOptions();
    vectorOptions.InteractionMode = VectorInteractionMode::PanZoom;
    vectorOptions.BackgroundColor = Colors::White;
    vectorOptions.MinZoom = 0.05f;
    vectorOptions.MaxZoom = 50.0f;
    vectorView->SetOptions(vectorOptions);
    vectorView->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    stage->AddChild(vectorView);

    auto viewers = std::make_shared<Viewers>();
    viewers->vector = vectorView;

#ifdef ULTRACANVAS_PLUGIN_PDF
    auto pdfView = CreatePDFView("AIPDFView", 0, 0, 0, 0);
    pdfView->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    pdfView->SetVisible(false);
    stage->AddChild(pdfView);
    viewers->pdf = pdfView;
#endif

    // ----- Status and route -----
    // The status turns red when a sample fails to load.
    auto statusLabel = DemoKit::CreateCaption("AIStatusLabel", "");
    statusLabel->SetFontSize(11);
    statusLabel->SetTextColor(Color(110, 110, 110, 255));

    auto routeLabel = DemoKit::CreateCaption("AIRouteLabel", "");
    routeLabel->SetFontSize(11);
    routeLabel->SetTextColor(Color(110, 110, 110, 255));

    // Loads one of the shipped samples: the Vector plugin first, the PDF
    // engine only for a file whose artwork really is in its PDF page.
    auto loadSample = [viewers, statusLabel, routeLabel](const AISample& sample) {
        const std::string path =
                NormalizePath(GetResourcesDir() + "media/vector/AI/" + sample.fileName);
        AILoadResult loaded = LoadAIDocument(path);

        auto vector = viewers->vector.lock();
        if (loaded.document && vector) {
            vector->SetDocument(loaded.document);
            viewers->ShowVector();
            vector->ZoomToFit();
            routeLabel->SetText("Vector plugin - AIConverter");
            statusLabel->SetTextColor(Color(110, 110, 110, 255));
            statusLabel->SetText(sample.caption + " - " + DescribeDocument(*loaded.document) +
                                 "  -  drag to pan, wheel to zoom.");
            return;
        }

#ifdef ULTRACANVAS_PLUGIN_PDF
        // No private artwork: a PDF-compatible .ai, which the PDF engine
        // renders exactly as Illustrator wrote it.
        if (auto pdf = viewers->pdf.lock()) {
            viewers->ShowPdf();
            if (pdf->LoadFromPath(path)) {
                pdf->ZoomToFit();
                routeLabel->SetText("PDF engine - MuPDF");
                statusLabel->SetTextColor(Color(110, 110, 110, 255));
                statusLabel->SetText(sample.caption +
                                     " - the artwork is in the PDF page  -  "
                                     "mouse-wheel scrolls, Ctrl+wheel zooms.");
                return;
            }
        }
#endif

        routeLabel->SetText("not loaded");
        statusLabel->SetTextColor(Color(180, 60, 60, 255));
        std::string message = "Failed to load " + path;
        for (const std::string& warning : loaded.warnings) message += "  -  " + warning;
        statusLabel->SetText(message);
    };

    loadSample(AISamples().front());

    // ----- Toolbar -----
    // Wraps onto a second line when the window is narrow or the texts long.
    auto toolbar = DemoKit::AddRow(viewerCard, "AIToolbar", 6);
    toolbar->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);

    DemoKit::AddText(toolbar, "AISampleLabel", "Artwork:");

    auto sampleDropdown = CreateDropdown("AISample", 0, 0, 220, 30);
    for (const auto& sample : AISamples()) sampleDropdown->AddItem(sample.fileName);
    sampleDropdown->SetSelectedIndex(0, false);
    sampleDropdown->onSelectionChanged =
        [loadSample](int index, const DropdownItem&) {
            const auto& samples = AISamples();
            if (index >= 0 && index < static_cast<int>(samples.size())) {
                loadSample(samples[static_cast<size_t>(index)]);
            }
        };
    DemoKit::AddFixed(toolbar, sampleDropdown);

    // The zoom buttons drive whichever viewer is showing.
    auto zoomBy = [viewers](float factor) {
        if (auto vector = viewers->ActiveVector()) {
            vector->SetZoom(vector->GetZoom() * factor);
            return;
        }
#ifdef ULTRACANVAS_PLUGIN_PDF
        if (auto pdf = viewers->pdf.lock()) {
            if (factor > 1.0f) pdf->ZoomIn(); else pdf->ZoomOut();
        }
#else
        (void)factor;
#endif
    };
    auto fitPage = [viewers]() {
        if (auto vector = viewers->ActiveVector()) { vector->ZoomToFit(); return; }
#ifdef ULTRACANVAS_PLUGIN_PDF
        if (auto pdf = viewers->pdf.lock()) pdf->ZoomToFit();
#endif
    };
    auto actualSize = [viewers]() {
        if (auto vector = viewers->ActiveVector()) { vector->ZoomToActualSize(); return; }
#ifdef ULTRACANVAS_PLUGIN_PDF
        if (auto pdf = viewers->pdf.lock()) pdf->ZoomActualSize();
#endif
    };

    auto addToolbarButton = [&](const std::string& id, const std::string& text,
                                int width, std::function<void()> onClick) {
        auto button = CreateButton(id, 0, 0, width, 30, text);
        button->onClick = std::move(onClick);
        DemoKit::Add(toolbar, button);
    };

    addToolbarButton("AIZoomOut", "Zoom -", 70, [zoomBy]() { zoomBy(1.0f / 1.25f); });
    addToolbarButton("AIZoomIn", "Zoom +", 70, [zoomBy]() { zoomBy(1.25f); });
    addToolbarButton("AIFitPage", "Fit Page", 80, fitPage);
    addToolbarButton("AIActualSize", "100%", 60, actualSize);
    // The route takes the rest of the line, right-aligned.
    routeLabel->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    DemoKit::Add(toolbar, routeLabel, 1);

    // ----- The view and the status below it -----
    DemoKit::Add(viewerCard, stage, 1);
    DemoKit::Add(viewerCard, statusLabel);

    // ----- Format notes -----
    auto notes = DemoKit::AddCard(page, "AINotes", "How UltraCanvas handles .ai");
    DemoKit::AddText(notes, "AINotesText",
            "Reading: AIConverter takes the artwork from the file's /AIPrivateData streams and interprets "
            "Illustrator's art language - paths, compound paths, clips, layers, groups, the grey / CMYK / RGB / "
            "spot colour operators and the AI9 transparency operator. Legacy (v8 and earlier) EPS-based .ai files "
            "carry the same language in the open and read through the same parser. A .ai saved WITH \"Create PDF "
            "compatible file\" also draws through its PDF page, so it falls back to the MuPDF engine. Writing: "
            "AIConverter is export-only and emits PDF under the .ai extension.");

    return page;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_HAS_VECTOR_PLUGIN
