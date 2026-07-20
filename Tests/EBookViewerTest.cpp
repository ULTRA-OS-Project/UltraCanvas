// Tests/EBookViewerTest.cpp
// Headless smoke test for UltraCanvasEBookViewer: loads a TXT book from
// memory through the engine registry, drives chapter navigation, theme and
// font-size changes, TOC toggling, and close — without a window (nothing is
// rendered; this verifies widget wiring and content building only).
// Links the full UltraCanvas library.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "UltraCanvasEBookViewer.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasImageElement.h"
#include "HTMLReader/HTMLElementBuilder.h"
#include "Documents/eBook/TXTEngine.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

// 1x1 transparent PNG (the same one used in the FB2 case below).
static const unsigned char kDotPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
    0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0x63, 0x64, 0xF8, 0xCF, 0x50,
    0x0F, 0x00, 0x03, 0x86, 0x01, 0x80, 0x5A, 0x34, 0x7D, 0x6B, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

// eBook-viewer regression cases for the element builder: svg-wrapped cover
// images, per-range clickable links, #fragment anchors, and content
// containers without own scrollbars.
static void TestChapterContentBuilding() {
    HTML::BuildOptions options;
    std::string activatedHref;
    options.resourceLoader = [](const std::string&) {
        return std::vector<uint8_t>(kDotPng, kDotPng + sizeof(kDotPng));
    };
    options.onLinkActivated = [&](const std::string& href) {
        activatedHref = href;
    };

    // EPUB cover-page pattern: raster image wrapped in an svg viewport
    // (xlink:href), plus a paragraph with two distinct links and anchors.
    HTML::ElementBuilder builder;
    auto result = builder.Build(
        "<html><body>"
        "<div id=\"top\"><svg viewBox=\"0 0 513 751\">"
        "<image width=\"513\" height=\"751\" xlink:href=\"../Images/cover.jpeg\"/>"
        "</svg></div>"
        "<p><a id=\"mark\"></a>Go to <a href=\"a.html\">first</a> and "
        "<a href=\"b.html\">second</a>.</p>"
        "</body></html>",
        options);
    CHECK(result.root != nullptr);
    if (!result.root) return;

    int images = 0;
    int scrollbarContainers = 0;
    UltraCanvasLabel* linkLabel = nullptr;
    std::function<void(UltraCanvasContainer&)> walk = [&](UltraCanvasContainer& c) {
        if (c.GetContainerStyle().autoShowScrollbars) ++scrollbarContainers;
        for (auto& childPtr : c.GetChildren()) {
            if (dynamic_cast<UltraCanvasImageElement*>(childPtr.get())) ++images;
            if (auto* label = dynamic_cast<UltraCanvasLabel*>(childPtr.get())) {
                if (!label->GetTextLinks().empty() && !linkLabel) linkLabel = label;
            }
            if (auto* sub = dynamic_cast<UltraCanvasContainer*>(childPtr.get())) {
                walk(*sub);
            }
        }
    };
    walk(*result.root);

    CHECK(images == 1);                 // svg <image xlink:href> became an image
    CHECK(scrollbarContainers == 0);    // scrolling stays with the host view

    // Both links carry byte ranges into the rendered text
    // ("Go to first and second.") and activate independently.
    CHECK(linkLabel != nullptr);
    if (linkLabel) {
        const auto& links = linkLabel->GetTextLinks();
        CHECK(links.size() == 2);
        if (links.size() == 2) {
            CHECK(links[0].href == std::string("a.html"));
            CHECK(links[0].startByte == 6 && links[0].endByte == 11);   // "first"
            CHECK(links[1].href == std::string("b.html"));
            CHECK(links[1].startByte == 16 && links[1].endByte == 22);  // "second"
        }
        CHECK(bool(linkLabel->onLinkActivated));
        if (linkLabel->onLinkActivated && links.size() == 2) {
            linkLabel->onLinkActivated(links[1].href);
            CHECK(activatedHref == std::string("b.html"));
        }
    }

    // #fragment anchors: block-level ids and empty inline <a id> targets are
    // both registered and point at elements inside the built tree.
    CHECK(result.anchors.count("top") == 1);
    CHECK(result.anchors.count("mark") == 1);
    CHECK(result.anchors.count("missing") == 0);
    if (result.anchors.count("top")) CHECK(result.anchors.at("top") != nullptr);
    if (result.anchors.count("mark")) CHECK(result.anchors.at("mark") != nullptr);
}

