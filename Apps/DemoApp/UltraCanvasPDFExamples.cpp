// Apps/DemoApp/UltraCanvasPDFExamples.cpp
// PDF viewer demo for the UltraCanvas demo app: loads a bundled sample document
// into an UltraCanvasPDFView with a navigation / zoom / search toolbar.
// Programmer's guide: Docs/UltraCanvas/UltraCanvasPDFExamples.md
// Version: 1.7.0 - the shared page look (UltraCanvasDemoPageKit): the viewer in a
//                  card that fills the page area, a wrapping toolbar, buttons and
//                  labels sized to their text
// Version: 1.6.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"

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
    // The viewer scrolls its pages itself: the page is the display area's
    // height and the viewer takes what the title, toolbar and status leave.
    auto root = DemoKit::CreatePage("PDFExamples", "PDF Document Viewer", "");
    root->SetFillsArea(true);

    // ----- Attribution / info line (with a clickable MuPDF link) -----
    auto infoRow = DemoKit::AddRow(root, "PDFInfoRow", 4);

    auto info = DemoKit::CreateText("PDFInfo",
                                    "UltraCanvas support for PDF files is based on the MuPDF "
                                    "library \xE2\x80\x94");
    info->SetFontSize(11);
    info->SetTextColor(DemoKit::SubtitleText());
    DemoKit::Add(infoRow, info);
    info->layoutItem.SetFlexShrink(1);   // wraps rather than pushing the link out

    auto link = DemoKit::CreateText("PDFMuPDFLink", "");
    link->SetTextIsMarkup(true);
    link->SetText("<span color=\"blue\" underline=\"single\">https://mupdf.com/</span>");
    link->SetFontSize(11);
    link->SetMouseCursor(UCMouseCursor::Hand);
    link->onClick = []() { OpenURL("https://mupdf.com/"); };
    DemoKit::Add(infoRow, link);

    // ----- The card: toolbar, viewer, status -----
    auto card = DemoKit::CreateCard("PDFCard");
    card->SetPadding(8);
    DemoKit::Add(root, card, 1);

    // ----- The viewer (created first so the toolbar can capture it) -----
    auto view = CreatePDFView("DemoPDFView", 0, 0, 0, 0);
    view->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
    std::weak_ptr<UltraCanvasPDFView> viewWeak = view;

    // Page indicator (lives in the toolbar) and a status line (below the view).
    auto pageLabel = DemoKit::CreateText("PDFPageLabel", "Page - / -");

    auto statusLabel = DemoKit::CreateText("PDFStatusLabel",
                         "Mouse-wheel scrolls, Ctrl+wheel zooms (+ / - / 0 / 1 "
                         "on the keyboard), F3 finds next, \"Select\" then drag "
                         "to select text, right-click to copy/export or extract "
                         "an image.");
    statusLabel->SetFontSize(11);
    statusLabel->SetTextColor(Color(110, 110, 110, 255));

    // Zoom level read-out, shown below the view at the right edge.
    auto zoomLabel = DemoKit::CreateText("PDFZoomLabel", "100%");
    zoomLabel->SetFontSize(11);
    zoomLabel->SetTextColor(Color(110, 110, 110, 255));
    zoomLabel->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);

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
    // Wraps onto a second line when the window is narrow.
    auto toolbar = DemoKit::AddRow(card, "PDFToolbar", 6);
    toolbar->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);

    // A button as wide as its text, at least the width it had, 30 px high.
    auto makeToolbarButton = [](const std::string& id, const std::string& text, int w) {
        auto btn = std::make_shared<UltraCanvasButton>(id, text);
        CSSLayout::BoxConstraints limits;
        limits.minWidth = CSSLayout::Dimension::Px(static_cast<float>(w));
        limits.minHeight = CSSLayout::Dimension::Px(30);
        btn->boxConstraints = limits;
        return btn;
    };
    auto addToolbarButton = [&](const std::string& id, const std::string& text,
                                int w, std::function<void()> onClick) -> UltraCanvasButton& {
        auto btn = makeToolbarButton(id, text, w);
        btn->onClick = std::move(onClick);
        DemoKit::Add(toolbar, btn);
        return *btn;
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
    DemoKit::Add(toolbar, zoomDropdown);

    auto searchInput = CreateTextInput("PDFSearch", 0, 0, 180, 30);
    searchInput->SetPlaceholder("Search...");
    std::weak_ptr<UltraCanvasTextInput> searchWeak = searchInput;

    auto doSearch = [viewWeak, searchWeak]() {
        auto v = viewWeak.lock();
        auto s = searchWeak.lock();
        if (v && s) v->SetSearchQuery(s->GetText());
    };
    searchInput->onEnterPressed = [doSearch](const std::string&) { doSearch(); return true; };
    DemoKit::Add(toolbar, searchInput);

    addToolbarButton("PDFSearchBtn", "Search", 70, doSearch);
    // Up / down arrows step through the search results. They are icons, not
    // "▲" / "▼" glyphs, which the bundled UI font does not have.
    UltraCanvasButton& prevHit = addToolbarButton("PDFPrevHit", "", 36, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->PrevHit();
    });
    SetFlatButtonIcon(prevHit, "menu/chevron-up.svg", ButtonIconPosition::Center);
    prevHit.SetTooltip("Previous result");
    UltraCanvasButton& nextHit = addToolbarButton("PDFNextHit", "", 36, [viewWeak]() {
        if (auto v = viewWeak.lock()) v->NextHit();
    });
    SetFlatButtonIcon(nextHit, "menu/chevron-down.svg", ButtonIconPosition::Center);
    nextHit.SetTooltip("Next result");

    // Text-selection mode toggle (Pan <-> Select) and a Copy button.
    auto modeBtn = makeToolbarButton("PDFSelectMode", "Select", 80);
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
    DemoKit::Add(toolbar, modeBtn);

    addToolbarButton("PDFCopy", "Copy", 60, [viewWeak, statusLabel]() {
        if (auto v = viewWeak.lock()) {
            const bool ok = v->CopySelectionToClipboard();
            statusLabel->SetTextColor(ok ? Color(60, 140, 60, 255)
                                         : Color(110, 110, 110, 255));
            statusLabel->SetText(ok ? "Selection copied to clipboard"
                                    : "Nothing selected to copy");
        }
    });

    // Toggle thumbnail page-number style (caption beneath <-> big overlay).
    auto numBtn = makeToolbarButton("PDFThumbNum", "#: Under", 100);
    std::weak_ptr<UltraCanvasButton> numWeak = numBtn;
    numBtn->onClick = [viewWeak, numWeak]() {
        auto v = viewWeak.lock();
        auto b = numWeak.lock();
        if (!v || !b) return;
        using TNS = UltraCanvasPDFView::ThumbnailNumberStyle;
        const bool toOverlay = v->GetThumbnailNumberStyle() == TNS::Caption;
        v->SetThumbnailNumberStyle(toOverlay ? TNS::Overlay : TNS::Caption);
        b->SetText(toOverlay ? "#: On Page" : "#: Under");
    };
    DemoKit::Add(toolbar, numBtn);

    // Toggle the page inventory's width mode: a share of the view's width
    // (the default) <-> a fixed thumbnail width in pixels.
    auto widthBtn = makeToolbarButton("PDFThumbWidth", "Strip: 25%", 110);
    std::weak_ptr<UltraCanvasButton> widthWeak = widthBtn;
    widthBtn->onClick = [viewWeak, widthWeak]() {
        auto v = viewWeak.lock();
        auto b = widthWeak.lock();
        if (!v || !b) return;
        using TWM = UltraCanvasPDFView::ThumbnailWidthMode;
        const bool toAbsolute = v->GetThumbnailWidthMode() == TWM::Relative;
        if (toAbsolute) v->SetThumbnailWidth(56);          // the Filer's width
        else            v->SetThumbnailWidthFraction(0.25f);
        b->SetText(toAbsolute ? "Strip: 56px" : "Strip: 25%");
    };
    DemoKit::Add(toolbar, widthBtn);

    pageLabel->SetAlignment(TextAlignment::Right, VerticalAlignment::Middle);
    DemoKit::Add(toolbar, pageLabel, 1);

    // ----- Viewer, then the status row below it: hint text (left) + zoom level (right) -----
    DemoKit::Add(card, view, 1);

    auto statusRow = DemoKit::AddRow(card, "PDFStatusRow", 8);
    statusRow->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    DemoKit::Add(statusRow, statusLabel, 1);
    DemoKit::Add(statusRow, zoomLabel);

    return root;
}

} // namespace UltraCanvas

#endif // ULTRACANVAS_PLUGIN_PDF
