// Tests/DocumentRenderProbe.cpp
// A developer's probe, not a test: loads a word-processing document (.odt,
// .docx, .doc, ...) through UCWordDocumentIO, lays it out in an
// UltraCanvasRichTextEdit in page view and writes what it draws as a PDF, to
// be put beside the PDF the document's own program exports.
//
//   xvfb-run -a ./DocumentRenderProbe letter.odt letter.pdf
//   xvfb-run -a ./DocumentRenderProbe letter.odt letter.pdf letter.docx   (and write it as .docx)
//   pdftoppm -r 60 -png letter.pdf letter
//
// Needs a display, as the element does. Exit code 0 when the PDF was written.
// Version: 1.1.0 - DOCUMENT_PROBE_VERBOSE lists each table cell's lines
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasRichTextEdit.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: DocumentRenderProbe <document> <out.pdf> [<save-as.odt|.docx>]" << std::endl;
        return 2;
    }
    UltraCanvasApplication app;
    if (!app.Initialize("DocumentRenderProbe")) {
        std::cerr << "the application would not initialise (no display?)" << std::endl;
        return 2;
    }
    WindowConfig cfg;
    cfg.title = "DocumentRenderProbe";
    cfg.width = 1000;
    cfg.height = 800;
    auto window = CreateWindow(cfg);
    if (!window) {
        std::cerr << "no window" << std::endl;
        return 2;
    }
    window->Show();

    auto document = std::make_shared<UCRichDocument>();
    std::string error;
    if (!UCWordDocumentIO::Load(argv[1], *document, error)) {
        std::cerr << "load failed: " << error << std::endl;
        return 1;
    }
    // A third argument: the document as read, written again in that file's
    // format - to load in turn and see what a conversion keeps.
    if (argc > 3) {
        if (!UCWordDocumentIO::Save(argv[3], *document, error)) {
            std::cerr << "save failed: " << error << std::endl;
            return 1;
        }
    }
    std::cerr << document->blocks.size() << " block(s), " << document->frames.size() << " frame(s), "
              << document->media.size() << " picture(s)" << std::endl;

    // What the reader made of it: the page, each floating object's place and
    // each table's grid - the things a layout difference usually comes from.
    std::cerr << "page " << document->page.widthPt << " x " << document->page.heightPt << " pt, margins "
              << document->page.marginLeftPt << " " << document->page.marginTopPt << " "
              << document->page.marginRightPt << " " << document->page.marginBottomPt << std::endl;
    // DOCUMENT_PROBE_VERBOSE=1: every block's font and spacing too.
    const bool verbose = std::getenv("DOCUMENT_PROBE_VERBOSE") != nullptr;
    auto dumpRuns = [&](const std::vector<RichTextRun>& runs, const std::string& where) {
        for (const RichTextRun& run : runs) {
            if (!run.IsFloatingObject()) continue;
            std::cerr << "  float in " << where << (run.IsTextFrame() ? " frame " : " picture ")
                      << run.imageWidthPt << " x " << run.imageHeightPt << " pt, wrap " << static_cast<int>(run.imageWrap)
                      << ", align " << static_cast<int>(run.imageFloatAlign) << ", from h "
                      << static_cast<int>(run.floatHorizontalFrom) << " v " << static_cast<int>(run.floatVerticalFrom)
                      << ", offset " << run.imageOffsetXPt << " " << run.imageOffsetYPt << std::endl;
        }
    };
    std::cerr << "header " << document->pageFurniture.header.size() << " block(s), footer "
              << document->pageFurniture.footer.size() << "; " << document->sectionFurniture.size()
              << " section(s) with their own" << std::endl;
    for (size_t i = 0; i < document->blocks.size(); ++i) {
        const RichDocBlock& block = document->blocks[i];
        if (block.sectionStart) {
            std::cerr << "  section at block " << i << (block.section.newPage ? " (new page)" : "") << ": furniture "
                      << block.section.furniture << ", margins top " << block.section.marginTopPt << " bottom "
                      << block.section.marginBottomPt << std::endl;
        }
        dumpRuns(block.runs, "block " + std::to_string(i));
        if (verbose && block.type != RichBlockType::Table) {
            std::cerr << "  block " << i << " type " << static_cast<int>(block.type) << ", font " << block.paragraphFontFamily
                      << " " << block.paragraphFontSizePt << " pt, space " << block.spaceBeforePt << " / "
                      << block.spaceAfterPt << ", line " << block.lineSpacing << " / " << block.lineHeightPt
                      << ", " << block.runs.size() << " run(s): \""
                      << UCRichDocument::ConcatenateRunText(block.runs).substr(0, 24) << "\"" << std::endl;
        }
        if (block.type != RichBlockType::Table) continue;
        std::cerr << "  table at block " << i << ": width " << block.tableWidthPt << " pt, columns";
        for (float width : block.tableColumnWidths) std::cerr << " " << width;
        std::cerr << std::endl;
        for (const RichTableRow& row : block.tableRows) {
            std::cerr << "    row height " << row.heightPt << (row.heightExact ? " exact" : "") << ", cells";
            for (const RichTableCell& cell : row.cells) {
                std::cerr << " [" << cell.columnSpan << "x" << cell.rowSpan << "]";
                dumpRuns(cell.runs, "a cell of block " + std::to_string(i));
                if (verbose) {
                    // The cell's lines as the reader made them.
                    std::cerr << "\n      cell pad " << cell.paddingTopPt << "/" << cell.paddingBottomPt
                              << " line " << cell.lineHeightPt << (cell.lineHeightAtLeast ? "+" : "")
                              << " x" << cell.lineSpacing << ":";
                    for (const RichTextRun& run : cell.runs) {
                        std::cerr << "\n        " << (run.lineBreakBefore ? "| " : "  ") << run.fontSizePt << "pt ";
                        if (run.emptyLineHeightPt > 0.0f) std::cerr << "(space " << run.emptyLineHeightPt << ") ";
                        if (run.IsTextFrame()) std::cerr << "[frame " << run.imageWidthPt << "x" << run.imageHeightPt << "]";
                        else if (run.IsInlineImage()) std::cerr << "[picture " << run.imageWidthPt << "x" << run.imageHeightPt << "]";
                        else std::cerr << "\"" << run.text.substr(0, 28) << "\"";
                    }
                    std::cerr << "\n     ";
                }
            }
            std::cerr << std::endl;
        }
    }

    auto edit = std::make_shared<UltraCanvasRichTextEdit>("Probe", 0, 0, 980, 780);
    window->AddChild(edit);
    edit->SetDocument(document);
    edit->SetPageView(true);
    window->UpdateAndRender();
    window->UpdateAndRender();
    if (!edit->ExportToPdf(std::string(argv[2]), error)) {
        std::cerr << "export failed: " << error << std::endl;
        return 1;
    }
    return 0;
}
