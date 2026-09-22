// include/UltraCanvasRichDocument.h
// UCRichDocument — shared intermediate model for word-processing documents.
// Format readers (ODT, DOCX) produce it; format writers and the
// Markdown/HTML/plain-text serializers consume it, so no format is ever
// coupled directly to a UI element. See Docs/UltraCanvas/ODT-DOCX-Support-Proposal.md.
// The model is deliberately UI-free: only std types, no framework headers.
// Version: 1.1.0
// Last Modified: 2026-09-09
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

enum class RichTextAlign {
    Default,
    Left,
    Center,
    Right,
    Justify
};

// One contiguous span of identically-formatted text inside a block.
struct RichTextRun {
    std::string text;               // UTF-8; may contain '\n' only via lineBreakBefore handling
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    bool code = false;              // monospace/inline-code intent
    bool subscript = false;
    bool superscript = false;
    bool math = false;              // text is LaTeX math source, typeset inline ($...$)
    std::string linkTarget;         // non-empty => hyperlink around this run
    std::string fontFamily;         // empty = inherit
    float fontSizePt = 0.0f;        // 0 = inherit
    std::string color;              // "#RRGGBB" or empty = inherit
    bool lineBreakBefore = false;   // hard line break precedes this run (within the same paragraph)

    // ===== INLINE IMAGE =====
    // A run with mediaIndex >= 0 IS a picture sitting in the run of text - a
    // logo mid-sentence, an icon in a heading - rather than a paragraph of its
    // own (that is RichBlockType::Image). Its `text` is a single U+FFFC OBJECT
    // REPLACEMENT CHARACTER, the standard placeholder for an inline attachment:
    // it gives the picture one character's worth of the block's text, so the
    // caret steps over it, a selection covers it and Backspace deletes it, all
    // without any position needing to know it is not a letter.
    int mediaIndex = -1;                // index into UCRichDocument::media
    float imageWidthPt = 0.0f;          // 0 = use the image's own size
    float imageHeightPt = 0.0f;
    std::string imageAltText;

    static constexpr const char* kObjectReplacement = "\xEF\xBF\xBC";   // U+FFFC

    bool IsInlineImage() const { return mediaIndex >= 0; }

    bool HasSameFormatting(const RichTextRun& other) const {
        // Two pictures are never one run, and a picture never merges into the
        // text beside it: each placeholder has to stay its own run, because the
        // run is what carries which picture it is.
        if (IsInlineImage() || other.IsInlineImage()) return false;
        return bold == other.bold && italic == other.italic && underline == other.underline
            && strikethrough == other.strikethrough && code == other.code
            && subscript == other.subscript && superscript == other.superscript
            && math == other.math && linkTarget == other.linkTarget && fontFamily == other.fontFamily
            && fontSizePt == other.fontSizePt && color == other.color;
    }
};

// Embedded binary resource (images). Blocks reference media by index so the
// same picture used twice is stored once.
struct RichDocMedia {
    std::string name;               // package-local name, e.g. "image1.png"
    std::string mimeType;           // e.g. "image/png"
    std::vector<uint8_t> data;
};

enum class RichBlockType {
    Paragraph,
    Heading,        // headingLevel 1..6
    ListItem,       // orderedList + listLevel (0-based nesting)
    CodeBlock,      // runs hold raw lines separated by lineBreakBefore
    BlockQuote,
    Table,
    Image,          // standalone image paragraph; mediaIndex into media
    HorizontalRule,
    PageBreak,
    MathBlock       // display formula; runs hold the LaTeX source lines (lineBreakBefore)
};

struct RichTableCell {
    std::vector<RichTextRun> runs;
    int columnSpan = 1;
    int rowSpan = 1;
};

struct RichTableRow {
    std::vector<RichTableCell> cells;
    bool header = false;
};

struct RichDocBlock {
    RichBlockType type = RichBlockType::Paragraph;
    std::vector<RichTextRun> runs;      // Paragraph/Heading/ListItem/CodeBlock/BlockQuote
    int headingLevel = 0;               // Heading: 1..6
    bool orderedList = false;           // ListItem
    int listLevel = 0;                  // ListItem: 0-based nesting depth
    RichTextAlign align = RichTextAlign::Default;
    std::string codeLanguage;           // CodeBlock fence language hint
    std::vector<RichTableRow> tableRows;
    int mediaIndex = -1;                // Image
    std::string imageAltText;           // Image
    float imageWidthPt = 0.0f;          // Image: 0 = unknown
    float imageHeightPt = 0.0f;
};

