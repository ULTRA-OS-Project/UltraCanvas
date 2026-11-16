// Tests/DocumentRenderProbe.cpp
// A developer's probe, not a test: loads a word-processing document (.odt,
// .docx, .doc, ...) through UCWordDocumentIO, lays it out in an
// UltraCanvasRichTextEdit in page view and writes what it draws as a PDF, to
// be put beside the PDF the document's own program exports.
//
//   xvfb-run -a ./DocumentRenderProbe letter.odt letter.pdf
//   pdftoppm -r 60 -png letter.pdf letter
//
// Needs a display, as the element does. Exit code 0 when the PDF was written.
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasRichTextEdit.h"
#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"

#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: DocumentRenderProbe <document> <out.pdf>" << std::endl;
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
    std::cerr << document->blocks.size() << " block(s), " << document->frames.size() << " frame(s), "
              << document->media.size() << " picture(s)" << std::endl;

    // What the reader made of it: the page, each floating object's place and
    // each table's grid - the things a layout difference usually comes from.
    std::cerr << "page " << document->page.widthPt << " x " << document->page.heightPt << " pt, margins "
              << document->page.marginLeftPt << " " << document->page.marginTopPt << " "
              << document->page.marginRightPt << " " << document->page.marginBottomPt << std::endl;
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
        if (block.type != RichBlockType::Table) continue;
        std::cerr << "  table at block " << i << ": width " << block.tableWidthPt << " pt, columns";
        for (float width : block.tableColumnWidths) std::cerr << " " << width;
        std::cerr << std::endl;
        for (const RichTableRow& row : block.tableRows) {
            std::cerr << "    row height " << row.heightPt << (row.heightExact ? " exact" : "") << ", cells";
            for (const RichTableCell& cell : row.cells) {
                std::cerr << " [" << cell.columnSpan << "x" << cell.rowSpan << "]";
                dumpRuns(cell.runs, "a cell of block " + std::to_string(i));
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
