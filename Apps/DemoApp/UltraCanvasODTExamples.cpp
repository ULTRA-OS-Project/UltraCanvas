// Apps/DemoApp/UltraCanvasODTExamples.cpp
// Demonstrates word-processing document support (.odt / .docx / legacy .doc):
// an "Open Document…" button that parses files through
// UltraCanvasFileLoader::LoadTextDocument into a UCRichDocument, displayed by
// the WYSIWYG UltraCanvasRichTextEdit in read-only mode - so fonts, sizes,
// colours, alignment, list numbering and table layout show as the document
// has them, not as Markdown can spell them. The view is in page view: the
// document's own page size, margins, headers and footers, like Writer's
// print layout. A standard sample document is loaded from
// media/docs/document.odt on entry.
// Version: 1.3.0
// Last Modified: 2026-09-25
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath
#include "UltraCanvasRichDocument.h"

#include <filesystem>
#include <memory>
#include <string>

namespace UltraCanvas {

    namespace {

        // Parses a document file and hands the document to the WYSIWYG view,
        // which draws its pictures straight from the document's media.
        bool ShowDocumentInView(const std::string& path,
                                const std::shared_ptr<UltraCanvasRichTextEdit>& view,
                                const std::shared_ptr<UltraCanvasLabel>& status) {
            std::string error;
            std::shared_ptr<UCRichDocument> document =
                UltraCanvasFileLoader::LoadTextDocument(path, error);
            if (!document) {
                status->SetText("Could not open document: " + error);
                status->RequestRedraw();
                return false;
            }

            view->SetDocument(document);
            view->SetModified(false);
            view->RequestRedraw();

            std::string name = std::filesystem::path(path).filename().string();
            std::string details = std::to_string(document->blocks.size()) + " blocks";
            if (!document->media.empty()) {
                details += ", " + std::to_string(document->media.size()) + " images";
            }
            if (!document->metadata.title.empty()) {
                details += ", title: " + document->metadata.title;
            }
            status->SetText("Loaded: " + name + " (" + details + ")");
            status->RequestRedraw();
            return true;
        }

    } // namespace

    std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateODTExamples() {
        // The document view shows the document's pages on a desk, each at
        // 96 DPI: a whole DIN A4 page (794 x 1123 px) plus the desk around
        // it, so a letter is on display at once. Further pages scroll inside
        // the view.
        const int kPageWidth  = 794 + 2 * 24;   // A4 at 96 DPI, and desk at the sides
        const int kPageHeight = 1123 + 2 * 16;  // and above and below
        const int kWidth   = 1020;
        const int kViewTop = 92;
        const int kHeight  = kViewTop + kPageHeight + 16;
        const int kViewLeft = (kWidth - kPageWidth) / 2;

        auto root = std::make_shared<UltraCanvasContainer>("ODTExamples", 0, 0, kWidth, kHeight);
        // Neutral desk color around the white page, document-viewer style.
        root->SetBackgroundColor(Color(235, 236, 239, 255));

        // ===== TITLE =====
        auto title = std::make_shared<UltraCanvasLabel>("odtTitle", 20, 12, 900, 28);
        title->SetText("OpenDocument Text (.odt) / Word (.docx) Documents");
        title->SetFontSize(18);
        title->SetFontWeight(FontWeight::Bold);
        root->AddChild(title);

        // ===== TOOLBAR: Open button + status label =====
        auto openBtn = std::make_shared<UltraCanvasButton>("odtOpenBtn", 20, 50, 180, 30,
                                                           "Open Document…");
        root->AddChild(openBtn);

        auto status = std::make_shared<UltraCanvasLabel>("odtStatus", 210, 56, kWidth - 230, 22);
        status->SetText("Supported formats: .odt, .docx, .doc, .tex, .md");
        status->SetFontSize(12);
        status->SetTextColor(Color(90, 90, 90, 255));
        root->AddChild(status);

        // ===== DOCUMENT VIEW =====
        // The parsed UCRichDocument goes to the WYSIWYG element as it is: no
        // Markdown in between, so run fonts, sizes and colours, paragraph
        // alignment, list numbers that run on past an interruption, table
        // column widths and cell alignment all display. Read-only: this page
        // is a viewer (the WYSIWYG Editor page edits). Page view lays the
        // text out in the document's own page and margins, with its headers
        // and footers.
        auto view = CreateRichTextEdit("odtView", kViewLeft, kViewTop, kPageWidth, kPageHeight);
        RichTextEditStyle pageStyle = view->GetStyle();
        pageStyle.padding = 0.0f;              // the pages bring their own margins
        pageStyle.deskColor = Color(235, 236, 239, 255);
        view->SetStyle(pageStyle);
        view->SetReadOnly(true);
        view->SetPageView(true);
        root->AddChild(view);

        // ===== STANDARD SAMPLE DOCUMENT =====
        const std::string samplePath =
            NormalizePath(GetResourcesDir() + "media/docs/document.odt");
        if (!ShowDocumentInView(samplePath, view, status)) {
            view->SetMarkdown("No sample document found.\n\nExpected: " + samplePath
                              + "\n\nUse **Open Document…** to load an .odt, .docx or .doc file.");
        }

        // ===== OPEN -> FileLoader::OpenTextDocument-style flow =====
        // Captures shared_ptrs so view/status stay valid for the async
        // callback; both are also owned by the container (no ownership cycle).
        openBtn->onClick = [view, status]() {
            FileDialogOptions opts;
            opts.SetTitle("Open Document")
                .AddFilter("Documents",
                           std::vector<std::string>{ "odt", "docx", "doc", "tex", "md" })
                .AddFilter("OpenDocument Text (*.odt)", "odt")
                .AddFilter("Word Document (*.docx)", "docx")
                .AddFilter("Word 97-2003 (*.doc)", "doc")
                .AddFilter("LaTeX (*.tex)", "tex")
                .AddFilter("Markdown (*.md)", "md")
                .AddFilter("All files", "*");

            UltraCanvasFileLoader::OpenFileDialog(opts,
                [view, status](DialogResult result, const std::string& path) {
                    if (result != DialogResult::OK || path.empty()) {
                        status->SetText("Open cancelled.");
                        status->RequestRedraw();
                        return;
                    }
                    ShowDocumentInView(path, view, status);
                });
        };

        return root;
    }

} // namespace UltraCanvas
