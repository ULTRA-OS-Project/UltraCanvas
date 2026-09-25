// include/UltraCanvasRichDocument.h
// UCRichDocument — shared intermediate model for word-processing documents.
// Format readers (ODT, DOCX) produce it; format writers and the
// Markdown/HTML/plain-text serializers consume it, so no format is ever
// coupled directly to a UI element. See Docs/UltraCanvas/ODT-DOCX-Support-Proposal.md.
// The model is deliberately UI-free: only std types, no framework headers.
// Version: 1.2.0
// Last Modified: 2026-09-25
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
    std::string highlightColor;     // background behind the text, "#RRGGBB"; empty = none
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
            && fontSizePt == other.fontSizePt && color == other.color
            && highlightColor == other.highlightColor;
    }
};

// Embedded binary resource (images). Blocks reference media by index so the
// same picture used twice is stored once.
struct RichDocMedia {
    std::string name;               // package-local name, e.g. "image1.png"
    std::string mimeType;           // e.g. "image/png"
    std::vector<uint8_t> data;
};

// How an ordered list level spells its number.
enum class RichNumberFormat {
    Decimal,        // 1 2 3
    DecimalZero,    // 01 02 ... 10
    LowerLetter,    // a b ... z aa
    UpperLetter,    // A B ... Z AA
    LowerRoman,     // i ii iii iv
    UpperRoman,     // I II III IV
    NoNumber        // the level shows no number of its own (not "None": X11 defines that)
};

// `number` spelled in `format` ("iv", "AB", "07"; "" for None).
std::string FormatListNumber(int number, RichNumberFormat format);

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

// ===== PARAGRAPH GEOMETRY =====
// Lengths are in points, the unit the formats use (ODF cm/in, Word twips) and
// the one run font sizes use, so a view keeps indents and tab stops in
// proportion to the text however it scales.

enum class RichTabKind {
    Left,       // text starts at the stop
    Center,     // text is centred on the stop
    Right,      // text ends at the stop
    Decimal     // the decimal separator sits on the stop (amounts in a column)
};

struct RichTabStop {
    float positionPt = 0.0f;   // from the text column's left edge (not the indent)
    RichTabKind kind = RichTabKind::Left;
};

enum class RichVerticalAlign { Top, Middle, Bottom };

// One side of a table cell's frame. widthPt 0 = no line.
struct RichBorder {
    float widthPt = 0.0f;
    std::string color;              // "#RRGGBB"; empty = automatic (black)
    bool IsVisible() const { return widthPt > 0.0f; }
    bool operator==(const RichBorder& other) const {
        return widthPt == other.widthPt && color == other.color;
    }
};

struct RichTableCell {
    std::vector<RichTextRun> runs;
    int columnSpan = 1;
    int rowSpan = 1;
    // Horizontal alignment of the cell's text: a column of amounts is
    // right-aligned, a date column centred. Default = the table's left.
    RichTextAlign align = RichTextAlign::Default;
    // The cell's frame and fill, as the document drew them. Only used when
    // the table's RichDocBlock::tableBordersFromDocument is set.
    RichBorder borderTop, borderBottom, borderLeft, borderRight;
    std::string backgroundColor;    // "#RRGGBB"; empty = none
    // Where the text sits in a cell taller than it, and the room between the
    // text and the cell's edges (points; < 0 = the view's default).
    RichVerticalAlign verticalAlign = RichVerticalAlign::Top;
    float paddingTopPt = -1.0f, paddingBottomPt = -1.0f, paddingLeftPt = -1.0f, paddingRightPt = -1.0f;

