// include/UltraCanvasOfficeClipboard.h
// Copy and paste between the office elements - the spreadsheet grid
// (UltraCanvasSpreadsheet), the document editor (UltraCanvasRichTextEdit) and
// the slide editor (UltraCanvasSlideEdit) - through the system clipboard's
// flavours, which other programs read and write as well:
//
//   cells             an HTML table and tab-separated text (the grid's Copy).
//                     A document reads the table as a table
//                     (UCRichDocument::FromHTML); a slide makes a table shape
//                     of it (PptxTableCellsFromHtml / FromText).
//   a document's      HTML and its text; a slide makes a text box of it
//   selection         (PptxParagraphsFromClipboardHtml), keeping bold, italic,
//                     underline, colour, size and lists.
//   slide shapes      their text and HTML (PptxShapesAsClipboardText / Html):
//                     a table as a table, the rest as paragraphs.
//   a chart           the one thing the flavours cannot carry: the office
//                     clipboard below keeps the chart's data (RichChart) in
//                     this process, beside a picture of it (HTML) and its data
//                     (tab-separated text) on the system clipboard for every
//                     other program. A document pastes it as a chart frame, a
//                     slide as a chart shape - a static copy; a live link to
//                     the cells is a later step.
//
// Nothing here needs a window: the converters are plain functions, and the
// office clipboard reads the system clipboard only to know whether it is
// still the newest copy.
//
// doc-check: example
//   std::vector<std::vector<std::string>> rows;
//   if (ClipboardTableFromTabbedText("Item\tPrice\nTea\t2.50", rows)) {
//       PptxTableCells cells = PptxTableCellsFromRows(rows);   // a slide table's cells
//   }
//
// Version: 1.0.0 - tables, text and shapes between the flavours; the office clipboard's chart
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasRichDocument.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// Rows of cell texts, as a table is copied.
using ClipboardCellRows = std::vector<std::vector<std::string>>;
// Rows of cells, each cell's paragraphs: a slide table's text (PptxShape::rows).
using PptxTableCells = std::vector<std::vector<std::vector<PptxParagraph>>>;

// ===== TABLES =====
// The first <table> of an HTML fragment as rows of cell texts, through
// thead / tbody / tfoot; a cell spanning columns or rows leaves the cells it
// covers empty. False when there is no table with a cell.
bool ClipboardTableFromHtml(const std::string& html, ClipboardCellRows& rows);
// True when the fragment's text is all in its first table: cells, not a
// document's selection that holds a table among its paragraphs.
bool ClipboardHtmlIsTable(const std::string& html);
// Tab-separated text as spreadsheets copy cells: a tab between cells, a line
// per row, a value holding a tab, a line break or a quote in quotes. False
// for text without a tab, which is text, not cells.
bool ClipboardTableFromTabbedText(const std::string& text, ClipboardCellRows& rows);

// The cells as a slide table's: a paragraph per line of each cell's text.
PptxTableCells PptxTableCellsFromRows(const ClipboardCellRows& rows);
// The fragment's first table as a slide table's cells, each cell's look
// (bold, italic, underline, colour, alignment) kept. False when the fragment
// holds no table, or holds text outside it (ClipboardHtmlIsTable).
bool PptxTableCellsFromHtml(const std::string& html, PptxTableCells& cells);

// ===== TEXT =====
// A document's selection (HTML) as a text box's paragraphs: each paragraph,
// heading and list item one, its runs' look kept; `look` gives what the HTML
// does not say (the typeface, the size). A table's rows become lines, its
// cells separated by a tab; pictures are left out.
std::vector<PptxParagraph> PptxParagraphsFromClipboardHtml(const std::string& html, const PptxRun& look);
// Plain text as a text box's paragraphs: a paragraph per line, in `look`.
std::vector<PptxParagraph> PptxParagraphsFromClipboardText(const std::string& text, const PptxRun& look);

// Slide shapes for other programs: their text (a shape's paragraphs a line
// each, a table's rows a line each with a tab between cells, an empty line
// between shapes) and HTML (a table as a <table>, the rest as paragraphs
// with their look).
std::string PptxShapesAsClipboardText(const std::vector<PptxShape>& shapes);
std::string PptxShapesAsClipboardHtml(const std::vector<PptxShape>& shapes);

// ===== CHARTS: THE OFFICE CLIPBOARD =====
// The chart's data as tab-separated text, as a spreadsheet pastes it: its
// title on the first line when it has one, then a row of the series' names
// after an empty cell, then a row per category with its values.
std::string RichChartAsClipboardText(const RichChart& chart);
// The chart drawn as a `width` x `height` PNG on white, by the chart engine.
// False when it has nothing to draw.
bool RichChartToPng(const RichChart& chart, int width, int height, std::vector<uint8_t>& png);

// Keeps `chart` (drawn `widthPx` x `heightPx` where it was copied) in this
// process, and puts it on the system clipboard for other programs: a picture
// of it (HTML) and its data (RichChartAsClipboardText). False when the chart
// has nothing to show.
bool CopyChartToClipboard(const RichChart& chart, int widthPx, int heightPx);
// The chart CopyChartToClipboard kept, while the system clipboard still
// holds what it put there (or no system clipboard is to be had); false once
// anything else was copied since, or none was copied.
bool GetClipboardChart(RichChart& chart, int& widthPx, int& heightPx);
void ClearClipboardChart();

} // namespace UltraCanvas
