// Apps/DemoApp/UltraCanvasODTExamples.cpp
// Demonstrates word-processing document support (.odt / .docx / legacy .doc):
// an "Open Document…" button that parses files through
// UltraCanvasFileLoader::LoadTextDocument into a UCRichDocument, displayed as
// rendered Markdown in a read-only TextArea. A standard sample document is
// loaded from media/docs/document.odt on entry.
// Version: 1.1.0
// Last Modified: 2026-07-07
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasTextArea.h"
#include "UltraCanvasFileLoader.h"
#include "UltraCanvasConfig.h"   // GetResourcesDir
#include "UltraCanvasUtils.h"    // NormalizePath
#include "Plugins/Documents/Word/UltraCanvasRichDocument.h"

#include <filesystem>
#include <memory>
#include <string>

namespace UltraCanvas {

    namespace {

        // Parses a document file and shows it in the markdown view. Embedded
        // images are extracted next to the system temp dir so the markdown
        // renderer can resolve them; each load gets a fresh subdirectory so
        // consecutive opens never mix pictures.
        bool ShowDocumentInView(const std::string& path,
                                const std::shared_ptr<UltraCanvasTextArea>& view,
                                const std::shared_ptr<UltraCanvasLabel>& status) {
            std::string error;
            std::shared_ptr<UCRichDocument> document =
                UltraCanvasFileLoader::LoadTextDocument(path, error);
            if (!document) {
                status->SetText("Could not open document: " + error);
                status->RequestRedraw();
                return false;
            }

            RichDocumentMarkdownOptions options;
            if (!document->media.empty()) {
                static int loadCounter = 0;
                auto mediaDir = std::filesystem::temp_directory_path()
                    / ("UltraCanvasODTDemo-" + std::to_string(++loadCounter));
                options.imageDirectory = mediaDir.string();
            }

            view->SetDocumentFilePath(path);
            view->SetText(document->ToMarkdown(options), false);
            view->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
            view->SetCursorPosition(LineColumnIndex::INVALID);
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
        // The document view holds a full DIN A4 page (210 x 297 mm) at the
        // conventional 96 DPI screen resolution, so an entire letter-style
        // document is on display at once. The page is taller than the demo
        // window; the surrounding display area scrolls.
        const int kPageWidth  = 794;    // 210 mm at 96 DPI
        const int kPageHeight = 1123;   // 297 mm at 96 DPI
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
        status->SetText("Supported formats: .odt, .docx, .doc (text only), .md");
        status->SetFontSize(12);
        status->SetTextColor(Color(90, 90, 90, 255));
        root->AddChild(status);

        // ===== DOCUMENT VIEW =====
        // The parsed UCRichDocument is serialized to Markdown and rendered by
        // the TextArea's MarkdownHybrid mode: headings, bold/italic, lists,
        // tables, images and $math$ all display without a separate viewer.
        // Sized and centered as one full DIN A4 page.
        auto view = std::make_shared<UltraCanvasTextArea>("odtView", kViewLeft, kViewTop,
                                                          kPageWidth, kPageHeight);
        view->SetEditingMode(TextAreaEditingMode::MarkdownHybrid);
        view->SetReadOnly(true);
        view->SetWordWrap(true);
        view->SetBackgroundColor(Colors::White);
        // Page margins approximating a printed letter (~10 mm at 96 DPI).
        view->SetPadding(38);
        root->AddChild(view);

        // ===== STANDARD SAMPLE DOCUMENT =====
        const std::string samplePath =
            NormalizePath(GetResourcesDir() + "media/docs/document.odt");
        if (!ShowDocumentInView(samplePath, view, status)) {
            view->SetText("No sample document found.\n\nExpected: " + samplePath
                          + "\n\nUse **Open Document…** to load an .odt or .docx file.",
                          false);
        }

        // ===== OPEN -> FileLoader::OpenTextDocument-style flow =====
        // Captures shared_ptrs so view/status stay valid for the async
        // callback; both are also owned by the container (no ownership cycle).
        openBtn->onClick = [view, status]() {
            FileDialogOptions opts;
            opts.SetTitle("Open Document")
                .AddFilter("Documents",
                           std::vector<std::string>{ "odt", "docx", "doc", "md" })
                .AddFilter("OpenDocument Text (*.odt)", "odt")
                .AddFilter("Word Document (*.docx)", "docx")
                .AddFilter("Word 97-2003 (*.doc)", "doc")
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
