// Apps/DemoApp/UltraCanvasPDFExamples.cpp
// PDF viewer demo for the UltraCanvas demo app: loads a bundled sample document
// into an UltraCanvasPDFView with a navigation / zoom / search toolbar.
// Programmer's guide: Docs/UltraCanvas/UltraCanvasPDFExamples.md
// Version: 1.4.0
// Last Modified: 2026-06-19
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#ifdef ULTRACANVAS_PLUGIN_PDF

#include "Plugins/Documents/UltraCanvasPDFView.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath

#include <cmath>
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

    // ----- Attribution / info line (with a clickable MuPDF link) -----
    auto infoRow = std::make_shared<UltraCanvasContainer>("PDFInfoRow", 0, 0, 0, 18);
    infoRow->layout.SetFlexRow().SetFlexGap(4)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    infoRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    auto info = std::make_shared<UltraCanvasLabel>("PDFInfo", 0, 0, 0, 18);
    info->SetText("UltraCanvas support for PDF files is based on the MuPDF "
                  "library \xE2\x80\x94");
    info->SetFontSize(11);
    info->SetTextColor(Color(110, 110, 110, 255));
    info->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    infoRow->AddChild(info);

    auto link = std::make_shared<UltraCanvasLabel>("PDFMuPDFLink", 0, 0, 0, 18);
    link->SetTextIsMarkup(true);
    link->SetText("<span color=\"blue\" underline=\"single\">https://mupdf.com/</span>");
    link->SetFontSize(11);
    link->SetMouseCursor(UCMouseCursor::Hand);
    link->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    link->onClick = []() { OpenURL("https://mupdf.com/"); };
    infoRow->AddChild(link);

    root->AddChild(infoRow);

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
    statusLabel->SetText("Mouse-wheel scrolls, Ctrl+wheel zooms, F3 finds next, "
                         "\"Select\" then drag to select text, right-click to "
                         "copy/export or extract an image.");
    statusLabel->SetFontSize(11);
    statusLabel->SetTextColor(Color(110, 110, 110, 255));
    statusLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(1);

    // Zoom level read-out, shown below the view at the right edge.
    auto zoomLabel = std::make_shared<UltraCanvasLabel>("PDFZoomLabel", 0, 0, 80, 22);
    zoomLabel->SetText("100%");
    zoomLabel->SetFontSize(11);
    zoomLabel->SetTextColor(Color(110, 110, 110, 255));
    zoomLabel->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    zoomLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    // Wire callbacks before loading so the initial page is reflected immediately.
    view->onPageChanged = [pageLabel](int cur, int total) {
        pageLabel->SetText("Page " + std::to_string(cur) + " / " + std::to_string(total));
    };
    view->onSearchResults = [statusLabel](int hits) {
        statusLabel->SetTextColor(Color(110, 110, 110, 255));
        statusLabel->SetText(hits > 0 ? (std::to_string(hits) + " match(es) found")
                                      : "No matches found");
    };
    view->onActiveHitChanged = [statusLabel](int active, int total) {
        if (total > 0) {
            statusLabel->SetTextColor(Color(110, 110, 110, 255));
            statusLabel->SetText("Match " + std::to_string(active) + " of " +
                                 std::to_string(total));
        }
    };
    view->onZoomChanged = [zoomLabel](float percent) {
        zoomLabel->SetText(std::to_string(static_cast<int>(std::lround(percent))) + "%");
    };
    view->onImageExtracted = [statusLabel](const std::string& path, bool ok) {
        statusLabel->SetTextColor(ok ? Color(60, 140, 60, 255)
                                     : Color(180, 60, 60, 255));
        statusLabel->SetText(ok ? ("Image extracted to " + path)
                                : "Failed to extract image");
    };
    view->onSelectionChanged = [statusLabel](int chars) {
        if (chars > 0) {
            statusLabel->SetTextColor(Color(110, 110, 110, 255));
            statusLabel->SetText(std::to_string(chars) + " character(s) selected"
                                 " — Ctrl+C to copy, right-click to export");
        }
    };
    view->onTextExported = [statusLabel](const std::string& path, bool ok) {
        statusLabel->SetTextColor(ok ? Color(60, 140, 60, 255)
                                     : Color(180, 60, 60, 255));
        statusLabel->SetText(ok ? ("Text exported to " + path)
                                : "Failed to export text");
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

    // Zoom presets: fit-page / fit-width plus a few fixed levels.
    auto zoomDropdown = CreateDropdown("PDFZoomMode", 0, 0, 120, 30);
    zoomDropdown->AddItem("Fit Page");
    zoomDropdown->AddItem("Fit Width");
    zoomDropdown->AddItem("Actual Size");
    zoomDropdown->AddItem("50%");
    zoomDropdown->AddItem("75%");
    zoomDropdown->AddItem("100%");
    zoomDropdown->AddItem("125%");
    zoomDropdown->AddItem("150%");
    zoomDropdown->AddItem("200%");
    zoomDropdown->SetSelectedIndex(0, false);   // Fit Page (the view's default)
    zoomDropdown->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    zoomDropdown->onSelectionChanged =
        [viewWeak](int index, const DropdownItem&) {
            auto v = viewWeak.lock();
            if (!v) return;
            switch (index) {
                case 0: v->ZoomToFit();      break;  // Fit Page
                case 1: v->ZoomToWidth();    break;  // Fit Width
                case 2: v->ZoomActualSize(); break;  // 100%
                case 3: v->SetZoom(0.50f);   break;
                case 4: v->SetZoom(0.75f);   break;
                case 5: v->SetZoom(1.00f);   break;
                case 6: v->SetZoom(1.25f);   break;
                case 7: v->SetZoom(1.50f);   break;
                case 8: v->SetZoom(2.00f);   break;
                default: break;
            }
        };
    toolbar->AddChild(zoomDropdown);

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

    addToolbarButton("PDFSearchBtn", "Search", 70, doSearch);
    // Up / down arrows step through the search results.
    addToolbarButton("PDFPrevHit", "\xE2\x96\xB2", 36, [viewWeak]() {  // U+25B2 ▲
        if (auto v = viewWeak.lock()) v->PrevHit();
    });
    addToolbarButton("PDFNextHit", "\xE2\x96\xBC", 36, [viewWeak]() {  // U+25BC ▼
        if (auto v = viewWeak.lock()) v->NextHit();
    });

    // Text-selection mode toggle (Pan <-> Select) and a Copy button.
    auto modeBtn = CreateButton("PDFSelectMode", 0, 0, 80, 30, "Select");
    modeBtn->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    std::weak_ptr<UltraCanvasButton> modeWeak = modeBtn;
    modeBtn->onClick = [viewWeak, modeWeak]() {
        auto v = viewWeak.lock();
        auto b = modeWeak.lock();
        if (!v || !b) return;
        const bool toSelect =
            v->GetMouseMode() == UltraCanvasPDFView::MouseMode::Pan;
        v->SetMouseMode(toSelect ? UltraCanvasPDFView::MouseMode::SelectText
                                 : UltraCanvasPDFView::MouseMode::Pan);
        b->SetText(toSelect ? "Pan" : "Select");
    };
    toolbar->AddChild(modeBtn);

    addToolbarButton("PDFCopy", "Copy", 60, [viewWeak, statusLabel]() {
        if (auto v = viewWeak.lock()) {
            const bool ok = v->CopySelectionToClipboard();
            statusLabel->SetTextColor(ok ? Color(60, 140, 60, 255)
                                         : Color(110, 110, 110, 255));
            statusLabel->SetText(ok ? "Selection copied to clipboard"
                                    : "Nothing selected to copy");
        }
    });

    pageLabel->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    pageLabel->layoutItem.SetFlexGrow(1).SetFlexShrink(0);
    toolbar->AddChild(pageLabel);

    // ----- Status row below the view: hint text (left) + zoom level (right) -----
    auto statusRow = std::make_shared<UltraCanvasContainer>("PDFStatusRow", 0, 0, 0, 22);
    statusRow->layout.SetFlexRow().SetFlexGap(8)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    statusRow->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    statusRow->AddChild(statusLabel);
    statusRow->AddChild(zoomLabel);

    // ----- Assemble in visual order: title, toolbar, viewer, status -----
    root->AddChild(toolbar);
    root->AddChild(view);
    root->AddChild(statusRow);

    return root;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_PDF
