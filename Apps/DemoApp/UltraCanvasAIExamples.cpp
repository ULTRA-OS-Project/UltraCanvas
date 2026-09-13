// Apps/DemoApp/UltraCanvasAIExamples.cpp
// Adobe Illustrator (.ai) artwork demo for the UltraCanvas demo app.
//
// Since Illustrator 9 a .ai file IS a PDF: the page content is ordinary PDF,
// with Illustrator's private editing data attached as an extra stream
// (AIPrivateData) that every other PDF consumer ignores. Both samples under
// media/vector/AI are exactly that - %PDF-1.5, one page, AIPrivateData
// present - so this page reads them with the MuPDF-backed
// UltraCanvasPDFView, the same viewer the PDF Documents page uses.
// UltraCanvasPDFView::LoadFromPath() reads a document of this size into
// memory and opens it as "application/pdf", so the .ai extension needs no
// special handling.
//
// Writing .ai is the Vector plugin's VectorConverter::AIConverter: it is
// export-only and emits the PDF writer's output under the .ai extension.
// Version: 1.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#ifdef ULTRACANVAS_PLUGIN_PDF

#include "Plugins/Documents/UltraCanvasPDFView.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath

#include <cmath>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

    struct AISample {
        std::string fileName;
        std::string caption;
    };

    // Every .ai file shipped under media/vector/AI.
    const std::vector<AISample>& AISamples() {
        static const std::vector<AISample> samples = {
                {"turtle.ai", "turtle.ai - filled and stroked artwork, one page"},
                {"mandalorian-star-wars.ai", "mandalorian-star-wars.ai - flat-colour line art, one page"},
        };
        return samples;
    }

}   // namespace

