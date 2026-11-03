// Apps/DemoApp/UltraCanvasODTExamples.cpp
// Demonstrates word-processing document support (.odt / .docx / legacy .doc):
// an "Open Document" button that parses files through
// UltraCanvasFileLoader::LoadTextDocument into a UCRichDocument, displayed by
// the WYSIWYG UltraCanvasRichTextEdit in read-only mode - so fonts, sizes,
// colours, alignment, list numbering and table layout show as the document
// has them, not as Markdown can spell them. The view is in page view: the
// document's own page size, margins, headers and footers, like Writer's
// print layout. A standard sample document is loaded from
// media/docs/document.odt on entry.
// Version: 1.5.0 - the shared page look (UltraCanvasDemoPageKit): the view
//                  fills the page area and scrolls inside, the button and the
//                  status sized to their text
// Version: 1.4.0 - Open offers what LoadTextDocument reads (AddTextDocumentLoadTypes)
// Version: 1.3.0
// Last Modified: 2026-10-23
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasDemoPageKit.h"
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

            std::string name = PathToUtf8(PathFromUtf8(path).filename());
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
        auto page = DemoKit::CreatePage("ODTExamples", "OpenDocument Text (.odt) / Word (.docx) Documents",
                "Documents parsed by UltraCanvasFileLoader::LoadTextDocument and shown read-only "
                "in the WYSIWYG view, in the document's own page size, margins, headers and footers");
        // The document view scrolls its pages itself: the page is the area's
        // height and the view takes what the title and the toolbar leave.
        page->SetFillsArea(true);

        auto card = DemoKit::CreateCard("odtCard");
        card->SetPadding(4);
        DemoKit::Add(page, card, 1);

        // ===== TOOLBAR: Open button + status label =====
        auto toolbar = DemoKit::AddRow(card, "odtToolbar");
        toolbar->layout.SetFlexAlignItems(CSSLayout::AlignItems::Center);
        toolbar->SetPadding(6);

        // As wide as its text, at least the width it had.
        auto openBtn = std::make_shared<UltraCanvasButton>("odtOpenBtn", "Open Document");
        CSSLayout::BoxConstraints openLimits;
        openLimits.minWidth = CSSLayout::Dimension::Px(180);
        openLimits.minHeight = CSSLayout::Dimension::Px(30);
        openBtn->boxConstraints = openLimits;
        DemoKit::Add(toolbar, openBtn);

        auto status = DemoKit::CreateText("odtStatus", "Supported formats: .odt, .docx, .doc, .tex, .md");
        status->SetTextColor(Color(90, 90, 90, 255));
        DemoKit::Add(toolbar, status, 1);

        // ===== DOCUMENT VIEW =====
        // The parsed UCRichDocument goes to the WYSIWYG element as it is: no
        // Markdown in between, so run fonts, sizes and colours, paragraph
        // alignment, list numbers that run on past an interruption, table
        // column widths and cell alignment all display. Read-only: this page
        // is a viewer (the WYSIWYG Editor page edits). Page view lays the
        // text out in the document's own page and margins, with its headers
        // and footers, each page at 96 DPI on a neutral desk.
        auto view = CreateRichTextEdit("odtView", 0, 0, 0, 0);
        RichTextEditStyle pageStyle = view->GetStyle();
        pageStyle.padding = 0.0f;              // the pages bring their own margins
        pageStyle.deskColor = Color(235, 236, 239, 255);
        view->SetStyle(pageStyle);
        view->SetReadOnly(true);
        view->SetPageView(true);
        view->layoutItem.SetFlexBasis(CSSLayout::Dimension::Px(0));
        DemoKit::Add(card, view, 1);

        // ===== STANDARD SAMPLE DOCUMENT =====
        const std::string samplePath =
            NormalizePath(GetResourcesDir() + "media/docs/document.odt");
        if (!ShowDocumentInView(samplePath, view, status)) {
            view->SetMarkdown("No sample document found.\n\nExpected: " + samplePath
                              + "\n\nUse **Open Document** to load an .odt, .docx or .doc file.");
        }

        // ===== OPEN -> FileLoader::OpenTextDocument-style flow =====
        // Captures shared_ptrs so view/status stay valid for the async
        // callback; both are also owned by the container (no ownership cycle).
        openBtn->onClick = [view, status]() {
            FileDialogOptions opts;
            opts.SetTitle("Open Document");
            UltraCanvasFileLoader::AddTextDocumentLoadTypes(opts);
            opts.AddFilter("All files", "*");

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

        return page;
    }

} // namespace UltraCanvas
