// Tests/FilePreviewsTest.cpp
// The preview provider registry (UltraCanvasFilePreviews.h): a provider
// answers for the extensions it lists whatever their case or dot, and for a
// path by its last extension; a later registration under the same name
// replaces it, and of two providers listing one extension the later wins; an
// unavailable provider answers nothing; a provider that answers nothing is
// refused; a throwing one is contained; queries from several threads at once
// while another registers are safe. Then the built-in providers through the
// Filer, which now reaches them only through the registry: a Word document's
// and a PowerPoint deck's text preview, an encrypted one refused. Headless.
//
// Version: 1.0.0
// Last Modified: 2026-11-10
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

    std::printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
