// Tests/FilePreviewsTest.cpp
// The preview provider registry (UltraCanvasFilePreviews.h): a provider
// answers for the extensions it lists whatever their case or dot, and for a
// path by its last extension; a later registration under the same name
// replaces it, and of two providers listing one extension the later wins; an
// unavailable provider answers nothing; a provider that answers nothing is
// refused; a throwing one is contained; queries from several threads at once
// while another registers are safe. Then the built-in providers through the
// Filer, which now reaches them only through the registry: a Word document's
// and a PowerPoint deck's text preview, an encrypted one refused; a
// word-processing document's first page as its thumbnail. Headless.
//
// Version: 1.1.1 - "the text is drawn": grey pixels darker than mid-grey, not darker than a fixed
//                  dark grey - at 96 px a thin face (Liberation, unhinted) drew 10 such pixels
//                  where DejaVu drew 36, and the check failed on the font, not the page
// Version: 1.1.0 - a word-processing document's first page (odt, docx, doc)
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "UltraCanvasFilePreviews.h"
#include "UltraCanvasFilerWidget.h"
#include "UltraCanvasRichDocument.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace UltraCanvas;

#ifndef FILEPREVIEWS_FIXTURE_DIR
#error "FILEPREVIEWS_FIXTURE_DIR must name Tests/fixtures"
#endif

static int failures = 0;
static void Check(bool condition, const std::string& what) {
    std::printf("  %s %s\n", condition ? "ok  " : "FAIL", what.c_str());
    if (!condition) ++failures;
}

static bool Contains(const std::vector<std::string>& list, const std::string& item) {
    return std::find(list.begin(), list.end(), item) != list.end();
}

static bool AnyLineContains(const std::vector<std::string>& lines, const std::string& text) {
    for (const auto& line : lines)
        if (line.find(text) != std::string::npos) return true;
    return false;
}

// A pixmap of the requested logical size, so a caller can tell which provider
// drew it.
static std::shared_ptr<UCPixmap> Sized(int w, int h) {
    auto pm = std::make_shared<UCPixmap>();
    return pm->Init(w, h) ? pm : nullptr;
}

