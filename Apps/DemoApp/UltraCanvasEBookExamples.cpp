// Apps/DemoApp/UltraCanvasEBookExamples.cpp
// eBook Reader demo: hosts UltraCanvasEBookViewer, opening the bundled MOBI
// sample (media/ebooks/Game-of-rat-and-dragon.mobi) by default, with an
// Open... dialog for real EPUB/TXT/MOBI files and zoom controls (zoom
// in/out, fit width, fit height). Falls back to an in-memory text sample
// when the bundled book is not present at runtime.
// Version: 1.2.0
// Last Modified: 2026-07-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"

#include "UltraCanvasEBookViewer.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath
#include "Documents/eBook/TXTEngine.h"   // RegisterBuiltinEBookEngines

#include <filesystem>
#include <string>
#include <system_error>

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

    auto container = std::make_shared<UltraCanvasContainer>("EBookDemo");
    container->layout.SetFlex(CSSLayout::FlexDirection::Column);

    // ---- header row: open button + status ----
    auto header = std::make_shared<UltraCanvasContainer>("EBookDemoHeader");
    header->size.height = CSSLayout::Dimension::Px(44);
    // The reader's flex base size is the chapter's full content height, so a
    // long chapter puts this column into shrink mode. The header is a fixed
    // chrome row: never let it be crushed (and never let it sprout its own
    // scrollbars over the buttons while layout is in flux).
    header->layoutItem.SetFlexShrink(0);
    {
        ContainerStyle headerStyle = header->GetContainerStyle();
        headerStyle.autoShowScrollbars = false;
        header->SetContainerStyle(headerStyle);
    }
    header->layout.SetFlex(CSSLayout::FlexDirection::Row);
    header->layout.SetFlexGap(8.f);
    header->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
    header->box.padding.left = CSSLayout::Dimension::Px(8);
    header->box.padding.right = CSSLayout::Dimension::Px(8);
    header->SetBackgroundColor(Color(235, 235, 235, 255));

    auto openBtn = std::make_shared<UltraCanvasButton>("EBookDemoOpen", "Open...");
    openBtn->size.width = CSSLayout::Dimension::Px(90);
    openBtn->size.height = CSSLayout::Dimension::Px(30);
    header->AddChild(openBtn);

    auto statusLabel = std::make_shared<UltraCanvasLabel>(
        "EBookDemoStatus", "Opening sample book\xE2\x80\xA6");
    statusLabel->layoutItem.SetFlexGrow(1.f);
    statusLabel->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    statusLabel->SetTextColor(Color(90, 90, 90, 255));
    header->AddChild(statusLabel);

    // ---- zoom controls (pushed to the right by the growing status label) ----
    auto makeZoomButton = [&](const char* name, const char* text, float width) {
        auto button = std::make_shared<UltraCanvasButton>(name, text);
        button->size.width = CSSLayout::Dimension::Px(width);
        button->size.height = CSSLayout::Dimension::Px(30);
        header->AddChild(button);
        return button;
    };

    auto zoomOutBtn = makeZoomButton("EBookDemoZoomOut", "\xE2\x88\x92", 34.f);   // −
    auto zoomLabel = std::make_shared<UltraCanvasLabel>("EBookDemoZoomLevel", "100%");
    zoomLabel->size.width = CSSLayout::Dimension::Px(48);
    zoomLabel->SetAlignment(TextAlignment::Center, VerticalAlignment::Middle);
    zoomLabel->SetTextColor(Color(90, 90, 90, 255));
    header->AddChild(zoomLabel);
    auto zoomInBtn = makeZoomButton("EBookDemoZoomIn", "+", 34.f);
    auto fitWidthBtn = makeZoomButton("EBookDemoFitWidth", "Fit Width", 78.f);
    auto fitHeightBtn = makeZoomButton("EBookDemoFitHeight", "Fit Height", 82.f);

    container->AddChild(header);

    // ---- the reader ----
    auto reader = CreateEBookViewer("EBookDemoReader");
    reader->layoutItem.SetFlexGrow(1.f);

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
    if (std::filesystem::exists(defaultBook, ec)) {
        reader->LoadDocument(defaultBook);
    } else {
        std::string sample = kSampleBook;
        reader->LoadDocumentFromMemory(
            std::vector<uint8_t>(sample.begin(), sample.end()), "sample.txt");
    }

    container->AddChild(reader);

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

    return container;
}

} // namespace UltraCanvas
