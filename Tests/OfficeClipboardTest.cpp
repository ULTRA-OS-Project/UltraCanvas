// Tests/OfficeClipboardTest.cpp
// Copy and paste between the office elements (UltraCanvasOfficeClipboard.h),
// on a clipboard backend that stands in for the platform's (no display):
//  - the converters: an HTML table and tab-separated text as cells, a
//    document's selection as a text box's paragraphs, slide shapes as text
//    and HTML, a chart's data as text;
//  - cells from the grid pasted into a document (a table) and onto a slide
//    (a table shape with the cells' text, the header row bold);
//  - the slide editor's Ctrl+V: its own shapes while the clipboard holds
//    their copy; else a document's selection as a text box, plain text as a
//    text box, tab-separated text as a table, a picture as a picture;
//  - a chart copied from the grid: on the office clipboard while the system
//    clipboard holds its picture and data, pasted as a chart into a
//    document and onto a slide, gone once something else is copied.
//
// Usage: OfficeClipboardTest
// Exit code is the number of failed checks.

#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasOfficeClipboard.h"
#include "UltraCanvasRichDocumentEditor.h"
#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasSlideEdit.h"
#include "UltraCanvasSpreadsheet.h"
#include "UltraCanvasSpreadsheetChart.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) ++failures;
    std::printf("%s: %s\n", ok ? "  ok" : "FAIL", what.c_str());
}

struct FakeState {
    std::string text;
    std::string html;
    std::vector<uint8_t> image;
    std::string imageFormat;
};

// A platform clipboard with text, HTML and image flavours; setting one
// flavour replaces the others, as a new owner does.
class FakeBackend : public UltraCanvasClipboardBackend {
public:
    explicit FakeBackend(std::shared_ptr<FakeState> state) : state_(std::move(state)) {}
    bool Initialize() override { return true; }
    void Shutdown() override {}
    bool GetClipboardText(std::string& text) override {
        text = state_->text;
        return !state_->text.empty() || state_->image.empty();
    }
    bool SetClipboardText(const std::string& text) override {
        *state_ = FakeState();
        state_->text = text;
        return true;
    }
    bool SetClipboardHtml(const std::string& html, const std::string& plain) override {
        *state_ = FakeState();
        state_->html = html;
        state_->text = plain;
        return true;
    }
    bool GetClipboardHtml(std::string& html) override { html = state_->html; return !html.empty(); }
    bool GetClipboardImage(std::vector<uint8_t>& data, std::string& format) override {
        data = state_->image;
        format = state_->imageFormat;
        return !data.empty();
    }
    bool SetClipboardImage(const std::vector<uint8_t>& data, const std::string& format) override {
        *state_ = FakeState();
        state_->image = data;
        state_->imageFormat = format;
        return true;
    }
    bool GetClipboardFiles(std::vector<std::string>&) override { return false; }
    bool SetClipboardFiles(const std::vector<std::string>&) override { return false; }
    bool HasClipboardChanged() override { return false; }
    void ResetChangeState() override {}
    std::vector<std::string> GetAvailableFormats() override { return {"text/plain", "text/html", "image/png"}; }
    bool IsFormatAvailable(const std::string&) override { return true; }

private:
    std::shared_ptr<FakeState> state_;
};

const PptxShape* LastShape(const UCSlideDeckEditor& editor) {
    const PptxSlide* slide = editor.CurrentSlide();
    return slide && !slide->shapes.empty() ? &slide->shapes.back() : nullptr;
}

std::string CellText(const PptxShape& table, size_t row, size_t col) {
    if (row >= table.rows.size() || col >= table.rows[row].size()) return "<none>";
    std::string text;
    for (size_t i = 0; i < table.rows[row][col].size(); ++i) {
        if (i) text += '\n';
        text += table.rows[row][col][i].Text();
    }
    return text;
}

bool CellBold(const PptxShape& table, size_t row, size_t col) {
    if (row >= table.rows.size() || col >= table.rows[row].size()) return false;
    for (const PptxParagraph& p : table.rows[row][col])
        for (const PptxRun& r : p.runs)
            if (!r.text.empty() && !r.bold) return false;
    return true;
}

const RichDocBlock* FirstTable(const UCRichDocument& document) {
    for (const RichDocBlock& block : document.blocks)
        if (block.type == RichBlockType::Table) return &block;
    return nullptr;
}

std::string RichCellText(const RichDocBlock& table, size_t row, size_t col) {
    if (row >= table.tableRows.size() || col >= table.tableRows[row].cells.size()) return "<none>";
    std::string text;
    for (const RichTextRun& run : table.tableRows[row].cells[col].runs) text += run.text;
    return text;
}

} // namespace