int main() {
    std::printf("FilePreviewsTest\n");
    const std::string fixtures = FILEPREVIEWS_FIXTURE_DIR;

    std::printf("Registry\n");
    {
        FilePreviewProvider p;
        p.name = "test-a";
        p.thumbnailExtensions = {".TstA", "tsta2"};
        p.RenderThumbnail = [](const std::string&, const FileThumbnailRequest& r) {
            return Sized(r.width, r.height);
        };
        RegisterFilePreviewProvider(std::move(p));
    }
    Check(CanRenderFileThumbnail("tsta") && CanRenderFileThumbnail(".TSTA") && CanRenderFileThumbnail("tsta2"),
          "an extension is matched whatever its case or dot");
    Check(CanRenderFileThumbnail("/some/dir/Picture.TstA"), "a path is matched by its last extension");
    Check(!CanRenderFileThumbnail("/some/dir.tsta/noextension"), "a path whose name has no extension matches nothing");
    Check(!CanReadPreviewDocument("tsta"), "a thumbnail provider reads no documents");
    FileThumbnailRequest request;
    request.width = 40;
    request.height = 30;
    auto pm = RenderFileThumbnailFromProvider("x.tsta", request);
    Check(pm && pm->GetWidth() == 40, "the provider draws at the requested size");
    request.width = 0;
    Check(!RenderFileThumbnailFromProvider("x.tsta", request), "a request of no size draws nothing");
    request.width = 40;
    Check(Contains(FileThumbnailExtensions(), "tsta"), "its extensions are listed");

    {
        FilePreviewProvider p;   // a second provider for the same extension
        p.name = "test-b";
        p.thumbnailExtensions = {"tsta"};
        p.RenderThumbnail = [](const std::string&, const FileThumbnailRequest&) { return Sized(7, 7); };
        RegisterFilePreviewProvider(std::move(p));
    }
    pm = RenderFileThumbnailFromProvider("x.tsta", request);
    Check(pm && pm->GetWidth() == 7, "of two providers for one extension, the later answers");
    pm = RenderFileThumbnailFromProvider("x.tsta2", request);
    Check(pm && pm->GetWidth() == 40, "the earlier still answers the extensions only it lists");
    pm = RenderFileThumbnailAs("x.anything", "TSTA2", request);
    Check(pm && pm->GetWidth() == 40, "RenderFileThumbnailAs asks the provider of the extension named");

    {
        FilePreviewProvider p;   // the same name: replaces test-b, now unavailable
        p.name = "test-b";
        p.thumbnailExtensions = {"tsta"};
        p.Available = [] { return false; };
        p.RenderThumbnail = [](const std::string&, const FileThumbnailRequest&) { return Sized(9, 9); };
        RegisterFilePreviewProvider(std::move(p));
    }
    pm = RenderFileThumbnailFromProvider("x.tsta", request);
    Check(pm && pm->GetWidth() == 40, "a provider not available now is passed over for the next");

    {
        FilePreviewProvider p;
        p.name = "test-empty";
        p.thumbnailExtensions = {"tste"};   // no RenderThumbnail
        RegisterFilePreviewProvider(std::move(p));
    }
    Check(!CanRenderFileThumbnail("tste"), "a provider that answers nothing is refused");

    {
        FilePreviewProvider p;
        p.name = "test-two-dots";
        p.documentExtensions = {"FB2.ZIP"};
        p.ReadDocument = [](const std::string&, UCRichDocument&) { return true; };
        RegisterFilePreviewProvider(std::move(p));
        FilePreviewProvider t;
        t.name = "test-throws";
        t.thumbnailExtensions = {"tstt"};
        t.RenderThumbnail = [](const std::string&, const FileThumbnailRequest&) -> std::shared_ptr<UCPixmap> {
            throw 42;
        };
        RegisterFilePreviewProvider(std::move(t));
    }
    Check(Contains(PreviewDocumentExtensions(), "fb2.zip"), "a two-part extension keeps both parts");
    Check(!RenderFileThumbnailFromProvider("x.tstt", request), "a provider that throws draws nothing, and the caller goes on");

    std::printf("Threads\n");
    {
        std::atomic<int> answered{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&] {
                FileThumbnailRequest r;
                r.width = r.height = 4;
                for (int i = 0; i < 200; ++i)
                    if (RenderFileThumbnailFromProvider("x.tsta2", r)) ++answered;
            });
        }
        threads.emplace_back([] {
            for (int i = 0; i < 100; ++i) {
                FilePreviewProvider p;
                p.name = "test-churn";
                p.thumbnailExtensions = {"tstc" + std::to_string(i % 3)};
                p.RenderThumbnail = [](const std::string&, const FileThumbnailRequest&) { return Sized(1, 1); };
                RegisterFilePreviewProvider(std::move(p));
            }
        });
        for (auto& th : threads) th.join();
        Check(answered == 8 * 200, "8 threads asking while another registers all get their answer");
    }

    std::printf("Built-in providers\n");
    const std::vector<std::string> docs = PreviewDocumentExtensions();
    Check(Contains(docs, "docx") && Contains(docs, "odt") && Contains(docs, "doc") && Contains(docs, "tex"),
          "Word-processing documents have a preview document");
    Check(Contains(docs, "pptx") && Contains(docs, "ppsx") && Contains(docs, "potm"),
          "PowerPoint decks have a preview document");
    const std::vector<std::string> thumbs = FileThumbnailExtensions();
    Check(Contains(thumbs, "epub") && Contains(thumbs, "mobi") && Contains(thumbs, "azw3"),
          "e-books have a cover thumbnail");

    std::printf("Through the Filer\n");
    {
        std::vector<std::string> lines;
        bool tabular = true;
        Check(UltraCanvasFilerWidget::TextPreviewLines(fixtures + "/word97-formatting.docx", lines, tabular) &&
                      !tabular && !lines.empty() && lines.front() == "First page header" &&
                      AnyLineContains(lines, "Fixture Heading"),
              "a .docx previews as its text, the first page's header first");
        lines.clear();
        Check(UltraCanvasFilerWidget::TextPreviewLines(fixtures + "/office-pptx-sample.pptx", lines, tabular) &&
                      !lines.empty(),
              "a .pptx previews as its slide outline");
        Check(lines.size() >= 3 && lines[0] == "1. Quarterly Review" && lines[2] == "2. Agenda",
              "the outline numbers each slide before its title");
        lines.clear();
        Check(!UltraCanvasFilerWidget::TextPreviewLines(fixtures + "/office-agile-encrypted.docx", lines, tabular),
              "an encrypted .docx has no preview (it keeps its type glyph)");
        lines.clear();
        Check(!UltraCanvasFilerWidget::TextPreviewLines(fixtures + "/office-agile-encrypted.pptx", lines, tabular),
              "an encrypted .pptx has no preview");
    }

    std::printf("A PDF's first page\n");
    if (CanRenderFileThumbnail("pdf")) {
        // The page paints a blue square and leaves its paper alone, as
        // nearly every PDF does: the engine hands that paper back
        // transparent, and on a dark tile the thumbnail was a dark page.
        FileThumbnailRequest request;
        request.width = 96;
        request.height = 96;
        auto page = RenderFileThumbnailFromProvider(fixtures + "/pdf-blue-square.pdf", request);
        Check(page != nullptr, "a PDF's first page as its thumbnail");
        if (page) {
            const int w = page->GetWidth(), h = page->GetHeight();
            int seeThrough = 0;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if ((page->GetPixel(x, y) >> 24) != 255) ++seeThrough;
            Check(seeThrough == 0, "the page is opaque: " + std::to_string(seeThrough) + " pixels see through");
            Check((page->GetPixel(2, 2) & 0xFFFFFF) == 0xFFFFFF, "the paper the page leaves alone is white");
            const uint32_t middle = page->GetPixel(w / 2, h / 2);
            const int r = (middle >> 16) & 0xFF, b = middle & 0xFF;
            Check(b > 180 && r < 90, "what the page paints keeps its colour (the blue square)");
        }
    } else {
        std::printf("  (no PDF engine in this build: the page checks skipped)\n");
    }

    std::printf("A word-processing document's first page\n");
    {
        Check(Contains(thumbs, "docx") && Contains(thumbs, "odt") && Contains(thumbs, "doc"),
              "odt, docx and doc have a first-page thumbnail");
        Check(!Contains(thumbs, "tex"), "a .tex keeps its outline (no page thumbnail)");
        FileThumbnailRequest request;
        request.width = 96;
        request.height = 96;
        for (const char* name : {"word97-formatting.docx", "word97-formatting.odt", "word97-formatting.doc"}) {
            const std::string what = name;
            auto page = RenderFileThumbnailFromProvider(fixtures + "/" + name, request);
            Check(page != nullptr, what + ": its first page as the thumbnail");
            if (!page) continue;
            const int w = page->GetWidth(), h = page->GetHeight();
            Check(h == 96 && w < h && w > 48,
                  what + ": a portrait page fitted to the box (" + std::to_string(w) + " x " +
                          std::to_string(h) + ")");
            // Ink: neutral grey darker than mid-grey. At this size a line of
            // text is antialiased grey, and how dark depends on the face
            // and hinting fontconfig picks - the fixture's Liberation Serif
            // drawn unhinted gives 80-110 such pixels, DejaVu (where
            // Liberation is not installed) 225-290. The red box and the
            // yellow highlight on the page are no text, so colours do not count.
            int seeThrough = 0, ink = 0;
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const uint32_t p = page->GetPixel(x, y);
                    if ((p >> 24) != 255) ++seeThrough;
                    const int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
                    const int spread = std::max({r, g, b}) - std::min({r, g, b});
                    if (r + g + b < 3 * 200 && spread < 40) ++ink;
                }
            }
            Check(seeThrough == 0, what + ": the page is opaque paper");
            Check((page->GetPixel(1, 1) & 0xFFFFFF) == 0xFFFFFF, what + ": the margin is white paper");
            Check(ink > 40, what + ": the text is drawn on it (" + std::to_string(ink) + " grey pixels)");
        }
        Check(!RenderFileThumbnailFromProvider(fixtures + "/office-agile-encrypted.docx", request),
              "an encrypted .docx has no page (it keeps its type glyph)");

        // Through the Filer, as UltraFiler's tiles and the desktop boards ask.
        auto tile = RenderFileThumbnail(fixtures + "/word97-formatting.docx", 120, 120);
        Check(tile != nullptr && tile->GetHeight() == 120 && tile->GetWidth() < 120,
              "RenderFileThumbnail gives a .docx its page, not a square page of text");
    }

    std::printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