    // Copies borders and background (a new cell next to this one looks
    // like it).
    void CopyCellFormat(const RichTableCell& from) {
        borderTop = from.borderTop;
        borderBottom = from.borderBottom;
        borderLeft = from.borderLeft;
        borderRight = from.borderRight;
        backgroundColor = from.backgroundColor;
        verticalAlign = from.verticalAlign;
        paddingTopPt = from.paddingTopPt;
        paddingBottomPt = from.paddingBottomPt;
        paddingLeftPt = from.paddingLeftPt;
        paddingRightPt = from.paddingRightPt;
    }
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
    // Ordered ListItem: > 0 = the item carries this number, and the siblings
    // after it count on from it. 0 = count from the previous sibling (or 1).
    // Word processors keep one list's numbering running across paragraphs
    // placed between its items ("1. ... <note> 2. ..."), which the model's
    // flat block list cannot see on its own; readers set this on the first
    // item after such an interruption, and on a list that starts at N.
    int listStartNumber = 0;
    // Ordered ListItem: how the label reads. numberTemplate uses Word's
    // notation: %1..%9 stand for the number of list level 1..9 (level 1 =
    // listLevel 0), each in that level's numberFormat, around literal text -
    // "%1.%2." gives "2.3.", "(%1)" gives "(4)". Empty = "%<level>." with this
    // item's own number only, the view's default.
    RichNumberFormat numberFormat = RichNumberFormat::Decimal;
    std::string numberTemplate;
    // Unordered ListItem: the document's bullet (UTF-8, e.g. "–", "✓"). Empty
    // = the view's bullet for the level.
    std::string bulletText;

    // Paragraph geometry (Paragraph, Heading, BlockQuote, CodeBlock; list
    // items keep the view's own list indentation and use only the spacing).
    // Indents are from the text column's edges; firstLineIndentPt is relative
    // to leftIndentPt and negative for a hanging indent.
    float leftIndentPt = 0.0f;
    float rightIndentPt = 0.0f;
    float firstLineIndentPt = 0.0f;
    // Space above / below the paragraph. < 0 = not stated: the view uses its
    // own block spacing. Stated spacing is added, as Word and Writer do.
    float spaceBeforePt = -1.0f;
    float spaceAfterPt = -1.0f;
    // Line spacing as a multiple of single spacing (1.5 = one and a half
    // lines). 0 = single / not stated.
    float lineSpacing = 0.0f;
    // A fixed line height in points instead (Word "exactly" / "at least",
    // ODF fo:line-height="14pt" / style:line-height-at-least). 0 = not set.
    float lineHeightPt = 0.0f;
    bool lineHeightAtLeast = false;     // true: lines are at least this tall
    // Paragraph frame and fill. Consecutive paragraphs with the same frame
    // form one box, as in Word and Writer (no line between them).
    RichBorder paragraphBorderTop, paragraphBorderBottom, paragraphBorderLeft, paragraphBorderRight;
    std::string paragraphBackground;    // "#RRGGBB"; empty = none
    // Explicit tab stops, sorted by position. Beyond the last one, tabs fall
    // on UCRichDocument::defaultTabStopPt.
    std::vector<RichTabStop> tabStops;

