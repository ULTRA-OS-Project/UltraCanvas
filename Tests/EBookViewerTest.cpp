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
#include "Documents/eBook/TXTEngine.h"

#include <cstdio>
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

int main() {
    // The FB2 case below decodes an embedded PNG; the image subsystem (vips)
    // must be up even without a window.
    UCImage::InitializeImageSubsysterm("EBookViewerTest");

    RegisterBuiltinEBookEngines();

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

    // Appearance changes rebuild without incident.
    viewer->SetTheme(EBookViewerTheme::Night);
    CHECK(viewer->GetTheme() == EBookViewerTheme::Night);
    viewer->IncreaseFontSize();
    CHECK(viewer->GetBaseFontSize() == 20.f);
    viewer->SetBaseFontSize(100.f);   // clamped
    CHECK(viewer->GetBaseFontSize() == 48.f);

    // TOC panel toggle.
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
