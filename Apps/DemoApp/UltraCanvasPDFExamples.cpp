// Apps/DemoApp/UltraCanvasPDFExamples.cpp
// PDF viewer demo for the UltraCanvas demo app: loads a bundled sample document
// into an UltraCanvasPDFView with a navigation / zoom / search toolbar.
// Version: 1.0.0
// Last Modified: 2026-06-17
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#ifdef ULTRACANVAS_PLUGIN_PDF

#include "Plugins/Documents/UltraCanvasPDFView.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath

#include <string>

namespace UltraCanvas {

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreatePDFExamples() {
    // Root: a flex column that fills the display area.
    auto root = std::make_shared<UltraCanvasContainer>("PDFExamples", 0, 0, 1000, 600);
    root->layout.SetFlexColumn().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root->SetPadding(10, 12, 10, 12);

    // ----- Title -----
    auto title = std::make_shared<UltraCanvasLabel>("PDFTitle", 0, 0, 0, 28);
    title->SetText("PDF Document Viewer");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 150, 255));
    title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    root->AddChild(title);

    // ----- The viewer (created first so the toolbar can capture it) -----
    auto view = CreatePDFView("DemoPDFView", 0, 0, 0, 0);
    view->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    std::weak_ptr<UltraCanvasPDFView> viewWeak = view;

    // Page indicator (lives in the toolbar) and a status line (below the view).
    auto pageLabel = std::make_shared<UltraCanvasLabel>("PDFPageLabel", 0, 0, 120, 30);
    pageLabel->SetText("Page - / -");
    pageLabel->SetFontSize(12);
    pageLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    auto statusLabel = std::make_shared<UltraCanvasLabel>("PDFStatusLabel", 0, 0, 0, 22);
    statusLabel->SetText("Mouse-wheel scrolls, Ctrl+wheel zooms, click a thumbnail "
                         "to jump, F3 finds next.");
    statusLabel->SetFontSize(11);
    statusLabel->SetTextColor(Color(110, 110, 110, 255));
    statusLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    // Wire callbacks before loading so the initial page is reflected immediately.
    view->onPageChanged = [pageLabel](int cur, int total) {
        pageLabel->SetText("Page " + std::to_string(cur) + " / " + std::to_string(total));
    };
    view->onSearchResults = [statusLabel](int hits) {
        statusLabel->SetTextColor(Color(110, 110, 110, 255));
        statusLabel->SetText(hits > 0 ? (std::to_string(hits) + " match(es) found")
                                      : "No matches found");
    };
    view->onError = [statusLabel](const std::string& msg) {
        statusLabel->SetTextColor(Color(180, 60, 60, 255));
        statusLabel->SetText("Error: " + msg);
    };

    const std::string pdfPath = NormalizePath(GetResourcesDir() + "media/sample.pdf");
    view->LoadFromPath(pdfPath);

    // ----- Toolbar -----
    auto toolbar = std::make_shared<UltraCanvasContainer>("PDFToolbar", 0, 0, 0, 40);
    toolbar->layout.SetFlexRow().SetFlexGap(6)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    toolbar->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    auto addToolbarButton = [&](const std::string& id, const std::string& text,
                                int w, std::function<void()> onClick) {
        auto btn = CreateButton(id, 0, 0, w, 30, text);
        btn->onClick = std::move(onClick);
        btn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        toolbar->AddChild(btn);
    };

    addToolbarButton("PDFPrev", "Prev", 60, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->GoToPrevPage();
    });
    addToolbarButton("PDFNext", "Next", 60, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->GoToNextPage();
    });
    addToolbarButton("PDFZoomOut", "Zoom -", 70, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->ZoomOut();
    });
    addToolbarButton("PDFZoomIn", "Zoom +", 70, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->ZoomIn();
    });
    addToolbarButton("PDFZoomFit", "Fit", 50, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->ZoomToFit();
    });

    auto searchInput = CreateTextInput("PDFSearch", 0, 0, 180, 30);
    searchInput->SetPlaceholder("Search...");
    searchInput->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    std::weak_ptr<UltraCanvasTextInput> searchWeak = searchInput;

    auto doSearch = [viewWeak, searchWeak]() {
        auto v = viewWeak.lock();
        auto s = searchWeak.lock();
        if (v && s) v->SetSearchQuery(s->GetText());
    };
    searchInput->onEnterPressed = [doSearch](const std::string&) { doSearch(); return true; };
    toolbar->AddChild(searchInput);

    addToolbarButton("PDFFind", "Find", 60, doSearch);
    addToolbarButton("PDFNextHit", "Next hit", 80, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->NextHit();
    });

    pageLabel->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    toolbar->AddChild(pageLabel);

    // ----- Assemble in visual order: title, toolbar, viewer, status -----
    root->AddChild(toolbar);
    root->AddChild(view);
    root->AddChild(statusLabel);

    return root;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_PDF