    bool HasParagraphGeometry() const {
        return leftIndentPt != 0.0f || rightIndentPt != 0.0f || firstLineIndentPt != 0.0f
            || spaceBeforePt >= 0.0f || spaceAfterPt >= 0.0f || lineSpacing > 0.0f
            || lineHeightPt > 0.0f || HasParagraphFrame() || !tabStops.empty();
    }
    bool HasParagraphFrame() const {
        return paragraphBorderTop.IsVisible() || paragraphBorderBottom.IsVisible()
            || paragraphBorderLeft.IsVisible() || paragraphBorderRight.IsVisible()
            || !paragraphBackground.empty();
    }
    bool SameParagraphFrame(const RichDocBlock& other) const {
        return paragraphBorderTop == other.paragraphBorderTop
            && paragraphBorderBottom == other.paragraphBorderBottom
            && paragraphBorderLeft == other.paragraphBorderLeft
            && paragraphBorderRight == other.paragraphBorderRight
            && paragraphBackground == other.paragraphBackground
            && leftIndentPt == other.leftIndentPt && rightIndentPt == other.rightIndentPt;
    }
    // Copies indents, spacing, line spacing, frame and tab stops - what a
    // paragraph split in two (Enter) gives the new half.
    void CopyParagraphGeometry(const RichDocBlock& from) {
        leftIndentPt = from.leftIndentPt;
        rightIndentPt = from.rightIndentPt;
        firstLineIndentPt = from.firstLineIndentPt;
        spaceBeforePt = from.spaceBeforePt;
        spaceAfterPt = from.spaceAfterPt;
        lineSpacing = from.lineSpacing;
        lineHeightPt = from.lineHeightPt;
        lineHeightAtLeast = from.lineHeightAtLeast;
        paragraphBorderTop = from.paragraphBorderTop;
        paragraphBorderBottom = from.paragraphBorderBottom;
        paragraphBorderLeft = from.paragraphBorderLeft;
        paragraphBorderRight = from.paragraphBorderRight;
        paragraphBackground = from.paragraphBackground;
        tabStops = from.tabStops;
    }
    RichTextAlign align = RichTextAlign::Default;
    std::string codeLanguage;           // CodeBlock fence language hint
    std::vector<RichTableRow> tableRows;
    // Table: relative column widths (any unit - points as read), one per grid
    // column. Empty = equal columns. A renderer scales them to its width.
    std::vector<float> tableColumnWidths;
    // Table: true when the cells' borders and backgrounds come from a
    // document (ODT/DOCX/DOC), so a cell without borders really has none - a
    // layout table in a letterhead. false (Markdown, a table built in the
    // editor): the view draws its own grid.
    bool tableBordersFromDocument = false;
    // Table: its width and where it sits in the text column. tableWidthPt
    // > 0 = that many points; else tableWidthPercent > 0 = that share of the
    // column; else the whole column. A table narrower than the column is
    // placed by tableAlign (Default/Left: tableIndentPt from the left edge).
    float tableWidthPt = 0.0f;
    float tableWidthPercent = 0.0f;
    RichTextAlign tableAlign = RichTextAlign::Default;
    float tableIndentPt = 0.0f;
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

// The number an ordered ListItem at `index` displays: its own
// listStartNumber when it has one, else one more than the previous sibling at
// its level, counting back until a paragraph, a shallower item or a switch
// between ordered and bullet ends the list. 0 for anything that is not an
// ordered list item. The one definition renderers and readers share, so what
// a reader intends and what the view draws cannot disagree.
int RichDocOrderedItemNumber(const std::vector<RichDocBlock>& blocks, size_t index);

// The label an ordered ListItem at `index` displays ("3.", "b)", "1.2.",
// "iv."), built from its numberTemplate with each level's current number.
// An ancestor level's number is that of the nearest item of that level above
// (1 when the list has none). "" for anything that is not an ordered item.
std::string RichDocListLabel(const std::vector<RichDocBlock>& blocks, size_t index);

// Word-processor list numbering, as ODT, DOCX and DOC define it: one counter
// per list and nesting level. An item advances its level and restarts every
// deeper level, so a sublist begins again at its start value under each new
// parent item, while items of the same list keep counting across whatever
// paragraphs sit between them.
class RichListNumbering {
public:
    // Number of the next item of list `listKey` at `level` (0-based) whose
    // level starts at `startAt`.
    int Next(const std::string& listKey, int level, int startAt = 1);
    // Makes the next item of that list and level take `number`.
    void Restart(const std::string& listKey, int level, int number);
    // Stores `number` on the ordered item at `index` when the renderer's own
    // count (RichDocOrderedItemNumber) would show something else.
    static void Apply(std::vector<RichDocBlock>& blocks, size_t index, int number);

private:
    struct Counter { int value = 0; bool started = false; int restartAt = 0; };
    std::vector<std::pair<std::string, std::vector<Counter>>> lists_;
    std::vector<Counter>& LevelsOf(const std::string& listKey);
};

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
    // Distance between default tab stops (after a paragraph's own stops).
    // 0 = the view's default. ODF: style:tab-stop-distance; Word: defaultTabStop.
    float defaultTabStopPt = 0.0f;

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