int main() {
    // The FB2 case below decodes an embedded PNG; the image subsystem (vips)
    // must be up even without a window.
    UCImage::InitializeImageSubsysterm("EBookViewerTest");

    RegisterBuiltinEBookEngines();

    TestChapterContentBuilding();

    auto viewer = CreateEBookViewer("reader", 800, 600);
    CHECK(viewer != nullptr);
    CHECK(!viewer->IsDocumentLoaded());
    CHECK(viewer->GetChapterCount() == 0);

    // Unsupported extension surfaces an error and stays unloaded.
    std::string errorSeen;
    viewer->onError = [&](const std::string& e) { errorSeen = e; };
    std::vector<uint8_t> junk = {'x'};
    CHECK(!viewer->LoadDocumentFromMemory(junk, "book.xyz"));
    CHECK(!errorSeen.empty());
    CHECK(!viewer->IsDocumentLoaded());

    // A three-part TXT book.
    std::string text = "Part one text.\n\fPart two text.\n\fPart three text.\n";
    std::vector<uint8_t> data(text.begin(), text.end());

    int loadedCalls = 0;
    int chapterChanges = 0;
    std::string lastTitle;
    viewer->onDocumentLoaded = [&](const EBookMetadata&) { ++loadedCalls; };
    viewer->onChapterChanged = [&](int, const std::string& title) {
        ++chapterChanges;
        lastTitle = title;
    };

    CHECK(viewer->LoadDocumentFromMemory(data, "book.txt"));
    CHECK(viewer->IsDocumentLoaded());
    CHECK(viewer->GetChapterCount() == 3);
    CHECK(viewer->GetCurrentChapter() == 0);
    CHECK(loadedCalls == 1);
    CHECK(chapterChanges == 1);

    // Navigation with clamping.
    viewer->NextChapter();
    CHECK(viewer->GetCurrentChapter() == 1);
    CHECK(lastTitle == std::string("Part 2"));
    viewer->GoToChapter(99);
    CHECK(viewer->GetCurrentChapter() == 2);
    viewer->PreviousChapter();
    viewer->PreviousChapter();
    viewer->PreviousChapter();   // clamped at 0
    CHECK(viewer->GetCurrentChapter() == 0);
    CHECK(chapterChanges == 5);

    // Appearance changes rebuild without incident. The initial font size
    // follows the platform UI font (or the 12px fallback when no application
    // instance exists, as in this headless test).
    viewer->SetTheme(EBookViewerTheme::Night);
    CHECK(viewer->GetTheme() == EBookViewerTheme::Night);
    const float initialFontSize = viewer->GetBaseFontSize();
    CHECK(initialFontSize > 0.f);
    viewer->IncreaseFontSize();
    CHECK(viewer->GetBaseFontSize() == initialFontSize + 2.f);
    viewer->SetBaseFontSize(100.f);   // clamped
    CHECK(viewer->GetBaseFontSize() == 48.f);

    // TOC panel is shown by default; toggling hides and re-shows it.
    CHECK(viewer->IsTableOfContentsVisible());
    viewer->ToggleTableOfContents();
    CHECK(!viewer->IsTableOfContentsVisible());
    viewer->ToggleTableOfContents();
    CHECK(viewer->IsTableOfContentsVisible());

    // Search through the viewer.
    auto results = viewer->Search("part");
    CHECK(results.size() == 3);

    // Reload replaces the document.
    std::string text2 = "Single chapter.";
    std::vector<uint8_t> data2(text2.begin(), text2.end());
    CHECK(viewer->LoadDocumentFromMemory(data2, "other.txt"));
    CHECK(viewer->GetChapterCount() == 1);
    CHECK(viewer->GetCurrentChapter() == 0);

    viewer->CloseDocument();
    CHECK(!viewer->IsDocumentLoaded());
    CHECK(viewer->GetChapterCount() == 0);

    // FB2 through the registry: chapters, embedded image resource, metadata.
    std::string fb2 =
        "<?xml version=\"1.0\"?>\n"
        "<FictionBook xmlns:l=\"http://www.w3.org/1999/xlink\">\n"
        " <description><title-info>\n"
        "  <author><first-name>Ivan</first-name><last-name>Petrov</last-name></author>\n"
        "  <book-title>FB2 Viewer Book</book-title>\n"
        " </title-info></description>\n"
        " <body>\n"
        "  <section><title><p>One</p></title>\n"
        "   <p>With <emphasis>markup</emphasis> and an image:</p>\n"
        "   <image l:href=\"#dot.png\"/>\n"
        "  </section>\n"
        "  <section><title><p>Two</p></title><p>Second chapter.</p></section>\n"
        " </body>\n"
        // 1x1 transparent PNG
        " <binary id=\"dot.png\" content-type=\"image/png\">"
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwAD"
        "hgGAWjR9awAAAABJRU5ErkJggg==</binary>\n"
        "</FictionBook>\n";
    std::vector<uint8_t> fb2Data(fb2.begin(), fb2.end());

    EBookMetadata fb2Meta;
    viewer->onDocumentLoaded = [&](const EBookMetadata& m) { fb2Meta = m; };
    CHECK(viewer->LoadDocumentFromMemory(fb2Data, "book.fb2"));
    CHECK(viewer->IsDocumentLoaded());
    CHECK(viewer->GetChapterCount() == 2);
    CHECK(fb2Meta.title == std::string("FB2 Viewer Book"));
    CHECK(fb2Meta.GetPrimaryAuthor() == std::string("Ivan Petrov"));
    CHECK(viewer->GetEngine() != nullptr);
    if (viewer->GetEngine()) {
        CHECK(viewer->GetEngine()->GetFormat() == EBookFormat::FB2);
        CHECK(viewer->GetEngine()->GetResource("#dot.png").size() == 70);
    }
    viewer->NextChapter();
    CHECK(viewer->GetCurrentChapter() == 1);
    viewer->CloseDocument();

    std::printf("%s: %d checks, %d failures\n",
                failures == 0 ? "PASS" : "FAIL", checks, failures);
    return failures == 0 ? 0 : 1;
}