int main() {
    std::printf("=== Copy and paste between cells, documents and slides ===\n");
    InitializeClipboard();   // no display: the platform's backend may not start
    auto state = std::make_shared<FakeState>();
    Check(GetClipboard() && GetClipboard()->InitializeWithBackend(std::make_unique<FakeBackend>(state)),
          "a stand-in clipboard backend");

    // ----- the converters -----
    std::printf("\n-- converters --\n");
    {
        ClipboardCellRows rows;
        Check(ClipboardTableFromHtml("<table><tr><th>A</th><th colspan=\"2\">B</th></tr>"
                                     "<tbody><tr><td>1</td><td>2</td><td>3</td></tr></tbody></table>", rows) &&
                  rows.size() == 2 && rows[0].size() == 3 && rows[0][1] == "B" && rows[0][2].empty() && rows[1][2] == "3",
              "an HTML table as cells, a spanned cell's cover left empty");
        Check(!ClipboardTableFromHtml("<p>no table</p>", rows), "no table: no cells");
        Check(ClipboardHtmlIsTable("<html><body><table><tr><td>x</td></tr></table></body></html>"),
              "a fragment that is only a table is cells");
        Check(!ClipboardHtmlIsTable("<h1>Report</h1><table><tr><td>x</td><td>y</td></tr></table>"),
              "a heading beside the table: a document's selection");
        Check(ClipboardTableFromTabbedText("Item\tPrice\n\"Tab\there\"\t2.5\r\n", rows) && rows.size() == 2 &&
                  rows[1][0] == "Tab\there" && rows[1][1] == "2.5",
              "tab-separated text as cells, a quoted value keeping its tab");
        Check(!ClipboardTableFromTabbedText("just words\nand more", rows), "text without a tab is not cells");

        PptxTableCells cells;
        Check(PptxTableCellsFromHtml("<table><tr><td style=\"font-weight:bold;\">Name</td><td>Sum</td></tr>"
                                     "<tr><td>Tea</td><td style=\"text-align:right;\">2.5</td></tr></table>", cells) &&
                  cells.size() == 2 && cells[0].size() == 2,
              "an HTML table as a slide table's cells");
        if (cells.size() == 2 && cells[0].size() == 2) {
            const PptxParagraph& name = cells[0][0].front();
            Check(name.Text() == "Name" && !name.runs.empty() && name.runs.front().bold, "a bold cell stays bold");
            Check(cells[1][1].front().align == PptxAlign::Right, "a right-aligned cell stays right-aligned");
        }

        PptxRun look;
        look.sizePt = 18;
        look.font = "Body Font";
        const std::vector<PptxParagraph> paragraphs = PptxParagraphsFromClipboardHtml(
            "<h1>Plan</h1><p>Some <b>bold</b> and <i>slanted</i> words.</p><ul><li>first</li><li>second</li></ul>", look);
        Check(paragraphs.size() == 4, "a document's selection: a paragraph each for heading, paragraph and items (" +
                                          std::to_string(paragraphs.size()) + ")");
        if (paragraphs.size() == 4) {
            Check(paragraphs[0].Text() == "Plan" && paragraphs[0].runs.front().bold, "the heading, bold");
            bool bold = false, italic = false;
            for (const PptxRun& run : paragraphs[1].runs) {
                bold = bold || (run.text == "bold" && run.bold);
                italic = italic || (run.text == "slanted" && run.italic);
            }
            Check(bold && italic, "bold and italic runs kept");
            Check(paragraphs[2].bullet && paragraphs[2].Text() == "first" && paragraphs[3].bullet, "list items bulleted");
            Check(paragraphs[1].runs.front().font == "Body Font" && paragraphs[1].runs.front().sizePt == 18,
                  "the look the HTML does not give comes from the slide's");
        }
        const std::vector<PptxParagraph> lines = PptxParagraphsFromClipboardText("one\r\ntwo\n", look);
        Check(lines.size() == 2 && lines[1].Text() == "two" && lines[1].runs.front().sizePt == 18,
              "plain text: a paragraph per line");

        PptxShape title;
        title.kind = PptxShapeKind::Title;
        title.paragraphs = PptxParagraphsFromClipboardText("Quarterly report", look);
        PptxShape table;
        table.kind = PptxShapeKind::Table;
        table.rows = PptxTableCellsFromRows({{"a", "b"}, {"1", "2"}});
        Check(PptxShapesAsClipboardText({title, table}) == "Quarterly report\n\na\tb\n1\t2",
              "shapes as text: a shape's lines, a table's rows tab-separated");
        const std::string html = PptxShapesAsClipboardHtml({title, table});
        Check(html.find("Quarterly report") != std::string::npos && html.find("<table") != std::string::npos,
              "shapes as HTML: the text, and the table as a table");

        RichChart chart;
        chart.kind = RichChart::Kind::Column;
        chart.title = "Sales";
        chart.categories = {"Jan", "Feb"};
        RichChartSeries series;
        series.name = "North";
        series.values = {1.5, 2};
        chart.series.push_back(series);
        Check(RichChartAsClipboardText(chart) == "Sales\n\tNorth\nJan\t1.5\nFeb\t2",
              "a chart's data as text [" + RichChartAsClipboardText(chart) + "]");
    }

    // ----- cells into a document and onto a slide -----
    std::printf("\n-- cells into a document and onto a slide --\n");
    auto grid = std::make_shared<UltraCanvasSpreadsheet>("grid", 0, 0, 900, 600);
    grid->SetCellValue(0, 0, std::string("Item"));
    grid->SetCellValue(0, 1, std::string("Price"));
    grid->SetCellValue(1, 0, std::string("Tea"));
    grid->SetCellValue(1, 1, 2.5);
    grid->SetCellValue(2, 0, std::string("Cake"));
    grid->SetCellValue(2, 1, 4.0);
    grid->Select(CellRange(0, 0, 2, 1));
    grid->Copy();
    Check(state->html.find("<table>") == 0, "the grid's copy is an HTML table");

    auto edit = std::make_shared<UltraCanvasRichTextEdit>("doc", 0, 0, 700, 500);
    edit->SetDocument(std::make_shared<UCRichDocument>(UCRichDocument::FromMarkdown("Before\n")));
    edit->Paste();
    const RichDocBlock* table = FirstTable(*edit->GetDocument());
    Check(table && table->tableRows.size() == 3 && RichCellText(*table, 0, 0) == "Item" &&
              RichCellText(*table, 2, 1) == "4",
          "cells into a document: a table holding them");

    auto deck = std::make_shared<PptxPresentation>();
    deck->slideWidth = 12192000;
    deck->slideHeight = 6858000;
    deck->minorFont = "Body Font";
    auto slides = CreateSlideEdit("deck", 0, 0, 1400, 800);
    slides->SetDeck(deck);
    UCSlideDeckEditor& editor = slides->Editor();
    editor.NewSlide(-1);
    const size_t before = editor.CurrentSlide()->shapes.size();
    slides->Paste();
    const PptxShape* pasted = LastShape(editor);
    Check(editor.CurrentSlide()->shapes.size() == before + 1 && pasted && pasted->kind == PptxShapeKind::Table,
          "cells onto a slide: a table shape");
    if (pasted && pasted->kind == PptxShapeKind::Table) {
        Check(pasted->rows.size() == 3 && pasted->rows[0].size() == 2 && CellText(*pasted, 0, 1) == "Price" &&
                  CellText(*pasted, 1, 1) == "2.5",
              "the cells' text in the table's cells");
        Check(CellBold(*pasted, 0, 0) && !CellBold(*pasted, 1, 0), "the header row bold, as the banded look has it");
        Check(pasted->width > 0 && pasted->height > 0 && pasted->x >= 0 && pasted->x + pasted->width <= deck->slideWidth,
              "placed on the slide");
    }
    editor.Undo();
    Check(editor.CurrentSlide()->shapes.size() == before, "the paste is one undo step");

    // ----- the slide editor's own shapes, and other text -----
    std::printf("\n-- the slide editor's own copy, text and pictures --\n");
    const int box = editor.InsertTextBox(SlideBox{914400, 914400, 3 * 914400, 914400}, "Hello slide");
    editor.Select({box});
    slides->Copy();
    Check(state->text == "Hello slide", "Copy puts the shapes' text on the clipboard [" + state->text + "]");
    size_t count = editor.CurrentSlide()->shapes.size();
    slides->Paste();
    Check(editor.CurrentSlide()->shapes.size() == count + 1 && LastShape(editor)->kind == PptxShapeKind::Text &&
              LastShape(editor)->x == 914400 + 152400,
          "Ctrl+V takes its own shapes while the clipboard holds their copy");

    SetClipboardText("First line\nSecond line");
    count = editor.CurrentSlide()->shapes.size();
    slides->Paste();
    pasted = LastShape(editor);
    Check(editor.CurrentSlide()->shapes.size() == count + 1 && pasted->kind == PptxShapeKind::Text &&
              pasted->paragraphs.size() == 2 && pasted->paragraphs[1].Text() == "Second line" &&
              pasted->paragraphs[0].runs.front().font == "Body Font",
          "another program's text: a text box, a paragraph a line, in the deck's body font");

    SetClipboardText("a\tb\nc\td\n");
    slides->Paste();
    pasted = LastShape(editor);
    Check(pasted->kind == PptxShapeKind::Table && CellText(*pasted, 1, 1) == "d", "tab-separated text: a table");

    {
        // A document's selection, formatted.
        auto source = std::make_shared<UltraCanvasRichTextEdit>("source", 0, 0, 700, 500);
        source->SetDocument(std::make_shared<UCRichDocument>(UCRichDocument::FromMarkdown("Some **bold** words\n")));
        source->SelectAll();
        source->Copy();
        Check(!state->html.empty(), "a document's copy carries HTML");
        slides->Paste();
        pasted = LastShape(editor);
        bool bold = false;
        if (pasted && !pasted->paragraphs.empty())
            for (const PptxRun& run : pasted->paragraphs.front().runs) bold = bold || (run.text == "bold" && run.bold);
        Check(pasted && pasted->kind == PptxShapeKind::Text && UCSlideDeckEditor::ShapeText(*pasted) == "Some bold words" && bold,
              "a document's selection: a text box with its bold [" + (pasted ? UCSlideDeckEditor::ShapeText(*pasted) : "") + "]");
    }

    {
        ClipboardDib::RgbaImage image;
        image.width = 40;
        image.height = 20;
        image.pixels.assign(40 * 20 * 4, 200);
        std::vector<uint8_t> png;
        if (ClipboardDib::EncodePng(image, png)) {
            GetClipboard()->SetImage(png, "image/png");
            count = editor.CurrentSlide()->shapes.size();
            slides->Paste();
            pasted = LastShape(editor);
            Check(editor.CurrentSlide()->shapes.size() == count + 1 && pasted->kind == PptxShapeKind::Picture &&
                      deck->pictures.count(pasted->pictureTarget) == 1 &&
                      std::abs(static_cast<double>(pasted->width) / pasted->height - 2.0) < 0.01,
                  "a picture on the clipboard: a picture, its proportions kept");
        } else {
            std::printf("skip: this cairo writes no PNG\n");
        }
    }

    // ----- a chart -----
    std::printf("\n-- a chart from the grid --\n");
    const int chart = grid->InsertChartFromSelection(RichChart::Kind::Column);
    Check(chart >= 0, "a chart over the cells");
    grid->SelectChart(chart);
    grid->Copy();
    RichChart held;
    int width = 0, height = 0;
    Check(GetClipboardChart(held, width, height) && held.kind == RichChart::Kind::Column && width > 0 && height > 0,
          "Copy with a chart selected keeps the chart on the office clipboard");
    Check(state->text.find("Tea\t2.5") != std::string::npos, "...its data as text for other programs");
    Check(state->html.find("data:image/png;base64,") != std::string::npos, "...and a picture of it as HTML");

    count = editor.CurrentSlide()->shapes.size();
    slides->Paste();
    pasted = LastShape(editor);
    Check(editor.CurrentSlide()->shapes.size() == count + 1 && pasted->kind == PptxShapeKind::Chart &&
              pasted->chart.series.size() == held.series.size() && pasted->width > 0,
          "a chart onto a slide: a chart shape with its data");

    const size_t frames = edit->GetDocument()->frames.size();
    edit->Paste();
    const auto& document = *edit->GetDocument();
    bool chartFrame = false;
    for (size_t i = frames; i < document.frames.size(); ++i) chartFrame = chartFrame || document.frames[i].chart.IsChart();
    bool chartRun = false;
    for (const RichDocBlock& block : document.blocks)
        for (const RichTextRun& run : block.runs)
            if (run.IsTextFrame() && run.frameIndex < static_cast<int>(document.frames.size()) &&
                document.frames[static_cast<size_t>(run.frameIndex)].chart.IsChart())
                chartRun = run.imageWidthPt > 0;
    Check(chartFrame && chartRun, "a chart into a document: a chart frame where the caret is");

    grid->SelectChart(-1);
    grid->Select(CellRange(0, 0, 0, 0));
    grid->Copy();
    Check(!GetClipboardChart(held, width, height), "copying cells since: the chart is no longer what pastes");

    {
        // The document editor on its own: a chart frame where the caret is.
        UCRichDocumentEditor docEditor(std::make_shared<UCRichDocument>(UCRichDocument::FromMarkdown("Text\n")));
        RichChart tiny;
        Check(docEditor.InsertChart(tiny, 200, 100) < 0, "an empty chart is not inserted");
        Check(docEditor.InsertChart(held, 300, 200) >= 0 && docEditor.CanUndo(), "InsertChart is an undo step");
    }

    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
