// core/UltraCanvasOfficeClipboard.cpp
// Copy and paste between the office elements (include/UltraCanvasOfficeClipboard.h).
//
// Version: 1.0.0 - tables, text and shapes between the flavours; the office clipboard's chart
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#include "UltraCanvasOfficeClipboard.h"

#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "UltraCanvasCSVImport.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasClipboardDib.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasRichDocumentChart.h"
#include "UltraCanvasTextUtils.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <functional>
#include <mutex>

namespace UltraCanvas {

namespace {

std::string WithoutCarriageReturns(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

std::string WithoutSpaces(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (!std::isspace(c)) out += static_cast<char>(c);
    }
    // A no-break space (U+00A0) is a space too.
    std::string result;
    for (size_t i = 0; i < out.size(); ++i) {
        if (static_cast<unsigned char>(out[i]) == 0xC2 && i + 1 < out.size() && static_cast<unsigned char>(out[i + 1]) == 0xA0) {
            ++i;
            continue;
        }
        result += out[i];
    }
    return result;
}

// A paragraph per line of `text`, each with one run in `look`.
std::vector<PptxParagraph> LinesAsParagraphs(const std::string& text, const PptxRun& look) {
    std::vector<PptxParagraph> out;
    size_t start = 0;
    const std::string clean = WithoutCarriageReturns(text);
    while (true) {
        const size_t end = clean.find('\n', start);
        PptxRun run = look;
        run.text = clean.substr(start, end == std::string::npos ? std::string::npos : end - start);
        PptxParagraph paragraph;
        paragraph.runs.push_back(std::move(run));
        out.push_back(std::move(paragraph));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

// The rows of `table`, inside thead / tbody / tfoot or not (not into a table
// nested in a cell).
std::vector<HTML::Node*> RowsOf(HTML::Node* table) {
    std::vector<HTML::Node*> rows;
    std::function<void(HTML::Node*)> collect = [&](HTML::Node* node) {
        for (const auto& child : node->children) {
            if (child->IsElement("tr")) rows.push_back(child.get());
            else if (child->IsElement("thead") || child->IsElement("tbody") || child->IsElement("tfoot")) collect(child.get());
        }
    };
    collect(table);
    return rows;
}

int SpanOf(const HTML::Node& cell, const char* name) {
    int span = 1;
    const std::string value = cell.GetAttribute(name);
    if (!value.empty()) {
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), span);
        if (parsed.ec != std::errc()) span = 1;
    }
    return std::clamp(span, 1, 1000);
}

// A rich block's runs without what has no place on a slide: pictures, text
// frames, note references.
std::vector<RichTextRun> TextRuns(const std::vector<RichTextRun>& runs) {
    std::vector<RichTextRun> out;
    for (const RichTextRun& run : runs) {
        if (run.IsInlineImage() || run.IsTextFrame() || run.IsNoteReference()) continue;
        out.push_back(run);
    }
    return out;
}

// One cell's runs as a slide cell's paragraphs.
std::vector<PptxParagraph> CellParagraphs(const RichTableCell& cell) {
    UCRichDocument one;
    RichDocBlock block;
    block.align = cell.align;
    block.runs = TextRuns(cell.runs);
    one.blocks.push_back(std::move(block));
    return PptxParagraphsFromRichDocument(one, {});
}

// A slide cell's (or shape's) paragraphs as one rich block's runs, a line
// break between paragraphs.
std::vector<RichTextRun> ParagraphsAsRuns(const std::vector<PptxParagraph>& paragraphs) {
    std::vector<RichTextRun> out;
    const UCRichDocument document = PptxParagraphsToRichDocument(paragraphs);
    for (size_t b = 0; b < document.blocks.size(); ++b) {
        const auto& runs = document.blocks[b].runs;
        for (size_t r = 0; r < runs.size(); ++r) {
            RichTextRun run = runs[r];
            if (b > 0 && r == 0) run.lineBreakBefore = true;
            out.push_back(std::move(run));
        }
        if (b > 0 && runs.empty()) {
            RichTextRun gap;
            gap.lineBreakBefore = true;
            out.push_back(std::move(gap));
        }
    }
    return out;
}

std::string ParagraphsText(const std::vector<PptxParagraph>& paragraphs) {
    std::string text;
    for (size_t i = 0; i < paragraphs.size(); ++i) {
        if (i) text += '\n';
        text += paragraphs[i].Text();
    }
    return text;
}

bool HasText(const std::vector<PptxParagraph>& paragraphs) {
    for (const PptxParagraph& paragraph : paragraphs)
        for (const PptxRun& run : paragraph.runs)
            if (!run.text.empty()) return true;
    return false;
}

// What the office clipboard holds.
struct OfficeClipboard {
    bool hasChart = false;
    RichChart chart;
    int width = 0, height = 0;
    std::string text;            // what was put on the system clipboard as text
};

OfficeClipboard& Office() {
    static OfficeClipboard office;
    return office;
}

} // namespace

// ===== TABLES =====

bool ClipboardTableFromHtml(const std::string& html, ClipboardCellRows& out) {
    out.clear();
    HTML::Parser parser;
    HTML::Document document = parser.Parse(html);
    HTML::Node* table = document.root ? document.root->FindFirst("table") : nullptr;
    if (!table) return false;
    const std::vector<HTML::Node*> rows = RowsOf(table);
    if (rows.empty()) return false;
    ClipboardCellRows grid(rows.size());
    std::vector<std::vector<bool>> taken(rows.size());
    bool anyCell = false;
    for (size_t r = 0; r < rows.size(); ++r) {
        size_t col = 0;
        for (const auto& child : rows[r]->children) {
            if (!child->IsElement("td") && !child->IsElement("th")) continue;
            while (col < taken[r].size() && taken[r][col]) ++col;
            const int colSpan = SpanOf(*child, "colspan");
            const int rowSpan = SpanOf(*child, "rowspan");
            for (int dr = 0; dr < rowSpan && r + static_cast<size_t>(dr) < rows.size(); ++dr) {
                auto& line = taken[r + static_cast<size_t>(dr)];
                auto& values = grid[r + static_cast<size_t>(dr)];
                const size_t end = col + static_cast<size_t>(colSpan);
                if (line.size() < end) line.resize(end, false);
                if (values.size() < end) values.resize(end);
                for (size_t c = col; c < end; ++c) line[c] = true;
            }
            grid[r][col] = HTML::ExtractPlainText(*child);
            col += static_cast<size_t>(colSpan);
            anyCell = true;
        }
    }
    if (!anyCell) return false;
    out = std::move(grid);
    return true;
}

bool ClipboardHtmlIsTable(const std::string& html) {
    HTML::Parser parser;
    HTML::Document document = parser.Parse(html);
    HTML::Node* table = document.root ? document.root->FindFirst("table") : nullptr;
    if (!table) return false;
    // Everything said outside the table (a heading, a paragraph) makes it
    // a document's selection, which a table alone would lose.
    return WithoutSpaces(HTML::ExtractPlainText(*document.root)) == WithoutSpaces(HTML::ExtractPlainText(*table));
}

bool ClipboardTableFromTabbedText(const std::string& given, ClipboardCellRows& out) {
    out.clear();
    std::string text = WithoutCarriageReturns(given);
    while (!text.empty() && text.back() == '\n') text.pop_back();   // the line end after the last row
    if (text.find('\t') == std::string::npos) return false;
    CSVImportOptions options;
    options.SetSingleSeparator('\t');
    for (const CSVRow& row : CSVParse(text, options)) {
        std::vector<std::string> values;
        for (const CSVField& field : row) values.push_back(field.text);
        out.push_back(std::move(values));
    }
    return !out.empty();
}

PptxTableCells PptxTableCellsFromRows(const ClipboardCellRows& rows) {
    size_t columns = 0;
    for (const auto& row : rows) columns = std::max(columns, row.size());
    PptxTableCells cells;
    for (const auto& row : rows) {
        std::vector<std::vector<PptxParagraph>> line;
        for (size_t c = 0; c < columns; ++c) line.push_back(LinesAsParagraphs(c < row.size() ? row[c] : std::string(), PptxRun()));
        cells.push_back(std::move(line));
    }
    return cells;
}

bool PptxTableCellsFromHtml(const std::string& html, PptxTableCells& cells) {
    cells.clear();
    if (!ClipboardHtmlIsTable(html)) return false;
    ClipboardCellRows rows;
    if (!ClipboardTableFromHtml(html, rows)) return false;
    cells = PptxTableCellsFromRows(rows);
    // Each cell's look, where the importer read the same table: no spans, a
    // cell for every one of the grid's (a one-column table it reads as
    // paragraphs, which keeps the plain cells).
    const UCRichDocument document = UCRichDocument::FromHTML(html);
    const RichDocBlock* table = nullptr;
    for (const RichDocBlock& block : document.blocks) {
        if (block.type == RichBlockType::Table) { table = &block; break; }
    }
    if (!table || table->tableRows.size() != rows.size()) return true;
    for (size_t r = 0; r < rows.size(); ++r) {
        const auto& richCells = table->tableRows[r].cells;
        if (richCells.size() != rows[r].size()) return true;
        for (const RichTableCell& cell : richCells)
            if (cell.columnSpan != 1 || cell.rowSpan != 1) return true;
    }
    for (size_t r = 0; r < rows.size(); ++r) {
        const auto& richCells = table->tableRows[r].cells;
        for (size_t c = 0; c < richCells.size() && c < cells[r].size(); ++c) {
            std::vector<PptxParagraph> paragraphs = CellParagraphs(richCells[c]);
            if (ParagraphsText(paragraphs) == ParagraphsText(cells[r][c])) cells[r][c] = std::move(paragraphs);
        }
    }
    return true;
}

// ===== TEXT =====

std::vector<PptxParagraph> PptxParagraphsFromClipboardHtml(const std::string& html, const PptxRun& look) {
    const UCRichDocument document = UCRichDocument::FromHTML(html);
    UCRichDocument flat;
    for (const RichDocBlock& block : document.blocks) {
        switch (block.type) {
            case RichBlockType::Table:
                // A row a line, a tab between its cells.
                for (const RichTableRow& row : block.tableRows) {
                    RichDocBlock line;
                    for (size_t c = 0; c < row.cells.size(); ++c) {
                        if (c) {
                            RichTextRun tab;
                            tab.text = "\t";
                            line.runs.push_back(std::move(tab));
                        }
                        for (RichTextRun run : TextRuns(row.cells[c].runs)) {
                            run.lineBreakBefore = false;
                            line.runs.push_back(std::move(run));
                        }
                    }
                    flat.blocks.push_back(std::move(line));
                }
                break;
            case RichBlockType::Image:
            case RichBlockType::HorizontalRule:
            case RichBlockType::PageBreak:
                break;
            default: {
                RichDocBlock copy = block;
                copy.runs = TextRuns(block.runs);
                if (block.type == RichBlockType::Heading) {
                    copy.type = RichBlockType::Paragraph;   // a heading stands out in bold
                    for (RichTextRun& run : copy.runs) run.bold = true;
                } else if (block.type != RichBlockType::ListItem) {
                    copy.type = RichBlockType::Paragraph;
                }
                flat.blocks.push_back(std::move(copy));
                break;
            }
        }
    }
    // Empty paragraphs at the ends are the fragment's edges, not text.
    auto empty = [](const RichDocBlock& block) {
        for (const RichTextRun& run : block.runs)
            if (!run.text.empty()) return false;
        return true;
    };
    while (!flat.blocks.empty() && empty(flat.blocks.back())) flat.blocks.pop_back();
    while (!flat.blocks.empty() && empty(flat.blocks.front())) flat.blocks.erase(flat.blocks.begin());
    if (flat.blocks.empty()) return {};
    PptxParagraph model;
    PptxRun first = look;
    first.text.clear();
    model.runs.push_back(first);
    return PptxParagraphsFromRichDocument(flat, {model});
}

std::vector<PptxParagraph> PptxParagraphsFromClipboardText(const std::string& given, const PptxRun& look) {
    std::string text = WithoutCarriageReturns(given);
    while (!text.empty() && text.back() == '\n') text.pop_back();
    PptxRun model = look;
    model.text.clear();
    return LinesAsParagraphs(text, model);
}

std::string PptxShapesAsClipboardText(const std::vector<PptxShape>& shapes) {
    std::string out;
    for (const PptxShape& shape : shapes) {
        std::string text;
        if (shape.kind == PptxShapeKind::Table) {
            for (size_t r = 0; r < shape.rows.size(); ++r) {
                if (r) text += '\n';
                for (size_t c = 0; c < shape.rows[r].size(); ++c) {
                    if (c) text += '\t';
                    std::string cell = ParagraphsText(shape.rows[r][c]);
                    std::replace(cell.begin(), cell.end(), '\n', ' ');
                    std::replace(cell.begin(), cell.end(), '\t', ' ');
                    text += cell;
                }
            }
        } else {
            text = ParagraphsText(shape.paragraphs);
        }
        if (text.empty()) continue;
        if (!out.empty()) out += "\n\n";
        out += text;
    }
    return out;
}

std::string PptxShapesAsClipboardHtml(const std::vector<PptxShape>& shapes) {
    UCRichDocument document;
    for (const PptxShape& shape : shapes) {
        if (shape.kind == PptxShapeKind::Table) {
            RichDocBlock table;
            table.type = RichBlockType::Table;
            for (size_t r = 0; r < shape.rows.size(); ++r) {
                RichTableRow row;
                row.header = r == 0;
                for (const auto& cell : shape.rows[r]) {
                    RichTableCell richCell;
                    richCell.runs = ParagraphsAsRuns(cell);
                    row.cells.push_back(std::move(richCell));
                }
                table.tableRows.push_back(std::move(row));
            }
            if (!table.tableRows.empty()) document.blocks.push_back(std::move(table));
        } else if (HasText(shape.paragraphs)) {
            for (RichDocBlock& block : PptxParagraphsToRichDocument(shape.paragraphs).blocks)
                document.blocks.push_back(std::move(block));
        }
    }
    if (document.blocks.empty()) return std::string();
    return document.ToHTML();
}

// ===== CHARTS =====

std::string RichChartAsClipboardText(const RichChart& chart) {
    std::string out;
    auto clean = [](std::string text) {
        std::replace(text.begin(), text.end(), '\t', ' ');
        std::replace(text.begin(), text.end(), '\n', ' ');
        return text;
    };
    if (!chart.title.empty()) out += clean(chart.title) + "\n";
    const bool scatter = chart.kind == RichChart::Kind::Scatter || chart.kind == RichChart::Kind::Bubble;
    size_t points = chart.categories.size();
    for (const RichChartSeries& series : chart.series) points = std::max(points, series.values.size());
    for (const RichChartSeries& series : chart.series) out += "\t" + clean(series.name);
    out += "\n";
    for (size_t i = 0; i < points; ++i) {
        if (i < chart.categories.size()) out += clean(chart.categories[i]);
        else if (scatter && !chart.series.empty() && i < chart.series.front().xValues.size())
            out += FormatFloatClassic(chart.series.front().xValues[i], 15);
        for (const RichChartSeries& series : chart.series) {
            out += "\t";
            if (i < series.values.size()) out += FormatFloatClassic(series.values[i], 15);
        }
        if (i + 1 < points) out += "\n";
    }
    return out;
}

bool RichChartToPng(const RichChart& chart, int width, int height, std::vector<uint8_t>& png) {
    png.clear();
    if (!chart.IsChart() || width <= 0 || height <= 0) return false;
    width = std::min(width, 4096);
    height = std::min(height, 4096);
    // The process-wide font machinery, one drawing at a time.
    static std::mutex drawing;
    std::lock_guard<std::mutex> lock(drawing);
    UCPixmap pixmap;
    if (!pixmap.Init(width, height)) return false;
    std::unique_ptr<IRenderContext> ctx = CreateRenderContext(Size2Di(width, height), nullptr);
    if (!ctx) return false;
    ctx->SetFillPaint(Color(255, 255, 255));
    ctx->FillRectangle(Rect2Dd(0, 0, width, height));
    if (!DrawRichDocumentChart(ctx.get(), chart, Rect2Dd(0, 0, width, height), Size2Di(width, height))) return false;
    ctx->FlushToSurface(pixmap.GetSurface(), Point2Dd(0, 0));
    pixmap.Flush();
    const uint32_t* pixels = pixmap.GetPixelData();
    if (!pixels) return false;
    // Cairo's rows may be wider than the picture.
    const int stride = pixmap.GetRawWidth() > 0 ? pixmap.GetRawWidth() : width;
    ClipboardDib::RgbaImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint32_t p = pixels[static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x)];
            uint8_t* to = &image.pixels[(static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4];
            // Opaque on white: premultiplied or not, the same bytes.
            to[0] = static_cast<uint8_t>((p >> 16) & 0xFF);
            to[1] = static_cast<uint8_t>((p >> 8) & 0xFF);
            to[2] = static_cast<uint8_t>(p & 0xFF);
            to[3] = 255;
        }
    }
    return ClipboardDib::EncodePng(image, png);
}