// ===== TABLE GRID =====
// Cells are stored SPARSELY: a cell spanning two columns is one RichTableCell
// with columnSpan 2, and the slot it covers holds no cell of its own; a cell
// spanning two rows appears only in its first row, and the row below simply has
// one fewer cell. So a cell's index within its row is NOT its column, and every
// piece of code that has to know where a cell actually sits - measuring one for
// layout, inserting a column, deleting a row - needs the same walk over the
// grid. This resolves it once.
struct RichTableGridSlot {
    int row = -1;           // model row owning the cell (its ORIGIN row)
    int cellIndex = -1;     // index into that row's cells
    bool origin = false;    // true only at the cell's top-left slot
    bool Occupied() const { return cellIndex >= 0; }
};

struct RichTableGrid {
    int rowCount = 0;
    int columnCount = 0;    // widest row, counting column spans
    std::vector<RichTableGridSlot> slots;   // rowCount * columnCount, row-major

    const RichTableGridSlot& At(int row, int column) const;
    // Grid position of a model cell's top-left corner. False when the cell is
    // not in the grid (an index past the row's cells).
    bool OriginOf(int row, int cellIndex, int& outRow, int& outColumn) const;
    // The model cell occupying (row, column), wherever it originates. False for
    // a slot no cell reaches - a ragged row that ends early.
    bool CellAt(int row, int column, int& outRow, int& outCellIndex) const;

private:
    static const RichTableGridSlot kEmpty;
};

// Resolves `table`'s cells onto the grid they occupy. A non-table block yields
// an empty grid. Spans are clamped so a malformed document (a span reaching
// past the last row, a zero span) still produces a consistent grid rather than
// reading out of bounds.
RichTableGrid BuildTableGrid(const RichDocBlock& table);

struct RichDocumentMetadata {
    std::string title;
    std::string author;
    std::string description;
    std::string createdDate;            // ISO-8601 when available
    std::string modifiedDate;
};

// Options for UCRichDocument::ToMarkdown. When imageDirectory is set, the
// serializer writes each referenced media entry into that directory and the
// markdown references the written files (absolute paths), which is what the
// TextArea markdown renderer resolves. When empty, images degrade to their
// alt text in square brackets.
struct RichDocumentMarkdownOptions {
    std::string imageDirectory;
};

class UCRichDocument {
public:
    RichDocumentMetadata metadata;
    std::vector<RichDocBlock> blocks;
    std::vector<RichDocMedia> media;

    bool IsEmpty() const { return blocks.empty(); }

    // Adds (or reuses an identical) media entry and returns its index.
    int AddMedia(std::string name, std::string mimeType, std::vector<uint8_t> data);

    // ===== SERIALIZERS =====
    // Markdown round-trip is the editable representation (Texter opens
    // documents in MarkdownHybrid mode). Formatting that Markdown cannot
    // express (fonts, colors, alignment, page layout) is dropped.
    std::string ToMarkdown(const RichDocumentMarkdownOptions& options = {}) const;
    // Parses a Markdown subset (headings, emphasis, lists, pipe tables,
    // fenced code, block quotes, images, links, rules) back into a document.
    // Relative image paths resolve against baseDirectory; readable image
    // files are re-embedded as media.
    static UCRichDocument FromMarkdown(const std::string& markdown,
                                       const std::string& baseDirectory = "");

    // Self-contained HTML fragment (images inlined as data: URIs) for the
    // HTMLConverter / read-only viewing path.
    std::string ToHTML() const;

    std::string ToPlainText() const;

    // ===== HELPERS SHARED BY FORMAT READERS/WRITERS =====
    static std::string MimeTypeForImageName(const std::string& fileName);
    static std::string FileExtensionForMimeType(const std::string& mimeType);
    // Reads PNG/GIF/JPEG pixel dimensions from raw bytes (0 on failure).
    static bool SniffImagePixelSize(const std::vector<uint8_t>& data, int& width, int& height);
    static std::string ConcatenateRunText(const std::vector<RichTextRun>& runs);
};

} // namespace UltraCanvas
