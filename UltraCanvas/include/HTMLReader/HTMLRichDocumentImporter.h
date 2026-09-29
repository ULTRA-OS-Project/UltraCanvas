// include/HTMLReader/HTMLRichDocumentImporter.h
// HTML → UCRichDocument: turns an HTML page or fragment (a mail's HTML body,
// a pasted snippet) into the editable word-processing model, so it can be
// edited in UltraCanvasRichTextEdit and written out again with ToHTML.
//
// It reads the markup through the HTMLReader's parser and style resolver - the
// same cascade the reader renders with, <style> blocks and presentational
// attributes included - and maps what the model can hold:
//   * paragraphs, headings, lists (ordered, nested, start numbers), rules;
//   * bold / italic / underline / strike / sub / sup / code, links, text and
//     highlight colours, font families and sizes;
//   * alignment, left indents, and the space between paragraphs (collapsed
//     the way CSS collapses margins);
//   * <blockquote> as the blocks' quote level;
//   * pictures, standalone or inside a line of text, with their sizes;
//   * tables with several columns (spans, cell colours, borders, widths).
//     A one-column table - the scaffolding mail layouts are built from - is
//     unwrapped into the text flow; a table inside a table cell becomes lines
//     of that cell, since the model has no nested tables.
// What it cannot hold (floats, positioning, scripts, forms) is dropped, and
// its text kept.
//
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
};

// A new document holding `html`.
UCRichDocument ImportHTMLToRichDocument(const std::string& html,
                                        const HTMLRichImportOptions& options = {});

// Appends `html` to the end of `document`, adding its pictures to the
// document's media (an identical picture is stored once).
void AppendHTMLToRichDocument(UCRichDocument& document, const std::string& html,
                              const HTMLRichImportOptions& options = {});

} // namespace UltraCanvas
