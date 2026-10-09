// include/HTMLReader/HTMLRichDocumentImporter.h
// HTML → UCRichDocument: turns an HTML page or fragment (a mail's HTML body,
// a pasted snippet) into the editable word-processing model, so it can be
// edited in UltraCanvasRichTextEdit and written out again with ToHTML.
//
// It reads the markup through the HTMLReader's parser and style resolver - the
// same cascade the reader renders with, <style> blocks and presentational
// attributes included - and maps what the model can hold:
//   * paragraphs, headings, lists (ordered, nested, start numbers), rules;
//     Word's list paragraphs (<p style="mso-list:l0 level1 lfo1"> with the
//     label typed out in front, as Word copies and Outlook sends a list) as
//     list items - the label decides bullet or number, the number format
//     and where the count starts;
//   * bold / italic / underline / strike / sub / sup / code, links, text and
//     highlight colours, font families and sizes;
//   * alignment, left indents, and the space between paragraphs (collapsed
//     the way CSS collapses margins); right-to-left paragraphs (dir="rtl" on
//     the element or one around it);
//   * <blockquote> as the blocks' quote level;
//   * pictures, standalone or inside a line of text, with their sizes;
//   * tables with several columns (spans, cell colours, borders, widths).
//     A one-column table - the scaffolding mail layouts are built from - is
//     unwrapped into the text flow; a table inside a table cell becomes lines
//     of that cell, since the model has no nested tables.
// What it cannot hold (floats, positioning, scripts, forms) is dropped, and
// its text kept. UCRichDocument::FromHTML - a rich paste - reads through it
// too, with preAsCodeBlock and skipWordListLabels on.
//
// Version: 1.2.0 - Word's list paragraphs are list items; a <pre>'s blank lines
//                  are kept (only the newline right after <pre> is not content)
// Version: 1.1.0 - dir="rtl" paragraphs; preAsCodeBlock and skipWordListLabels
//                  (a paste: UCRichDocument::FromHTML reads through the importer)
// Version: 1.0.0
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraCanvas {

// The bytes behind an <img src>.
struct HTMLRichImportImage {
    std::string mimeType;          // empty = recognised from the bytes
    std::vector<uint8_t> data;
};

struct HTMLRichImportOptions {
    // Supplies a picture the markup only points to - a mail's cid: parts, a
    // file next to the page, a remote image already fetched. data: URIs are
    // decoded without it. Return false to leave the picture out: it then
    // becomes its alt text in brackets, or nothing when it has none.
    std::function<bool(const std::string& src, HTMLRichImportImage& out)> resolveImage;
    // Added to every block's quote level: 1 puts the whole import inside a
    // quote, the way a reply quotes the message it answers.
    int quoteLevel = 0;
    // The size the markup's "normal" text has (CSS px). Runs at this size get
    // no size of their own and follow the editor's default.
    float baseFontSizePx = 16.0f;
    // Keep the markup's font families and sizes / its text and background
    // colours. Off gives the text the editor's own look.
    bool keepFonts = true;
    bool keepColors = true;
    // <pre> becomes a code block (RichBlockType::CodeBlock): its lines as
    // written, in the view's code style. Off, it is a paragraph of
    // monospaced lines - right for a mail, whose <pre> is mostly a quoted
    // plain-text message (Thunderbird's moz-quote-pre), not code.
    bool preAsCodeBlock = false;
    // Leaves out a list label Word types out (a <span style="mso-list:Ignore">)
    // where no list item takes it as its marker - the "1." of a numbered
    // heading. A browser shows it, so a mail keeps it; a paste from Word
    // drops it. A list paragraph's label is always its item's marker.
    bool skipWordListLabels = false;
};

// A new document holding `html`.
UCRichDocument ImportHTMLToRichDocument(const std::string& html,
                                        const HTMLRichImportOptions& options = {});

// Appends `html` to the end of `document`, adding its pictures to the
// document's media (an identical picture is stored once).
void AppendHTMLToRichDocument(UCRichDocument& document, const std::string& html,
                              const HTMLRichImportOptions& options = {});

} // namespace UltraCanvas