bool CopyChartToClipboard(const RichChart& chart, int widthPx, int heightPx) {
    if (!chart.IsChart()) return false;
    widthPx = std::clamp(widthPx, 40, 2400);
    heightPx = std::clamp(heightPx, 40, 2400);
    OfficeClipboard& office = Office();
    office.hasChart = true;
    office.chart = chart;
    office.width = widthPx;
    office.height = heightPx;
    office.text = RichChartAsClipboardText(chart);
    // Other programs: a picture of it, and its data as text.
    std::vector<uint8_t> png;
    if (RichChartToPng(chart, widthPx, heightPx, png)) {
        std::string alt = chart.title;
        std::string escaped;
        for (char c : alt) {
            if (c == '"') escaped += "&quot;";
            else if (c == '&') escaped += "&amp;";
            else if (c == '<') escaped += "&lt;";
            else escaped += c;
        }
        const std::string html = "<img src=\"data:image/png;base64," + Base64Encode(png, false) + "\" width=\"" +
                                 std::to_string(widthPx) + "\" height=\"" + std::to_string(heightPx) + "\" alt=\"" +
                                 escaped + "\">";
        SetClipboardHtml(html, office.text);
    } else {
        SetClipboardText(office.text);
    }
    return true;
}

bool GetClipboardChart(RichChart& chart, int& widthPx, int& heightPx) {
    OfficeClipboard& office = Office();
    if (!office.hasChart) return false;
    std::string now;
    if (GetClipboardText(now) && WithoutCarriageReturns(now) != WithoutCarriageReturns(office.text)) {
        ClearClipboardChart();   // something else was copied since
        return false;
    }
    chart = office.chart;
    widthPx = office.width;
    heightPx = office.height;
    return true;
}

void ClearClipboardChart() {
    OfficeClipboard& office = Office();
    office.hasChart = false;
    office.chart = RichChart();
    office.text.clear();
}

} // namespace UltraCanvas
