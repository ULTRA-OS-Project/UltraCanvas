// Apps/DemoApp/UltraCanvasEBookExamples.cpp
// eBook Reader demo: hosts UltraCanvasEBookViewer, opening the bundled MOBI
// sample (media/ebooks/Game-of-rat-and-dragon.mobi) by default, with an
// Open... dialog for real EPUB/TXT/MOBI files and zoom controls (zoom
// in/out, fit width, fit height). Falls back to an in-memory text sample
// when the bundled book is not present at runtime.
// Version: 1.3.0 - the shared page look (UltraCanvasDemoPageKit): title and
//                  description above the reader, which fills the rest of the
//                  area in a card; the controls wrap and the status sizes to its text
// Version: 1.2.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"

#include "UltraCanvasEBookViewer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath
#include "Documents/eBook/TXTEngine.h"   // RegisterBuiltinEBookEngines

#include <filesystem>
#include <string>
#include <system_error>
#include "UltraCanvasPathUtf8.h"

namespace UltraCanvas {

namespace {

// A small multi-chapter sample so the demo shows a working reader without
// needing a file on disk. Form feeds split TXT chapters.
const char* kSampleBook =
    "UltraCanvas eBook Reader\n"
    "\n"
    "This sample book is generated in memory by the demo. Use the toolbar to "
    "change chapters, cycle the reading theme (Normal, Sepia, Night), adjust "
    "the font size, or show the table of contents.\n"
    "\n"
    "Open a real book with the \"Open...\" button above — EPUB 2 and EPUB 3 "
    "files are fully supported: metadata, spine, nested table of contents, "
    "stylesheets and images.\n"
    "\f"
    "How it works\n"
    "\n"
    "Chapters come from a format engine (IEBookEngine). The chapter XHTML is "
    "parsed by the HTMLReader module, styled by a CSS cascade, and converted "
    "into native UltraCanvas elements - every paragraph you are reading is an "
    "UltraCanvasLabel laid out by the CSSLayout engine.\n"
    "\n"
    "There is no page-image rendering: text stays crisp at any font size, and "
    "themes recolor instantly.\n"
    "\f"
    "Keyboard shortcuts\n"
    "\n"
    "Left / PageUp: previous chapter. Right / PageDown: next chapter. Home / "
    "End: first / last chapter.\n";

} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateEBookExamples() {
    RegisterBuiltinEBookEngines();

    auto page = DemoKit::CreatePage("EBookDemo", "eBook Reader",
            "EPUB/FB2/MOBI/TXT reading with chapters, TOC, themes and font scaling "
            "rendered natively through the CSSLayout engine");
    // The reader scrolls itself: the page is the area's height and the
    // reader's card takes what the title leaves.
    page->SetFillsArea(true);

    auto card = DemoKit::CreateCard("EBookDemoCard");
    card->SetPadding(4);
    DemoKit::Add(page, card, 1);

    // ---- header row: open button + status ----
    // A row that wraps: a long status (or a longer translation) moves the
    // zoom controls onto a second line instead of being cut. Added with no
    // grow, so it is never crushed by the reader below - whose flex base
    // size is the chapter's full content height.
    auto header = DemoKit::AddRow(card, "EBookDemoHeader", 8);
    header->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header->SetPadding(4, 8);
    header->SetBackgroundColor(Color(235, 235, 235, 255));

    auto openBtn = std::make_shared<UltraCanvasButton>("EBookDemoOpen", "Open");
    openBtn->size.width = CSSLayout::Dimension::Px(90);
    openBtn->size.height = CSSLayout::Dimension::Px(30);
    DemoKit::Add(header, openBtn);

    auto statusLabel = DemoKit::CreateText("EBookDemoStatus", "Opening sample book\xE2\x80\xA6");
    statusLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    statusLabel->SetTextColor(Color(90, 90, 90, 255));
    DemoKit::Add(header, statusLabel, 1);

    // ---- zoom controls (pushed to the right by the growing status label) ----
    auto makeZoomButton = [&](const char* name, const char* text, float width) {
        auto button = std::make_shared<UltraCanvasButton>(name, text);
        button->size.width = CSSLayout::Dimension::Px(width);
        button->size.height = CSSLayout::Dimension::Px(30);
        DemoKit::Add(header, button);
        return button;
    };

    auto zoomOutBtn = makeZoomButton("EBookDemoZoomOut", "\xE2\x88\x92", 34.f);   // −
    auto zoomLabel = DemoKit::CreateText("EBookDemoZoomLevel", "100%");
    zoomLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
    zoomLabel->SetTextColor(Color(90, 90, 90, 255));
    DemoKit::Add(header, zoomLabel);
    auto zoomInBtn = makeZoomButton("EBookDemoZoomIn", "+", 34.f);
    auto fitWidthBtn = makeZoomButton("EBookDemoFitWidth", "Fit Width", 78.f);
    auto fitHeightBtn = makeZoomButton("EBookDemoFitHeight", "Fit Height", 82.f);

    // ---- the reader ----
    auto reader = CreateEBookViewer("EBookDemoReader");

    reader->onDocumentLoaded = [statusLabel, readerPtr = reader.get()](
                                   const EBookMetadata& meta) {
        std::string text = meta.title.empty() ? "(untitled)" : meta.title;
        if (!meta.authors.empty()) text += " — " + meta.GetAuthorsString();
        text += "  [" + readerPtr->GetEngine()->GetFormatName() + "]";
        statusLabel->SetText(text);
    };
    reader->onError = [statusLabel](const std::string& error) {
        statusLabel->SetText("Error: " + error);
    };
    reader->onZoomChanged = [zoomLabel](float percent) {
        zoomLabel->SetText(std::to_string(static_cast<int>(percent + 0.5f)) + "%");
    };

    // ---- zoom wiring ----
    auto zoomReaderWeak = std::weak_ptr<UltraCanvasEBookViewer>(reader);
    zoomOutBtn->SetOnClick([zoomReaderWeak]() {
        if (auto viewer = zoomReaderWeak.lock()) viewer->ZoomOut();
    });
    zoomInBtn->SetOnClick([zoomReaderWeak]() {
        if (auto viewer = zoomReaderWeak.lock()) viewer->ZoomIn();
    });
    fitWidthBtn->SetOnClick([zoomReaderWeak]() {
        if (auto viewer = zoomReaderWeak.lock()) viewer->ZoomToWidth();
    });
    fitHeightBtn->SetOnClick([zoomReaderWeak]() {
        if (auto viewer = zoomReaderWeak.lock()) viewer->ZoomToHeight();
    });

    // Default document: the bundled MOBI sample (a real Kindle KF8 book that
    // exercises the MOBI engine, drop caps and the table of contents). If it
    // is not present at runtime, fall back to the in-memory text sample.
    const std::string defaultBook =
        NormalizePath(GetResourcesDir() + "media/ebooks/Game-of-rat-and-dragon.mobi");
    std::error_code ec;
    if (std::filesystem::exists(UltraCanvas::PathFromUtf8(defaultBook), ec)) {
        reader->LoadDocument(defaultBook);
    } else {
        std::string sample = kSampleBook;
        reader->LoadDocumentFromMemory(
            std::vector<uint8_t>(sample.begin(), sample.end()), "sample.txt");
    }

    // A zero flex basis: the reader's own base size is the chapter's full
    // content height, which would make the card (and the page) that tall.
    // From zero it takes exactly the height the header leaves.
    DemoKit::Add(card, reader, 1);
    reader->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0)).SetFlexShrink(1);

    // ---- open dialog ----
    auto readerWeak = std::weak_ptr<UltraCanvasEBookViewer>(reader);
    openBtn->SetOnClick([readerWeak]() {
        auto viewer = readerWeak.lock();
        if (!viewer) return;

        FileDialogOptions opts;
        opts.title = "Open eBook";
        opts.parentWindow = viewer->GetWindow();
        opts.AddFilter("eBooks", GetRegisteredEBookExtensions());
        UltraCanvasFileLoader::OpenFileDialog(opts,
            [readerWeak](DialogResult result, const std::string& path) {
                if (result != DialogResult::OK || path.empty()) return;
                if (auto viewer = readerWeak.lock()) {
                    viewer->LoadDocument(path);
                }
            });
    });

    return page;
}

} // namespace UltraCanvas