// ===== AI VECTOR EXAMPLES IMPLEMENTATION =====
std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateAIVectorExamples() {
    auto root = std::make_shared<UltraCanvasContainer>("AIExamples", 0, 0, 1000, 780);
    root->layout.SetFlexColumn().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root->SetPadding(10, 12, 10, 12);

    // ----- Title -----
    auto title = std::make_shared<UltraCanvasLabel>("AITitle", 0, 0, 0, 28);
    title->SetText("Adobe Illustrator (.ai) Artwork");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 150, 255));
    title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    root->AddChild(title);

    // ----- Explanation of what an .ai file actually is -----
    auto description = std::make_shared<UltraCanvasLabel>("AIDescription", 0, 0, 0, 34);
    description->SetText("Since Illustrator 9 an .ai file is a PDF with Illustrator's private editing data attached, "
                         "so UltraCanvas reads it\nwith the same MuPDF-backed viewer as the PDF page. "
                         "Writing .ai is the Vector plugin's AIConverter (export only).");
    description->SetFontSize(11);
    description->SetTextColor(Color(110, 110, 110, 255));
    description->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    root->AddChild(description);

    // ----- The viewer (created first so the toolbar can capture it) -----
    auto view = CreatePDFView("AIView", 0, 0, 0, 0);
    view->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    std::weak_ptr<UltraCanvasPDFView> viewWeak = view;

    auto pageLabel = std::make_shared<UltraCanvasLabel>("AIPageLabel", 0, 0, 120, 30);
    pageLabel->SetText("Page - / -");
    pageLabel->SetFontSize(12);
    pageLabel->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    pageLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(0);

    auto statusLabel = std::make_shared<UltraCanvasLabel>("AIStatusLabel", 0, 0, 0, 22);
    statusLabel->SetFontSize(11);
    statusLabel->SetTextColor(Color(110, 110, 110, 255));
    statusLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

    auto zoomLabel = std::make_shared<UltraCanvasLabel>("AIZoomLabel", 0, 0, 80, 22);
    zoomLabel->SetText("100%");
    zoomLabel->SetFontSize(11);
    zoomLabel->SetTextColor(Color(110, 110, 110, 255));
    zoomLabel->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    zoomLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    // Wire the callbacks before loading so the first document is reflected too.
    view->onPageChanged = [pageLabel](int cur, int total) {
        pageLabel->SetText("Page " + std::to_string(cur) + " / " + std::to_string(total));
    };
    view->onZoomChanged = [zoomLabel](float percent) {
        zoomLabel->SetText(std::to_string(static_cast<int>(std::lround(percent))) + "%");
    };
    view->onError = [statusLabel](const std::string& msg) {
        statusLabel->SetTextColor(Color(180, 60, 60, 255));
        statusLabel->SetText("Error: " + msg);
    };

    // Loads one of the shipped samples and reports what happened.
    auto loadSample = [viewWeak, statusLabel](const AISample& sample) {
        auto v = viewWeak.lock();
        if (!v) return;
        const std::string path =
                NormalizePath(GetResourcesDir() + "media/vector/AI/" + sample.fileName);
        if (v->LoadFromPath(path)) {
            v->ZoomToFit();
            statusLabel->SetTextColor(Color(110, 110, 110, 255));
            statusLabel->SetText("Loaded " + sample.caption +
                                 "  -  mouse-wheel scrolls, Ctrl+wheel zooms.");
        } else {
            statusLabel->SetTextColor(Color(180, 60, 60, 255));
            statusLabel->SetText("Failed to load " + path);
        }
    };

    loadSample(AISamples().front());

    // ----- Toolbar -----
    auto toolbar = std::make_shared<UltraCanvasContainer>("AIToolbar", 0, 0, 0, 40);
    toolbar->layout.SetFlexRow().SetFlexGap(6)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    auto sampleLabel = std::make_shared<UltraCanvasLabel>("AISampleLabel", 0, 0, 56, 30);
    sampleLabel->SetText("Artwork:");
    sampleLabel->SetFontSize(12);
    sampleLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    toolbar->AddChild(sampleLabel);

    auto sampleDropdown = CreateDropdown("AISample", 0, 0, 220, 30);
    for (const auto& sample : AISamples()) sampleDropdown->AddItem(sample.fileName);
    sampleDropdown->SetSelectedIndex(0, false);
    sampleDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    sampleDropdown->onSelectionChanged =
        [loadSample](int index, const DropdownItem&) {
            const auto& samples = AISamples();
            if (index >= 0 && index < static_cast<int>(samples.size())) {
                loadSample(samples[static_cast<size_t>(index)]);
            }
        };
    toolbar->AddChild(sampleDropdown);

    auto addToolbarButton = [&](const std::string& id, const std::string& text,
                                int w, std::function<void()> onClick) {
        auto btn = CreateButton(id, 0, 0, w, 30, text);
        btn->onClick = std::move(onClick);
        btn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        toolbar->AddChild(btn);
    };

    addToolbarButton("AIPrev", "Prev", 60, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->GoToPrevPage();
    });
    addToolbarButton("AINext", "Next", 60, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->GoToNextPage();
    });
    addToolbarButton("AIZoomOut", "Zoom -", 70, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->ZoomOut();
    });
    addToolbarButton("AIZoomIn", "Zoom +", 70, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->ZoomIn();
    });
    addToolbarButton("AIFitPage", "Fit Page", 80, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->ZoomToFit();
    });
    addToolbarButton("AIFitWidth", "Fit Width", 80, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->ZoomToWidth();
    });
    addToolbarButton("AIActualSize", "100%", 60, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->ZoomActualSize();
    });

    toolbar->AddChild(pageLabel);

    // ----- Status row below the view -----
    auto statusRow = std::make_shared<UltraCanvasContainer>("AIStatusRow", 0, 0, 0, 22);
    statusRow->layout.SetFlexRow().SetFlexGap(8)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    statusRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    statusRow->AddChild(statusLabel);
    statusRow->AddChild(zoomLabel);

    // ----- Format notes -----
    auto notes = std::make_shared<UltraCanvasContainer>("AINotes", 0, 0, 0, 116);
    notes->SetBackgroundColor(Color(240, 248, 255, 255));
    notes->SetBorders(2, Color(100, 149, 237, 255));
    notes->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    auto notesTitle = std::make_shared<UltraCanvasLabel>("AINotesTitle", 10, 6, 940, 22);
    notesTitle->SetText("How UltraCanvas handles .ai");
    notesTitle->SetFontWeight(FontWeight::Bold);
    notesTitle->SetFontSize(13);
    notes->AddChild(notesTitle);

    auto notesText = std::make_shared<UltraCanvasLabel>("AINotesText", 10, 30, 940, 82);
    notesText->SetText(
            "Reading: modern (Illustrator 9 and later) .ai files are PDF documents, so they open through the MuPDF-backed\n"
            "PDF engine - paths, clips, transparency groups, gradients, embedded fonts and images all render as in a PDF.\n"
            "Writing: VectorConverter::AIConverter is export-only and writes the Vector plugin's PDF output under the .ai\n"
            "extension - valid for Illustrator and for every PDF consumer.   Legacy (v8 and earlier) .ai files are\n"
            "EPS/PostScript-based; AIConverter recognises them in ValidateData() but does not write them."
    );
    notesText->SetFontSize(11);
    notesText->SetTextColor(Color(50, 50, 50, 255));
    notes->AddChild(notesText);

    // ----- Assemble in visual order -----
    root->AddChild(toolbar);
    root->AddChild(view);
    root->AddChild(statusRow);
    root->AddChild(notes);

    return root;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_PDF
