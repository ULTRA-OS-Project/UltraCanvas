// core/UltraCanvasRichTextEdit.cpp
// The WYSIWYG editing element. See UltraCanvasRichTextEdit.h for what it is
// and how it relates to UltraCanvasTextArea.
//
// Rendering model: one ITextLayout per block, holding the block's concatenated
// run text with each run's formatting applied as text attributes. Because the
// layout text and the editor's byte offsets are the same string, hit testing,
// caret geometry and selection painting need no translation layer.
//
// Version: 1.1.0
// Author: UltraCanvas Framework

#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasUI.h"
#include "UltraCanvasCaret.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPdfSurface.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cmath>
#include <fstream>

namespace UltraCanvas {

std::vector<RichDocBlock> UltraCanvasRichTextEdit::internalClipboard;
std::string UltraCanvasRichTextEdit::internalClipboardText;
const UCRichDocument* UltraCanvasRichTextEdit::internalClipboardSource = nullptr;
std::vector<RichDocMedia> UltraCanvasRichTextEdit::internalClipboardMedia;

namespace {

// Document lengths are points; the view draws in pixels. Text sizes go to
// Pango as points at the 96 DPI every Cairo context is pinned to
// (RenderContextCairo), so indents, tab stops, spacing and table widths have
// to be scaled the same way or they come out a quarter too small beside the
// text they belong to.
constexpr float kPixelsPerPoint = 96.0f / 72.0f;
inline float Px(float points) { return points * kPixelsPerPoint; }

// Room between a paragraph's text and its frame (and what the frame adds to
// the space around the paragraph).
constexpr float kParagraphFramePadding = 3.0f;

Color ParseHexColor(const std::string& hex, const Color& fallback) {
    if (hex.size() != 7 || hex[0] != '#') return fallback;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[6];
    for (int i = 0; i < 6; i++) {
        v[i] = digit(hex[static_cast<size_t>(i) + 1]);
        if (v[i] < 0) return fallback;
    }
    return Color(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
}

// One border line in its style: a dotted or dashed stroke, or for a double
// border two thin lines a line's width apart, together as wide as the border.
void DrawBorderLine(IRenderContext* ctx, const RichBorder& border, const Point2Dd& from,
                    const Point2Dd& to, const Color& color) {
    const double width = std::max(1.0, static_cast<double>(Px(border.widthPt)));
    ctx->PushState();
    switch (border.style) {
        case RichBorderStyle::Double: {
            const double line = std::max(1.0, width / 3.0);
            const double offset = std::max(1.0, width / 3.0);
            // Perpendicular to the line: sides are horizontal or vertical.
            const bool horizontal = std::abs(to.y - from.y) < std::abs(to.x - from.x);
            const double dx = horizontal ? 0.0 : offset, dy = horizontal ? offset : 0.0;
            ctx->SetStrokeWidth(line);
            ctx->DrawLine(Point2Dd(from.x - dx, from.y - dy), Point2Dd(to.x - dx, to.y - dy), color);
            ctx->DrawLine(Point2Dd(from.x + dx, from.y + dy), Point2Dd(to.x + dx, to.y + dy), color);
            break;
        }
        case RichBorderStyle::Dotted:
            ctx->SetStrokeWidth(width);
            ctx->SetLineDash(UCDashPattern({width, width * 1.5}));
            ctx->DrawLine(from, to, color);
            break;
        case RichBorderStyle::Dashed:
            ctx->SetStrokeWidth(width);
            ctx->SetLineDash(UCDashPattern({std::max(4.0, width * 3.0), std::max(3.0, width * 2.0)}));
            ctx->DrawLine(from, to, color);
            break;
        default:
            ctx->SetStrokeWidth(width);
            ctx->DrawLine(from, to, color);
            break;
    }
    ctx->PopState();
}

TextAlignment ToTextAlignment(RichTextAlign align) {
    switch (align) {
        case RichTextAlign::Center:  return TextAlignment::Center;
        case RichTextAlign::Right:   return TextAlignment::Right;
        case RichTextAlign::Justify: return TextAlignment::Justify;
        default:                     return TextAlignment::Left;
    }
}

} // namespace

// ===== CONSTRUCTION =====

UltraCanvasRichTextEdit::UltraCanvasRichTextEdit(const std::string& name, float x, float y,
                                                 float width, float height)
    : UltraCanvasUIElement(name, x, y, width, height) {
    style.baseFont.fontFamily = "";
    style.baseFont.fontSize = 14.0;

    editor.onChanged = [this]() {
        layoutsDirty = true;
        AnnounceAccessibility(AccessibilityEventType::TextChanged);
        // Editing a header or footer changes the document as it goes.
        if (furnitureEdit) SyncFurnitureToDocument();
        if (onDocumentChanged) onDocumentChanged();
    };
    editor.onSelectionChanged = [this]() {
        // Selection is painted as layout attributes, so a changed selection
        // invalidates the layouts it covers. Rebuilding all of them is only
        // the visible ones in practice (see EnsureLayouts).
        layoutsDirty = true;
        caretMoved = true;
        AnnounceAccessibility(editor.HasSelection() ? AccessibilityEventType::SelectionChanged
                                                    : AccessibilityEventType::CaretMoved);
        if (onSelectionChanged) onSelectionChanged();
    };
    SetMouseCursor(UCMouseCursor::Text);
    // Typing gets a word processor's corrections (smart quotes, dashes,
    // lists from "1. "...); SetAutoFormatEnabled(false) turns them off.
    editor.SetAutoFormatEnabled(true);
}

UltraCanvasRichTextEdit::~UltraCanvasRichTextEdit() {
    UltraCanvasCaret::GetInstance().Hide(this);
    // The spell notifier hops through the worker and the UI thread; clearing
    // this before the service is told to forget us closes both.
    spellAlive->store(false);
    if (spellContextId != 0) {
        UltraCanvasSpellChecker::Instance().CancelContext(spellContextId);
    }
}

// ===== DOCUMENT =====

void UltraCanvasRichTextEdit::SetDocument(std::shared_ptr<UCRichDocument> document) {
    if (furnitureEdit) FinishHeaderFooterEditing();
    editor.SetDocument(std::move(document));
    blockLayouts.clear();
    furnitureCache.clear();
    visibleAreaDirty = true;          // another page size
    scrollOffset = 0.0f;
    layoutsDirty = true;
    RequestRedraw();
}

void UltraCanvasRichTextEdit::SetMarkdown(const std::string& markdown,
                                          const std::string& baseDirectory) {
    auto document = std::make_shared<UCRichDocument>(
        UCRichDocument::FromMarkdown(markdown, baseDirectory));
    SetDocument(std::move(document));
}

void UltraCanvasRichTextEdit::Clear() {
    SetDocument(std::make_shared<UCRichDocument>());
}

void UltraCanvasRichTextEdit::InvalidateDocument() {
    layoutsDirty = true;
    blockLayouts.clear();
    furnitureCache.clear();
    RequestRedraw();
}

void UltraCanvasRichTextEdit::InvalidateBlock(int blockIndex) {
    if (blockIndex >= 0 && blockIndex < static_cast<int>(blockLayouts.size())) {
        blockLayouts[blockIndex].valid = false;
    }
    layoutsDirty = true;
    RequestRedraw();
}

void UltraCanvasRichTextEdit::SetReadOnly(bool value) {
    if (readOnly == value) return;
    readOnly = value;
    if (readOnly) UltraCanvasCaret::GetInstance().Hide(this);
    RequestRedraw();
}

void UltraCanvasRichTextEdit::SetPageView(bool enabled) {
    if (pageView == enabled) return;
    if (furnitureEdit) FinishHeaderFooterEditing();
    pageView = enabled;
    pages.clear();
    visibleAreaDirty = true;
    InvalidateDocument();
}

void UltraCanvasRichTextEdit::SetStyle(const RichTextEditStyle& s) {
    style = s;
    InvalidateDocument();
}

// ===== LAYOUT =====

void UltraCanvasRichTextEdit::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
    UltraCanvasUIElement::Arrange(finalRect, ctx);
    visibleAreaDirty = true;
}

// The visible area is kept in document space - the element's pixels divided
// by the zoom - which is the space everything is laid out and hit-tested in.
// Its origin is the same in both spaces; drawing scales about it.
void UltraCanvasRichTextEdit::RecalculateVisibleArea() {
    Rect2Df bounds = GetLocalBounds();
    const float bar = style.scrollbarWidth;
    float availableHeight = bounds.height - 2 * style.padding;
    const float vertical = (contentHeight * zoom > availableHeight) ? bar : 0.0f;
    const float pane = commentPaneShown ? style.commentPaneWidth : 0.0f;
    const float availableWidth = std::max(40.0f, bounds.width - 2 * style.padding - vertical - pane);
    // A page (and the desk either side of it) wider than the view scrolls
    // sideways.
    needsHorizontalScrollbar = false;
    if (pageView) {
        const float pageWidth = Px(EffectivePageSetup().widthPt) + 2.0f * style.pageGap;
        needsHorizontalScrollbar = pageWidth * zoom > availableWidth + 0.5f;
    }
    if (needsHorizontalScrollbar) availableHeight -= bar;
    visibleArea = Rect2Df(bounds.x + style.padding,
                          bounds.y + style.padding,
                          std::max(1.0f, availableWidth / zoom),
                          std::max(1.0f, availableHeight / zoom));
    visibleAreaDirty = false;
    UpdateColumnGeometry();
    hScrollOffset = std::clamp(hScrollOffset, 0.0f, MaxHorizontalScroll());
}

float UltraCanvasRichTextEdit::ContentWidth() const {
    return pageView ? pageWidthPx + 2.0f * style.pageGap : visibleArea.width;
}

float UltraCanvasRichTextEdit::MaxHorizontalScroll() const {
    return std::max(0.0f, ContentWidth() - visibleArea.width);
}

Point2Df UltraCanvasRichTextEdit::ToDocument(const Point2Df& p) const {
    return Point2Df(visibleArea.x + (p.x - visibleArea.x) / zoom, visibleArea.y + (p.y - visibleArea.y) / zoom);
}

Point2Df UltraCanvasRichTextEdit::ToDocument(const Point2Di& p) const {
    return ToDocument(Point2Df(static_cast<float>(p.x), static_cast<float>(p.y)));
}

Rect2Df UltraCanvasRichTextEdit::ToElement(const Rect2Df& r) const {
    return Rect2Df(visibleArea.x + (r.x - visibleArea.x) * zoom, visibleArea.y + (r.y - visibleArea.y) * zoom,
                   r.width * zoom, r.height * zoom);
}

void UltraCanvasRichTextEdit::SetZoom(float factor) {
    factor = std::clamp(factor, 0.25f, 5.0f);
    if (std::abs(factor - zoom) < 0.001f) return;
    // The scroll offset is in document pixels, so the view keeps its place.
    zoom = factor;
    visibleAreaDirty = true;
    InvalidateDocument();
    caretMoved = true;
    if (onZoomChanged) onZoomChanged(zoom);
}

void UltraCanvasRichTextEdit::SetHorizontalScrollOffset(float offset) {
    const float clamped = std::clamp(offset, 0.0f, MaxHorizontalScroll());
    if (std::abs(clamped - hScrollOffset) < 0.01f) return;
    hScrollOffset = clamped;
    RequestRedraw();
}

// ===== PAGES =====

// The document's page, or A4 portrait with 2 cm margins for a document that
// states none (Markdown, plain text) - what Writer gives a new document.
RichPageSetup UltraCanvasRichTextEdit::EffectivePageSetup() const {
    const std::shared_ptr<UCRichDocument>& document = editor.GetDocument();
    if (document && document->page.HasPage()) return document->page;
    RichPageSetup a4;
    a4.widthPt = 595.28f;
    a4.heightPt = 841.89f;
    a4.marginTopPt = a4.marginBottomPt = a4.marginLeftPt = a4.marginRightPt = 56.69f;
    a4.headerTopPt = a4.footerBottomPt = 56.69f;
    return a4;
}

// Where the text column is. In page view it is the page's, between its side
// margins, with the page centred on the desk (or at its left edge when the
// element is narrower than the page); otherwise the whole visible width.
void UltraCanvasRichTextEdit::UpdateColumnGeometry() {
    if (!pageView) {
        columnOffsetX = 0.0f;
        columnWidth = visibleArea.width;
        pageLeftX = 0.0f;
        pageWidthPx = pageHeightPx = 0.0f;
        return;
    }
    const RichPageSetup page = EffectivePageSetup();
    pageWidthPx = Px(page.widthPt);
    pageHeightPx = Px(page.heightPt);
    // Centred on the desk; when the page is wider than the view it starts a
    // desk's width in and the view scrolls sideways over it.
    pageLeftX = std::max(style.pageGap, (visibleArea.width - pageWidthPx) * 0.5f);
    const float left = Px(std::max(0.0f, page.marginLeftPt));
    const float right = Px(std::max(0.0f, page.marginRightPt));
    columnOffsetX = pageLeftX + left;
    columnWidth = std::max(24.0f, pageWidthPx - left - right);
}

namespace {

// A header or footer with its page fields spelt out for one page.
std::vector<RichDocBlock> FillPageFields(const std::vector<RichDocBlock>& blocks,
                                         int pageNumber, int pageCount, bool& hasFields) {
    std::vector<RichDocBlock> out = blocks;
    auto fill = [&](std::vector<RichTextRun>& runs) {
        for (RichTextRun& run : runs) {
            if (run.field == RichTextRun::Field::PageNumber) {
                run.text = std::to_string(pageNumber);
                hasFields = true;
            } else if (run.field == RichTextRun::Field::PageCount) {
                run.text = std::to_string(pageCount);
                hasFields = true;
            }
        }
    };
    for (RichDocBlock& block : out) {
        fill(block.runs);
        for (RichTableRow& row : block.tableRows) {
            for (RichTableCell& cell : row.cells) fill(cell.runs);
        }
    }
    return out;
}

bool HasPageFields(const std::vector<RichDocBlock>& blocks) {
    bool hasFields = false;
    FillPageFields(blocks, 1, 1, hasFields);
    return hasFields;
}

} // namespace

// Lays out a header or footer for one page. Layouts are cached by content and
// page, so only a furniture with page fields is laid out once per page.
std::shared_ptr<UltraCanvasRichTextEdit::FurnitureLayout> UltraCanvasRichTextEdit::LayoutFurniture(
        IRenderContext* ctx, const std::vector<RichDocBlock>& blocks, int pageNumber, int pageCount) {
    if (blocks.empty()) return nullptr;
    if (furnitureCacheWidth != ColumnWidth()) {
        furnitureCache.clear();
        furnitureCacheWidth = ColumnWidth();
    }
    const bool numbered = HasPageFields(blocks);
    const std::string key = std::to_string(reinterpret_cast<uintptr_t>(&blocks)) + "|"
                          + (numbered ? std::to_string(pageNumber) + "|" + std::to_string(pageCount) : "");
    auto found = furnitureCache.find(key);
    if (found != furnitureCache.end()) return found->second;

    auto furniture = std::make_shared<FurnitureLayout>();
    bool hasFields = false;
    furniture->blocks = FillPageFields(blocks, pageNumber, pageCount, hasFields);
    furniture->layouts.resize(furniture->blocks.size());
    float y = 0.0f;
    for (size_t i = 0; i < furniture->blocks.size(); i++) {
        BlockLayout& bl = furniture->layouts[i];
        BuildBlockLayout(ctx, furniture->blocks, static_cast<int>(i), bl, -1);
        bl.bounds.x = bl.textLeft;
        bl.bounds.y = y;
        y += bl.bounds.height + GapAfterBlock(furniture->blocks, static_cast<int>(i));
    }
    furniture->height = y;
    furnitureCache[key] = furniture;
    return furniture;
}

// Page view: blocks go onto pages in order, and one that does not fit in
// what is left of a page's text area moves whole to the next page; a page
// break block ends its page. The space between two blocks is dropped at the
// top of a page. Each page's text area lies between its margins, pushed in
// further when a header or footer is taller than the margin leaves room for.
// Returns the content height.
float UltraCanvasRichTextEdit::PlaceBlocksOnPages(IRenderContext* ctx) {
    const UCRichDocument& document = *editor.GetDocument();
    const RichPageSetup page = EffectivePageSetup();
    const float gap = style.pageGap;
    const int blockCount = editor.GetBlockCount();

    // The frame of page `index`, header and footer included. The page count
    // is not known until the last block is placed, so furniture is laid for
    // `count` and the pages are re-framed once it is known.
    auto frameFor = [&](int index, int count) {
        PageFrame frame;
        frame.top = gap + static_cast<float>(index) * (pageHeightPx + gap);
        const RichPageFurniture& furniture = document.FurnitureForPage(index);
        frame.header = LayoutFurniture(ctx, furniture.header, index + 1, count);
        frame.footer = LayoutFurniture(ctx, furniture.footer, index + 1, count);
        const float spacing = Px(7.0f);           // 0.25 cm, Writer's header spacing
        frame.headerTop = frame.top + Px(page.headerTopPt > 0.0f ? page.headerTopPt : page.marginTopPt * 0.5f);
        frame.bodyTop = frame.top + Px(page.marginTopPt);
        if (frame.header) frame.bodyTop = std::max(frame.bodyTop, frame.headerTop + frame.header->height + spacing);
        const float pageBottom = frame.top + pageHeightPx;
        const float footerBottom = pageBottom - Px(page.footerBottomPt > 0.0f ? page.footerBottomPt
                                                                            : page.marginBottomPt * 0.5f);
        frame.bodyBottom = pageBottom - Px(page.marginBottomPt);
        frame.footerTop = footerBottom;
        if (frame.footer) {
            frame.footerTop = footerBottom - frame.footer->height;
            frame.bodyBottom = std::min(frame.bodyBottom, frame.footerTop - spacing);
        }
        // The page's footnotes sit at the foot of its text area.
        if (index < static_cast<int>(pageFootnoteRoom.size())) {
            frame.bodyBottom -= pageFootnoteRoom[static_cast<size_t>(index)];
        }
        // A page always keeps some room for text, whatever its furniture.
        frame.bodyBottom = std::max(frame.bodyBottom, frame.bodyTop + 24.0f);
        return frame;
    };

    // Two passes when the page count changes what the furniture says ("Page
    // 1 / 3" can wrap differently from "Page 1 / 12"); usually one.
    int count = std::max(1, static_cast<int>(pages.size()));
    for (int pass = 0; pass < 2; pass++) {
        pages.clear();
        pages.push_back(frameFor(0, count));
        float y = pages.back().bodyTop;
        bool pageEmpty = true;
        bool breakPending = false;
        placedFloats.clear();
        noteAreas.clear();
        std::vector<PlacedFloat> pageFloats;      // floats of the current page
        // Sections of columns: blocks fill a column to the foot of the page,
        // then the next column from the section's top, then the next page.
        RichSectionSetup section = document.firstSection;
        int columns = hasColumns ? std::max(1, section.columns) : 1;
        int column = 0;
        float columnTop = y;                      // where this page's columns of the section start
        float sectionBottom = y;                  // the lowest any of them reaches
        bool sectionFresh = false;                // at a section's top: no space before its first block
        auto columnX = [&]() {
            if (columns <= 1) return 0.0f;
            const float gap = Px(section.columnGapPt);
            const float width = (columnWidth - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
            return static_cast<float>(column) * (width + gap);
        };
        auto newPage = [&]() {
            placedFloats.insert(placedFloats.end(), pageFloats.begin(), pageFloats.end());
            pageFloats.clear();
            pages.push_back(frameFor(static_cast<int>(pages.size()), count));
            y = pages.back().bodyTop;
            pageEmpty = true;
            column = 0;
            columnTop = sectionBottom = y;
        };
        auto nextColumn = [&]() {
            if (column + 1 < columns) {
                column++;
                y = columnTop;
                pageEmpty = true;
            } else {
                newPage();
            }
        };
        for (int i = 0; i < blockCount; i++) {
            BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
            bl.slices.clear();
            const RichDocBlock& sourceBlock = editor.GetBlock(i);
            if (i > 0 && sourceBlock.sectionStart) {
                // A new section: on a new page, or below all of the last one.
                section = sourceBlock.section;
                columns = hasColumns ? std::max(1, section.columns) : 1;
                column = 0;
                if (section.newPage && !pageEmpty) {
                    breakPending = true;
                } else {
                    // Every column of the section starts at the same height.
                    y = std::max(y, sectionBottom) + (pageEmpty ? 0.0f : GapAfterBlock(i - 1));
                    columnTop = sectionBottom = y;
                    sectionFresh = true;
                }
            }
            const float before = (!pageEmpty && !sectionFresh && i > 0) ? GapAfterBlock(i - 1) : 0.0f;
            sectionFresh = false;
            // Fitted round this page's pictures first: that can change its
            // height, and so whether it fits.
            float top = pageEmpty ? y : y + before;
            if (breakPending) {
                newPage();
                breakPending = false;
                top = FlowAroundFloats(ctx, i, y, pageFloats);
            } else {
                std::vector<PlacedFloat> trial = pageFloats;
                top = FlowAroundFloats(ctx, i, top, trial);
                if (pageEmpty || top + bl.bounds.height <= pages.back().bodyBottom) pageFloats = std::move(trial);
            }
            // A heading keeps with the start of what follows it: alone at the
            // foot of a page it goes to the next one.
            float keepWithNext = 0.0f;
            if (editor.GetBlock(i).type == RichBlockType::Heading && i + 1 < blockCount) {
                const std::vector<float> next = PageBreakCandidates(i + 1);
                const float nextHeight = blockLayouts[static_cast<size_t>(i + 1)].bounds.height;
                keepWithNext = GapAfterBlock(i) + (next.empty() ? std::min(nextHeight, pageHeightPx * 0.25f) : next.front());
            }
            const float bodyBottom = pages.back().bodyBottom;
            if (columns > 1 && top + bl.bounds.height > bodyBottom) {
                // In columns a block moves whole to the next column, or on
                // to the next page; one taller than a whole column stays at
                // the top of a page's first column and runs past its foot.
                for (int guard = 0; guard < 2 * columns + 2 && top + bl.bounds.height > pages.back().bodyBottom
                                    && !(column == 0 && std::abs(y - pages.back().bodyTop) < 0.5f); guard++) {
                    nextColumn();
                    top = FlowAroundFloats(ctx, i, y, pageFloats);
                }
            } else if (top + bl.bounds.height + keepWithNext > bodyBottom) {
                std::vector<float> candidates = keepWithNext > 0.0f ? std::vector<float>{} : PageBreakCandidates(i);
                const float available = bodyBottom - top;
                const bool someFits = !candidates.empty() && candidates.front() <= available;
                if (!someFits && !pageEmpty) {
                    // Nothing of it fits here: it starts on the next page.
                    newPage();
                    top = FlowAroundFloats(ctx, i, y, pageFloats);
                }
                // Still longer than what is left: it runs on over pages.
                if (top + bl.bounds.height > pages.back().bodyBottom && keepWithNext <= 0.0f) {
                    if (candidates.empty() && bl.layout) {
                        // Too short for widow and orphan control, but too tall
                        // for a page: break between any two lines.
                        for (const LayoutLineExtent& line : bl.layout->GetLineExtents()) {
                            if (line.top > 0.0f) candidates.push_back(line.top);
                        }
                    }
                    float from = 0.0f;
                    while (!candidates.empty()) {
                        const float header = bl.slices.empty() ? 0.0f : bl.headerRowsHeight;
                        const float room = pages.back().bodyBottom - top - header;
                        if (bl.bounds.height - from <= room) break;
                        float cut = -1.0f;
                        for (float c : candidates) {
                            // The header rows alone on a page are no use.
                            if (bl.slices.empty() && c <= bl.headerRowsHeight + 0.5f) continue;
                            if (c > from + 0.5f && c - from <= room) cut = c;
                        }
                        // Not even one piece fits a whole page: take the
                        // smallest and let it overflow.
                        if (cut < 0.0f) {
                            for (float c : candidates) {
                                if (c > from + 0.5f) { cut = c; break; }
                            }
                        }
                        if (cut < 0.0f) break;
                        bl.slices.push_back({from, cut, top, header});
                        from = cut;
                        newPage();
                        top = y;
                    }
                    if (!bl.slices.empty()) {
                        const float header = bl.headerRowsHeight;
                        bl.slices.push_back({from, bl.bounds.height, top, header});
                    }
                }
            }
            bl.bounds.y = bl.slices.empty() ? top : bl.slices.front().top;
            bl.bounds.x = bl.textLeft;
            bl.columnX = columnX();
            y = BlockVisualBottom(bl);
            sectionBottom = std::max(sectionBottom, y);
            pageEmpty = false;
            if (editor.GetBlock(i).type == RichBlockType::PageBreak) breakPending = true;
        }
        // Endnotes after the body (below all of its columns), going on to
        // further pages as they need.
        y = std::max(y, sectionBottom);
        const std::vector<std::string> marks = document.NoteMarks();
        std::vector<bool> placed(document.notes.size(), false);
        const float ruleSpace = NoteRuleSpace();
        bool firstEndnote = true;
        for (const UCRichDocument::NoteReference& reference : document.NoteReferences()) {
            const size_t note = static_cast<size_t>(reference.noteIndex);
            if (placed[note] || document.notes[note].kind != RichNote::Kind::Endnote) continue;
            placed[note] = true;
            NoteArea area;
            area.noteIndex = reference.noteIndex;
            area.layout = LayoutNote(ctx, reference.noteIndex, marks[note]);
            if (!area.layout) continue;
            area.ruleAbove = firstEndnote;
            const float above = firstEndnote ? ruleSpace : style.blockSpacing;
            float top = pageEmpty ? y : y + above;
            if (breakPending || (!pageEmpty && top + area.layout->height > pages.back().bodyBottom)) {
                newPage();
                breakPending = false;
                top = y + (firstEndnote ? ruleSpace : 0.0f);
            }
            area.page = static_cast<int>(pages.size()) - 1;
            area.top = top;
            y = top + area.layout->height;
            pageEmpty = false;
            firstEndnote = false;
            noteAreas.push_back(std::move(area));
        }
        placedFloats.insert(placedFloats.end(), pageFloats.begin(), pageFloats.end());
        if (static_cast<int>(pages.size()) == count) break;
        count = static_cast<int>(pages.size());
    }
    return pages.back().top + pageHeightPx + gap;
}

float UltraCanvasRichTextEdit::FlowAroundFloats(IRenderContext* ctx, int index, float top,
                                                std::vector<PlacedFloat>& pageFloats) {
    BlockLayout& bl = blockLayouts[static_cast<size_t>(index)];
    const float gap = Px(9.0f);                 // Word's default distance from text
    // A block starting beside a picture text may not pass goes below it.
    for (bool moved = true; moved;) {
        moved = false;
        for (const PlacedFloat& placed : pageFloats) {
            if (placed.beside || placed.wrap == RichTextRun::ImageWrap::BehindText
                || placed.wrap == RichTextRun::ImageWrap::InFrontOfText) continue;
            if (top >= placed.rect.y - 0.5f && top < placed.rect.y + placed.rect.height + gap) {
                top = placed.rect.y + placed.rect.height + gap;
                moved = true;
            }
        }
    }
    // The block's own floating pictures, placed at its top.
    bool ownBelow = false;
    float ownBottom = top;
    for (const BlockLayout::InlineImage& image : bl.inlineImages) {
        if (!image.floating) continue;
        PlacedFloat placed;
        placed.blockIndex = index;
        placed.byteOffset = image.byteOffset;
        placed.wrap = image.wrap;
        placed.image = image.image;
        const float column = ColumnWidth();
        float x = 0.0f;
        switch (image.floatAlign) {
            case RichTextAlign::Right:  x = column - image.width; break;
            case RichTextAlign::Center: x = (column - image.width) * 0.5f; break;
            case RichTextAlign::Default: x = image.offsetX; break;
            default: x = 0.0f; break;
        }
        x = std::clamp(x, 0.0f, std::max(0.0f, column - image.width));
        placed.rect = Rect2Df(x, top + image.offsetY, image.width, image.height);
        // Text passes beside a square-wrapped picture at a side of the
        // column; a centred one leaves no useful room either side.
        placed.beside = image.wrap == RichTextRun::ImageWrap::Square && image.floatAlign != RichTextAlign::Center;
        if (image.wrap == RichTextRun::ImageWrap::TopAndBottom || (!placed.beside
            && image.wrap == RichTextRun::ImageWrap::Square)) {
            ownBelow = true;
            ownBottom = std::max(ownBottom, placed.rect.y + placed.rect.height + gap);
        }
        pageFloats.push_back(std::move(placed));
    }
    // Its text starts below its own top-and-bottom picture.
    if (ownBelow) top = ownBottom;

    // Beside square-wrapped pictures, the block is narrowed.
    float left = 0.0f, right = 0.0f;
    const float height = std::max(bl.bounds.height, 1.0f);
    for (const PlacedFloat& placed : pageFloats) {
        if (!placed.beside) continue;
        if (placed.rect.y + placed.rect.height <= top || placed.rect.y >= top + height) continue;
        const float middle = placed.rect.x + placed.rect.width * 0.5f;
        if (middle < ColumnWidth() * 0.5f) left = std::max(left, placed.rect.x + placed.rect.width + gap);
        else right = std::max(right, ColumnWidth() - placed.rect.x + gap);
    }
    // Never so narrow that nothing fits: then the text goes below instead.
    if (left + right > ColumnWidth() - 48.0f) {
        float below = top;
        for (const PlacedFloat& placed : pageFloats) {
            if (placed.beside && placed.rect.y < top + height) below = std::max(below, placed.rect.y + placed.rect.height + gap);
        }
        top = below;
        left = right = 0.0f;
    }
    if (std::abs(left - bl.intrudeLeft) > 0.5f || std::abs(right - bl.intrudeRight) > 0.5f) {
        bl.intrudeLeft = left;
        bl.intrudeRight = right;
        if (bl.valid) BuildBlockLayout(ctx, index);
        else bl.textLeft = std::max(0.0f, BlockIndentFor(editor.GetBlock(index)) + left);
    }
    return top;
}

void UltraCanvasRichTextEdit::DrawFloats(IRenderContext* ctx, bool behindText) {
    const float viewTop = scrollOffset, viewBottom = scrollOffset + visibleArea.height;
    for (const PlacedFloat& placed : placedFloats) {
        if ((placed.wrap == RichTextRun::ImageWrap::BehindText) != behindText) continue;
        if (placed.rect.y + placed.rect.height < viewTop || placed.rect.y > viewBottom) continue;
        const Rect2Dd target(ColumnLeft() + placed.rect.x, visibleArea.y + placed.rect.y - scrollOffset,
                             placed.rect.width, placed.rect.height);
        if (placed.image) {
            ctx->DrawImage(*placed.image, target, ImageFitMode::Contain);
        } else {
            ctx->DrawFilledRectangle(target, Colors::Transparent, 1.0f, style.imagePlaceholderColor);
        }
    }
}

float UltraCanvasRichTextEdit::BlockToContentY(const BlockLayout& bl, float layoutY) const {
    if (bl.slices.empty()) return bl.bounds.y + layoutY;
    for (size_t i = 0; i < bl.slices.size(); i++) {
        const BlockLayout::PageSlice& slice = bl.slices[i];
        if (layoutY < slice.to || i + 1 == bl.slices.size()) {
            return slice.top + slice.headerHeight + std::max(0.0f, layoutY - slice.from);
        }
    }
    return bl.bounds.y + layoutY;
}

float UltraCanvasRichTextEdit::ContentToBlockY(const BlockLayout& bl, float contentY) const {
    if (bl.slices.empty()) return contentY - bl.bounds.y;
    for (size_t i = 0; i < bl.slices.size(); i++) {
        const BlockLayout::PageSlice& slice = bl.slices[i];
        const float bottom = slice.top + slice.headerHeight + (slice.to - slice.from);
        const float nextTop = i + 1 < bl.slices.size() ? bl.slices[i + 1].top : bottom;
        // A point in the gap between two pages belongs to the nearer piece.
        if (contentY < (bottom + nextTop) * 0.5f || i + 1 == bl.slices.size()) {
            if (contentY < slice.top + slice.headerHeight) {
                // On a repeated header: the header rows themselves.
                return i == 0 ? contentY - slice.top : std::max(0.0f, contentY - slice.top);
            }
            return slice.from + std::min(contentY - slice.top - slice.headerHeight, slice.to - slice.from);
        }
    }
    return contentY - bl.bounds.y;
}

float UltraCanvasRichTextEdit::BlockVisualBottom(const BlockLayout& bl) const {
    if (bl.slices.empty()) return bl.bounds.y + bl.bounds.height;
    const BlockLayout::PageSlice& last = bl.slices.back();
    return last.top + last.headerHeight + (last.to - last.from);
}

std::vector<float> UltraCanvasRichTextEdit::PageBreakCandidates(int index) const {
    std::vector<float> candidates;
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(index)];
    if (!bl.valid) return candidates;
    if (!bl.rowBreaks.empty()) return bl.rowBreaks;
    if (!bl.layout) return candidates;
    const std::vector<LayoutLineExtent> lines = bl.layout->GetLineExtents();
    const int count = static_cast<int>(lines.size());
    // Two lines at least stay together at the bottom of a page (no orphan)
    // and at the top of the next (no widow).
    for (int k = 2; k + 2 <= count; k++) candidates.push_back(lines[static_cast<size_t>(k)].top);
    return candidates;
}

int UltraCanvasRichTextEdit::PageIndexAt(float contentY) const {
    if (!pageView || pages.empty()) return 0;
    for (size_t p = 0; p < pages.size(); p++) {
        if (contentY < pages[p].top + pageHeightPx + style.pageGap * 0.5f) return static_cast<int>(p);
    }
    return static_cast<int>(pages.size()) - 1;
}

// Sets every page field in the body to the page its block is on. The value
// lives in the run's text (the model's "value it was last shown with"), so
// plain-text output and a save carry it too. Not an edit: no undo step, and
// the document is not marked modified. True when any field changed.
bool UltraCanvasRichTextEdit::UpdateBodyPageFields() {
    const std::shared_ptr<UCRichDocument>& document = editor.GetDocument();
    if (!document) return false;
    const std::string count = std::to_string(std::max<size_t>(1, pages.size()));
    bool changed = false;
    for (size_t i = 0; i < document->blocks.size() && i < blockLayouts.size(); i++) {
        RichDocBlock& block = document->blocks[i];
        const std::string page = std::to_string(PageIndexAt(blockLayouts[i].bounds.y) + 1);
        bool blockChanged = false;
        auto fill = [&](std::vector<RichTextRun>& runs) {
            for (RichTextRun& run : runs) {
                const std::string* value = run.field == RichTextRun::Field::PageNumber ? &page
                                         : run.field == RichTextRun::Field::PageCount ? &count : nullptr;
                if (value && run.text != *value) {
                    run.text = *value;
                    blockChanged = true;
                }
            }
        };
        fill(block.runs);
        for (RichTableRow& row : block.tableRows) {
            for (RichTableCell& cell : row.cells) fill(cell.runs);
        }
        if (blockChanged) {
            blockLayouts[i].valid = false;
            changed = true;
        }
    }
    // Cross-references to a page, and the table of contents' page numbers.
    std::vector<int> blockPages(std::min(document->blocks.size(), blockLayouts.size()));
    for (size_t i = 0; i < blockPages.size(); i++) blockPages[i] = PageIndexAt(blockLayouts[i].bounds.y) + 1;
    if (document->UpdatePageReferences(blockPages)) {
        for (size_t i = 0; i < blockLayouts.size(); i++) blockLayouts[i].valid = false;
        changed = true;
    }
    if (changed) {
        // A caret past a number that got shorter is brought back inside it.
        const RichDocPosition caret = editor.GetCaret();
        const RichDocPosition clamped = editor.ClampPosition(caret);
        if (clamped != caret) editor.SetCaret(clamped, false);
    }
    return changed;
}

// Continuous view: one column, with the first page's header above the body
// and its footer below it (so a letterhead still shows its bank lines).
float UltraCanvasRichTextEdit::PlaceBlocksInColumn(IRenderContext* ctx) {
    const UCRichDocument& document = *editor.GetDocument();
    const RichPageFurniture& furniture = document.FurnitureForPage(0);
    pages.clear();
    PageFrame frame;
    frame.header = LayoutFurniture(ctx, furniture.header, 1, 1);
    frame.footer = LayoutFurniture(ctx, furniture.footer, 1, 1);
    const float separation = style.blockSpacing * 3.0f;
    float y = frame.header ? frame.header->height + separation : 0.0f;
    frame.bodyTop = y;
    const int blockCount = editor.GetBlockCount();
    placedFloats.clear();
    std::vector<PlacedFloat> floats;
    for (int i = 0; i < blockCount; i++) {
        BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
        y = FlowAroundFloats(ctx, i, y, floats);
        bl.bounds.x = bl.textLeft;
        bl.columnX = 0.0f;
        bl.bounds.y = y;
        y += bl.bounds.height + GapAfterBlock(i);
    }
    // The column ends below its last picture too.
    for (const PlacedFloat& placed : floats) y = std::max(y, placed.rect.y + placed.rect.height);
    placedFloats = std::move(floats);
    // Footnotes and endnotes follow the text.
    noteAreas.clear();
    y = PlaceEndnotes(ctx, y, /*footnotesToo*/ true);
    frame.bodyBottom = y;
    if (frame.footer) {
        frame.footerTop = y + separation;
        y = frame.footerTop + frame.footer->height;
    }
    if (frame.header || frame.footer) pages.push_back(std::move(frame));
    return y;
}

float UltraCanvasRichTextEdit::QuoteInset(const RichDocBlock& block) const {
    return style.quoteIndent * static_cast<float>(std::max(0, block.quoteLevel));
}

float UltraCanvasRichTextEdit::BlockIndentFor(const RichDocBlock& block) const {
    // A document's own left indent adds to the view's indent for the block's
    // kind. List items keep the view's list indentation only (see the model).
    // Every kind of block moves right by its quote levels, a table or a
    // picture as much as a paragraph.
    const float documentIndent = Px(std::max(0.0f, block.leftIndentPt));
    const float quote = QuoteInset(block);
    switch (block.type) {
        case RichBlockType::ListItem:
            return quote + style.listIndent * static_cast<float>(block.listLevel + 1);
        case RichBlockType::BlockQuote:
            return quote + style.quoteIndent + documentIndent;
        case RichBlockType::CodeBlock:
            return quote + style.codeIndent + documentIndent;
        case RichBlockType::Paragraph:
        case RichBlockType::Heading:
        case RichBlockType::MathBlock:
            return quote + documentIndent;
        default:
            return quote;
    }
}

// One bar per quote level at the block's left, like a mail program's quoted
// text. A bar reaches down across the gap to the next block while that one
// is still inside the same quote, so a quoted reply reads as one column.
void UltraCanvasRichTextEdit::DrawQuoteBars(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks,
                                            int index, const BlockLayout& bl,
                                            float originX, float originY) const {
    const RichDocBlock& block = blocks[static_cast<size_t>(index)];
    if (block.quoteLevel <= 0) return;
    const size_t next = static_cast<size_t>(index) + 1;
    const int nextLevel = next < blocks.size() ? blocks[next].quoteLevel : 0;
    const float gap = GapAfterBlock(blocks, index);
    for (int level = 0; level < block.quoteLevel; ++level) {
        const float height = bl.bounds.height + (level < nextLevel ? gap : 0.0f);
        ctx->DrawFilledRectangle(Rect2Dd(originX + style.quoteIndent * static_cast<float>(level) + 1.0f,
                                         originY, 3.0, height),
                                 style.quoteBarColor, 0.0f, Colors::Transparent);
    }
}

// The number or bullet matches the item's own text: an 11 pt list must not
// carry 14 pt numbers.
FontStyle UltraCanvasRichTextEdit::MarkerFontFor(const RichDocBlock& block) const {
    FontStyle font = style.baseFont;
    for (const RichTextRun& run : block.runs) {
        if (run.IsInlineImage()) continue;
        if (run.fontSizePt > 0.0f) font.fontSize = run.fontSizePt;
        if (!run.fontFamily.empty()) font.fontFamily = run.fontFamily;
        break;
    }
    return font;
}

Rect2Df UltraCanvasRichTextEdit::CheckboxRect(const RichDocBlock& block, const BlockLayout& bl) const {
    const float size = std::max(8.0f, Px(static_cast<float>(MarkerFontFor(block).fontSize)) * 0.7f);
    float lineHeight = size * 1.4f;
    if (bl.layout) {
        const int first = bl.layout->GetCursorPos(0).strongPos.height;
        if (first > 0) lineHeight = static_cast<float>(first);
    }
    const float gap = std::max(4.0f, size * 0.5f);
    const float x = std::max(0.0f, std::min(bl.markerLeft, bl.textLeft - gap - size));
    return Rect2Df(x, std::max(0.0f, (lineHeight - size) * 0.5f), size, size);
}

int UltraCanvasRichTextEdit::CheckboxAtPoint(const Point2Df& localPoint) const {
    const float contentY = localPoint.y - visibleArea.y + scrollOffset;
    const float contentX = localPoint.x - ColumnLeft();
    const auto& blocks = editor.GetDocument()->blocks;
    for (size_t i = 0; i < blockLayouts.size() && i < blocks.size(); i++) {
        const BlockLayout& bl = blockLayouts[i];
        if (!bl.checkbox || !bl.valid) continue;
        if (contentY < bl.bounds.y - 4.0f || contentY > bl.bounds.y + bl.bounds.height) continue;
        Rect2Df box = CheckboxRect(blocks[i], bl);
        box.x += bl.columnX;
        // A little slack around the box: it is small, and a near miss should
        // still tick it rather than put the caret before the text.
        if (contentX >= box.x - 3.0f && contentX <= box.x + box.width + 3.0f
            && contentY >= bl.bounds.y + box.y - 3.0f && contentY <= bl.bounds.y + box.y + box.height + 3.0f) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Width of the widest label among the ordered items of `blockIndex`'s list
// level - the siblings before and after it in the same list, up to a
// shallower item or the end of the list. Bounded, so a very long list costs
// no more than a few hundred measurements.
float UltraCanvasRichTextEdit::WidestSiblingLabel(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks,
                                                  int blockIndex) const {
    const RichDocBlock& block = blocks[static_cast<size_t>(blockIndex)];
    float widest = 0.0f;
    auto measure = [&](size_t i) {
        const RichDocBlock& sibling = blocks[i];
        ctx->PushState();
        ctx->SetFontStyle(MarkerFontFor(sibling));
        widest = std::max(widest, static_cast<float>(ctx->GetTextLineWidth(RichDocListLabel(blocks, i))));
        ctx->PopState();
    };
    // A sibling with another label format belongs to another list.
    auto sameList = [&](const RichDocBlock& other) {
        return other.numberFormat == block.numberFormat && other.numberTemplate == block.numberTemplate;
    };
    const size_t limit = 200;
    size_t seen = 0;
    for (size_t i = static_cast<size_t>(blockIndex) + 1; i-- > 0 && seen < limit;) {
        const RichDocBlock& other = blocks[i];
        if (other.type != RichBlockType::ListItem || other.listLevel < block.listLevel) break;
        if (other.listLevel == block.listLevel && other.orderedList) {
            if (!sameList(other)) break;
            measure(i);
            ++seen;
        }
    }
    for (size_t i = static_cast<size_t>(blockIndex) + 1; i < blocks.size() && seen < 2 * limit; ++i) {
        const RichDocBlock& other = blocks[i];
        if (other.type != RichBlockType::ListItem || other.listLevel < block.listLevel) break;
        if (other.listLevel == block.listLevel && other.orderedList) {
            if (!sameList(other)) break;
            measure(i);
            ++seen;
        }
    }
    return widest;
}

// Space between block `index` and the one after it: the document's stated
// spacing (after + before, added as word processors do) when either states
// any, otherwise the view's block spacing.
float UltraCanvasRichTextEdit::GapAfterBlock(int index) const {
    return GapAfterBlock(editor.GetDocument()->blocks, index);
}

float UltraCanvasRichTextEdit::GapAfterBlock(const std::vector<RichDocBlock>& blocks, int index) const {
    const int count = static_cast<int>(blocks.size());
    if (index < 0 || index + 1 >= count) return 0.0f;
    const RichDocBlock& block = blocks[static_cast<size_t>(index)];
    const RichDocBlock& next = blocks[static_cast<size_t>(index) + 1];
    float gap = (block.spaceAfterPt < 0.0f && next.spaceBeforePt < 0.0f)
            ? style.blockSpacing
            : Px(std::max(0.0f, block.spaceAfterPt) + std::max(0.0f, next.spaceBeforePt));
    // A paragraph frame takes room of its own, as in a word processor: its
    // padding and line width below the last paragraph of a box and above the
    // first. Paragraphs within one box keep their plain spacing.
    const bool oneBox = block.HasParagraphFrame() && next.HasParagraphFrame() && block.SameParagraphFrame(next);
    if (!oneBox) {
        if (block.HasParagraphFrame()) {
            gap += kParagraphFramePadding + Px(std::max(0.0f, block.paragraphBorderBottom.widthPt));
        }
        if (next.HasParagraphFrame()) {
            gap += kParagraphFramePadding + Px(std::max(0.0f, next.paragraphBorderTop.widthPt));
        }
    }
    return gap;
}

// First-line indent, line spacing and tab stops of a paragraph's layout.
// `originX` is where the layout's left edge sits in the text column, which
// is what the column-relative tab positions are converted against.
void UltraCanvasRichTextEdit::ApplyParagraphGeometry(ITextLayout* layout, const RichDocBlock& block,
                                                     const std::string& text, float originX,
                                                     float wrapWidth) const {
    if (block.type != RichBlockType::ListItem && block.firstLineIndentPt != 0.0f) {
        layout->SetIndent(static_cast<int>(std::lround(Px(block.firstLineIndentPt))));
    }
    if (block.lineSpacing > 0.0f) layout->SetLineSpacing(block.lineSpacing);
    if (block.lineHeightPt > 0.0f && !text.empty()) {
        // "Exactly" is the height as given; "at least" never squeezes the
        // text below its natural line height.
        float height = block.lineHeightPt;
        if (block.lineHeightAtLeast) {
            float fontSize = static_cast<float>(style.baseFont.fontSize);
            for (const RichTextRun& run : block.runs) {
                if (run.fontSizePt > 0.0f) fontSize = std::max(fontSize, run.fontSizePt);
            }
            height = std::max(height, fontSize * 1.2f);
        }
        auto attribute = TextAttributeFactory::CreateAbsoluteLineHeight(Px(height));
        if (attribute) {
            attribute->SetRange(0, static_cast<int>(text.size()));
            layout->InsertAttribute(std::move(attribute));
        }
    }
    if (text.find('\t') == std::string::npos) return;

    std::vector<UCLayoutTabPos> tabs;
    float last = 0.0f;
    for (const RichTabStop& stop : block.tabStops) {
        const float x = Px(stop.positionPt) - originX;
        if (x <= 0.0f) continue;
        UCLayoutTabPos tab;
        tab.xPos = static_cast<int>(std::lround(x));
        tab.align = stop.kind == RichTabKind::Center ? UCLayoutTabAlignment::TabCenter
                  : stop.kind == RichTabKind::Right ? UCLayoutTabAlignment::TabRight
                  : stop.kind == RichTabKind::Decimal ? UCLayoutTabAlignment::TabDecimal
                  : UCLayoutTabAlignment::TabLeft;
        tabs.push_back(tab);
        last = std::max(last, Px(stop.positionPt));
    }
    // Past the explicit stops, tabs fall on the document's default interval,
    // counted from the column edge like the stops themselves.
    const UCRichDocument* document = editor.GetDocument().get();
    const float interval = Px((document && document->defaultTabStopPt > 0.0f)
                              ? document->defaultTabStopPt : style.defaultTabStop);
    if (interval > 1.0f) {
        const float limit = originX + std::max(wrapWidth, 0.0f) + interval;
        float x = (std::floor(last / interval) + 1.0f) * interval;
        for (int guard = 0; x < limit && guard < 256; x += interval, ++guard) {
            if (x - originX <= 0.0f) continue;
            UCLayoutTabPos tab;
            tab.xPos = static_cast<int>(std::lround(x - originX));
            tabs.push_back(tab);
        }
    }
    if (!tabs.empty()) layout->SetTabs(tabs);
}

FontStyle UltraCanvasRichTextEdit::FontForBlock(const RichDocBlock& block) const {
    FontStyle font = style.baseFont;
    if (block.type == RichBlockType::Heading) {
        int level = std::max(1, std::min(block.headingLevel, 6));
        font.fontSize = style.baseFont.fontSize * style.headingSizeMultipliers[static_cast<size_t>(level - 1)];
        if (style.headingsBold) font.fontWeight = FontWeight::Bold;
    } else if (block.type == RichBlockType::CodeBlock) {
        font.fontFamily = style.codeFontFamily;
    } else {
        if (block.paragraphFontSizePt > 0.0f) font.fontSize = block.paragraphFontSizePt;
        if (!block.paragraphFontFamily.empty()) font.fontFamily = block.paragraphFontFamily;
    }
    return font;
}

void UltraCanvasRichTextEdit::ApplyRunAttributes(ITextLayout* layout, const RichDocBlock& block,
                                                  const std::vector<RichTextRun>& runs,
                                                  std::vector<RichTextHitRect>* outHits,
                                                  int blockIndex,
                                                  std::vector<BlockLayout::InlineImage>* outInlineImages,
                                                  int cellRow, int cellColumn) const {
    if (!layout) return;

    int position = 0;
    for (const RichTextRun& run : runs) {
        int span = (run.lineBreakBefore ? 1 : 0) + static_cast<int>(run.text.size());
        int start = position + (run.lineBreakBefore ? 1 : 0);
        int end = position + span;
        position = end;
        if (start >= end) continue;

        auto add = [&](std::unique_ptr<ITextAttribute> attr) {
            if (!attr) return;
            attr->SetRange(start, end);
            layout->InsertAttribute(std::move(attr));
        };

        // A floating picture takes no room in the line: its placeholder is
        // squeezed to nothing and the placement pass puts the picture beside
        // the text. Only a body paragraph floats one; in a cell or a header it
        // stays in the line.
        const bool floats = run.IsFloatingImage() && cellRow < 0 && blockIndex >= 0 && outInlineImages
                            && block.type != RichBlockType::Table;
        if (floats) {
            std::shared_ptr<UCImage> image;
            const UCRichDocument& document = *editor.GetDocument();
            if (run.mediaIndex >= 0 && run.mediaIndex < static_cast<int>(document.media.size())) {
                image = UCImage::LoadFromMemory(document.media[static_cast<size_t>(run.mediaIndex)].data);
            }
            float width = run.imageWidthPt > 0.0f ? Px(run.imageWidthPt)
                        : (image ? static_cast<float>(image->GetWidth()) : 16.0f);
            float height = run.imageHeightPt > 0.0f ? Px(run.imageHeightPt)
                         : (image ? static_cast<float>(image->GetHeight()) : 16.0f);
            const float maxWidth = std::max(16.0f, ColumnWidth());
            if (width > maxWidth) {
                height *= maxWidth / width;
                width = maxWidth;
            }
            add(TextAttributeFactory::CreateShape(0.0, 0.0, 0.0));
            BlockLayout::InlineImage placed;
            placed.byteOffset = start;
            placed.width = width;
            placed.height = height;
            placed.image = image;
            placed.altText = run.imageAltText;
            placed.floating = true;
            placed.wrap = run.imageWrap;
            placed.floatAlign = run.imageFloatAlign;
            placed.offsetX = Px(run.imageOffsetXPt);
            placed.offsetY = Px(run.imageOffsetYPt);
            outInlineImages->push_back(std::move(placed));
            continue;
        }

        // A picture in the line: reserve a box over its placeholder so the text
        // flows around it, and record where to draw it once the layout is laid.
        if (run.IsInlineImage()) {
            std::shared_ptr<UCImage> image;
            const UCRichDocument& document = *editor.GetDocument();
            if (run.mediaIndex >= 0 && run.mediaIndex < static_cast<int>(document.media.size())) {
                image = UCImage::LoadFromMemory(document.media[static_cast<size_t>(run.mediaIndex)].data);
            }
            float width = run.imageWidthPt > 0.0f ? Px(run.imageWidthPt)
                        : (image ? static_cast<float>(image->GetWidth()) : 16.0f);
            float height = run.imageHeightPt > 0.0f ? Px(run.imageHeightPt)
                         : (image ? static_cast<float>(image->GetHeight()) : 16.0f);
            // Never wider than the line it sits in (a quoted or indented
            // paragraph's is narrower than the column); keep the aspect ratio.
            const double lineWidth = layout->GetExplicitWidth();
            const float maxWidth = std::max(16.0f, lineWidth > 0.0 ? static_cast<float>(lineWidth) : ColumnWidth());
            if (width > maxWidth) {
                height *= maxWidth / width;
                width = maxWidth;
            }
            // The picture sits on the baseline, which is where a word processor
            // puts an inline image.
            add(TextAttributeFactory::CreateShape(width, height, 0.0));
            if (outInlineImages) {
                BlockLayout::InlineImage placed;
                placed.byteOffset = start;
                placed.width = width;
                placed.height = height;
                placed.image = image;
                placed.altText = run.imageAltText;
                outInlineImages->push_back(std::move(placed));
            }
            continue;       // a picture carries no text formatting
        }

        // A formula: typeset, and the run's source hidden behind it - unless
        // the caret is in it, when the source is what is being edited.
        if (run.math && blockIndex >= 0 && !CaretWithin(blockIndex, cellRow, cellColumn, start, end)
            && UltraCanvasInlineMath::IsAvailable()) {
            const float sizePt = run.fontSizePt > 0.0f ? run.fontSizePt
                               : static_cast<float>(FontForBlock(block).fontSize);
            const Color color = run.color.empty() ? style.textColor : ParseHexColor(run.color, style.textColor);
            std::shared_ptr<UltraCanvasInlineMath> math =
                UltraCanvasInlineMath::Typeset(run.text, Px(sizePt), color, false);
            if (math) {
                const std::string& text = layout->GetText();
                const int firstEnd = std::min(end, UCRichDocumentEditor::NextCharOffset(text, start));
                auto shape = TextAttributeFactory::CreateShape(math->GetWidth(), math->GetAscent(),
                                                               math->GetDescent());
                shape->SetRange(start, firstEnd);
                layout->InsertAttribute(std::move(shape));
                if (firstEnd < end) {
                    auto hidden = TextAttributeFactory::CreateShape(0.0, 0.0, 0.0);
                    hidden->SetRange(firstEnd, end);
                    layout->InsertAttribute(std::move(hidden));
                }
                add(TextAttributeFactory::CreateAllowBreaks(false));
                if (outInlineImages) {
                    BlockLayout::InlineImage placed;
                    placed.byteOffset = start;
                    placed.width = math->GetWidth();
                    placed.height = math->GetHeight();
                    placed.math = math;
                    outInlineImages->push_back(std::move(placed));
                }
                continue;
            }
        }

        if (run.bold)          add(TextAttributeFactory::CreateFontWeight(FontWeight::Bold));
        if (run.italic)        add(TextAttributeFactory::CreateFontStyle(FontSlant::Italic));
        if (run.underline)     add(TextAttributeFactory::CreateUnderline(UCUnderlineType::UnderlineSingle));
        if (run.strikethrough) add(TextAttributeFactory::CreateStrikethrough(true));
        if (run.code) {
            add(TextAttributeFactory::CreateFontFamily(style.codeFontFamily));
            add(TextAttributeFactory::CreateBackground(style.codeBackgroundColor));
            add(TextAttributeFactory::CreateForeground(style.codeTextColor));
        }
        // Sub- and superscript ride on the same scale+rise pair the Markdown
        // renderer uses, so the two surfaces agree visually.
        double lineHeight = style.baseFont.fontSize * 1.3;
        if (run.subscript) {
            add(TextAttributeFactory::CreateScale(0.7));
            add(TextAttributeFactory::CreateRise(static_cast<int>(-lineHeight * 0.15)));
        }
        if (run.superscript) {
            add(TextAttributeFactory::CreateScale(0.7));
            add(TextAttributeFactory::CreateRise(static_cast<int>(lineHeight * 0.25)));
        }
        if (!run.fontFamily.empty()) add(TextAttributeFactory::CreateFontFamily(run.fontFamily));
        if (run.fontSizePt > 0.0f)   add(TextAttributeFactory::CreateFontSize(run.fontSizePt));
        if (!run.color.empty()) {
            add(TextAttributeFactory::CreateForeground(ParseHexColor(run.color, style.textColor)));
        }
        if (run.change == RichTextRun::Change::Inserted) {
            add(TextAttributeFactory::CreateUnderline(UCUnderlineType::UnderlineSingle));
            add(TextAttributeFactory::CreateForeground(style.insertionColor));
        } else if (run.change == RichTextRun::Change::Deleted) {
            add(TextAttributeFactory::CreateStrikethrough(true));
            add(TextAttributeFactory::CreateForeground(style.deletionColor));
        }
        if (!run.highlightColor.empty()) {
            add(TextAttributeFactory::CreateBackground(ParseHexColor(run.highlightColor, Colors::Transparent)));
        } else if (!run.commentIds.empty() && showComments && !printing) {
            // Text under a comment that is still open.
            const auto& comments = editor.GetDocument()->comments;
            bool open = false;
            for (int id : run.commentIds) {
                open = open || (id >= 0 && id < static_cast<int>(comments.size()) && !comments[static_cast<size_t>(id)].resolved);
            }
            if (open) add(TextAttributeFactory::CreateBackground(style.commentHighlightColor));
        }
        if (!run.linkTarget.empty()) {
            add(TextAttributeFactory::CreateForeground(style.linkColor));
            add(TextAttributeFactory::CreateUnderline(UCUnderlineType::UnderlineSingle));
            if (outHits) {
                // One rect per layout line the run covers, so a link that wraps
                // is clickable on both lines.
                for (const LayoutLineRange& line : layout->GetLineByteRanges()) {
                    int from = std::max(start, line.startByte);
                    int to = std::min(end, line.startByte + line.lengthBytes);
                    if (from >= to) continue;
                    Rect2Di a = layout->IndexToPos(from);
                    Rect2Di b = layout->IndexToPos(to);
                    RichTextHitRect hit;
                    hit.bounds = Rect2Df(static_cast<float>(a.x), static_cast<float>(a.y),
                                         static_cast<float>(std::max(1, b.x - a.x)),
                                         static_cast<float>(std::max(1, a.height)));
                    hit.linkTarget = run.linkTarget;
                    hit.blockIndex = blockIndex;
                    outHits->push_back(hit);
                }
            }
        }
    }

    if (block.type == RichBlockType::BlockQuote) {
        auto fg = TextAttributeFactory::CreateForeground(style.quoteTextColor);
        fg->SetRange(0, position);
        layout->InsertAttribute(std::move(fg));
    }
}

bool UltraCanvasRichTextEdit::CaretWithin(int blockIndex, int cellRow, int cellColumn,
                                          int start, int end) const {
    if (printing) return false;               // output shows formulas typeset
    auto inside = [&](const RichDocPosition& p) {
        return p.blockIndex == blockIndex && p.cellRow == cellRow && p.cellColumn == cellColumn
            && p.byteOffset > start && p.byteOffset < end;
    };
    // At either edge the caret is beside the formula, not in it; only a
    // caret strictly inside opens the source.
    return inside(editor.GetCaret()) || inside(editor.GetAnchor());
}

void UltraCanvasRichTextEdit::ApplySelectionAttributes(ITextLayout* layout, int blockIndex,
                                                       int cellRow, int cellColumn) const {
    if (!layout || !editor.HasSelection() || printing) return;
    RichDocRange range = editor.GetSelectionRange();
    if (blockIndex < range.start.blockIndex || blockIndex > range.end.blockIndex) return;

    // A block of whole cells is shown as a wash over each cell (see
    // RenderBlock), not as highlighted text.
    if (editor.HasCellSelection()) return;
    // Otherwise a cell's layout only carries the highlight when the selection
    // is in that cell - the selection is inside it, so start and end agree.
    const bool wantCell = (cellRow >= 0 && cellColumn >= 0);
    if (wantCell != range.start.InCell()) return;
    if (wantCell && (range.start.cellRow != cellRow || range.start.cellColumn != cellColumn)) return;

    int length = static_cast<int>(layout->GetText().size());
    int from = (blockIndex == range.start.blockIndex) ? range.start.byteOffset : 0;
    int to   = (blockIndex == range.end.blockIndex)   ? range.end.byteOffset   : length;
    from = std::max(0, std::min(from, length));
    to   = std::max(from, std::min(to, length));
    if (from == to) return;

    auto bg = TextAttributeFactory::CreateBackground(style.selectionColor);
    bg->SetRange(from, to);
    layout->InsertAttribute(std::move(bg));
}

std::unique_ptr<ITextLayout> UltraCanvasRichTextEdit::MakeRunsLayout(
        IRenderContext* ctx, const RichDocBlock& block, const std::vector<RichTextRun>& runs,
        float wrapWidth, std::vector<RichTextHitRect>* outHits, int blockIndex,
        std::vector<BlockLayout::InlineImage>* outInlineImages, float paragraphOriginX,
        int cellRow, int cellColumn) const {
    std::string text = UCRichDocumentEditor::RunsText(runs);
    auto layout = ctx->CreateTextLayout(text, false);
    layout->SetFontStyle(FontForBlock(block));
    if (wrapWidth > 0) {
        layout->SetExplicitWidth(wrapWidth);
        layout->SetWrap(TextWrap::WrapWordChar);
    }
    // Right-to-left: the text layout takes its base direction from the first
    // letter, and mirrors left and right alignment in a paragraph that starts
    // with a right-to-left one. A paragraph marked right-to-left starts at
    // the right whatever its first letter.
    const int firstStrong = UCRichDocumentEditor::FirstStrongDirection(text);
    if (block.rightToLeft || firstStrong > 0) {
        RichTextAlign visual = block.align;
        if (visual == RichTextAlign::Default) visual = RichTextAlign::Right;
        if (firstStrong > 0 && (visual == RichTextAlign::Left || visual == RichTextAlign::Right)) {
            visual = visual == RichTextAlign::Left ? RichTextAlign::Right : RichTextAlign::Left;
        }
        layout->SetAlignment(ToTextAlignment(visual));
    } else if (block.align != RichTextAlign::Default) {
        layout->SetAlignment(ToTextAlignment(block.align));
    }
    if (paragraphOriginX >= 0.0f) {
        ApplyParagraphGeometry(layout.get(), block, text, paragraphOriginX, wrapWidth);
    }
    ApplyRunAttributes(layout.get(), block, runs, outHits, blockIndex, outInlineImages, cellRow, cellColumn);
    return layout;
}

void UltraCanvasRichTextEdit::BuildBlockLayout(IRenderContext* ctx, int blockIndex) {
    // The block with the caret shows an input method's composition in its
    // text: laid out with it spliced in at the caret, underlined, and put back.
    const RichDocPosition caret = editor.GetCaret();
    if (!preeditText.empty() && caret.blockIndex == blockIndex
        && blockIndex < static_cast<int>(editor.GetDocument()->blocks.size())) {
        RichDocBlock& block = editor.GetDocument()->blocks[static_cast<size_t>(blockIndex)];
        std::vector<RichTextRun>* runs = &block.runs;
        if (caret.InCell()) {
            runs = nullptr;
            if (caret.cellRow < static_cast<int>(block.tableRows.size())
                && caret.cellColumn < static_cast<int>(block.tableRows[static_cast<size_t>(caret.cellRow)].cells.size())) {
                runs = &block.tableRows[static_cast<size_t>(caret.cellRow)].cells[static_cast<size_t>(caret.cellColumn)].runs;
            }
        }
        if (runs) {
            const std::vector<RichTextRun> saved = *runs;
            RichTextRun composing = editor.FormatAt(caret);
            composing.text = preeditText;
            composing.underline = true;
            composing.lineBreakBefore = false;
            // Split the run the caret is in and put the composition between.
            int offset = 0;
            size_t at = runs->size();
            for (size_t i = 0; i < runs->size(); i++) {
                RichTextRun& run = (*runs)[i];
                const int start = offset + (run.lineBreakBefore ? 1 : 0);
                const int end = start + static_cast<int>(run.text.size());
                if (caret.byteOffset <= start && (caret.byteOffset < start || !run.lineBreakBefore)) {
                    at = i;
                    break;
                }
                if (caret.byteOffset < end) {
                    RichTextRun tail = run;
                    tail.text = run.text.substr(static_cast<size_t>(caret.byteOffset - start));
                    tail.lineBreakBefore = false;
                    run.text.resize(static_cast<size_t>(caret.byteOffset - start));
                    runs->insert(runs->begin() + static_cast<std::ptrdiff_t>(i) + 1, tail);
                    at = i + 1;
                    break;
                }
                offset = end;
            }
            runs->insert(runs->begin() + static_cast<std::ptrdiff_t>(at), composing);
            BuildBlockLayoutFor(ctx, blockIndex);
            *runs = saved;
            return;
        }
    }
    BuildBlockLayoutFor(ctx, blockIndex);
}

void UltraCanvasRichTextEdit::BuildBlockLayoutFor(IRenderContext* ctx, int blockIndex) {
    // A block in a section of columns is laid out at its column's width.
    const bool bodyBlock = !furnitureEdit || placingParkedBody;
    const float width = bodyBlock && static_cast<size_t>(blockIndex) < blockColumnWidths.size()
                      ? blockColumnWidths[static_cast<size_t>(blockIndex)] : 0.0f;
    columnWidthOverride = width;
    BuildBlockLayout(ctx, editor.GetDocument()->blocks, blockIndex,
                     blockLayouts[static_cast<size_t>(blockIndex)], blockIndex);
    columnWidthOverride = 0.0f;
    blockLayouts[static_cast<size_t>(blockIndex)].builtWidth = width;
}

void UltraCanvasRichTextEdit::BuildBlockLayout(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks,
                                               int index, BlockLayout& bl, int blockIndex) {
    const RichDocBlock& block = blocks[static_cast<size_t>(index)];

    bl.layout.reset();
    bl.cells.clear();
    bl.cellColumns.clear();
    bl.cellRows.clear();
    bl.hitRects.clear();
    bl.markerText.clear();
    bl.checkbox = bl.checked = false;
    bl.image.reset();
    bl.displayMath.reset();
    bl.inlineImages.clear();
    bl.slices.clear();
    bl.rowBreaks.clear();
    bl.headerRowsHeight = 0.0f;

    // Floating pictures beside the block take room from the column's sides.
    const float intrudeLeft = blockIndex >= 0 ? bl.intrudeLeft : 0.0f;
    const float intrudeRight = blockIndex >= 0 ? bl.intrudeRight : 0.0f;
    float indent = BlockIndentFor(block) + intrudeLeft;
    bl.markerLeft = std::max(intrudeLeft, indent - style.listIndent * 0.8f);
    if (block.type == RichBlockType::ListItem && block.orderedList) {
        // A level's text starts after its widest label, as in a word
        // processor: "(III)" and "1.2.10." need more room than "1.", and
        // every item of the level lines up behind the widest one.
        const float labels = WidestSiblingLabel(ctx, blocks, index);
        const float needed = style.listIndent * static_cast<float>(block.listLevel) + labels
                           + std::max(4.0f, static_cast<float>(MarkerFontFor(block).fontSize) * 0.4f);
        indent = std::max(indent, needed);
    }
    bl.textLeft = indent;
    const float columnSpan = std::max(24.0f + indent, ColumnWidth() - intrudeRight);
    float wrapWidth = std::max(1.0f, columnSpan - indent);
    // A right-to-left list item is the mirror image: its indent and its
    // number or bullet are on the right, the text ends before them.
    bl.markerOnRight = false;
    if (block.type == RichBlockType::ListItem
        && (block.rightToLeft
            || UCRichDocumentEditor::FirstStrongDirection(UCRichDocumentEditor::RunsText(block.runs)) > 0)) {
        bl.markerOnRight = true;
        bl.textLeft = intrudeLeft;
        bl.markerLeft = columnSpan - (bl.markerLeft - intrudeLeft) ;    // mirrored: the marker's right edge
    }
    if (block.type != RichBlockType::ListItem) {
        // A hanging indent puts the first line left of the others: the layout
        // starts there, and its (negative) indent moves the rest back in.
        const float hang = Px(std::min(0.0f, block.firstLineIndentPt));
        bl.textLeft = std::max(0.0f, indent + hang);
        wrapWidth = std::max(1.0f, columnSpan - bl.textLeft - Px(std::max(0.0f, block.rightIndentPt)));
    }

    switch (block.type) {
        case RichBlockType::HorizontalRule:
            bl.bounds.width = columnSpan;
            bl.bounds.height = std::max(8.0f, static_cast<float>(style.baseFont.fontSize));
            break;

        case RichBlockType::PageBreak:
            bl.bounds.width = columnSpan;
            bl.bounds.height = std::max(12.0f, static_cast<float>(style.baseFont.fontSize) * 1.2f);
            break;

        case RichBlockType::Image: {
            const UCRichDocument& document = *editor.GetDocument();
            if (block.mediaIndex >= 0
                && block.mediaIndex < static_cast<int>(document.media.size())) {
                bl.image = UCImage::LoadFromMemory(document.media[static_cast<size_t>(block.mediaIndex)].data);
            }
            float width = block.imageWidthPt > 0 ? Px(block.imageWidthPt)
                                                 : (bl.image ? static_cast<float>(bl.image->GetWidth()) : 160.0f);
            float height = block.imageHeightPt > 0 ? Px(block.imageHeightPt)
                                                   : (bl.image ? static_cast<float>(bl.image->GetHeight()) : 120.0f);
            // Never wider than the text column; keep the aspect ratio.
            const float room = std::max(8.0f, columnSpan - indent);
            if (width > room && width > 0) {
                height *= room / width;
                width = room;
            }
            bl.bounds.width = std::max(8.0f, width);
            bl.bounds.height = std::max(8.0f, height);
            // A centred or right-aligned picture paragraph sits where its
            // alignment puts it in the room it has.
            const float spare = std::max(0.0f, room - bl.bounds.width);
            if (block.align == RichTextAlign::Center) bl.textLeft += spare * 0.5f;
            else if (block.align == RichTextAlign::Right) bl.textLeft += spare;
            break;
        }

        case RichBlockType::Table: {
            // Where each cell actually sits is resolved by the shared grid
            // walk in the model, which the structural table operations use too
            // - two implementations of "which column is this cell in" would
            // drift, and a disagreement between layout and editing is a caret
            // landing in the wrong cell.
            const RichTableGrid grid = BuildTableGrid(block);
            const size_t columnCount = static_cast<size_t>(grid.columnCount);
            if (columnCount == 0) {
                bl.bounds.width = columnSpan;
                bl.bounds.height = static_cast<float>(style.baseFont.fontSize);
                break;
            }
            // The table's own width and place in the column: a fixed or
            // relative width from the document, else the whole column.
            const float columnSpace = std::max(24.0f * static_cast<float>(columnCount), columnSpan - indent);
            float tableWidth = columnSpace;
            if (block.tableWidthPt > 0.0f) tableWidth = std::min(Px(block.tableWidthPt), columnSpace);
            else if (block.tableWidthPercent > 0.0f) tableWidth = columnSpace * std::min(100.0f, block.tableWidthPercent) / 100.0f;
            tableWidth = std::max(tableWidth, 24.0f * static_cast<float>(columnCount));
            float tableLeft = 0.0f;
            switch (block.tableAlign) {
                case RichTextAlign::Center: tableLeft = (columnSpace - tableWidth) * 0.5f; break;
                case RichTextAlign::Right: tableLeft = columnSpace - tableWidth; break;
                default: tableLeft = std::clamp(Px(block.tableIndentPt), 0.0f, std::max(0.0f, columnSpace - tableWidth)); break;
            }
            float columnWidth = tableWidth / static_cast<float>(columnCount);
            // Column geometry: the document's own relative widths when it has
            // one per grid column (a narrow date column beside a wide text
            // one), otherwise equal shares - scaled to the text column either
            // way. A width list that no longer matches the grid (a column
            // inserted since) falls back to equal shares.
            std::vector<float> columnLeft(columnCount + 1, 0.0f);
            {
                const std::vector<float>& widths = block.tableColumnWidths;
                float total = 0.0f;
                bool usable = widths.size() == columnCount;
                for (float w : widths) {
                    if (!(w > 0.0f)) usable = false;
                    total += w;
                }
                for (size_t c = 0; c < columnCount; c++) {
                    float share = usable ? tableWidth * widths[c] / total : columnWidth;
                    columnLeft[c + 1] = columnLeft[c] + std::max(24.0f, share);
                }
            }

            // A cell's position is its GRID column, which is not its index in
            // the row once anything spans: a row-spanning cell above occupies a
            // column here, and the cells of this row shift past it. The model
            // indices (row, index-within-row) are what positions address, so
            // cellRows/cellColumns keep storing those while the geometry
            // follows the grid.
            //
            // Cells still growing downwards: index into bl.cells, and the row
            // they must reach. Their height is fixed up once rows are measured.
            struct PendingSpan { size_t cellIndex; size_t lastRow; };
            std::vector<PendingSpan> pendingSpans;
            std::vector<float> rowTop(block.tableRows.size(), 0.0f);
            // The view's own grid leaves a gap between rows; a document's
            // borders are continuous lines, so its rows abut.
            const float rowGap = block.tableBordersFromDocument ? 0.0f : 4.0f;
            std::vector<float> rowBottom(block.tableRows.size(), 0.0f);

            float y = 0.0f;
            for (size_t r = 0; r < block.tableRows.size(); r++) {
                const RichTableRow& row = block.tableRows[r];
                rowTop[r] = y;
                float rowHeight = 0.0f;
                const size_t firstCellOfRow = bl.cells.size();

                for (size_t gridColumn = 0; gridColumn < columnCount; ) {
                    const RichTableGridSlot& slot =
                        grid.At(static_cast<int>(r), static_cast<int>(gridColumn));
                    if (!slot.Occupied() || !slot.origin) {
                        // Covered by a cell from an earlier row, or a slot this
                        // row's cells never reach (a ragged row). Either way
                        // there is nothing to lay out here.
                        gridColumn++;
                        continue;
                    }
                    const size_t cellIndex = static_cast<size_t>(slot.cellIndex);
                    const RichTableCell& modelCell = row.cells[cellIndex];
                    // Clamped exactly as the grid clamped them, or the geometry
                    // would claim room the grid does not agree the cell has.
                    const int columnSpan = std::min(std::max(1, modelCell.columnSpan),
                                                    static_cast<int>(columnCount - gridColumn));
                    const int rowSpan = std::min(std::max(1, modelCell.rowSpan),
                                                 static_cast<int>(block.tableRows.size() - r));
                    const float cellWidth = columnLeft[gridColumn + static_cast<size_t>(columnSpan)]
                                          - columnLeft[gridColumn];

                    // The room around the text: the document's cell padding,
                    // else 4 at the sides and 2 above (and below, for a
                    // document's table, whose rows abut).
                    const float padLeft = modelCell.paddingLeftPt >= 0.0f ? Px(modelCell.paddingLeftPt) : 4.0f;
                    const float padRight = modelCell.paddingRightPt >= 0.0f ? Px(modelCell.paddingRightPt) : 4.0f;
                    const float padTop = modelCell.paddingTopPt >= 0.0f ? Px(modelCell.paddingTopPt) : 2.0f;
                    const float padBottom = modelCell.paddingBottomPt >= 0.0f ? Px(modelCell.paddingBottomPt)
                                          : (block.tableBordersFromDocument ? 2.0f : 0.0f);

                    RichDocBlock cellBlock;
                    cellBlock.type = RichBlockType::Paragraph;
                    cellBlock.align = modelCell.align;
                    auto cell = std::make_unique<BlockLayout>();
                    cell->layout = MakeRunsLayout(ctx, cellBlock, modelCell.runs,
                                                  std::max(1.0f, cellWidth - padLeft - padRight), nullptr,
                                                  blockIndex, &cell->inlineImages, -1.0f,
                                                  static_cast<int>(r), static_cast<int>(cellIndex));
                    ApplySelectionAttributes(cell->layout.get(), blockIndex,
                                             static_cast<int>(r), static_cast<int>(cellIndex));
                    const float textHeight = static_cast<float>(cell->layout->GetLayoutHeight());
                    cell->bounds = Rect2Df(indent + tableLeft + columnLeft[gridColumn], y,
                                           cellWidth, textHeight + padTop + padBottom);
                    cell->textLeft = padLeft;
                    cell->textTop = padTop;
                    cell->textHeight = textHeight;
                    cell->textBottomPad = padBottom;
                    cell->verticalAlign = modelCell.verticalAlign;
                    // A cell spanning rows must not force this row to its full
                    // height; it stretches over the rows below instead.
                    if (rowSpan == 1) rowHeight = std::max(rowHeight, cell->bounds.height);

                    bl.cellColumns.push_back(static_cast<int>(cellIndex));
                    bl.cellRows.push_back(static_cast<int>(r));
                    if (rowSpan > 1) {
                        pendingSpans.push_back(PendingSpan{
                            bl.cells.size(),
                            std::min(r + static_cast<size_t>(rowSpan) - 1,
                                     block.tableRows.size() - 1)});
                    }
                    bl.cells.push_back(std::move(cell));

                    gridColumn += static_cast<size_t>(columnSpan);
                }

                if (rowHeight <= 0.0f) rowHeight = static_cast<float>(style.baseFont.fontSize) * 1.3f;
                // Every cell that belongs to this row alone takes its height.
                for (size_t i = firstCellOfRow; i < bl.cells.size(); i++) {
                    bool spans = false;
                    for (const PendingSpan& pending : pendingSpans) {
                        if (pending.cellIndex == i) { spans = true; break; }
                    }
                    if (!spans) bl.cells[i]->bounds.height = rowHeight;
                }
                y += rowHeight + rowGap;
                rowBottom[r] = y - rowGap;
            }

            // Now that every row has a height, stretch the row-spanning cells
            // down to the bottom of the last row they cover.
            for (const PendingSpan& pending : pendingSpans) {
                BlockLayout& cell = *bl.cells[pending.cellIndex];
                const float bottom = rowBottom[pending.lastRow];
                cell.bounds.height = std::max(cell.bounds.height, bottom - cell.bounds.y);
            }
            // A cell taller than its text places the text at its top,
            // middle or bottom.
            for (auto& cell : bl.cells) {
                const float spare = cell->bounds.height - cell->textTop - cell->textHeight - cell->textBottomPad;
                if (spare <= 0.0f) continue;
                if (cell->verticalAlign == RichVerticalAlign::Middle) cell->textTop += spare * 0.5f;
                else if (cell->verticalAlign == RichVerticalAlign::Bottom) cell->textTop += spare;
            }

            // Where a page may break the table: between rows no cell spans.
            for (size_t r = 1; r < block.tableRows.size(); r++) {
                bool spanned = false;
                for (const PendingSpan& pending : pendingSpans) {
                    const size_t origin = static_cast<size_t>(bl.cellRows[pending.cellIndex]);
                    if (origin < r && pending.lastRow >= r) { spanned = true; break; }
                }
                if (!spanned) bl.rowBreaks.push_back(rowTop[r]);
            }
            // Leading header rows repeat at the top of every page the table
            // continues on - as long as something follows them.
            size_t headerRows = 0;
            while (headerRows < block.tableRows.size() && block.tableRows[headerRows].header) headerRows++;
            if (headerRows > 0 && headerRows < block.tableRows.size()) bl.headerRowsHeight = rowTop[headerRows];

            bl.bounds.width = tableLeft + columnLeft[columnCount];
            bl.bounds.height = y;
            break;
        }

        default: {
            if (block.type == RichBlockType::MathBlock && blockIndex >= 0
                && (printing || (editor.GetCaret().blockIndex != blockIndex
                                 && editor.GetAnchor().blockIndex != blockIndex))
                && UltraCanvasInlineMath::IsAvailable()) {
                std::string source = UCRichDocumentEditor::RunsText(block.runs);
                std::replace(source.begin(), source.end(), '\n', ' ');
                bl.displayMath = UltraCanvasInlineMath::Typeset(
                    source, Px(static_cast<float>(FontForBlock(block).fontSize)), style.textColor, true);
                if (bl.displayMath) {
                    bl.bounds.width = columnSpan;
                    bl.bounds.height = bl.displayMath->GetHeight() + 8.0f;
                    break;
                }
            }
            bl.layout = MakeRunsLayout(ctx, block, block.runs, wrapWidth,
                                       blockIndex >= 0 ? &bl.hitRects : nullptr, blockIndex,
                                       &bl.inlineImages, bl.textLeft);
            ApplySelectionAttributes(bl.layout.get(), blockIndex);
            bl.bounds.width = static_cast<float>(bl.layout->GetLayoutWidth());
            bl.bounds.height = static_cast<float>(bl.layout->GetLayoutHeight()) + style.paragraphLeading;
            if (block.type == RichBlockType::ListItem) {
                // The document's own label ("b)", "1.2.", "(iv)") or bullet
                // when it has one; the view's otherwise.
                if (block.checkbox && !block.orderedList) {
                    bl.checkbox = true;
                    bl.checked = block.checked;
                } else if (block.orderedList) {
                    bl.markerText = RichDocListLabel(blocks, static_cast<size_t>(index));
                } else if (!block.bulletText.empty()) {
                    bl.markerText = block.bulletText;
                } else {
                    bl.markerText = style.bulletCharacters[static_cast<size_t>(
                        std::min<int>(block.listLevel, static_cast<int>(style.bulletCharacters.size()) - 1))];
                }
            }
            break;
        }
    }

    // An empty block still needs a caret-sized box to click into.
    if (bl.bounds.height <= 0.0f) {
        bl.bounds.height = static_cast<float>(style.baseFont.fontSize) * 1.3f;
    }
    bl.valid = true;
}

void UltraCanvasRichTextEdit::EnsureLayouts(IRenderContext* ctx) {
    int blockCount = editor.GetBlockCount();
    if (static_cast<int>(blockLayouts.size()) != blockCount) {
        blockLayouts.clear();
        blockLayouts.resize(static_cast<size_t>(blockCount));
        layoutsDirty = true;
    }
    if (lastWrapWidth != ColumnWidth()) {
        for (auto& bl : blockLayouts) bl.valid = false;
        lastWrapWidth = ColumnWidth();
        layoutsDirty = true;
    }
    // The selection is painted as a layout attribute, so a layout built under
    // the old selection is stale. Invalidate what the selection left and what
    // it entered — not the whole document.
    RichDocRange selection = editor.HasSelection()
            ? editor.GetSelectionRange()
            : RichDocRange(editor.GetCaret(), editor.GetCaret());
    if (selection.start != appliedSelection.start || selection.end != appliedSelection.end) {
        auto invalidateSpan = [this](const RichDocRange& range) {
            for (int i = std::max(0, range.start.blockIndex);
                 i <= range.end.blockIndex && i < static_cast<int>(blockLayouts.size()); i++) {
                blockLayouts[static_cast<size_t>(i)].valid = false;
            }
        };
        invalidateSpan(appliedSelection);
        invalidateSpan(selection);
        appliedSelection = selection;
        layoutsDirty = true;
    }

    if (!layoutsDirty) return;

    // Note marks number themselves in document order; a reference added,
    // moved or removed renumbers the others.
    // The comment pane comes and goes with the document's comments (and
    // narrows the text when it is there).
    if (!furnitureEdit && editor.GetDocument()) {
        const bool pane = showComments && !printing && !editor.GetDocument()->comments.empty()
                          && !editor.GetDocument()->ActiveComments().empty();
        if (pane != commentPaneShown) {
            commentPaneShown = pane;
            visibleAreaDirty = true;
        }
    }
    // So do caption numbers and cross-references.
    bool fieldsChanged = false;
    if (!furnitureEdit && editor.GetDocument()) fieldsChanged = editor.GetDocument()->UpdateFields();
    if (!furnitureEdit && editor.GetDocument()
        && ((!editor.GetDocument()->notes.empty() && editor.GetDocument()->UpdateNoteMarks()) || fieldsChanged)) {
        for (auto& bl : blockLayouts) bl.valid = false;
        const RichDocPosition caret = editor.GetCaret();
        const RichDocPosition clamped = editor.ClampPosition(caret);
        if (clamped != caret) editor.SetCaret(clamped, false);
    }

    // Sections of several columns (page view only): each block's width.
    // (The body's, also while a header or note is edited over it.)
    blockColumnWidths.clear();
    const UCRichDocument* body = furnitureEdit ? furnitureEdit->bodyEditor.GetDocument().get()
                                               : editor.GetDocument().get();
    hasColumns = pageView && body && body->HasColumns();
    if (hasColumns) {
        const UCRichDocument& document = *body;
        blockColumnWidths.resize(document.blocks.size(), 0.0f);
        RichSectionSetup section = document.firstSection;
        for (size_t i = 0; i < document.blocks.size(); i++) {
            if (document.blocks[i].sectionStart) section = document.blocks[i].section;
            if (section.columns > 1) {
                const float gap = Px(section.columnGapPt);
                blockColumnWidths[i] = std::max(24.0f, (columnWidth - gap * static_cast<float>(section.columns - 1))
                                                           / static_cast<float>(section.columns));
            }
        }
    }
    for (int i = 0; i < blockCount && !furnitureEdit; i++) {
        BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
        const float want = static_cast<size_t>(i) < blockColumnWidths.size() ? blockColumnWidths[static_cast<size_t>(i)] : 0.0f;
        if (bl.valid && std::abs(bl.builtWidth - want) > 0.5f) bl.valid = false;
    }

    // Continuous view: full layouts only for the blocks near the viewport.
    // Blocks outside it keep the height they had (or an estimate), so a long
    // document never pays for layouts nobody is looking at. Page view needs
    // every block's true height to know where the pages end, so it builds
    // them all.
    float y = 0.0f;
    float viewTop = scrollOffset - visibleArea.height;
    float viewBottom = scrollOffset + 2 * visibleArea.height;

    for (int i = 0; i < blockCount; i++) {
        BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
        bool visible = pageView || furnitureEdit || ((y + bl.bounds.height >= viewTop) && (y <= viewBottom));
        if (!bl.valid && visible) {
            BuildBlockLayout(ctx, i);
        } else if (!bl.valid) {
            // Estimate: one line per block until it scrolls into view.
            const RichDocBlock& block = editor.GetBlock(i);
            bl.bounds.width = ColumnWidth();
            bl.bounds.height = static_cast<float>(style.baseFont.fontSize)
                             * (block.type == RichBlockType::Heading ? 2.0f : 1.4f);
            bl.textLeft = std::max(0.0f, BlockIndentFor(block) + Px(std::min(0.0f, block.firstLineIndentPt)));
        }
        y += bl.bounds.height + GapAfterBlock(i);
    }
    y = furnitureEdit ? PlaceFurnitureBeingEdited(ctx) : PlaceBody(ctx);
    // A page number in the body shows the page its block landed on, which is
    // only known now. Renumbering can change a block's width ("9" to "10"),
    // so the renumbered blocks are laid out and the pages placed again.
    if (pageView && !furnitureEdit && UpdateBodyPageFields()) {
        for (int i = 0; i < blockCount; i++) {
            BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
            if (!bl.valid) BuildBlockLayout(ctx, i);
        }
        y = PlaceBody(ctx);
    }
    contentHeight = std::max(0.0f, y);
    layoutsDirty = false;

    float maxScroll = std::max(0.0f, contentHeight - visibleArea.height);
    scrollOffset = std::max(0.0f, std::min(scrollOffset, maxScroll));
}

// ===== RENDERING =====

void UltraCanvasRichTextEdit::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!IsVisible()) return;

    if (visibleAreaDirty) RecalculateVisibleArea();
    EnsureLayouts(ctx);
    // The scrollbar appearing or disappearing changes the wrap width, so the
    // visible area is settled before anything is drawn.
    float previousWidth = visibleArea.width;
    RecalculateVisibleArea();
    if (std::abs(previousWidth - visibleArea.width) > 0.5f) {
        layoutsDirty = true;
        EnsureLayouts(ctx);
    }

    if (caretMoved) {
        ScrollToCaret();
        caretMoved = false;
        EnsureLayouts(ctx);     // the scroll may have brought unbuilt blocks in
    }

    UltraCanvasUIElement::Render(ctx, dirtyRect);

    Rect2Df bounds = GetLocalBounds();
    ctx->DrawFilledRectangle(Rect2Dd(bounds.x, bounds.y, bounds.width, bounds.height),
                             pageView ? style.deskColor : style.backgroundColor,
                             style.drawBorder ? 1.0f : 0.0f,
                             style.drawBorder ? style.borderColor : Colors::Transparent);

    ctx->PushState();
    const Rect2Df view = ToElement(visibleArea);
    ctx->ClipRect(Rect2Dd(view.x, view.y, view.width, view.height));
    if (zoom != 1.0f) {
        // Drawn in document space, scaled about the view's origin.
        ctx->Translate(visibleArea.x, visibleArea.y);
        ctx->Scale(zoom, zoom);
        ctx->Translate(-visibleArea.x, -visibleArea.y);
    }
    RenderPages(ctx);
    if (furnitureEdit) RenderBodyBackdrop(ctx);
    DrawFloats(ctx, /*behindText*/ true);

    float viewTop = scrollOffset;
    float viewBottom = scrollOffset + visibleArea.height;
    for (int i = 0; i < static_cast<int>(blockLayouts.size()); i++) {
        const BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
        if (BlockVisualBottom(bl) < viewTop) continue;
        if (bl.bounds.y > viewBottom) { if (hasColumns) continue; break; }
        RenderBlock(ctx, i, bl);
    }
    DrawFloats(ctx, /*behindText*/ false);
    if (!furnitureEdit) RenderNotes(ctx);
    DrawSpellErrorMarks(ctx);
    DrawImageSelection(ctx);
    if (draggingText) {
        // Where the dragged text will land: a caret-shaped mark.
        const Rect2Df drop = PositionRect(dropPosition);
        if (drop.height > 0.0f) {
            ctx->DrawFilledRectangle(Rect2Dd(drop.x - 1.0, drop.y, 2.0, drop.height),
                                     style.cursorColor, 0.0f, Colors::Transparent);
        }
    }
    ctx->PopState();

    // A block of selected cells has no caret, as in a word processor.
    if (IsFocused() && !readOnly && !editor.HasCellSelection()) {
        UpdateCaret();
    } else {
        UltraCanvasCaret::GetInstance().Hide(this);
    }

    if (commentPaneShown) RenderCommentPane(ctx);
    DrawScrollbar(ctx);
    DrawHorizontalScrollbar(ctx);
}

// Draws the pictures sitting inside a laid-out text. The layout reserved a box
// for each over its placeholder, so IndexToPos() gives the box's top-left and
// the picture goes straight into it.
void UltraCanvasRichTextEdit::DrawInlineImages(IRenderContext* ctx, const BlockLayout& bl,
                                               float originX, float originY) const {
    if (bl.inlineImages.empty() || !bl.layout) return;
    for (const BlockLayout::InlineImage& placed : bl.inlineImages) {
        if (placed.floating) continue;        // drawn where the placement put it
        Rect2Di box = bl.layout->IndexToPos(placed.byteOffset);
        if (placed.math) {
            placed.math->Draw(ctx, originX + static_cast<double>(box.x),
                              originY + bl.layout->IndexToBaseline(placed.byteOffset));
            continue;
        }
        Rect2Dd target(originX + static_cast<double>(box.x),
                       originY + static_cast<double>(box.y),
                       placed.width, placed.height);
        if (placed.image) {
            ctx->DrawImage(*placed.image, target, ImageFitMode::Contain);
        } else {
            // Undecodable media: the same framed placeholder a block image
            // gets, so the picture's place in the line stays visible.
            ctx->DrawFilledRectangle(target, Colors::Transparent, 1.0f,
                                     style.imagePlaceholderColor);
        }
    }
}

// The pages under the text (page view), and every page's header and footer;
// outside page view, the first page's header and footer around the body.
void UltraCanvasRichTextEdit::RenderPages(IRenderContext* ctx) {
    const float viewTop = scrollOffset;
    const float viewBottom = scrollOffset + visibleArea.height;
    for (const PageFrame& frame : pages) {
        if (pageView) {
            const float bottom = frame.top + pageHeightPx;
            if (bottom < viewTop) continue;
            if (frame.top > viewBottom) break;
            const double x = visibleArea.x + pageLeftX - hScrollOffset;
            const double y = visibleArea.y + frame.top - scrollOffset;
            if (!printing) {
                ctx->DrawFilledRectangle(Rect2Dd(x + 3.0, y + 3.0, pageWidthPx, pageHeightPx),
                                         style.pageShadowColor, 0.0f, Colors::Transparent);
            }
            ctx->DrawFilledRectangle(Rect2Dd(x, y, pageWidthPx, pageHeightPx),
                                     style.pageColor, printing ? 0.0f : 1.0f,
                                     printing ? Colors::Transparent : style.borderColor);
            if (ShowsEditingMarks()) {
                // Writer's text boundaries: a corner mark at each corner of
                // the text area.
                const double left = ColumnLeft(), right = left + ColumnWidth();
                const double top = visibleArea.y + frame.bodyTop - scrollOffset;
                const double foot = visibleArea.y + frame.bodyBottom - scrollOffset;
                const double arm = 12.0;
                auto corner = [&](double cx, double cy, double dx, double dy) {
                    ctx->DrawLine(Point2Dd(cx, cy), Point2Dd(cx + dx * arm, cy), style.pageMarginGuideColor);
                    ctx->DrawLine(Point2Dd(cx, cy), Point2Dd(cx, cy + dy * arm), style.pageMarginGuideColor);
                };
                corner(left, top, -1, -1);
                corner(right, top, 1, -1);
                corner(left, foot, -1, 1);
                corner(right, foot, 1, 1);
            }
        }
        // The header or footer being edited is drawn live, as the blocks.
        const bool editedPage = furnitureEdit && furnitureEdit->noteIndex < 0
                                && static_cast<int>(&frame - pages.data()) == furnitureEdit->pageIndex;
        if (frame.header && !(editedPage && !furnitureEdit->footer)) {
            RenderFurniture(ctx, *frame.header, pageView ? frame.headerTop : 0.0f);
        }
        if (frame.footer && !(editedPage && furnitureEdit->footer)) RenderFurniture(ctx, *frame.footer, frame.footerTop);
    }
}

void UltraCanvasRichTextEdit::RenderFurniture(IRenderContext* ctx, const FurnitureLayout& furniture, float top) {
    for (size_t i = 0; i < furniture.layouts.size(); i++) {
        const BlockLayout& bl = furniture.layouts[i];
        const float originY = visibleArea.y + top + bl.bounds.y - scrollOffset;
        if (originY > visibleArea.y + visibleArea.height || originY + bl.bounds.height < visibleArea.y) continue;
        RenderBlock(ctx, furniture.blocks, static_cast<int>(i), bl, ColumnLeft(), originY, -1);
    }
}

void UltraCanvasRichTextEdit::RenderBlock(IRenderContext* ctx, int blockIndex,
                                          const BlockLayout& bl) {
    RenderBlockPieces(ctx, editor.GetDocument()->blocks, blockIndex, bl, blockIndex);
}

// A block at its place in the content, in however many page pieces it has.
// `selectionIndex` is its index for selection and links, -1 for none.
void UltraCanvasRichTextEdit::RenderBlockPieces(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks,
                                                int blockIndex, const BlockLayout& bl, int selectionIndex) {
    if (bl.slices.empty()) {
        RenderBlock(ctx, blocks, blockIndex, bl, BlockLeft(bl),
                    visibleArea.y + bl.bounds.y - scrollOffset, selectionIndex);
        return;
    }
    // A block running over pages: each piece drawn through a clip of its own,
    // the block shifted so that piece's part of it shows there. A table's
    // header rows are drawn again above every piece after the first.
    const double left = visibleArea.x;
    const double width = visibleArea.width;
    const float viewTop = scrollOffset, viewBottom = scrollOffset + visibleArea.height;
    for (const BlockLayout::PageSlice& slice : bl.slices) {
        const float height = slice.to - slice.from;
        if (slice.top + slice.headerHeight + height < viewTop || slice.top > viewBottom) continue;
        if (slice.headerHeight > 0.0f) {
            ctx->PushState();
            ctx->ClipRect(Rect2Dd(left, visibleArea.y + slice.top - scrollOffset, width, slice.headerHeight));
            RenderBlock(ctx, blocks, blockIndex, bl, BlockLeft(bl),
                        visibleArea.y + slice.top - scrollOffset, selectionIndex);
            ctx->PopState();
        }
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(left, visibleArea.y + slice.top + slice.headerHeight - scrollOffset, width, height));
        RenderBlock(ctx, blocks, blockIndex, bl, BlockLeft(bl),
                    visibleArea.y + slice.top + slice.headerHeight - slice.from - scrollOffset, selectionIndex);
        ctx->PopState();
    }
}

void UltraCanvasRichTextEdit::RenderBlock(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks,
                                          int index, const BlockLayout& bl,
                                          float originX, float originY, int blockIndex) {
    const RichDocBlock& block = blocks[static_cast<size_t>(index)];
    float textX = originX + bl.textLeft;
    DrawQuoteBars(ctx, blocks, index, bl, originX, originY);
    const float ruleX = originX + QuoteInset(block);

    switch (block.type) {
        case RichBlockType::HorizontalRule: {
            float centerY = originY + bl.bounds.height / 2.0f;
            ctx->DrawLine(Point2Dd(ruleX, centerY),
                          Point2Dd(originX + ColumnWidth(), centerY), style.ruleColor);
            if (blockIndex >= 0) DrawSelectionForNonTextBlock(ctx, blockIndex, bl, originY);
            return;
        }
        case RichBlockType::PageBreak: {
            // In page view the break is the page's end; only an editable
            // view marks where it sits.
            if (pageView && !ShowsEditingMarks()) return;
            float centerY = originY + bl.bounds.height / 2.0f;
            ctx->PushState();
            ctx->SetLineDash(UCDashPattern({4.0, 3.0}));
            ctx->DrawLine(Point2Dd(ruleX, centerY),
                          Point2Dd(originX + ColumnWidth(), centerY), style.pageBreakColor);
            ctx->PopState();
            if (blockIndex >= 0) DrawSelectionForNonTextBlock(ctx, blockIndex, bl, originY);
            return;
        }
        case RichBlockType::Image: {
            Rect2Dd target(textX, originY, bl.bounds.width, bl.bounds.height);
            if (bl.image) {
                ctx->DrawImage(*bl.image, target, ImageFitMode::Contain);
            } else {
                // Missing or undecodable media: a framed box with the alt text,
                // so the document's structure stays visible and editable.
                ctx->DrawFilledRectangle(target, Colors::Transparent, 1.0f,
                                         style.imagePlaceholderColor);
                if (!block.imageAltText.empty()) {
                    ctx->PushState();
                    ctx->SetFontStyle(style.baseFont);
                    ctx->SetTextPaint(style.imagePlaceholderColor);
                    ctx->DrawTextInRect(block.imageAltText, target);
                    ctx->PopState();
                }
            }
            if (blockIndex >= 0) DrawSelectionForNonTextBlock(ctx, blockIndex, bl, originY);
            return;
        }
        case RichBlockType::Table: {
            // A block of selected cells: each one filled whole, under its text.
            std::vector<RichDocPosition> selectedCells;
            if (blockIndex >= 0 && !printing && editor.HasCellSelection()
                && editor.GetCaret().blockIndex == blockIndex) {
                selectedCells = editor.SelectedCells();
            }
            for (size_t i = 0; i < bl.cells.size(); i++) {
                const BlockLayout* cell = bl.cells[i].get();
                if (!cell || !cell->layout) continue;
                Rect2Dd cellRect(originX + cell->bounds.x, originY + cell->bounds.y,
                                 cell->bounds.width, cell->bounds.height);
                const RichTableCell* modelCell = nullptr;
                if (i < bl.cellRows.size() && i < bl.cellColumns.size()) {
                    const size_t r = static_cast<size_t>(bl.cellRows[i]);
                    const size_t c = static_cast<size_t>(bl.cellColumns[i]);
                    if (r < block.tableRows.size() && c < block.tableRows[r].cells.size()) {
                        modelCell = &block.tableRows[r].cells[c];
                    }
                }
                if (block.tableBordersFromDocument && modelCell) {
                    DrawDocumentCellFrame(ctx, *modelCell, cellRect);
                } else {
                    ctx->DrawFilledRectangle(cellRect, Colors::Transparent, 1.0f, style.tableBorderColor);
                }
                for (const RichDocPosition& selected : selectedCells) {
                    if (selected.cellRow == bl.cellRows[i] && selected.cellColumn == bl.cellColumns[i]) {
                        ctx->DrawFilledRectangle(cellRect, style.selectionColor, 0.0f, Colors::Transparent);
                        break;
                    }
                }
                ctx->SetCurrentPaint(style.textColor);
                const Point2Dd textAt(cellRect.x + cell->textLeft, cellRect.y + cell->textTop);
                ctx->DrawTextLayout(*cell->layout, textAt);
                DrawInlineImages(ctx, *cell, static_cast<float>(textAt.x), static_cast<float>(textAt.y));
            }

            if (blockIndex >= 0) DrawSelectionForNonTextBlock(ctx, blockIndex, bl, originY);
            return;
        }
        default:
            break;
    }

    if (block.HasParagraphFrame()) DrawParagraphFrame(ctx, blocks, index, bl, originX, originY);

    if (block.type == RichBlockType::BlockQuote) {
        ctx->DrawFilledRectangle(Rect2Dd(originX, originY, 3.0, bl.bounds.height),
                                 style.quoteBarColor, 0.0f, Colors::Transparent);
    } else if (block.type == RichBlockType::CodeBlock) {
        ctx->DrawFilledRectangle(Rect2Dd(originX, originY, ColumnWidth(), bl.bounds.height),
                                 style.codeBackgroundColor, 1.0f, style.codeBorderColor);
    }

    if (bl.checkbox) {
        const Rect2Df box = CheckboxRect(block, bl);
        const Rect2Dd rect(originX + box.x, originY + box.y, box.width, box.height);
        ctx->DrawFilledRectangle(rect, style.backgroundColor, 1.0f, style.listMarkerColor, 2.0f);
        if (bl.checked) {
            // A tick: down to the lower third, then up to the far corner.
            ctx->PushState();
            ctx->SetStrokeWidth(std::max(1.5, box.width / 7.0));
            const Point2Dd a(rect.x + rect.width * 0.22, rect.y + rect.height * 0.52);
            const Point2Dd b(rect.x + rect.width * 0.42, rect.y + rect.height * 0.74);
            const Point2Dd c(rect.x + rect.width * 0.80, rect.y + rect.height * 0.28);
            ctx->DrawLine(a, b, style.listMarkerColor);
            ctx->DrawLine(b, c, style.listMarkerColor);
            ctx->PopState();
        }
    } else if (!bl.markerText.empty()) {
        const FontStyle markerFont = MarkerFontFor(block);
        ctx->PushState();
        ctx->SetFontStyle(markerFont);
        ctx->SetTextPaint(style.listMarkerColor);
        // At the usual marker position, unless the label is too wide to fit
        // before the text there ("1.2.3.", "(viii)"): then it moves left to
        // end a small gap before the text, but never past the column edge.
        const double width = static_cast<double>(ctx->GetTextLineWidth(bl.markerText));
        const double gap = std::max(4.0, markerFont.fontSize * 0.4);
        double markerX = std::max(static_cast<double>(originX),
                                  std::min(static_cast<double>(originX + bl.markerLeft),
                                           static_cast<double>(originX + bl.textLeft) - gap - width));
        if (bl.markerOnRight) {
            // Mirrored: just right of where the text ends, within the indent.
            const double textRight = originX + bl.textLeft + (bl.layout ? bl.layout->GetExplicitWidth() : 0.0);
            markerX = std::max(textRight + gap, static_cast<double>(originX + bl.markerLeft) - width);
        }
        ctx->DrawText(bl.markerText, Point2Dd(markerX, originY));
        ctx->PopState();
    }

    if (bl.displayMath) {
        const double width = bl.displayMath->GetWidth();
        const double x = originX + std::max(0.0, (static_cast<double>(ColumnWidth()) - width) * 0.5);
        bl.displayMath->Draw(ctx, x, originY + 4.0 + bl.displayMath->GetAscent());
    }
    if (bl.layout) {
        ctx->SetCurrentPaint(style.textColor);
        ctx->DrawTextLayout(*bl.layout, Point2Dd(textX, originY));
        DrawInlineImages(ctx, bl, static_cast<float>(textX), static_cast<float>(originY));
    }
}

// A cell of a table whose borders come from a document: its fill, then each
// side it has a line on, at the line's width and colour. Where a side has
// none, an editable view draws a faint guide so the cell can still be seen
// (Writer's "table boundaries"); a read-only view draws nothing there.
void UltraCanvasRichTextEdit::DrawDocumentCellFrame(IRenderContext* ctx, const RichTableCell& cell,
                                                   const Rect2Dd& rect) const {
    if (!cell.backgroundColor.empty()) {
        ctx->DrawFilledRectangle(rect, ParseHexColor(cell.backgroundColor, Colors::Transparent),
                                 0.0f, Colors::Transparent);
    }
    const double left = rect.x, top = rect.y;
    const double right = rect.x + rect.width, bottom = rect.y + rect.height;
    auto side = [&](const RichBorder& border, const Point2Dd& from, const Point2Dd& to) {
        if (border.IsVisible()) {
            DrawBorderLine(ctx, border, from, to, ParseHexColor(border.color, style.textColor));
        } else if (ShowsEditingMarks()) {
            ctx->PushState();
            ctx->SetStrokeWidth(1.0);
            ctx->DrawLine(from, to, style.tableGuideColor);
            ctx->PopState();
        }
    };
    side(cell.borderTop, Point2Dd(left, top), Point2Dd(right, top));
    side(cell.borderBottom, Point2Dd(left, bottom), Point2Dd(right, bottom));
    side(cell.borderLeft, Point2Dd(left, top), Point2Dd(left, bottom));
    side(cell.borderRight, Point2Dd(right, top), Point2Dd(right, bottom));
}

// A paragraph's frame and fill, just outside its text. A run of paragraphs
// with the same frame is one box: the fill closes the gaps between them, and
// only the first draws the top line and only the last the bottom one.
void UltraCanvasRichTextEdit::DrawParagraphFrame(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks,
                                                 int blockIndex, const BlockLayout& bl,
                                                 float originX, float originY) const {
    const size_t at = static_cast<size_t>(blockIndex);
    const RichDocBlock& block = blocks[at];
    const bool withPrevious = at > 0 && blocks[at - 1].HasParagraphFrame()
                              && blocks[at - 1].SameParagraphFrame(block);
    const bool withNext = at + 1 < blocks.size() && blocks[at + 1].HasParagraphFrame()
                          && blocks[at + 1].SameParagraphFrame(block);
    const float padding = kParagraphFramePadding;
    const double left = originX + QuoteInset(block) + Px(std::max(0.0f, block.leftIndentPt)) - padding;
    const double right = originX + ColumnWidth() - Px(std::max(0.0f, block.rightIndentPt));
    // Grouped paragraphs meet halfway across the gap between them.
    const float gapBelow = GapAfterBlock(blocks, blockIndex);
    const float gapAbove = blockIndex > 0 ? GapAfterBlock(blocks, blockIndex - 1) : 0.0f;
    const double top = originY - (withPrevious ? gapAbove * 0.5f : padding);
    const double bottom = originY + bl.bounds.height + (withNext ? gapBelow * 0.5f : padding);

    if (!block.paragraphBackground.empty()) {
        ctx->DrawFilledRectangle(Rect2Dd(left, top, right - left, bottom - top),
                                 ParseHexColor(block.paragraphBackground, Colors::Transparent),
                                 0.0f, Colors::Transparent);
    }
    auto line = [&](const RichBorder& border, const Point2Dd& from, const Point2Dd& to) {
        if (!border.IsVisible()) return;
        DrawBorderLine(ctx, border, from, to, ParseHexColor(border.color, style.textColor));
    };
    if (!withPrevious) line(block.paragraphBorderTop, Point2Dd(left, top), Point2Dd(right, top));
    if (!withNext) line(block.paragraphBorderBottom, Point2Dd(left, bottom), Point2Dd(right, bottom));
    line(block.paragraphBorderLeft, Point2Dd(left, top), Point2Dd(left, bottom));
    line(block.paragraphBorderRight, Point2Dd(right, top), Point2Dd(right, bottom));
}

void UltraCanvasRichTextEdit::DrawSelectionForNonTextBlock(IRenderContext* ctx, int blockIndex,
                                                            const BlockLayout& bl, float originY) {
    // Blocks with no text cannot carry a selection attribute, so a selection
    // that swallows one is shown as a translucent wash over its box.
    if (!editor.HasSelection() || printing) return;
    // A selected picture shows its frame and handles instead.
    if (HasSelectedImage() && !selectedImage.InCell() && selectedImage.blockIndex == blockIndex) return;
    RichDocRange range = editor.GetSelectionRange();
    RichDocPosition here(blockIndex, 0);
    if (here < range.start || !(here < range.end)) return;
    Color wash = style.selectionColor;
    wash.a = 110;
    // At `originY`, where this call draws the block: for a block running over
    // pages that is shifted per piece, and clipped to it.
    ctx->DrawFilledRectangle(Rect2Dd(BlockLeft(bl), originY, bl.builtWidth > 0.0f ? bl.builtWidth : ColumnWidth(), bl.bounds.height),
                             wash, 0.0f, Colors::Transparent);
}

void UltraCanvasRichTextEdit::DrawScrollbar(IRenderContext* ctx) {
    if (contentHeight <= visibleArea.height) {
        thumbRect = Rect2Df(0, 0, 0, 0);
        return;
    }
    Rect2Df bounds = GetLocalBounds();
    float trackX = bounds.x + bounds.width - style.scrollbarWidth;
    Rect2Dd track(trackX, bounds.y, style.scrollbarWidth, bounds.height);
    ctx->DrawFilledRectangle(track, Color(245, 245, 245), 0.0f, Colors::Transparent);

    float ratio = visibleArea.height / contentHeight;
    float thumbHeight = std::max(24.0f, bounds.height * ratio);
    float maxScroll = contentHeight - visibleArea.height;
    float travel = bounds.height - thumbHeight;
    float thumbY = bounds.y + (maxScroll > 0 ? (scrollOffset / maxScroll) * travel : 0.0f);

    thumbRect = Rect2Df(trackX + 2.0f, thumbY, style.scrollbarWidth - 4.0f, thumbHeight);
    ctx->DrawFilledRectangle(Rect2Dd(thumbRect.x, thumbRect.y, thumbRect.width, thumbRect.height),
                             Color(180, 180, 180), 0.0f, Colors::Transparent, 3.0f);
}

void UltraCanvasRichTextEdit::DrawHorizontalScrollbar(IRenderContext* ctx) {
    if (!needsHorizontalScrollbar || MaxHorizontalScroll() <= 0.0f) {
        hThumbRect = Rect2Df(0, 0, 0, 0);
        return;
    }
    Rect2Df bounds = GetLocalBounds();
    const float bar = style.scrollbarWidth;
    const float trackWidth = bounds.width - (thumbRect.width > 0 ? bar : 0.0f);
    const float trackY = bounds.y + bounds.height - bar;
    ctx->DrawFilledRectangle(Rect2Dd(bounds.x, trackY, trackWidth, bar), Color(245, 245, 245), 0.0f, Colors::Transparent);
    const float ratio = visibleArea.width / ContentWidth();
    const float thumbWidth = std::max(24.0f, trackWidth * ratio);
    const float travel = trackWidth - thumbWidth;
    const float thumbX = bounds.x + (MaxHorizontalScroll() > 0 ? hScrollOffset / MaxHorizontalScroll() * travel : 0.0f);
    hThumbRect = Rect2Df(thumbX, trackY + 2.0f, thumbWidth, bar - 4.0f);
    ctx->DrawFilledRectangle(Rect2Dd(hThumbRect.x, hThumbRect.y, hThumbRect.width, hThumbRect.height),
                             Color(180, 180, 180), 0.0f, Colors::Transparent, 3.0f);
}

void UltraCanvasRichTextEdit::UpdateCaret() {
    auto& caret = UltraCanvasCaret::GetInstance();
    Rect2Df rect = CaretRect();
    if (rect.width <= 0 && rect.height <= 0) { caret.Hide(this); return; }

    if (rect.y + rect.height < visibleArea.y || rect.y > visibleArea.y + visibleArea.height
        || rect.x < visibleArea.x - 1.0f || rect.x > visibleArea.x + visibleArea.width + 1.0f) {
        caret.Hide(this);
        return;
    }
    rect = ToElement(rect);
    Point2Df windowPos = GetPositionInWindow();
    Rect2Di rectInWindow(static_cast<int>(windowPos.x + rect.x),
                         static_cast<int>(windowPos.y + rect.y),
                         2, static_cast<int>(std::max(4.0f, rect.height)));
    caret.Show(this, rectInWindow, style.cursorColor);
}

// ===== PICTURES =====

void UltraCanvasRichTextEdit::ForEachImage(
        const std::function<void(const RichDocPosition&, const Rect2Df&)>& visit) const {
    const auto& blocks = editor.GetDocument()->blocks;
    // originY is the layout's top in its block's own coordinates; the block
    // maps it to where it is drawn (through its page pieces).
    auto inlineImages = [&](const BlockLayout& owner, const BlockLayout& layout, float originX, float originY,
                            RichDocPosition at) {
        if (!layout.layout) return;
        for (const BlockLayout::InlineImage& placed : layout.inlineImages) {
            if (placed.math || placed.floating) continue;
            const Rect2Di box = layout.layout->IndexToPos(placed.byteOffset);
            at.byteOffset = placed.byteOffset;
            const float y = visibleArea.y + BlockToContentY(owner, originY + static_cast<float>(box.y)) - scrollOffset;
            visit(at, Rect2Df(originX + static_cast<float>(box.x), y, placed.width, placed.height));
        }
    };
    for (const PlacedFloat& placed : placedFloats) {
        visit(RichDocPosition(placed.blockIndex, placed.byteOffset),
              Rect2Df(ColumnLeft() + placed.rect.x, visibleArea.y + placed.rect.y - scrollOffset,
                      placed.rect.width, placed.rect.height));
    }
    for (size_t i = 0; i < blockLayouts.size() && i < blocks.size(); i++) {
        const BlockLayout& bl = blockLayouts[i];
        if (!bl.valid) continue;
        const float top = visibleArea.y + bl.bounds.y - scrollOffset;
        const float bottom = visibleArea.y + BlockVisualBottom(bl) - scrollOffset;
        if (top > visibleArea.y + visibleArea.height || bottom < visibleArea.y) continue;
        const int index = static_cast<int>(i);
        if (blocks[i].type == RichBlockType::Image) {
            visit(RichDocPosition(index, 0), Rect2Df(BlockLeft(bl) + bl.textLeft, top, bl.bounds.width, bl.bounds.height));
            continue;
        }
        inlineImages(bl, bl, BlockLeft(bl) + bl.textLeft, 0.0f, RichDocPosition(index, 0));
        for (size_t c = 0; c < bl.cells.size(); c++) {
            const BlockLayout& cell = *bl.cells[c];
            inlineImages(bl, cell, BlockLeft(bl) + cell.bounds.x + cell.textLeft, cell.bounds.y + cell.textTop,
                         RichDocPosition(index, bl.cellRows[c], bl.cellColumns[c], 0));
        }
    }
}

bool UltraCanvasRichTextEdit::ImageAtPoint(const Point2Df& localPoint, RichDocPosition& outImage,
                                           Rect2Df& outRect) const {
    bool found = false;
    ForEachImage([&](const RichDocPosition& at, const Rect2Df& rect) {
        if (found) return;
        if (localPoint.x >= rect.x && localPoint.x <= rect.x + rect.width
            && localPoint.y >= rect.y && localPoint.y <= rect.y + rect.height) {
            outImage = at;
            outRect = rect;
            found = true;
        }
    });
    return found;
}

bool UltraCanvasRichTextEdit::ImageRectFor(const RichDocPosition& image, Rect2Df& outRect) const {
    bool found = false;
    ForEachImage([&](const RichDocPosition& at, const Rect2Df& rect) {
        if (!found && at == image) {
            outRect = rect;
            found = true;
        }
    });
    return found;
}

std::array<Point2Df, 8> UltraCanvasRichTextEdit::ImageHandleCentres(const Rect2Df& r) {
    const float cx = r.x + r.width * 0.5f, cy = r.y + r.height * 0.5f;
    const float right = r.x + r.width, bottom = r.y + r.height;
    return {Point2Df(r.x, r.y), Point2Df(cx, r.y), Point2Df(right, r.y), Point2Df(right, cy),
            Point2Df(right, bottom), Point2Df(cx, bottom), Point2Df(r.x, bottom), Point2Df(r.x, cy)};
}

int UltraCanvasRichTextEdit::ImageHandleAt(const Rect2Df& imageRect, const Point2Df& p) const {
    const std::array<Point2Df, 8> centres = ImageHandleCentres(imageRect);
    for (int i = 0; i < 8; i++) {
        if (std::abs(p.x - centres[static_cast<size_t>(i)].x) <= 6.0f
            && std::abs(p.y - centres[static_cast<size_t>(i)].y) <= 6.0f) {
            return i;
        }
    }
    return -1;
}

// The picture's rectangle while handle `resizeHandle` is dragged to
// `pointer`: the opposite side stays put; a corner keeps the proportions.
Rect2Df UltraCanvasRichTextEdit::ResizedImageRect(const Point2Df& pointer) const {
    const Rect2Df& r = resizeStartRect;
    float left = r.x, top = r.y, right = r.x + r.width, bottom = r.y + r.height;
    const float dx = pointer.x - resizeStartPoint.x, dy = pointer.y - resizeStartPoint.y;
    const int h = resizeHandle;
    const bool west = h == 0 || h == 6 || h == 7, east = h == 2 || h == 3 || h == 4;
    const bool north = h == 0 || h == 1 || h == 2, south = h == 4 || h == 5 || h == 6;
    if (west) left += dx;
    if (east) right += dx;
    if (north) top += dy;
    if (south) bottom += dy;
    const float minimum = 8.0f;
    float width = std::max(minimum, right - left);
    float height = std::max(minimum, bottom - top);
    width = std::min(width, std::max(minimum, ColumnWidth()));
    const bool corner = (west || east) && (north || south);
    if (corner && r.width > 0.0f && r.height > 0.0f) {
        // The larger of the two changes decides; the other follows it.
        const float scale = std::max(width / r.width, height / r.height);
        width = std::min(std::max(minimum, r.width * scale), std::max(minimum, ColumnWidth()));
        height = std::max(minimum, width * r.height / r.width);
    }
    const float x = west ? r.x + r.width - width : r.x;
    const float y = north ? r.y + r.height - height : r.y;
    return Rect2Df(x, y, width, height);
}

bool UltraCanvasRichTextEdit::HasSelectedImage() const {
    return imageSelected && editor.GetAnchor() == imageSelectionAnchor && editor.GetCaret() == imageSelectionCaret
        && editor.IsImageAt(selectedImage);
}

bool UltraCanvasRichTextEdit::SelectImage(const RichDocPosition& image) {
    if (!editor.IsImageAt(image)) return false;
    if (!image.InCell() && editor.GetBlock(image.blockIndex).type == RichBlockType::Image) {
        // The block, up to the start of what follows it (so Delete removes it).
        RichDocPosition next(image.blockIndex, 0);
        if (image.blockIndex + 1 < editor.GetBlockCount()) next = RichDocPosition(image.blockIndex + 1, 0);
        editor.SetSelection(image, next);
    } else {
        RichDocPosition end = image;
        end.byteOffset += static_cast<int>(std::string(RichTextRun::kObjectReplacement).size());
        editor.SetSelection(image, end);
    }
    selectedImage = image;
    imageSelectionAnchor = editor.GetAnchor();
    imageSelectionCaret = editor.GetCaret();
    imageSelected = true;
    editor.BreakUndoCoalescing();
    AfterSelectionChange();
    return true;
}

bool UltraCanvasRichTextEdit::SetSelectedImageSize(float widthPt, float heightPt) {
    if (readOnly || !HasSelectedImage()) return false;
    if (!editor.SetImageSize(selectedImage, widthPt, heightPt)) return false;
    AfterEdit();
    return true;
}

std::string UltraCanvasRichTextEdit::GetSelectedImageAltText() const {
    float w = 0, h = 0;
    int media = -1;
    std::string alt;
    if (HasSelectedImage()) editor.GetImageInfo(selectedImage, w, h, alt, media);
    return alt;
}

bool UltraCanvasRichTextEdit::SetSelectedImageAltText(const std::string& altText) {
    if (readOnly || !HasSelectedImage()) return false;
    if (!editor.SetImageAltText(selectedImage, altText)) return false;
    AfterEdit();
    return true;
}

void UltraCanvasRichTextEdit::DrawImageSelection(IRenderContext* ctx) {
    if (!HasSelectedImage() || printing) return;
    Rect2Df rect;
    if (resizeHandle >= 0) {
        rect = resizePreview;
    } else if (!ImageRectFor(selectedImage, rect)) {
        return;
    }
    const Color frame(40, 110, 220);
    ctx->DrawFilledRectangle(Rect2Dd(rect.x, rect.y, rect.width, rect.height), Colors::Transparent, 1.0f, frame);
    if (readOnly) return;
    for (const Point2Df& c : ImageHandleCentres(rect)) {
        ctx->DrawFilledRectangle(Rect2Dd(c.x - 3.5, c.y - 3.5, 7.0, 7.0), Colors::White, 1.0f, frame);
    }
}

// ===== HIT TESTING =====

Rect2Df UltraCanvasRichTextEdit::CaretRect() const {
    // While composing, the caret is in the composition.
    RichDocPosition caret = editor.GetCaret();
    if (!preeditText.empty()) caret.byteOffset += preeditCursor;
    return PositionRect(caret);
}

bool UltraCanvasRichTextEdit::VisualStep(const RichDocPosition& pos, int direction, RichDocPosition& out) const {
    if (pos.blockIndex < 0 || pos.blockIndex >= static_cast<int>(blockLayouts.size())) return false;
    const BlockLayout* bl = pos.InCell() ? CellLayoutFor(pos) : &blockLayouts[static_cast<size_t>(pos.blockIndex)];
    if (!bl || !bl->layout) return false;
    const std::string text = bl->layout->GetText();
    if (!UCRichDocumentEditor::ContainsRightToLeft(text)) return false;
    const UCCursorMoveResult moved = bl->layout->MoveCursorVisually(true, pos.byteOffset, 0, direction);
    // Off either end of the paragraph: the logical neighbour (previous or
    // next paragraph) takes over.
    if (moved.newIndex < 0 || moved.newIndex > static_cast<int>(text.size())) {
        const bool rtlBlock = UCRichDocumentEditor::FirstStrongDirection(text) > 0;
        const bool forward = (direction > 0) != rtlBlock;
        out = forward ? editor.NextCharacter(editor.ContainerEnd(pos)) : editor.PreviousCharacter(editor.ContainerStart(pos));
        if (out == pos) return false;
        return true;
    }
    int index = moved.newIndex;
    for (int t = 0; t < moved.newTrailing && index < static_cast<int>(text.size()); t++) {
        index = UCRichDocumentEditor::NextCharOffset(text, index);
    }
    out = pos;
    out.byteOffset = std::clamp(index, 0, static_cast<int>(text.size()));
    return true;
}

// ===== ACCESSIBILITY =====

std::vector<UltraCanvasRichTextEdit::AccessSegment> UltraCanvasRichTextEdit::AccessSegments(std::string* joined) const {
    std::vector<AccessSegment> segments;
    std::string text;
    int characters = 0;
    auto add = [&](const RichDocPosition& container, const std::string& content, const char* separator) {
        segments.push_back({container, content, characters});
        text += content;
        characters += UltraCanvasAccessibility::CharacterCount(content);
        if (separator && *separator) {
            text += separator;
            characters += 1;
        }
    };
    const auto& blocks = editor.GetDocument()->blocks;
    for (size_t b = 0; b < blocks.size(); b++) {
        const RichDocBlock& block = blocks[b];
        const bool last = b + 1 == blocks.size();
        if (block.type == RichBlockType::Table) {
            for (size_t r = 0; r < block.tableRows.size(); r++) {
                const auto& cells = block.tableRows[r].cells;
                for (size_t c = 0; c < cells.size(); c++) {
                    const bool rowEnd = c + 1 == cells.size();
                    const bool tableEnd = rowEnd && r + 1 == block.tableRows.size();
                    add(RichDocPosition(static_cast<int>(b), static_cast<int>(r), static_cast<int>(c), 0),
                        UCRichDocumentEditor::RunsText(cells[c].runs), tableEnd ? (last ? "" : "\n") : rowEnd ? "\n" : "\t");
                }
            }
            continue;
        }
        add(RichDocPosition(static_cast<int>(b), 0), editor.BlockText(static_cast<int>(b)), last ? "" : "\n");
    }
    if (joined) *joined = std::move(text);
    return segments;
}

int UltraCanvasRichTextEdit::AccessOffsetOf(const RichDocPosition& position) const {
    for (const AccessSegment& segment : AccessSegments()) {
        if (segment.container.SameContainer(position)) {
            return segment.charStart + UltraCanvasAccessibility::CharacterOffsetOfByte(
                                           segment.text, static_cast<size_t>(std::max(0, position.byteOffset)));
        }
    }
    return 0;
}

RichDocPosition UltraCanvasRichTextEdit::AccessPositionOf(int offset) const {
    const std::vector<AccessSegment> segments = AccessSegments();
    if (segments.empty()) return RichDocPosition(0, 0);
    size_t index = 0;
    for (size_t i = 0; i < segments.size(); i++) {
        if (segments[i].charStart <= offset) index = i;
    }
    const AccessSegment& segment = segments[index];
    RichDocPosition position = segment.container;
    const int within = std::clamp(offset - segment.charStart, 0, UltraCanvasAccessibility::CharacterCount(segment.text));
    position.byteOffset = static_cast<int>(UltraCanvasAccessibility::ByteOffsetOfCharacter(segment.text, within));
    return position;
}

void UltraCanvasRichTextEdit::AnnounceAccessibility(AccessibilityEventType type) {
    if (!UltraCanvasAccessibility::HasListeners()) return;
    AccessibilityEvent event;
    event.type = type;
    event.element = this;
    if (type == AccessibilityEventType::CaretMoved || type == AccessibilityEventType::SelectionChanged) {
        event.offset = AccessOffsetOf(editor.GetCaret());
    }
    UltraCanvasAccessibility::Notify(event);
}

std::string UltraCanvasRichTextEdit::GetAccessibleName() const {
    const auto& document = editor.GetDocument();
    if (document && !document->metadata.title.empty()) return document->metadata.title;
    return GetIdentifier();
}

class UltraCanvasRichTextEdit::AccessibleText : public IAccessibleText {
public:
    explicit AccessibleText(UltraCanvasRichTextEdit& owner) : edit(owner) {}

    std::string GetAccessibleText() const override {
        std::string text;
        edit.AccessSegments(&text);
        return text;
    }
    int GetCharacterCount() const override { return UltraCanvasAccessibility::CharacterCount(GetAccessibleText()); }
    int GetCaretOffset() const override { return edit.AccessOffsetOf(edit.editor.GetCaret()); }
    bool IsReadOnly() const override { return edit.IsReadOnly(); }
    bool SetCaretOffset(int offset) override {
        edit.editor.SetCaret(edit.AccessPositionOf(offset));
        edit.AfterSelectionChange();
        return true;
    }
    bool GetSelection(int& start, int& end) const override {
        if (!edit.editor.HasSelection()) {
            start = end = GetCaretOffset();
            return false;
        }
        const RichDocRange range = edit.editor.GetSelectionRange();
        start = edit.AccessOffsetOf(range.start);
        end = edit.AccessOffsetOf(range.end);
        return true;
    }
    bool SetSelection(int start, int end) override {
        edit.editor.SetSelection(edit.AccessPositionOf(start), edit.AccessPositionOf(end));
        edit.AfterSelectionChange();
        return true;
    }
    Rect2Df GetCharacterBounds(int offset) const override {
        const RichDocPosition from = edit.AccessPositionOf(offset);
        const Rect2Df caret = edit.ToElement(edit.PositionRect(from));
        if (caret.height <= 0.0f) return Rect2Df(0, 0, 0, 0);
        RichDocPosition next = edit.editor.NextCharacter(from);
        float width = 0.0f;
        if (next.SameContainer(from) && next.byteOffset > from.byteOffset) {
            const Rect2Df after = edit.ToElement(edit.PositionRect(next));
            if (std::abs(after.y - caret.y) < 0.5f) width = std::abs(after.x - caret.x);
        }
        const Point2Df origin = edit.GetPositionInWindow();
        return Rect2Df(origin.x + caret.x, origin.y + caret.y, width, caret.height);
    }
    int GetOffsetAtPoint(const Point2Df& windowPoint) const override {
        const Point2Df origin = edit.GetPositionInWindow();
        const Point2Df local(windowPoint.x - origin.x, windowPoint.y - origin.y);
        if (!edit.GetLocalBounds().Contains(local)) return -1;
        return edit.AccessOffsetOf(edit.PositionFromPoint(edit.ToDocument(local)));
    }
    AccessibleTextAttributes GetAttributesAt(int offset, int& runStart, int& runEnd) const override {
        AccessibleTextAttributes attributes;
        const RichDocPosition position = edit.AccessPositionOf(offset);
        runStart = runEnd = offset;
        const auto& blocks = edit.editor.GetDocument()->blocks;
        if (position.blockIndex < 0 || position.blockIndex >= static_cast<int>(blocks.size())) return attributes;
        const RichDocBlock& block = blocks[static_cast<size_t>(position.blockIndex)];
        attributes.headingLevel = block.type == RichBlockType::Heading ? block.headingLevel : 0;
        attributes.listItem = block.type == RichBlockType::ListItem;
        const std::vector<RichTextRun>* runs = &block.runs;
        if (position.InCell()) {
            runs = &block.tableRows[static_cast<size_t>(position.cellRow)].cells[static_cast<size_t>(position.cellColumn)].runs;
        }
        const int containerStart = offset - UltraCanvasAccessibility::CharacterOffsetOfByte(
                                                UCRichDocumentEditor::RunsText(*runs), static_cast<size_t>(position.byteOffset));
        std::string text = UCRichDocumentEditor::RunsText(*runs);
        int byte = 0;
        for (const RichTextRun& run : *runs) {
            const int start = byte + (run.lineBreakBefore ? 1 : 0);
            const int end = start + static_cast<int>(run.text.size());
            byte = end;
            if (position.byteOffset < start || (position.byteOffset >= end && &run != &runs->back())) continue;
            attributes.bold = run.bold;
            attributes.italic = run.italic;
            attributes.underline = run.underline;
            attributes.strikethrough = run.strikethrough;
            attributes.superscript = run.superscript;
            attributes.subscript = run.subscript;
            attributes.fontFamily = run.fontFamily;
            attributes.fontSizePt = run.fontSizePt;
            attributes.color = run.color;
            attributes.backgroundColor = run.highlightColor;
            attributes.link = run.linkTarget;
            attributes.inserted = run.change == RichTextRun::Change::Inserted;
            attributes.deleted = run.change == RichTextRun::Change::Deleted;
            attributes.commented = !run.commentIds.empty();
            runStart = containerStart + UltraCanvasAccessibility::CharacterOffsetOfByte(text, static_cast<size_t>(start));
            runEnd = containerStart + UltraCanvasAccessibility::CharacterOffsetOfByte(text, static_cast<size_t>(end));
            break;
        }
        return attributes;
    }
    std::string GetTextAtOffset(int offset, AccessibleTextBoundary boundary, int& start, int& end) const override {
        if (boundary != AccessibleTextBoundary::Line) return IAccessibleText::GetTextAtOffset(offset, boundary, start, end);
        // A line as laid out: from its first character to the first of the
        // next, found by walking the positions of the same height.
        const std::string text = GetAccessibleText();
        const int count = UltraCanvasAccessibility::CharacterCount(text);
        offset = std::clamp(offset, 0, std::max(0, count - 1));
        const float y = GetCharacterBounds(offset).y;
        start = offset;
        while (start > 0 && std::abs(GetCharacterBounds(start - 1).y - y) < 0.5f) start--;
        end = offset;
        while (end < count && std::abs(GetCharacterBounds(end).y - y) < 0.5f) end++;
        if (end == offset) end = offset + 1;
        const size_t from = UltraCanvasAccessibility::ByteOffsetOfCharacter(text, start);
        const size_t to = UltraCanvasAccessibility::ByteOffsetOfCharacter(text, end);
        return text.substr(from, to - from);
    }

private:
    UltraCanvasRichTextEdit& edit;
};

IAccessibleText* UltraCanvasRichTextEdit::GetAccessibleTextInterface() {
    if (!accessibleText) accessibleText = std::make_shared<AccessibleText>(*this);
    return accessibleText.get();
}

bool UltraCanvasRichTextEdit::HandleComposition(const UCEvent& event) {
    const bool starting = preeditText.empty() && !event.text.empty();
    // Composing over a selection replaces it, as typing would.
    if (starting && editor.HasSelection()) {
        editor.DeleteSelection();
        AfterEdit();
    }
    preeditText = event.text;
    preeditCursor = std::clamp(event.compositionCursor < 0 ? static_cast<int>(preeditText.size()) : event.compositionCursor,
                               0, static_cast<int>(preeditText.size()));
    InvalidateBlock(editor.GetCaret().blockIndex);
    caretMoved = true;
    RequestRedraw();
    return true;
}

Rect2Df UltraCanvasRichTextEdit::PositionRect(const RichDocPosition& position) const {
    if (position.blockIndex < 0 || position.blockIndex >= static_cast<int>(blockLayouts.size())) {
        return Rect2Df(0, 0, 0, 0);
    }
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(position.blockIndex)];
    float x = BlockLeft(bl) + bl.textLeft;
    float y = visibleArea.y + bl.bounds.y - scrollOffset;

    // Inside a table the caret belongs to a cell's layout, positioned at that
    // cell's origin rather than the table's.
    if (const BlockLayout* cell = CellLayoutFor(position)) {
        // Where the cell's text starts, as it is drawn (padding, vertical
        // alignment).
        const float cellX = BlockLeft(bl) + cell->bounds.x + cell->textLeft;
        const float cellTop = cell->bounds.y + cell->textTop;          // in the table's layout
        if (!cell->layout) {
            return Rect2Df(cellX, visibleArea.y + BlockToContentY(bl, cell->bounds.y) - scrollOffset,
                           2.0f, cell->bounds.height);
        }
        int cellLength = static_cast<int>(cell->layout->GetText().size());
        int cellOffset = std::max(0, std::min(position.byteOffset, cellLength));
        Rect2Di cursor = cell->layout->GetCursorPos(cellOffset).strongPos;
        float cellHeight = cursor.height > 0 ? static_cast<float>(cursor.height)
                                             : static_cast<float>(style.baseFont.fontSize) * 1.3f;
        return Rect2Df(cellX + static_cast<float>(cursor.x),
                       visibleArea.y + BlockToContentY(bl, cellTop + static_cast<float>(cursor.y)) - scrollOffset,
                       2.0f, cellHeight);
    }

    if (!bl.layout) {
        // Structural block: a full-height bar at its left edge.
        return Rect2Df(x, y, 2.0f, bl.bounds.height);
    }
    int length = static_cast<int>(bl.layout->GetText().size());
    int offset = std::max(0, std::min(position.byteOffset, length));
    Rect2Di cursor = bl.layout->GetCursorPos(offset).strongPos;
    float height = cursor.height > 0 ? static_cast<float>(cursor.height)
                                     : static_cast<float>(style.baseFont.fontSize) * 1.3f;
    (void)y;
    return Rect2Df(x + static_cast<float>(cursor.x),
                   visibleArea.y + BlockToContentY(bl, static_cast<float>(cursor.y)) - scrollOffset, 2.0f, height);
}

// The laid-out cell a position addresses, or null when it addresses a block's
// own runs (or the table's layout has not been built yet).
const UltraCanvasRichTextEdit::BlockLayout* UltraCanvasRichTextEdit::CellLayoutFor(
        const RichDocPosition& pos) const {
    if (!pos.InCell()) return nullptr;
    if (pos.blockIndex < 0 || pos.blockIndex >= static_cast<int>(blockLayouts.size())) return nullptr;
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(pos.blockIndex)];
    for (size_t i = 0; i < bl.cells.size(); i++) {
        if (bl.cellRows[i] == pos.cellRow && bl.cellColumns[i] == pos.cellColumn) {
            return bl.cells[i].get();
        }
    }
    return nullptr;
}

RichDocPosition UltraCanvasRichTextEdit::PositionFromPoint(const Point2Df& localPoint) const {
    if (blockLayouts.empty()) return RichDocPosition(0, 0);

    float contentY = localPoint.y - visibleArea.y + scrollOffset;
    float contentX = localPoint.x - ColumnLeft();

    int blockIndex = static_cast<int>(blockLayouts.size()) - 1;
    if (hasColumns) {
        // Blocks side by side in columns: the nearest one, across and down.
        float best = -1.0f;
        for (int i = 0; i < static_cast<int>(blockLayouts.size()); i++) {
            const BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
            const float width = bl.builtWidth > 0.0f ? bl.builtWidth : columnWidth;
            const float dx = std::max(0.0f, std::max(bl.columnX - contentX, contentX - (bl.columnX + width)));
            const float dy = std::max(0.0f, std::max(bl.bounds.y - contentY, contentY - BlockVisualBottom(bl)));
            const float distance = dx * dx * 4.0f + dy * dy;
            if (best < 0.0f || distance < best) {
                best = distance;
                blockIndex = i;
            }
        }
    } else {
        for (int i = 0; i < static_cast<int>(blockLayouts.size()); i++) {
            const BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
            // The gap below a block belongs half to it, half to the next one.
            const float bottom = BlockVisualBottom(bl);
            const float nextTop = i + 1 < static_cast<int>(blockLayouts.size())
                    ? blockLayouts[static_cast<size_t>(i + 1)].bounds.y : bottom;
            if (contentY < (bottom + nextTop) * 0.5f || contentY < bottom) {
                blockIndex = i;
                break;
            }
        }
    }
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(blockIndex)];
    contentX -= bl.columnX;

    // A table has no layout of its own; the click lands in one of its cells.
    // The nearest cell wins, so a click in the padding between cells still puts
    // the caret somewhere sensible rather than nowhere.
    // In the block's own layout coordinates (through its page pieces).
    const float layoutContentY = ContentToBlockY(bl, contentY);
    if (!bl.cells.empty()) {
        const float cellY = layoutContentY;
        size_t best = 0;
        float bestDistance = -1.0f;
        for (size_t i = 0; i < bl.cells.size(); i++) {
            const Rect2Df& cb = bl.cells[i]->bounds;
            const float dx = std::max(0.0f, std::max(cb.x - contentX, contentX - (cb.x + cb.width)));
            const float dy = std::max(0.0f, std::max(cb.y - cellY, cellY - (cb.y + cb.height)));
            const float distance = dx * dx + dy * dy;
            if (bestDistance < 0.0f || distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        const BlockLayout& cell = *bl.cells[best];
        RichDocPosition out(blockIndex, bl.cellRows[best], bl.cellColumns[best], 0);
        if (cell.layout) {
            int layoutX = static_cast<int>(contentX - cell.bounds.x - cell.textLeft);
            int layoutY = static_cast<int>(cellY - cell.bounds.y - cell.textTop);
            UCLayoutHitResult hit = cell.layout->XYToIndex(std::max(0, layoutX), std::max(0, layoutY));
            std::string text = cell.layout->GetText();
            int offset = hit.index;
            if (hit.trailing > 0) offset = UCRichDocumentEditor::NextCharOffset(text, offset);
            out.byteOffset = std::max(0, std::min(offset, static_cast<int>(text.size())));
        }
        return out;
    }

    if (!bl.layout) return RichDocPosition(blockIndex, 0);

    int layoutX = static_cast<int>(contentX - bl.textLeft);
    int layoutY = static_cast<int>(layoutContentY);
    UCLayoutHitResult hit = bl.layout->XYToIndex(std::max(0, layoutX), std::max(0, layoutY));

    std::string text = bl.layout->GetText();
    int offset = hit.index;
    if (hit.trailing > 0) offset = UCRichDocumentEditor::NextCharOffset(text, offset);
    offset = std::max(0, std::min(offset, static_cast<int>(text.size())));
    return RichDocPosition(blockIndex, offset);
}

const RichTextHitRect* UltraCanvasRichTextEdit::LinkAtPoint(const Point2Df& localPoint) const {
    float contentY = localPoint.y - visibleArea.y + scrollOffset;
    float contentX = localPoint.x - ColumnLeft();
    for (const BlockLayout& bl : blockLayouts) {
        if (contentY < bl.bounds.y || contentY > BlockVisualBottom(bl)) continue;
        for (const RichTextHitRect& hit : bl.hitRects) {
            Rect2Df box(bl.columnX + bl.textLeft + hit.bounds.x, BlockToContentY(bl, hit.bounds.y),
                        hit.bounds.width, hit.bounds.height);
            if (contentX >= box.x && contentX <= box.x + box.width
                && contentY >= box.y && contentY <= box.y + box.height) {
                return &hit;
            }
        }
    }
    return nullptr;
}

float UltraCanvasRichTextEdit::CaretLayoutX() const {
    RichDocPosition position = editor.GetCaret();
    if (position.blockIndex < 0 || position.blockIndex >= static_cast<int>(blockLayouts.size())) {
        return 0.0f;
    }
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(position.blockIndex)];
    if (!bl.layout) return 0.0f;
    int length = static_cast<int>(bl.layout->GetText().size());
    int offset = std::max(0, std::min(position.byteOffset, length));
    return static_cast<float>(bl.layout->GetCursorPos(offset).strongPos.x);
}

RichDocPosition UltraCanvasRichTextEdit::VerticalStep(const RichDocPosition& pos,
                                                       int direction) const {
    // Walk by visual line: within a wrapped block first, then into the
    // neighbouring block, keeping the goal x so the caret tracks a column.
    if (pos.blockIndex < 0 || pos.blockIndex >= static_cast<int>(blockLayouts.size())) return pos;
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(pos.blockIndex)];

    float wantX = goalColumnX;
    if (bl.layout && wantX < 0) {
        wantX = static_cast<float>(bl.layout->GetCursorPos(pos.byteOffset).strongPos.x);
    }
    if (wantX < 0) wantX = 0;

    if (bl.layout) {
        Rect2Di here = bl.layout->GetCursorPos(pos.byteOffset).strongPos;
        int targetY = here.y + direction * std::max(1, here.height);
        if (targetY >= 0 && targetY < static_cast<int>(bl.layout->GetLayoutHeight())) {
            UCLayoutHitResult hit = bl.layout->XYToIndex(static_cast<int>(wantX), targetY);
            std::string text = bl.layout->GetText();
            int offset = hit.index;
            if (hit.trailing > 0) offset = UCRichDocumentEditor::NextCharOffset(text, offset);
            return RichDocPosition(pos.blockIndex, std::min(offset, static_cast<int>(text.size())));
        }
    }

    int nextBlock = pos.blockIndex + direction;
    if (nextBlock < 0 || nextBlock >= static_cast<int>(blockLayouts.size())) {
        return direction < 0 ? RichDocPosition(pos.blockIndex, 0)
                             : RichDocPosition(pos.blockIndex, editor.BlockTextLength(pos.blockIndex));
    }
    const BlockLayout& target = blockLayouts[static_cast<size_t>(nextBlock)];
    if (!target.layout) return RichDocPosition(nextBlock, 0);

    int lineY = (direction > 0) ? 0
                                : std::max(0, static_cast<int>(target.layout->GetLayoutHeight()) - 1);
    UCLayoutHitResult hit = target.layout->XYToIndex(static_cast<int>(wantX), lineY);
    std::string text = target.layout->GetText();
    int offset = hit.index;
    if (hit.trailing > 0) offset = UCRichDocumentEditor::NextCharOffset(text, offset);
    return RichDocPosition(nextBlock, std::min(offset, static_cast<int>(text.size())));
}

// ===== SCROLLING =====

void UltraCanvasRichTextEdit::SetScrollOffset(float offset) {
    float maxScroll = std::max(0.0f, contentHeight - visibleArea.height);
    float clamped = std::max(0.0f, std::min(offset, maxScroll));
    if (std::abs(clamped - scrollOffset) < 0.01f) return;
    scrollOffset = clamped;
    layoutsDirty = true;      // new blocks may scroll into view
    RequestRedraw();
}

void UltraCanvasRichTextEdit::ScrollToTop() {
    SetScrollOffset(0.0f);
}

void UltraCanvasRichTextEdit::ScrollToCaret() {
    const RichDocPosition caret = editor.GetCaret();
    int blockIndex = caret.blockIndex;
    if (blockIndex < 0 || blockIndex >= static_cast<int>(blockLayouts.size())) return;
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(blockIndex)];

    float top = bl.bounds.y;
    float bottom = BlockVisualBottom(bl);
    if (const BlockLayout* cell = CellLayoutFor(caret)) {
        // Scroll to the line inside the cell, not to the whole table — a tall
        // table would otherwise jump the view to its top on every keystroke.
        top = BlockToContentY(bl, cell->bounds.y);
        bottom = top + cell->bounds.height;
        if (cell->layout) {
            Rect2Di cursor = cell->layout->GetCursorPos(caret.byteOffset).strongPos;
            top = BlockToContentY(bl, cell->bounds.y + cell->textTop + static_cast<float>(cursor.y));
            bottom = top + static_cast<float>(std::max(cursor.height, 4));
        }
    } else if (bl.layout) {
        Rect2Di cursor = bl.layout->GetCursorPos(caret.byteOffset).strongPos;
        top = BlockToContentY(bl, static_cast<float>(cursor.y));
        bottom = top + static_cast<float>(std::max(cursor.height, 4));
    }
    if (top < scrollOffset) {
        SetScrollOffset(top);
    } else if (bottom > scrollOffset + visibleArea.height) {
        SetScrollOffset(bottom - visibleArea.height);
    }
    // Sideways too, when the page is wider than the view.
    if (MaxHorizontalScroll() > 0.0f) {
        const Rect2Df at = CaretRect();
        const float margin = 16.0f;
        if (at.x < visibleArea.x + margin) {
            SetHorizontalScrollOffset(hScrollOffset - (visibleArea.x + margin - at.x));
        } else if (at.x > visibleArea.x + visibleArea.width - margin) {
            SetHorizontalScrollOffset(hScrollOffset + (at.x - (visibleArea.x + visibleArea.width - margin)));
        }
    }
}

// ===== EDIT PLUMBING =====

// ===== SEARCH =====

void UltraCanvasRichTextEdit::SelectMatch(const RichDocRange& match) {
    editor.SetSelection(match.start, match.end);
    // A match is a caret move by any other means, so a typing run ends here:
    // typing over a found word must undo separately from what came before.
    editor.BreakUndoCoalescing();
    AfterSelectionChange();
    if (onSelectionChanged) onSelectionChanged();
}

bool UltraCanvasRichTextEdit::FindNext(const std::string& needle) {
    // From the end of the selection, so pressing Find again moves on rather
    // than finding the match the user is already looking at.
    const RichDocPosition from = editor.HasSelection()
        ? editor.GetSelectionRange().end : editor.GetCaret();
    RichDocRange match;
    if (!editor.Find(needle, from, /*backwards*/ false, findOptions, match)) return false;
    SelectMatch(match);
    return true;
}

bool UltraCanvasRichTextEdit::FindPrevious(const std::string& needle) {
    const RichDocPosition from = editor.HasSelection()
        ? editor.GetSelectionRange().start : editor.GetCaret();
    RichDocRange match;
    if (!editor.Find(needle, from, /*backwards*/ true, findOptions, match)) return false;
    SelectMatch(match);
    return true;
}

bool UltraCanvasRichTextEdit::ReplaceCurrent(const std::string& needle,
                                             const std::string& replacement) {
    if (readOnly || needle.empty()) return false;

    // Only replace what the user can see is selected. Pressing Replace before
    // Find should find, not overwrite whatever happens to be selected.
    if (editor.HasSelection()) {
        const RichDocRange selection = editor.GetSelectionRange();
        const std::string selected = editor.RangeToPlainText(selection);
        bool isMatch = selected.size() == needle.size();
        if (isMatch && findOptions.caseSensitive) {
            isMatch = selected == needle;
        } else if (isMatch) {
            for (size_t i = 0; i < needle.size() && isMatch; i++) {
                isMatch = std::tolower(static_cast<unsigned char>(selected[i]))
                       == std::tolower(static_cast<unsigned char>(needle[i]));
            }
        }
        if (isMatch) {
            editor.ReplaceRange(selection, replacement);
            AfterEdit();
            QueueSpellCheck();
            if (onDocumentChanged) onDocumentChanged();
        }
    }
    return FindNext(needle);
}

int UltraCanvasRichTextEdit::ReplaceAll(const std::string& needle,
                                        const std::string& replacement) {
    if (readOnly) return 0;
    const int count = editor.ReplaceAll(needle, replacement, findOptions);
    if (count > 0) {
        AfterEdit();
        QueueSpellCheck();
        if (onDocumentChanged) onDocumentChanged();
    }
    return count;
}

int UltraCanvasRichTextEdit::CountMatches(const std::string& needle) const {
    return static_cast<int>(editor.FindAll(needle, findOptions).size());
}

// ===== SPELL CHECKING =====

// One string for the whole document, blocks joined by '\n' — so the checker
// runs one job rather than one per block, and sees sentence context across a
// block the way it would across a line.
std::string UltraCanvasRichTextEdit::BuildSpellText(std::vector<int>& outBlockStarts) const {
    std::string text;
    outBlockStarts.assign(static_cast<size_t>(editor.GetBlockCount()), 0);
    for (int i = 0; i < editor.GetBlockCount(); i++) {
        if (i) text += '\n';
        // Recorded for every block, text or not, so the index of a block is
        // always usable as an index into this table.
        outBlockStarts[static_cast<size_t>(i)] = static_cast<int>(text.size());
        text += editor.BlockText(i);
    }
    return text;
}

RichDocPosition UltraCanvasRichTextEdit::SpellBytePosition(size_t byteOffset) const {
    if (spellBlockStarts.empty()) return {0, 0};
    // The last block whose start is at or before byteOffset owns it.
    int block = 0;
    for (int i = 0; i < static_cast<int>(spellBlockStarts.size()); i++) {
        if (static_cast<size_t>(spellBlockStarts[static_cast<size_t>(i)]) > byteOffset) break;
        block = i;
    }
    const int within = static_cast<int>(byteOffset)
                     - spellBlockStarts[static_cast<size_t>(block)];
    return editor.ClampPosition(RichDocPosition(block, within));
}

void UltraCanvasRichTextEdit::SetSpellCheckEnabled(bool enabled) {
    if (spellCheckEnabled == enabled) return;
    spellCheckEnabled = enabled;

    if (spellContextId == 0) spellContextId = reinterpret_cast<uint64_t>(this);

    if (enabled) {
        RegisterSpellResultNotifier();
        QueueSpellCheck();
    } else {
        UltraCanvasSpellChecker::Instance().CancelContext(spellContextId);
        spellErrors.clear();
        RequestRedraw();
    }
}

void UltraCanvasRichTextEdit::SetSpellCheckOptions(const SpellCheckOptions& options) {
    spellOptions = options;
    QueueSpellCheck();
}

void UltraCanvasRichTextEdit::RunSpellCheck() {
    QueueSpellCheck();
}

// Results are drained while rendering, so a check that finishes after an edit's
// repaint would sit undelivered until something else redrew. This asks for that
// frame. The notifier runs on the worker thread and can outlive this element,
// so it marshals to the UI thread and both hops test the liveness flag the
// destructor clears — the same pattern UltraCanvasTextArea uses.
void UltraCanvasRichTextEdit::RegisterSpellResultNotifier() {
    if (spellContextId == 0) spellContextId = reinterpret_cast<uint64_t>(this);

    std::shared_ptr<std::atomic<bool>> alive = spellAlive;
    UltraCanvasRichTextEdit* self = this;

    UltraCanvasSpellChecker::Instance().SetContextNotifier(spellContextId,
        [self, alive]() {
            if (!alive->load()) return;
            auto* app = UltraCanvasApplication::GetInstance();
            if (!app) return;
            app->PostToUIThread([self, alive]() {
                if (!alive->load()) return;   // element destroyed meanwhile
                self->RequestRedraw();
            });
        });
}

void UltraCanvasRichTextEdit::QueueSpellCheck() {
    if (!spellCheckEnabled) return;

    UltraCanvasSpellChecker& service = UltraCanvasSpellChecker::Instance();
    if (!service.IsEnabled()) return;

    if (spellContextId == 0) spellContextId = reinterpret_cast<uint64_t>(this);

    DropStaleSpellErrors();

    std::vector<int> starts;
    std::string text = BuildSpellText(starts);

    // Options that depend on the text — shouldSkipRange above all — have to be
    // rebuilt from the text this check will actually run on, so the host gets a
    // copy to fill in rather than the stored options being mutated.
    SpellCheckOptions options = spellOptions;
    if (onPrepareSpellCheck) onPrepareSpellCheck(options, text);

    // Queuing replaces any job still pending for this element, so holding a key
    // down builds no backlog.
    service.QueueCheckText(spellContextId, text, options);
}

// The errors on hand describe the document as it was when the check ran. Until
// the next result arrives they would be painted against the edited text, so any
// whose span no longer holds the word it was raised for is dropped now. Keeping
// the ones that still match matters: typing at the end of a document leaves
// every earlier mark in place, so squiggles stay put instead of blinking off
// and back on at every keystroke.
void UltraCanvasRichTextEdit::DropStaleSpellErrors() {
    if (spellErrors.empty()) return;

    std::vector<int> starts;
    const std::string current = BuildSpellText(starts);

    const size_t sizeBefore = spellErrors.size();
    spellErrors.erase(
        std::remove_if(spellErrors.begin(), spellErrors.end(),
                       [&current](const SpellError& error) {
                           if (error.startByte + error.byteLength > current.size()) {
                               return true;
                           }
                           return current.compare(error.startByte, error.byteLength,
                                                  error.word) != 0;
                       }),
        spellErrors.end());

    if (spellErrors.size() != sizeBefore) {
        spellText = current;
        spellBlockStarts = std::move(starts);
        RequestRedraw();
    }
}

const SpellError* UltraCanvasRichTextEdit::GetSpellErrorAtPosition(int x, int y) {
    if (!spellCheckEnabled || spellErrors.empty()) return nullptr;

    const RichDocPosition hit = PositionFromPoint(ToDocument(Point2Df(static_cast<float>(x),
                                                                      static_cast<float>(y))));
    if (hit.blockIndex < 0
        || hit.blockIndex >= static_cast<int>(spellBlockStarts.size())) {
        return nullptr;
    }
    const size_t byteOffset =
        static_cast<size_t>(spellBlockStarts[static_cast<size_t>(hit.blockIndex)]
                            + hit.byteOffset);
    return SpellCheckText::FindErrorAtByteOffset(spellErrors, byteOffset);
}

bool UltraCanvasRichTextEdit::ApplySpellSuggestion(const SpellError& error,
                                                   const std::string& replacement) {
    if (readOnly) return false;

    // `error` may point into spellErrors, which the edit below rebuilds, so the
    // span is copied out before anything is replaced.
    const size_t startByte = error.startByte;
    const size_t byteLength = error.byteLength;

    const RichDocPosition start = SpellBytePosition(startByte);
    const RichDocPosition end = SpellBytePosition(startByte + byteLength);
    if (start.blockIndex != end.blockIndex) return false;   // spans a block: not a word

    editor.ReplaceRange(RichDocRange(start, end), replacement);
    AfterEdit();
    QueueSpellCheck();
    if (onDocumentChanged) onDocumentChanged();
    return true;
}

bool UltraCanvasRichTextEdit::ShowSpellSuggestionMenu(const UCEvent& event) {
    if (!spellCheckEnabled) return false;

    const SpellError* hit = GetSpellErrorAtPosition(event.pointer.x, event.pointer.y);
    if (!hit) return false;

    auto window = GetWindow();
    if (!window) return false;

    // The menu outlives this call, and applying a suggestion re-runs the check
    // and rebuilds spellErrors — which `hit` points into. Copy the error so the
    // callbacks below never dereference freed storage.
    const SpellError error = *hit;

    spellSuggestionMenu = std::make_shared<UltraCanvasMenu>(
        "RichTextEditSpellSuggestions", 0, 0, 220, 0);
    spellSuggestionMenu->SetMenuType(MenuType::PopupMenu);

    std::weak_ptr<UltraCanvasRichTextEdit> weakSelf =
        std::static_pointer_cast<UltraCanvasRichTextEdit>(shared_from_this());

    for (MenuItemData& item : UltraCanvasSpellChecker::BuildSuggestionMenuItems(
             error,
             [weakSelf, error](const std::string& replacement) {
                 if (auto self = weakSelf.lock()) {
                     self->ApplySpellSuggestion(error, replacement);
                 }
             },
             [weakSelf]() {
                 if (auto self = weakSelf.lock()) self->RunSpellCheck();
             })) {
        spellSuggestionMenu->AddItem(std::move(item));
    }

    spellSuggestionMenu->OpenMenu(event.pointerWindow, *window, PopupElementSettings());
    return true;
}

// Element-local rectangles for a byte range of one block — one per visual line
// the range crosses, so a word broken across a wrap still gets a full mark.
std::vector<Rect2Df> UltraCanvasRichTextEdit::BlockRangeRects(int blockIndex,
                                                              int startByte,
                                                              int endByte) const {
    std::vector<Rect2Df> rects;
    if (blockIndex < 0 || blockIndex >= static_cast<int>(blockLayouts.size())) return rects;
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(blockIndex)];
    if (!bl.layout || endByte <= startByte) return rects;

    const float originX = BlockLeft(bl) + bl.textLeft;

    for (const LayoutLineRange& line : bl.layout->GetLineByteRanges()) {
        const int from = std::max(startByte, line.startByte);
        const int to = std::min(endByte, line.startByte + line.lengthBytes);
        if (to <= from) continue;

        const Rect2Di head = bl.layout->IndexToPos(from);
        const Rect2Di tail = bl.layout->IndexToPos(to);
        // A trailing position at a line end reports the next line's origin, so
        // the width is taken from the head's line rather than across the wrap.
        float right = static_cast<float>(tail.x);
        if (tail.y != head.y) right = static_cast<float>(head.x + head.width);
        const float left = static_cast<float>(head.x);
        if (right <= left) continue;

        rects.emplace_back(originX + left,
                           visibleArea.y + BlockToContentY(bl, static_cast<float>(head.y)) - scrollOffset,
                           right - left, static_cast<float>(head.height));
    }
    return rects;
}

void UltraCanvasRichTextEdit::DrawSpellErrorMarks(IRenderContext* ctx) {
    if (!ctx || !spellCheckEnabled) return;

    UltraCanvasSpellChecker& service = UltraCanvasSpellChecker::Instance();

    // Drain anything the worker finished since the last frame. The block table
    // is rebuilt with it: the offsets in a fresh result index the text as it is
    // now, which is the text the marks are about to be drawn against.
    SpellCheckResult fresh;
    if (service.TryTakeResult(spellContextId, fresh)) {
        spellErrors = std::move(fresh.errors);
        spellText = BuildSpellText(spellBlockStarts);
    }
    if (spellErrors.empty()) return;

    const SpellCheckStyle markStyle = service.GetStyle();
    const float viewTop = scrollOffset;
    const float viewBottom = scrollOffset + visibleArea.height;

    ctx->PushState();
    for (const SpellError& error : spellErrors) {
        const RichDocPosition start = SpellBytePosition(error.startByte);
        const RichDocPosition end = SpellBytePosition(error.startByte + error.byteLength);
        if (start.blockIndex != end.blockIndex) continue;
        if (start.blockIndex >= static_cast<int>(blockLayouts.size())) continue;

        // Blocks outside the viewport have no built layout to measure against.
        const BlockLayout& bl = blockLayouts[static_cast<size_t>(start.blockIndex)];
        if (!bl.valid || BlockVisualBottom(bl) < viewTop) continue;
        if (bl.bounds.y > viewBottom) { if (hasColumns) continue; break; }

        for (const Rect2Df& wordBounds :
             BlockRangeRects(start.blockIndex, start.byteOffset, end.byteOffset)) {
            SpellCheckRendering::DrawSpellErrorMark(ctx, wordBounds, markStyle, error.kind);
        }
    }
    ctx->PopState();
}

void UltraCanvasRichTextEdit::AfterEdit() {
    // Marking the layouts dirty is not enough: the rebuild pass only rebuilds
    // blocks whose cached layout has been invalidated, and an edit that leaves
    // the caret where it was invalidates nothing. Such a block would keep
    // showing its old text - a paragraph centred with a collapsed caret stayed
    // left-aligned until something else moved the caret. So the blocks the
    // editor just changed are invalidated here, by name.
    int firstChanged = -1, lastChanged = -1;
    editor.GetLastChangedBlocks(firstChanged, lastChanged);
    if (firstChanged < 0) {
        for (auto& bl : blockLayouts) bl.valid = false;
    } else {
        for (int i = firstChanged; i <= lastChanged; i++) {
            if (i >= 0 && i < static_cast<int>(blockLayouts.size())) {
                blockLayouts[static_cast<size_t>(i)].valid = false;
            }
        }
    }
    layoutsDirty = true;
    caretMoved = true;
    QueueSpellCheck();
    RequestRedraw();
}

void UltraCanvasRichTextEdit::AfterSelectionChange() {
    layoutsDirty = true;
    caretMoved = true;
    RequestRedraw();
}

// ===== INPUT =====

bool UltraCanvasRichTextEdit::OnEvent(const UCEvent& event) {
    if (IsDisabled() || !IsVisible()) return false;

    switch (event.type) {
        case UCEventType::MouseDown:        return HandleMouseDown(event);
        case UCEventType::MouseUp:          return HandleMouseUp(event);
        case UCEventType::MouseMove:        return HandleMouseMove(event);
        case UCEventType::MouseDoubleClick: return HandleDoubleClick(event);
        case UCEventType::MouseWheel:       return HandleMouseWheel(event);
        case UCEventType::KeyDown:
            if (readOnly) return false;
            // Typed (committed) text ends any composition.
            if (!preeditText.empty() && !event.text.empty()) {
                preeditText.clear();
                InvalidateBlock(editor.GetCaret().blockIndex);
            }
            return HandleKeyDown(event);
        case UCEventType::TextComposition:  return readOnly ? false : HandleComposition(event);
        case UCEventType::DragEnter:
        case UCEventType::DragOver:
            if (readOnly || !Contains(event.pointer)) return false;
            // Show where a dropped file would go, with the same mark as an
            // internal drag.
            draggingText = true;
            dropPosition = PositionFromPoint(ToDocument(event.pointer));
            RequestRedraw();
            return true;
        case UCEventType::DragLeave:
            if (draggingText && !dragArmed) {
                draggingText = false;
                RequestRedraw();
            }
            return false;
        case UCEventType::Drop:
            return HandleFileDrop(event);
        case UCEventType::FocusGained:
            RequestRedraw();
            return true;
        case UCEventType::FocusLost:
            UltraCanvasCaret::GetInstance().Hide(this);
            editor.BreakUndoCoalescing();
            RequestRedraw();
            return true;
        default:
            return false;
    }
}

bool UltraCanvasRichTextEdit::HandleFileDrop(const UCEvent& event) {
    if (!dragArmed) draggingText = false;
    if (readOnly || !Contains(event.pointer) || event.droppedFiles.empty()) {
        RequestRedraw();
        return false;
    }
    const RichDocPosition at = PositionFromPoint(ToDocument(event.pointer));
    if (onFilesDropped && onFilesDropped(event.droppedFiles, at)) {
        RequestRedraw();
        return true;
    }
    bool inserted = false;
    editor.SetCaret(at, false);
    for (const std::string& path : event.droppedFiles) {
        const std::string mime = UCRichDocument::MimeTypeForImageName(path);
        if (mime.rfind("image/", 0) != 0) continue;
        inserted = InsertInlineImageFromFile(path) || inserted;
    }
    if (!inserted) AfterSelectionChange();
    return inserted;
}

bool UltraCanvasRichTextEdit::HandleMouseDown(const UCEvent& event) {
    if (!Contains(event.pointer)) return false;

    // The comment pane: a click on a comment selects its text; the pane
    // itself is not text.
    if (commentPaneShown && static_cast<float>(event.pointer.x) >= CommentPaneLeft()
        && static_cast<float>(event.pointer.x) < CommentPaneLeft() + style.commentPaneWidth) {
        const int index = CommentBoxAt(event.pointer);
        RichDocRange range;
        if (index >= 0 && !furnitureEdit && editor.CommentRange(index, range)) {
            editor.SetSelection(range.start, range.end);
            AfterSelectionChange();
            if (event.type == UCEventType::MouseDoubleClick && onCommentActivated) onCommentActivated(index);
        }
        if (!IsFocused()) SetFocus(true);
        return true;
    }

    // While a header or footer is edited, a click outside it leaves it and
    // lands in the body as any click would.
    if (furnitureEdit && !blockLayouts.empty()) {
        const float contentY = ToDocument(event.pointer).y - visibleArea.y + scrollOffset;
        const float top = blockLayouts.front().bounds.y - 8.0f;
        const float bottom = blockLayouts.back().bounds.y + blockLayouts.back().bounds.height + 8.0f;
        if (contentY < top || contentY > bottom) FinishHeaderFooterEditing();
    }

    if (event.button == UCMouseButton::Right) {
        if (!IsFocused()) SetFocus(true);
        // A click inside the selection keeps it, so a host menu's Cut and Copy
        // still act on what is highlighted; a click outside moves the caret so
        // that Paste lands where the user clicked. The hit test runs before
        // either, while the layouts still describe what was on screen.
        const RichDocPosition hit = PositionFromPoint(ToDocument(event.pointer));
        // A right-click on a picture selects it, so a host menu can offer
        // what applies to a picture (alt text, size).
        {
            RichDocPosition image;
            Rect2Df rect;
            if (ImageAtPoint(ToDocument(event.pointer), image, rect) && !(HasSelectedImage() && selectedImage == image)) {
                SelectImage(image);
            }
        }
        const bool insideSelection =
            editor.HasSelection() && editor.GetSelectionRange().Contains(hit);

        // The host gets first refusal, which is how it puts the spell
        // suggestions inside its own context menu rather than a competing one.
        const bool handled = (onContextMenu && onContextMenu(event))
                          || ShowSpellSuggestionMenu(event);
        if (handled) {
            if (!insideSelection) {
                editor.SetCaret(hit, false);
                AfterSelectionChange();
            }
            return true;
        }
        return false;
    }

    if (hThumbRect.width > 0 && hThumbRect.Contains(event.pointer)) {
        draggingHThumb = true;
        hThumbGrabOffset = static_cast<float>(event.pointer.x) - hThumbRect.x;
        UltraCanvasApplication::GetInstance()->CaptureMouse(this);
        return true;
    }
    if (thumbRect.width > 0 && thumbRect.Contains(event.pointer)) {
        draggingThumb = true;
        thumbGrabOffset = static_cast<float>(event.pointerGlobal.y) - GetYInWindow() - thumbRect.y;
        UltraCanvasApplication::GetInstance()->CaptureMouse(this);
        return true;
    }
    if (!IsFocused()) SetFocus(true);

    if (!readOnly && HasSelectedImage()) {
        Rect2Df rect;
        if (ImageRectFor(selectedImage, rect)) {
            const int handle = ImageHandleAt(rect, ToDocument(event.pointer));
            if (handle >= 0) {
                resizeHandle = handle;
                resizeStartRect = rect;
                resizeStartPoint = ToDocument(event.pointer);
                resizePreview = rect;
                UltraCanvasApplication::GetInstance()->CaptureMouse(this);
                return true;
            }
        }
    }
    {
        RichDocPosition image;
        Rect2Df rect;
        if (!event.shift && ImageAtPoint(ToDocument(event.pointer), image, rect)) {
            SelectImage(image);
            return true;
        }
    }

    if (!readOnly) {
        const int box = CheckboxAtPoint(ToDocument(event.pointer));
        if (box >= 0 && editor.ToggleChecked(box)) {
            AfterEdit();
            return true;
        }
    }

    if (const RichTextHitRect* link = LinkAtPoint(ToDocument(event.pointer))) {
        // Ctrl+click follows a link; a plain click places the caret, so a link
        // stays editable text rather than a trap. A link to "#name" inside
        // the document goes to that bookmark unless the host takes it.
        if (event.ctrl && onLinkClicked && onLinkClicked(link->linkTarget)) return true;
        if (event.ctrl && link->linkTarget.size() > 1 && link->linkTarget[0] == '#'
            && GoToBookmark(link->linkTarget.substr(1))) return true;
    }
    if (event.ctrl && !furnitureEdit) {
        const std::string target = BookmarkTargetAt(PositionFromPoint(ToDocument(event.pointer)));
        if (!target.empty() && GoToBookmark(target)) return true;
    }

    RichDocPosition position = PositionFromPoint(ToDocument(event.pointer));
    // A press inside the selection may be the start of dragging it away; it
    // is decided on the first move (or on release, a plain click).
    if (enableDragAndDrop && !readOnly && !event.shift && editor.HasSelection() && !editor.HasCellSelection()
        && editor.GetSelectionRange().start < position && position < editor.GetSelectionRange().end) {
        dragArmed = true;
        draggingText = false;
        dragStartPoint = Point2Df(static_cast<float>(event.pointer.x), static_cast<float>(event.pointer.y));
        dropPosition = position;
        UltraCanvasApplication::GetInstance()->CaptureMouse(this);
        return true;
    }
    editor.SetCaret(position, event.shift);
    goalColumnX = -1.0f;
    selecting = true;
    UltraCanvasApplication::GetInstance()->CaptureMouse(this);
    AfterSelectionChange();
    return true;
}

bool UltraCanvasRichTextEdit::HandleMouseUp(const UCEvent& event) {
    if (resizeHandle >= 0) {
        const Rect2Df rect = ResizedImageRect(ToDocument(event.pointer));
        resizeHandle = -1;
        UltraCanvasApplication::GetInstance()->ReleaseMouse();
        const RichDocPosition image = selectedImage;
        if (std::abs(rect.width - resizeStartRect.width) >= 1.0f
            || std::abs(rect.height - resizeStartRect.height) >= 1.0f) {
            if (editor.SetImageSize(image, rect.width / kPixelsPerPoint, rect.height / kPixelsPerPoint)) {
                AfterEdit();
            }
        }
        RequestRedraw();
        return true;
    }
    if (dragArmed) {
        dragArmed = false;
        UltraCanvasApplication::GetInstance()->ReleaseMouse();
        const RichDocPosition here = PositionFromPoint(ToDocument(event.pointer));
        if (draggingText) {
            draggingText = false;
            SetMouseCursor(UCMouseCursor::Text);
            if (editor.MoveRange(editor.GetSelectionRange(), here, /*copy*/ event.ctrl)) {
                AfterEdit();
            } else {
                RequestRedraw();
            }
        } else {
            // Pressed and released without dragging: an ordinary click.
            editor.SetCaret(here, false);
            goalColumnX = -1.0f;
            AfterSelectionChange();
        }
        return true;
    }
    if (draggingThumb || selecting || draggingHThumb) {
        draggingHThumb = false;
        draggingThumb = false;
        selecting = false;
        UltraCanvasApplication::GetInstance()->ReleaseMouse();
        return true;
    }
    return false;
}

bool UltraCanvasRichTextEdit::HandleMouseMove(const UCEvent& event) {
    if (draggingThumb) {
        Rect2Df bounds = GetLocalBounds();
        float thumbHeight = std::max(24.0f, thumbRect.height);
        float travel = std::max(1.0f, bounds.height - thumbHeight);
        float maxScroll = std::max(0.0f, contentHeight - visibleArea.height);
        float thumbY = static_cast<float>(event.pointerGlobal.y) - GetYInWindow()
                     - thumbGrabOffset - bounds.y;
        thumbY = std::max(0.0f, std::min(thumbY, travel));
        SetScrollOffset((thumbY / travel) * maxScroll);
        return true;
    }
    if (draggingHThumb) {
        Rect2Df bounds = GetLocalBounds();
        const float trackWidth = bounds.width - (thumbRect.width > 0 ? style.scrollbarWidth : 0.0f);
        const float travel = std::max(1.0f, trackWidth - hThumbRect.width);
        const float thumbX = std::clamp(static_cast<float>(event.pointer.x) - hThumbGrabOffset - bounds.x, 0.0f, travel);
        SetHorizontalScrollOffset(thumbX / travel * MaxHorizontalScroll());
        return true;
    }
    if (resizeHandle >= 0) {
        resizePreview = ResizedImageRect(ToDocument(event.pointer));
        RequestRedraw();
        return true;
    }
    // Over a selected picture's handle, the cursor says which way it resizes.
    if (!selecting && HasSelectedImage() && !readOnly) {
        Rect2Df rect;
        int handle = -1;
        if (ImageRectFor(selectedImage, rect)) handle = ImageHandleAt(rect, ToDocument(event.pointer));
        static const UCMouseCursor cursors[8] = {
            UCMouseCursor::SizeNWSE, UCMouseCursor::SizeNS, UCMouseCursor::SizeNESW, UCMouseCursor::SizeWE,
            UCMouseCursor::SizeNWSE, UCMouseCursor::SizeNS, UCMouseCursor::SizeNESW, UCMouseCursor::SizeWE};
        SetMouseCursor(handle >= 0 ? cursors[handle] : UCMouseCursor::Text);
    }
    if (dragArmed) {
        const float dx = static_cast<float>(event.pointer.x) - dragStartPoint.x;
        const float dy = static_cast<float>(event.pointer.y) - dragStartPoint.y;
        if (!draggingText && dx * dx + dy * dy >= 16.0f) {
            draggingText = true;
            SetMouseCursor(UCMouseCursor::Hand);
        }
        if (draggingText) {
            // Near the top or bottom edge the view scrolls along.
            const float pointerY = ToDocument(event.pointer).y;
            if (pointerY < visibleArea.y + 8.0f) SetScrollOffset(scrollOffset - 12.0f);
            else if (pointerY > visibleArea.y + visibleArea.height - 8.0f) SetScrollOffset(scrollOffset + 12.0f);
            dropPosition = PositionFromPoint(ToDocument(event.pointer));
            RequestRedraw();
        }
        return true;
    }
    if (selecting) {
        editor.SetCaret(PositionFromPoint(ToDocument(event.pointer)), /*extend*/ true);
        goalColumnX = -1.0f;
        AfterSelectionChange();
        return true;
    }
    return false;
}

bool UltraCanvasRichTextEdit::HandleDoubleClick(const UCEvent& event) {
    if (!Contains(event.pointer)) return false;
    if (commentPaneShown && static_cast<float>(event.pointer.x) >= CommentPaneLeft()) {
        const int index = CommentBoxAt(event.pointer);
        if (index >= 0 && onCommentActivated) onCommentActivated(index);
        return true;
    }
    // A double-click on a header or footer (or the margin where one would
    // be) edits it; one on the body while editing one goes back.
    if (!readOnly) {
        const float contentY = ToDocument(event.pointer).y - visibleArea.y + scrollOffset;
        int page = 0;
        bool footer = false;
        const bool inFurniture = FurnitureRegionAt(contentY, page, footer);
        // A note, or a note's reference, opens the note.
        const int noteArea = NoteAreaAt(contentY);
        if (!furnitureEdit) {
            const int referenced = noteArea >= 0 ? noteArea : editor.NoteAt(PositionFromPoint(ToDocument(event.pointer)));
            if (referenced >= 0 && EditNote(referenced)) return true;
        }
        if (IsEditingNote()) {
            if (noteArea != furnitureEdit->noteIndex) {
                FinishHeaderFooterEditing();
                return true;
            }
        } else {
            if (!furnitureEdit && inFurniture && (pageView || !pages.empty())) {
                if (BeginFurnitureEditing(page, footer)) return true;
            }
            if (furnitureEdit && !(inFurniture && page == furnitureEdit->pageIndex && footer == furnitureEdit->footer)) {
                FinishHeaderFooterEditing();
                return true;
            }
        }
    }
    editor.SelectWordAt(PositionFromPoint(ToDocument(event.pointer)));
    AfterSelectionChange();
    return true;
}

bool UltraCanvasRichTextEdit::HandleMouseWheel(const UCEvent& event) {
    if (!Contains(event.pointer)) return false;
    if (event.ctrl) {
        SetZoom(zoom * (event.wheelDelta > 0 ? 1.1f : 1.0f / 1.1f));
        return true;
    }
    float step = static_cast<float>(style.baseFont.fontSize) * 3.0f;
    if (event.shift && MaxHorizontalScroll() > 0.0f) {
        SetHorizontalScrollOffset(hScrollOffset - (event.wheelDelta > 0 ? step : -step));
        return true;
    }
    SetScrollOffset(scrollOffset - (event.wheelDelta > 0 ? step : -step));
    return true;
}

bool UltraCanvasRichTextEdit::HandleKeyDown(const UCEvent& event) {
    UltraCanvasCaret::GetInstance().ResetBlink(this);
    bool handled = true;
    bool keepGoalColumn = false;

    const RichDocPosition caret = editor.GetCaret();

    switch (event.virtualKey) {
        case UCKeys::Escape:
            if (furnitureEdit) {
                FinishHeaderFooterEditing();
                return true;
            }
            handled = false;
            break;
        case UCKeys::Left:
        case UCKeys::Right: {
            const bool right = event.virtualKey == UCKeys::Right;
            RichDocPosition target;
            if (!event.ctrl && VisualStep(caret, right ? 1 : -1, target)) {
                // Text with right-to-left letters: the arrow moves the way it
                // points, whichever way the text runs.
                editor.SetCaret(target, event.shift);
            } else if (right) {
                editor.SetCaret(event.ctrl ? editor.NextWord(caret) : editor.NextCharacter(caret), event.shift);
            } else {
                editor.SetCaret(event.ctrl ? editor.PreviousWord(caret) : editor.PreviousCharacter(caret), event.shift);
            }
            break;
        }
        case UCKeys::Up:
        case UCKeys::Down: {
            if (goalColumnX < 0) goalColumnX = CaretLayoutX();
            int direction = (event.virtualKey == UCKeys::Up) ? -1 : +1;
            editor.SetCaret(VerticalStep(caret, direction), event.shift);
            keepGoalColumn = true;
            break;
        }
        case UCKeys::Home:
            editor.SetCaret(event.ctrl ? editor.DocumentStart() : editor.BlockStart(caret),
                            event.shift);
            break;
        case UCKeys::End:
            editor.SetCaret(event.ctrl ? editor.DocumentEnd() : editor.BlockEnd(caret), event.shift);
            break;
        case UCKeys::PageUp:
        case UCKeys::PageDown: {
            int direction = (event.virtualKey == UCKeys::PageUp) ? -1 : 1;
            float page = visibleArea.height;
            SetScrollOffset(scrollOffset + direction * page);
            Point2Df probe(ColumnLeft() + 4.0f,
                           visibleArea.y + (direction < 0 ? 4.0f : visibleArea.height - 4.0f));
            editor.SetCaret(PositionFromPoint(probe), event.shift);
            break;
        }
        case UCKeys::Backspace:
            editor.DeleteBackward();
            break;
        case UCKeys::Delete:
            editor.DeleteForward();
            break;
        case UCKeys::Enter:
            if (event.shift) {
                editor.InsertLineBreak();
            } else {
                editor.TypeEnter();
            }
            break;
        case UCKeys::Tab:
            // Inside a table Tab walks the cells, which is what every word
            // processor does and the only way to reach a cell from the keyboard.
            if (caret.InCell()) {
                RichDocPosition target = caret;
                const bool moved = event.shift ? editor.PreviousContainer(target)
                                               : editor.NextContainer(target);
                // Only within this table: Tab out of the last cell is left to
                // focus traversal, as it is everywhere else.
                if (moved && target.blockIndex == caret.blockIndex && target.InCell()) {
                    editor.SetCaret(event.shift ? editor.ContainerEnd(target) : target);
                    AfterSelectionChange();
                } else {
                    handled = false;
                }
            } else if (editor.GetBlock(caret.blockIndex).type == RichBlockType::ListItem) {
                if (event.shift) editor.OutdentList(); else editor.IndentList();
            } else {
                handled = false;
            }
            break;
        case UCKeys::A:
            if (event.ctrl) { editor.SelectAll(); } else { handled = false; }
            break;
        case UCKeys::C:
            if (event.ctrl) { Copy(); } else { handled = false; }
            break;
        case UCKeys::X:
            if (event.ctrl) { Cut(); } else { handled = false; }
            break;
        case UCKeys::V:
            if (event.ctrl) { Paste(); } else { handled = false; }
            break;
        case UCKeys::Z:
            if (event.ctrl) {
                if (event.shift) editor.Redo(); else editor.Undo();
            } else {
                handled = false;
            }
            break;
        case UCKeys::Y:
            if (event.ctrl) { editor.Redo(); } else { handled = false; }
            break;
        case UCKeys::B:
            if (event.ctrl) { ToggleBold(); } else { handled = false; }
            break;
        case UCKeys::I:
            if (event.ctrl) { ToggleItalic(); } else { handled = false; }
            break;
        case UCKeys::U:
            if (event.ctrl) { ToggleUnderline(); } else { handled = false; }
            break;
        default:
            handled = false;
            break;
    }

    if (!handled && !event.ctrl && !event.alt && !event.text.empty()) {
        editor.TypeText(event.text);
        handled = true;
    }
    if (handled) {
        if (!keepGoalColumn) goalColumnX = -1.0f;
        AfterEdit();
    }
    return handled;
}

// ===== EDITING API =====

void UltraCanvasRichTextEdit::InsertText(const std::string& utf8) {
    if (readOnly) return;
    editor.InsertText(utf8);
    AfterEdit();
}

void UltraCanvasRichTextEdit::SelectAll() {
    editor.SelectAll();
    AfterSelectionChange();
}

std::string UltraCanvasRichTextEdit::GetSelectedText() const {
    if (!editor.HasSelection()) return {};
    return editor.RangeToPlainText(editor.GetSelectionRange());
}

void UltraCanvasRichTextEdit::Copy() {
    if (!editor.HasSelection()) return;
    RichDocRange range = editor.GetSelectionRange();
    internalClipboard = editor.ExtractRange(range);
    internalClipboardText = editor.RangeToPlainText(range);
    internalClipboardSource = editor.GetDocument().get();
    internalClipboardMedia = editor.GetDocument()->media;
    // Other applications get it formatted, as HTML (pictures inlined), next
    // to the plain text.
    UCRichDocument fragment;
    fragment.blocks = internalClipboard;
    fragment.media = internalClipboardMedia;
    for (RichDocBlock& block : fragment.blocks) {
        for (RichTextRun& run : block.runs) run.noteIndex = -1;
    }
    SetClipboardHtml(fragment.ToHTML(), internalClipboardText);
}

void UltraCanvasRichTextEdit::AdoptForeignBlocks(std::vector<RichDocBlock>& blocks, const std::vector<RichDocMedia>& media) {
    UCRichDocument& document = *editor.GetDocument();
    std::map<int, int> mapped;
    auto remap = [&](int& index) {
        if (index < 0) return;
        if (index >= static_cast<int>(media.size())) {
            index = -1;
            return;
        }
        auto found = mapped.find(index);
        if (found == mapped.end()) {
            const RichDocMedia& source = media[static_cast<size_t>(index)];
            found = mapped.emplace(index, document.AddMedia(source.name, source.mimeType, source.data)).first;
        }
        index = found->second;
    };
    auto adopt = [&](std::vector<RichTextRun>& runs) {
        // A reference whose note stayed behind would be a bare number.
        runs.erase(std::remove_if(runs.begin(), runs.end(), [](const RichTextRun& r) { return r.IsNoteReference(); }),
                   runs.end());
        for (RichTextRun& run : runs) {
            remap(run.mediaIndex);
            run.commentIds.clear();
            run.revision = -1;
            // Text deleted there is not text here; inserted text simply is.
            run.change = RichTextRun::Change::Unchanged;
        }
    };
    for (RichDocBlock& block : blocks) {
        remap(block.mediaIndex);
        block.bookmarks.clear();
        adopt(block.runs);
        for (RichTableRow& row : block.tableRows) {
            for (RichTableCell& cell : row.cells) adopt(cell.runs);
        }
    }
}

void UltraCanvasRichTextEdit::Cut() {
    if (readOnly || !editor.HasSelection()) return;
    Copy();
    editor.DeleteSelection();
    AfterEdit();
}

void UltraCanvasRichTextEdit::Paste() {
    if (readOnly) return;
    std::string text;
    if (!GetClipboardText(text)) return;

    // Formatting survives a copy/paste inside the application: the system
    // clipboard carries the plain text, and when it still matches what was
    // copied the richer payload is used instead.
    if (!internalClipboard.empty() && text == internalClipboardText) {
        if (internalClipboardSource == editor.GetDocument().get()) {
            editor.InsertBlocks(internalClipboard);
        } else {
            std::vector<RichDocBlock> blocks = internalClipboard;
            AdoptForeignBlocks(blocks, internalClipboardMedia);
            editor.InsertBlocks(blocks);
        }
        AfterEdit();
        return;
    }
    // From another application: its HTML, when it put some there.
    std::string html;
    if (GetClipboardHtml(html) && !html.empty()) {
        UCRichDocument pasted = UCRichDocument::FromHTML(html);
        if (!pasted.blocks.empty()) {
            AdoptForeignBlocks(pasted.blocks, pasted.media);
            editor.InsertBlocks(pasted.blocks);
            AfterEdit();
            return;
        }
    }
    if (!text.empty()) editor.InsertText(text);
    AfterEdit();
}

void UltraCanvasRichTextEdit::DeleteSelection() {
    if (readOnly) return;
    editor.DeleteSelection();
    AfterEdit();
}

bool UltraCanvasRichTextEdit::Undo() {
    if (readOnly) return false;
    bool done = editor.Undo();
    if (done) AfterEdit();
    return done;
}

bool UltraCanvasRichTextEdit::Redo() {
    if (readOnly) return false;
    bool done = editor.Redo();
    if (done) AfterEdit();
    return done;
}

// ===== FORMATTING API =====

#define UC_RTE_FORMAT_ACTION(name, call) \
    void UltraCanvasRichTextEdit::name { \
        if (readOnly) return; \
        call; \
        AfterEdit(); \
    }

UC_RTE_FORMAT_ACTION(ToggleBold(), editor.ToggleBold())
UC_RTE_FORMAT_ACTION(ToggleItalic(), editor.ToggleItalic())
UC_RTE_FORMAT_ACTION(ToggleUnderline(), editor.ToggleUnderline())
UC_RTE_FORMAT_ACTION(ToggleStrikethrough(), editor.ToggleStrikethrough())
UC_RTE_FORMAT_ACTION(ToggleInlineCode(), editor.ToggleCode())
UC_RTE_FORMAT_ACTION(ToggleSubscript(), editor.ToggleSubscript())
UC_RTE_FORMAT_ACTION(ToggleSuperscript(), editor.ToggleSuperscript())
UC_RTE_FORMAT_ACTION(ClearFormatting(), editor.ClearFormatting())
UC_RTE_FORMAT_ACTION(SetFontFamily(const std::string& family), editor.SetFontFamily(family))
UC_RTE_FORMAT_ACTION(SetFontSize(float pt), editor.SetFontSize(pt))
UC_RTE_FORMAT_ACTION(SetTextColor(const std::string& hexColor), editor.SetTextColor(hexColor))
UC_RTE_FORMAT_ACTION(SetLink(const std::string& target), editor.SetLink(target))
UC_RTE_FORMAT_ACTION(SetHeadingLevel(int level), editor.SetHeadingLevel(level))
UC_RTE_FORMAT_ACTION(SetAlignment(RichTextAlign align), editor.SetAlignment(align))
UC_RTE_FORMAT_ACTION(SetRightToLeft(bool rightToLeft), editor.SetRightToLeft(rightToLeft))
UC_RTE_FORMAT_ACTION(ToggleBulletList(), editor.ToggleList(false))
UC_RTE_FORMAT_ACTION(ToggleNumberedList(), editor.ToggleList(true))
UC_RTE_FORMAT_ACTION(IndentList(), editor.IndentList())
UC_RTE_FORMAT_ACTION(OutdentList(), editor.OutdentList())
UC_RTE_FORMAT_ACTION(ToggleBlockQuote(), editor.ToggleBlockQuote())
UC_RTE_FORMAT_ACTION(ToggleCodeBlock(const std::string& language), editor.ToggleCodeBlock(language))
UC_RTE_FORMAT_ACTION(ToggleCheckList(), editor.ToggleCheckList())
UC_RTE_FORMAT_ACTION(InsertPageNumberField(), editor.InsertField(RichTextRun::Field::PageNumber))
UC_RTE_FORMAT_ACTION(InsertPageCountField(), editor.InsertField(RichTextRun::Field::PageCount))
UC_RTE_FORMAT_ACTION(InsertHorizontalRule(), editor.InsertHorizontalRule())
UC_RTE_FORMAT_ACTION(InsertPageBreak(), editor.InsertPageBreak())

#undef UC_RTE_FORMAT_ACTION

// ===== TABLES =====

// Every structural table operation is "do this where the caret is", so they
// share one shape: refuse when read-only, resolve the caret's cell to its grid
// position, call the editing core, and repaint. The core owns the span
// bookkeeping; the element owns only the translation from caret to grid.
#define UC_RTE_TABLE_ACTION(name, call)                                       \
    bool UltraCanvasRichTextEdit::name {                                      \
        if (readOnly) return false;                                           \
        int row = 0, column = 0;                                              \
        if (!editor.CaretGridPosition(row, column)) return false;             \
        const int block = editor.GetCaret().blockIndex;                       \
        (void)block; (void)row; (void)column;                                 \
        if (!(call)) return false;                                            \
        AfterEdit();                                                          \
        return true;                                                          \
    }

UC_RTE_TABLE_ACTION(InsertRowAbove(), editor.InsertTableRow(block, row, false))
UC_RTE_TABLE_ACTION(InsertRowBelow(), editor.InsertTableRow(block, row, true))
UC_RTE_TABLE_ACTION(InsertColumnLeft(), editor.InsertTableColumn(block, column, false))
UC_RTE_TABLE_ACTION(InsertColumnRight(), editor.InsertTableColumn(block, column, true))
UC_RTE_TABLE_ACTION(DeleteCurrentRow(), editor.DeleteTableRow(block, row))
UC_RTE_TABLE_ACTION(DeleteCurrentColumn(), editor.DeleteTableColumn(block, column))
UC_RTE_TABLE_ACTION(MergeWithCellRight(),
                    editor.MergeTableCells(block, editor.GetCaret().cellRow,
                                           editor.GetCaret().cellColumn, 1, 0))
UC_RTE_TABLE_ACTION(MergeWithCellBelow(),
                    editor.MergeTableCells(block, editor.GetCaret().cellRow,
                                           editor.GetCaret().cellColumn, 0, 1))
UC_RTE_TABLE_ACTION(MergeSelectedCells(), editor.MergeSelectedCells())
UC_RTE_TABLE_ACTION(SplitCurrentCell(),
                    editor.SplitTableCell(block, editor.GetCaret().cellRow,
                                          editor.GetCaret().cellColumn))

#undef UC_RTE_TABLE_ACTION

void UltraCanvasRichTextEdit::InsertTable(int rows, int columns, bool headerRow) {
    if (readOnly) return;
    if (editor.InsertTable(rows, columns, headerRow) < 0) return;
    AfterEdit();
}

bool UltraCanvasRichTextEdit::IsCaretInTable() const {
    int row = 0, column = 0;
    return editor.CaretGridPosition(row, column);
}

bool UltraCanvasRichTextEdit::CaretTableGeometry(int& outRows, int& outColumns,
                                                 int& outRow, int& outColumn) const {
    outRows = outColumns = 0;
    outRow = outColumn = -1;
    if (!editor.CaretGridPosition(outRow, outColumn)) return false;
    const RichTableGrid grid = editor.TableGrid(editor.GetCaret().blockIndex);
    outRows = grid.rowCount;
    outColumns = grid.columnCount;
    return true;
}

bool UltraCanvasRichTextEdit::CanSplitCurrentCell() const {
    const RichDocPosition caret = editor.GetCaret();
    if (!caret.InCell()) return false;
    const RichDocBlock& block = editor.GetBlock(caret.blockIndex);
    if (block.type != RichBlockType::Table) return false;
    if (caret.cellRow < 0 || caret.cellRow >= static_cast<int>(block.tableRows.size())) return false;
    const RichTableRow& row = block.tableRows[static_cast<size_t>(caret.cellRow)];
    if (caret.cellColumn < 0 || caret.cellColumn >= static_cast<int>(row.cells.size())) return false;
    const RichTableCell& cell = row.cells[static_cast<size_t>(caret.cellColumn)];
    return std::max(1, cell.columnSpan) > 1 || std::max(1, cell.rowSpan) > 1;
}

bool UltraCanvasRichTextEdit::ApplyParagraphStyle(const std::string& id) {
    if (readOnly || !editor.ApplyParagraphStyle(id)) return false;
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::AddBookmark(const std::string& name) {
    if (readOnly) return false;
    if (furnitureEdit) FinishHeaderFooterEditing();
    if (!editor.AddBookmark(name)) return false;
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::RemoveBookmark(const std::string& name) {
    if (readOnly) return false;
    if (furnitureEdit) FinishHeaderFooterEditing();
    if (!editor.RemoveBookmark(name)) return false;
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::InsertCrossReference(const std::string& bookmark, bool pageNumber) {
    if (readOnly) return false;
    if (furnitureEdit) FinishHeaderFooterEditing();
    if (!editor.InsertCrossReference(bookmark, pageNumber ? RichTextRun::Field::PageReference
                                                          : RichTextRun::Field::Reference)) return false;
    AfterEdit();
    return true;
}

std::string UltraCanvasRichTextEdit::InsertCaption(const std::string& label, const std::string& text) {
    if (readOnly) return "";
    if (furnitureEdit) FinishHeaderFooterEditing();
    const std::string name = editor.InsertCaption(label, text);
    if (!name.empty()) AfterEdit();
    return name;
}

bool UltraCanvasRichTextEdit::InsertTableOfContents(int maxLevel) {
    if (readOnly) return false;
    if (furnitureEdit) FinishHeaderFooterEditing();
    if (!editor.InsertTableOfContents(maxLevel)) return false;
    InvalidateDocument();
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::UpdateTableOfContents(int maxLevel) {
    if (readOnly) return false;
    if (furnitureEdit) FinishHeaderFooterEditing();
    if (!editor.UpdateTableOfContents(maxLevel)) return false;
    InvalidateDocument();
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::GoToBookmark(const std::string& name) {
    if (furnitureEdit) FinishHeaderFooterEditing();
    const int block = editor.GetDocument()->FindBookmark(name);
    if (block < 0) return false;
    editor.SetCaret(RichDocPosition(block, 0));
    caretMoved = true;
    AfterSelectionChange();
    RequestRedraw();
    return true;
}

// The bookmark a Ctrl+click at `position` leads to: a cross-reference's, or
// that of the table of contents entry clicked; "" for none.
std::string UltraCanvasRichTextEdit::BookmarkTargetAt(const RichDocPosition& position) const {
    if (position.InCell() || position.blockIndex < 0 || position.blockIndex >= editor.GetBlockCount()) return "";
    const RichDocBlock& block = editor.GetBlock(position.blockIndex);
    int offset = 0;
    for (const RichTextRun& run : block.runs) {
        const int start = offset + (run.lineBreakBefore ? 1 : 0);
        const int end = start + static_cast<int>(run.text.size());
        const bool reference = run.field == RichTextRun::Field::Reference
                            || run.field == RichTextRun::Field::PageReference;
        if (reference && position.byteOffset >= start && position.byteOffset <= end) return run.fieldArgument;
        offset = end;
    }
    if (block.tocLevel > 0) {
        for (const RichTextRun& run : block.runs) {
            if (run.field == RichTextRun::Field::PageReference) return run.fieldArgument;
        }
    }
    return "";
}

bool UltraCanvasRichTextEdit::ApplyCharacterStyle(const std::string& id) {
    if (readOnly || !editor.ApplyCharacterStyle(id)) return false;
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::UpdateStyle(const RichStyle& style) {
    if (readOnly || !editor.UpdateStyle(style)) return false;
    InvalidateDocument();
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::DeleteStyle(const std::string& id) {
    if (readOnly || !editor.DeleteStyle(id)) return false;
    InvalidateDocument();
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::NewStyleFromCaret(const std::string& name) {
    if (readOnly || name.empty()) return false;
    // An id from the name, unique among the document's styles.
    std::string base;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80) base += c;
    }
    if (base.empty()) base = "Style";
    std::string id = base;
    const std::vector<RichStyle> styles = editor.GetStyles();
    for (int n = 2; std::any_of(styles.begin(), styles.end(), [&](const RichStyle& s) { return s.id == id; }); n++) {
        id = base + std::to_string(n);
    }
    RichStyle style = editor.StyleFromCaret(id, name);
    if (!editor.UpdateStyle(style)) return false;
    editor.ApplyParagraphStyle(id);
    InvalidateDocument();
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::UpdateStyleFromCaret() {
    if (readOnly) return false;
    const std::string id = editor.CurrentParagraphStyle();
    const std::vector<RichStyle> styles = editor.GetStyles();
    auto existing = std::find_if(styles.begin(), styles.end(), [&](const RichStyle& s) { return s.id == id; });
    if (existing == styles.end()) return false;
    RichStyle style = editor.StyleFromCaret(id, existing->name);
    style.basedOn = existing->basedOn;
    style.nextStyle = existing->nextStyle;
    // The caret's formatting on top of what the style already said.
    RichStyle merged = *existing;
    merged.character.Overlay(style.character);
    merged.paragraph.Overlay(style.paragraph);
    return UpdateStyle(merged);
}

bool UltraCanvasRichTextEdit::ToggleCheckedAtCaret() {
    if (readOnly || editor.GetCaret().InCell()) return false;
    if (!editor.ToggleChecked(editor.GetCaret().blockIndex)) return false;
    AfterEdit();
    return true;
}

RichBlockType UltraCanvasRichTextEdit::GetCurrentBlockType() const {
    int blockIndex = editor.GetCaret().blockIndex;
    if (blockIndex < 0 || blockIndex >= editor.GetBlockCount()) return RichBlockType::Paragraph;
    return editor.GetBlock(blockIndex).type;
}

int UltraCanvasRichTextEdit::GetCurrentHeadingLevel() const {
    int blockIndex = editor.GetCaret().blockIndex;
    if (blockIndex < 0 || blockIndex >= editor.GetBlockCount()) return 0;
    const RichDocBlock& block = editor.GetBlock(blockIndex);
    return block.type == RichBlockType::Heading ? block.headingLevel : 0;
}

bool UltraCanvasRichTextEdit::InsertInlineImageFromFile(const std::string& path,
                                                        const std::string& altText) {
    if (readOnly) return false;
    // The path is UTF-8 on every platform (see UltraCanvasPathUtf8.h).
    std::ifstream in(PathFromUtf8(path), std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (data.empty()) return false;

    std::string name = path;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    InsertInlineImageFromMemory(name, UCRichDocument::MimeTypeForImageName(name), data, altText);
    return true;
}

void UltraCanvasRichTextEdit::InsertInlineImageFromMemory(const std::string& name,
                                                          const std::string& mimeType,
                                                          const std::vector<uint8_t>& data,
                                                          const std::string& altText) {
    if (readOnly) return;
    editor.InsertInlineImage(name, mimeType, data, altText);
    AfterEdit();
}

void UltraCanvasRichTextEdit::InsertImageFromMemory(const std::string& name,
                                                    const std::string& mimeType,
                                                    const std::vector<uint8_t>& data,
                                                    const std::string& altText) {
    if (readOnly) return;
    editor.InsertImage(name, mimeType, data, altText);
    AfterEdit();
}

bool UltraCanvasRichTextEdit::InsertImageFromFile(const std::string& path,
                                                  const std::string& altText) {
    if (readOnly) return false;
    // The path is UTF-8 on every platform (see UltraCanvasPathUtf8.h).
    std::ifstream in(PathFromUtf8(path), std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    if (data.empty()) return false;

    std::string name = path;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    InsertImageFromMemory(name, UCRichDocument::MimeTypeForImageName(name), data, altText);
    return true;
}

// ===== EDITING A HEADER OR FOOTER =====

bool UltraCanvasRichTextEdit::EditHeader(int pageIndex) { return BeginFurnitureEditing(pageIndex, false); }
bool UltraCanvasRichTextEdit::EditFooter(int pageIndex) { return BeginFurnitureEditing(pageIndex, true); }

bool UltraCanvasRichTextEdit::BeginFurnitureEditing(int pageIndex, bool footer) {
    if (readOnly) return false;
    if (furnitureEdit) {
        if (furnitureEdit->noteIndex < 0 && furnitureEdit->pageIndex == pageIndex
            && furnitureEdit->footer == footer) return true;
        FinishHeaderFooterEditing();
    }
    // The pages must be laid out: which one is being edited, and where its
    // header is, come from them.
    if (pages.empty()) return false;
    pageIndex = std::clamp(pageIndex, 0, static_cast<int>(pages.size()) - 1);
    const std::shared_ptr<UCRichDocument> body = editor.GetDocument();
    const bool firstPage = body->firstPageDiffers && pageIndex == 0;
    const RichPageFurniture& furniture = firstPage ? body->firstPageFurniture : body->pageFurniture;

    auto furnitureDoc = std::make_shared<UCRichDocument>();
    furnitureDoc->blocks = footer ? furniture.footer : furniture.header;
    furnitureDoc->media = body->media;          // pictures keep their indices
    furnitureDoc->page = body->page;
    furnitureDoc->defaultTabStopPt = body->defaultTabStopPt;

    auto state = std::make_unique<FurnitureEditState>();
    state->footer = footer;
    state->firstPage = firstPage;
    state->pageIndex = pageIndex;
    StartEditingPart(std::move(furnitureDoc), std::move(state));
    return true;
}

void UltraCanvasRichTextEdit::StartEditingPart(std::shared_ptr<UCRichDocument> part,
                                               std::unique_ptr<FurnitureEditState> state) {
    state->bodyContentHeight = contentHeight;
    state->bodyEditor = std::move(editor);
    editor = UCRichDocumentEditor(std::move(part));
    editor.onChanged = state->bodyEditor.onChanged;
    editor.onSelectionChanged = state->bodyEditor.onSelectionChanged;
    editor.SetAutoFormatEnabled(state->bodyEditor.IsAutoFormatEnabled());
    editor.SetAutoFormatOptions(state->bodyEditor.GetAutoFormatOptions());
    editor.SetMaxUndoSteps(state->bodyEditor.GetMaxUndoSteps());

    parkedLayouts = std::move(blockLayouts);
    blockLayouts.clear();
    parkedPages = pages;
    parkedFloats = placedFloats;
    placedFloats.clear();
    furnitureEdit = std::move(state);
    editor.SetCaret(editor.DocumentEnd());

    layoutsDirty = true;
    caretMoved = true;
    RequestRedraw();
    if (onHeaderFooterEditingChanged) onHeaderFooterEditingChanged(true);
    if (onSelectionChanged) onSelectionChanged();
}

void UltraCanvasRichTextEdit::SyncFurnitureToDocument() {
    if (!furnitureEdit) return;
    const std::shared_ptr<UCRichDocument>& body = furnitureEdit->bodyEditor.GetDocument();
    const std::shared_ptr<UCRichDocument>& edited = editor.GetDocument();
    if (furnitureEdit->noteIndex >= 0) {
        if (furnitureEdit->noteIndex < static_cast<int>(body->notes.size())) {
            body->notes[static_cast<size_t>(furnitureEdit->noteIndex)].blocks = edited->blocks;
        }
        for (size_t i = body->media.size(); i < edited->media.size(); i++) body->media.push_back(edited->media[i]);
        furnitureEdit->bodyEditor.SetModified(true);
        furnitureCache.clear();
        return;
    }
    RichPageFurniture& furniture = furnitureEdit->firstPage ? body->firstPageFurniture : body->pageFurniture;
    std::vector<RichDocBlock>& target = furnitureEdit->footer ? furniture.footer : furniture.header;
    // A header emptied of everything is no header at all.
    const bool empty = edited->blocks.size() == 1 && edited->blocks[0].type == RichBlockType::Paragraph
                       && UCRichDocumentEditor::RunsText(edited->blocks[0].runs).empty();
    target = empty ? std::vector<RichDocBlock>{} : edited->blocks;
    // Pictures inserted into it join the document's media.
    for (size_t i = body->media.size(); i < edited->media.size(); i++) body->media.push_back(edited->media[i]);
    furnitureEdit->bodyEditor.SetModified(true);
    furnitureCache.clear();
}

void UltraCanvasRichTextEdit::FinishHeaderFooterEditing() {
    if (!furnitureEdit) return;
    SyncFurnitureToDocument();
    const bool modified = editor.IsModified() || furnitureEdit->bodyEditor.IsModified();
    editor = std::move(furnitureEdit->bodyEditor);
    if (modified) editor.SetModified(true);
    blockLayouts = std::move(parkedLayouts);
    parkedLayouts.clear();
    parkedPages.clear();
    parkedFloats.clear();
    furnitureEdit.reset();
    // The header's new height moves the body; everything is laid out again.
    InvalidateDocument();
    caretMoved = true;
    if (onHeaderFooterEditingChanged) onHeaderFooterEditingChanged(false);
    if (onSelectionChanged) onSelectionChanged();
}

float UltraCanvasRichTextEdit::PlaceFurnitureBeingEdited(IRenderContext* ctx) {
    // The body is placed again first, against the header as it now is (it is
    // written into the document as it is edited): a header growing a line
    // pushes the body down while it is typed, as it will once editing ends.
    std::swap(editor, furnitureEdit->bodyEditor);
    std::swap(blockLayouts, parkedLayouts);
    placingParkedBody = true;
    const float bodyHeight = PlaceBody(ctx);
    placingParkedBody = false;
    parkedPages = pages;
    parkedFloats = placedFloats;
    std::swap(blockLayouts, parkedLayouts);
    std::swap(editor, furnitureEdit->bodyEditor);
    furnitureEdit->bodyContentHeight = bodyHeight;

    pages = parkedPages;
    placedFloats.clear();
    const int count = editor.GetBlockCount();
    float total = 0.0f;
    for (int i = 0; i < count; i++) total += blockLayouts[static_cast<size_t>(i)].bounds.height + GapAfterBlock(i);
    float y = 0.0f;
    if (furnitureEdit->noteIndex >= 0) {
        // A note is edited where it is shown.
        y = furnitureEdit->bodyContentHeight;
        for (const NoteArea& area : noteAreas) {
            if (area.noteIndex == furnitureEdit->noteIndex) { y = area.top; break; }
        }
    } else if (!pages.empty()) {
        const PageFrame& frame = pages[static_cast<size_t>(std::clamp(furnitureEdit->pageIndex, 0,
                                                                      static_cast<int>(pages.size()) - 1))];
        if (furnitureEdit->footer) {
            // A footer grows upwards from where it ends.
            const float footerBottom = frame.footerTop + (frame.footer ? frame.footer->height : 0.0f);
            y = footerBottom - total;
        } else {
            y = pageView ? frame.headerTop : 0.0f;
        }
    }
    for (int i = 0; i < count; i++) {
        BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
        bl.bounds.x = bl.textLeft;
        bl.bounds.y = y;
        y += bl.bounds.height + GapAfterBlock(i);
    }
    return std::max(furnitureEdit->bodyContentHeight, y);
}

bool UltraCanvasRichTextEdit::FurnitureRegionAt(float contentY, int& outPage, bool& outFooter) const {
    const std::vector<PageFrame>& frames = furnitureEdit ? parkedPages : pages;
    for (size_t p = 0; p < frames.size(); p++) {
        const PageFrame& frame = frames[p];
        if (pageView) {
            if (contentY < frame.top || contentY > frame.top + pageHeightPx) continue;
            outPage = static_cast<int>(p);
            if (contentY < frame.bodyTop) { outFooter = false; return true; }
            if (contentY > frame.bodyBottom) { outFooter = true; return true; }
            return false;
        }
        // One column: the header above the body, the footer below it.
        outPage = 0;
        if (frame.header && contentY < frame.bodyTop) { outFooter = false; return true; }
        if (frame.footer && contentY >= frame.footerTop) { outFooter = true; return true; }
    }
    return false;
}

// The body under the header being edited: drawn as it was, then washed pale,
// as a word processor does.
void UltraCanvasRichTextEdit::RenderBodyBackdrop(IRenderContext* ctx) {
    const auto& blocks = furnitureEdit->bodyEditor.GetDocument()->blocks;
    const float viewTop = scrollOffset, viewBottom = scrollOffset + visibleArea.height;
    for (size_t i = 0; i < parkedLayouts.size() && i < blocks.size(); i++) {
        const BlockLayout& bl = parkedLayouts[i];
        if (!bl.valid || BlockVisualBottom(bl) < viewTop) continue;
        if (bl.bounds.y > viewBottom) { if (hasColumns) continue; break; }
        RenderBlockPieces(ctx, blocks, static_cast<int>(i), bl, -1);
    }
    for (const PlacedFloat& placed : parkedFloats) {
        if (!placed.image) continue;
        ctx->DrawImage(*placed.image, Rect2Dd(ColumnLeft() + placed.rect.x, visibleArea.y + placed.rect.y - scrollOffset,
                                              placed.rect.width, placed.rect.height), ImageFitMode::Contain);
    }
    RenderNotes(ctx);
    // Pale over the body; the edited header or footer marked with a rule.
    Color wash = pageView ? style.pageColor : style.backgroundColor;
    wash.a = 150;
    for (const PageFrame& frame : pages) {
        const float top = pageView ? frame.top : 0.0f;
        const float height = pageView ? pageHeightPx : contentHeight;
        if (top + height < viewTop || top > viewBottom) continue;
        const double x = pageView ? visibleArea.x + pageLeftX - hScrollOffset : visibleArea.x;
        const double width = pageView ? pageWidthPx : visibleArea.width;
        ctx->DrawFilledRectangle(Rect2Dd(x, visibleArea.y + top - scrollOffset, width, height), wash, 0.0f,
                                 Colors::Transparent);
    }
    if (!blockLayouts.empty() && !pages.empty()) {
        const BlockLayout& first = blockLayouts.front();
        const BlockLayout& last = blockLayouts.back();
        const bool below = furnitureEdit->footer || furnitureEdit->noteIndex >= 0;
        const float edge = below ? first.bounds.y - 4.0f : last.bounds.y + last.bounds.height + 4.0f;
        const double y = visibleArea.y + edge - scrollOffset;
        ctx->PushState();
        ctx->SetLineDash(UCDashPattern({4.0, 3.0}));
        ctx->DrawLine(Point2Dd(ColumnLeft(), y), Point2Dd(ColumnLeft() + ColumnWidth(), y), style.pageBreakColor);
        ctx->PopState();
        ctx->PushState();
        FontStyle label = style.baseFont;
        label.fontSize = std::max(7.0, style.baseFont.fontSize * 0.7);
        ctx->SetFontStyle(label);
        ctx->SetTextPaint(style.pageBreakColor);
        std::string name = std::string(furnitureEdit->firstPage ? "First Page " : "")
                         + (furnitureEdit->footer ? "Footer" : "Header");
        if (furnitureEdit->noteIndex >= 0) {
            const UCRichDocument& body = *furnitureEdit->bodyEditor.GetDocument();
            const size_t note = static_cast<size_t>(furnitureEdit->noteIndex);
            const std::vector<std::string> marks = body.NoteMarks();
            const std::string mark = note < marks.size() ? marks[note] : "";
            name = (note < body.notes.size() && body.notes[note].kind == RichNote::Kind::Endnote
                        ? "Endnote " : "Footnote ") + mark;
            // The note's mark, in front of its first line.
            if (!mark.empty()) {
                const double markX = ColumnLeft() - ctx->GetTextLineWidth(mark) - 4.0;
                ctx->DrawText(mark, Point2Dd(std::max<double>(visibleArea.x, markX),
                                             visibleArea.y + first.bounds.y - scrollOffset));
            }
        }
        // At the rule's right end, on the header's side of it.
        const double textY = below ? y + 2.0 : y - label.fontSize * 1.6;
        const double textX = ColumnLeft() + ColumnWidth() - ctx->GetTextLineWidth(name);
        ctx->DrawText(name, Point2Dd(textX, textY));
        ctx->PopState();
    }
}

// ===== FOOTNOTES AND ENDNOTES =====

bool UltraCanvasRichTextEdit::InsertFootnote() { return InsertNoteOf(RichNote::Kind::Footnote); }
bool UltraCanvasRichTextEdit::InsertEndnote() { return InsertNoteOf(RichNote::Kind::Endnote); }

bool UltraCanvasRichTextEdit::InsertNoteOf(RichNote::Kind kind) {
    if (readOnly) return false;
    // A note's reference goes into the body, never into a header or a note.
    if (furnitureEdit) FinishHeaderFooterEditing();
    const int noteIndex = editor.InsertNote(kind);
    if (noteIndex < 0) return false;
    furnitureCache.clear();                 // the notes moved in memory
    AfterEdit();
    return EditNote(noteIndex);
}

bool UltraCanvasRichTextEdit::EditNote(int noteIndex) { return BeginNoteEditing(noteIndex); }

bool UltraCanvasRichTextEdit::BeginNoteEditing(int noteIndex) {
    if (readOnly) return false;
    if (furnitureEdit) {
        if (furnitureEdit->noteIndex == noteIndex) return true;
        FinishHeaderFooterEditing();
    }
    const std::shared_ptr<UCRichDocument> body = editor.GetDocument();
    if (!body || noteIndex < 0 || noteIndex >= static_cast<int>(body->notes.size())) return false;

    auto noteDoc = std::make_shared<UCRichDocument>();
    noteDoc->blocks = body->notes[static_cast<size_t>(noteIndex)].blocks;
    if (noteDoc->blocks.empty()) noteDoc->blocks.emplace_back();
    noteDoc->media = body->media;
    noteDoc->page = body->page;
    noteDoc->defaultTabStopPt = body->defaultTabStopPt;
    noteDoc->styles = body->styles;

    auto state = std::make_unique<FurnitureEditState>();
    state->noteIndex = noteIndex;
    for (const NoteArea& area : noteAreas) {
        if (area.noteIndex == noteIndex) state->pageIndex = area.page;
    }
    StartEditingPart(std::move(noteDoc), std::move(state));
    return true;
}

std::shared_ptr<UltraCanvasRichTextEdit::FurnitureLayout> UltraCanvasRichTextEdit::LayoutNote(
        IRenderContext* ctx, int noteIndex, const std::string& mark) {
    const UCRichDocument& document = *editor.GetDocument();
    if (noteIndex < 0 || noteIndex >= static_cast<int>(document.notes.size())) return nullptr;
    if (furnitureCacheWidth != ColumnWidth()) {
        furnitureCache.clear();
        furnitureCacheWidth = ColumnWidth();
    }
    const RichNote& note = document.notes[static_cast<size_t>(noteIndex)];
    const std::string key = "note|" + std::to_string(noteIndex) + "|" + mark + "|"
                          + std::to_string(reinterpret_cast<uintptr_t>(&note.blocks));
    auto found = furnitureCache.find(key);
    if (found != furnitureCache.end()) return found->second;

    auto layout = std::make_shared<FurnitureLayout>();
    layout->blocks = note.blocks;
    if (layout->blocks.empty()) layout->blocks.emplace_back();
    // The mark in front of the note's first line, raised, then a space.
    RichDocBlock& first = layout->blocks.front();
    const bool hasText = first.type == RichBlockType::Paragraph || first.type == RichBlockType::Heading
                      || first.type == RichBlockType::ListItem || first.type == RichBlockType::BlockQuote;
    if (!mark.empty() && hasText) {
        RichTextRun look = first.runs.empty() ? RichTextRun{} : first.runs.front();
        look.text.clear();
        look.lineBreakBefore = false;
        look.mediaIndex = -1;
        look.math = false;
        look.code = false;
        look.linkTarget.clear();
        look.field = RichTextRun::Field::Plain;
        look.noteIndex = -1;
        look.subscript = false;
        RichTextRun raised = look;
        raised.superscript = true;
        raised.text = mark;
        look.superscript = false;
        look.text = " ";
        first.runs.insert(first.runs.begin(), {raised, look});
    }
    layout->layouts.resize(layout->blocks.size());
    float y = 0.0f;
    for (size_t i = 0; i < layout->blocks.size(); i++) {
        BlockLayout& bl = layout->layouts[i];
        BuildBlockLayout(ctx, layout->blocks, static_cast<int>(i), bl, -1);
        bl.bounds.x = bl.textLeft;
        bl.bounds.y = y;
        y += bl.bounds.height + GapAfterBlock(layout->blocks, static_cast<int>(i));
    }
    layout->height = y;
    furnitureCache[key] = layout;
    return layout;
}

bool UltraCanvasRichTextEdit::PlaceFootnotes(IRenderContext* ctx) {
    const UCRichDocument& document = *editor.GetDocument();
    std::vector<float> room(pages.size(), 0.0f);
    std::vector<std::vector<NoteArea>> perPage(pages.size());
    const std::vector<std::string> marks = document.NoteMarks();
    std::vector<bool> placed(document.notes.size(), false);

    // Where a reference sits: the middle of its line (a reference in a table
    // counts from the table's top).
    auto referenceY = [&](const UCRichDocument::NoteReference& reference) {
        const BlockLayout& bl = blockLayouts[static_cast<size_t>(reference.blockIndex)];
        if (reference.cellRow >= 0 || !bl.layout) return bl.bounds.y;
        const std::vector<RichTextRun>& runs = document.blocks[static_cast<size_t>(reference.blockIndex)].runs;
        int offset = 0;
        for (int r = 0; r <= reference.runIndex && r < static_cast<int>(runs.size()); r++) {
            offset += runs[static_cast<size_t>(r)].lineBreakBefore ? 1 : 0;
            if (r < reference.runIndex) offset += static_cast<int>(runs[static_cast<size_t>(r)].text.size());
        }
        const Rect2Di box = bl.layout->IndexToPos(offset);
        return BlockToContentY(bl, static_cast<float>(box.y) + static_cast<float>(box.height) * 0.5f);
    };
    for (const UCRichDocument::NoteReference& reference : document.NoteReferences()) {
        const size_t note = static_cast<size_t>(reference.noteIndex);
        if (placed[note] || document.notes[note].kind != RichNote::Kind::Footnote) continue;
        if (reference.blockIndex >= static_cast<int>(blockLayouts.size())) continue;
        placed[note] = true;
        NoteArea area;
        area.noteIndex = reference.noteIndex;
        area.layout = LayoutNote(ctx, reference.noteIndex, marks[note]);
        if (!area.layout) continue;
        area.page = std::clamp(PageIndexAt(referenceY(reference)), 0, static_cast<int>(pages.size()) - 1);
        perPage[static_cast<size_t>(area.page)].push_back(std::move(area));
    }
    const float ruleSpace = NoteRuleSpace();
    const float gap = Px(2.0f);
    for (size_t p = 0; p < perPage.size(); p++) {
        std::vector<NoteArea>& areas = perPage[p];
        if (areas.empty()) continue;
        float height = ruleSpace;
        for (size_t k = 0; k < areas.size(); k++) height += areas[k].layout->height + (k > 0 ? gap : 0.0f);
        // No more than two thirds of the page's text area.
        const float old = p < pageFootnoteRoom.size() ? pageFootnoteRoom[p] : 0.0f;
        const float textArea = pages[p].bodyBottom + old - pages[p].bodyTop;
        room[p] = std::min(height, textArea * 0.66f);
        float top = pages[p].bodyBottom + old - height + ruleSpace;
        for (size_t k = 0; k < areas.size(); k++) {
            areas[k].top = top;
            areas[k].ruleAbove = k == 0;
            top += areas[k].layout->height + gap;
            noteAreas.push_back(std::move(areas[k]));
        }
    }
    bool changed = room.size() != pageFootnoteRoom.size();
    for (size_t p = 0; !changed && p < room.size(); p++) changed = std::abs(room[p] - pageFootnoteRoom[p]) > 0.5f;
    pageFootnoteRoom = std::move(room);
    return changed;
}

float UltraCanvasRichTextEdit::PlaceEndnotes(IRenderContext* ctx, float y, bool footnotesToo) {
    const UCRichDocument& document = *editor.GetDocument();
    if (document.notes.empty()) return y;
    const std::vector<std::string> marks = document.NoteMarks();
    const std::vector<UCRichDocument::NoteReference> references = document.NoteReferences();
    std::vector<bool> placed(document.notes.size(), false);
    bool first = true;
    for (RichNote::Kind kind : {RichNote::Kind::Footnote, RichNote::Kind::Endnote}) {
        if (kind == RichNote::Kind::Footnote && !footnotesToo) continue;
        for (const UCRichDocument::NoteReference& reference : references) {
            const size_t note = static_cast<size_t>(reference.noteIndex);
            if (placed[note] || document.notes[note].kind != kind) continue;
            placed[note] = true;
            NoteArea area;
            area.noteIndex = reference.noteIndex;
            area.layout = LayoutNote(ctx, reference.noteIndex, marks[note]);
            if (!area.layout) continue;
            area.ruleAbove = first;
            area.top = y + (first ? NoteRuleSpace() + style.blockSpacing : Px(2.0f));
            y = area.top + area.layout->height;
            first = false;
            noteAreas.push_back(std::move(area));
        }
    }
    return y;
}

// Page view: the pages are placed, then the footnotes of each page, and -
// since their room shortens the page's text - the pages again, until the
// footnotes stay on the pages they were given room on.
float UltraCanvasRichTextEdit::PlaceBody(IRenderContext* ctx) {
    if (!pageView) return PlaceBlocksInColumn(ctx);
    pageFootnoteRoom.clear();
    float height = PlaceBlocksOnPages(ctx);
    const bool hasNotes = editor.GetDocument() && !editor.GetDocument()->notes.empty();
    if (!hasNotes) return height;
    for (int pass = 0; pass < 4; pass++) {
        const std::vector<float> before = pageFootnoteRoom;
        if (!PlaceFootnotes(ctx)) return height;
        // A reference going back and forth over a page break settles on the
        // larger room.
        if (pass >= 2) {
            for (size_t p = 0; p < pageFootnoteRoom.size() && p < before.size(); p++) {
                pageFootnoteRoom[p] = std::max(pageFootnoteRoom[p], before[p]);
            }
        }
        height = PlaceBlocksOnPages(ctx);
    }
    PlaceFootnotes(ctx);
    return height;
}

int UltraCanvasRichTextEdit::NoteAreaAt(float contentY) const {
    for (const NoteArea& area : noteAreas) {
        if (!area.layout) continue;
        if (contentY >= area.top - 2.0f && contentY < area.top + area.layout->height + 2.0f) return area.noteIndex;
    }
    return -1;
}

void UltraCanvasRichTextEdit::RenderNotes(IRenderContext* ctx) {
    const float viewTop = scrollOffset, viewBottom = scrollOffset + visibleArea.height;
    for (const NoteArea& area : noteAreas) {
        if (!area.layout) continue;
        if (area.top + area.layout->height < viewTop || area.top - NoteRuleSpace() > viewBottom) continue;
        if (area.ruleAbove) {
            // The short rule a word processor puts above the notes.
            const double y = visibleArea.y + area.top - NoteRuleSpace() * 0.5f - scrollOffset;
            const double length = std::min<double>(ColumnWidth() / 3.0, Px(144.0f));
            ctx->DrawLine(Point2Dd(ColumnLeft(), y), Point2Dd(ColumnLeft() + length, y), style.textColor);
        }
        // The note being edited is drawn live, as the blocks.
        if (IsEditingNote() && area.noteIndex == furnitureEdit->noteIndex) continue;
        RenderFurniture(ctx, *area.layout, area.top);
    }
}

// ===== COMMENTS =====

// ===== SECTIONS =====

bool UltraCanvasRichTextEdit::InsertSectionBreak(bool newPage) {
    if (readOnly) return false;
    if (furnitureEdit) FinishHeaderFooterEditing();
    if (!editor.InsertSectionBreak(newPage)) return false;
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::SetSectionColumns(int columns, float gapPt) {
    if (readOnly) return false;
    if (furnitureEdit) FinishHeaderFooterEditing();
    if (!editor.SetSectionColumns(columns, gapPt)) return false;
    AfterEdit();
    return true;
}

// ===== TRACKED CHANGES =====

std::string UltraCanvasRichTextEdit::CurrentIsoTime() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02dZ", utc.tm_year + 1900, utc.tm_mon + 1,
                  utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec);
    return text;
}

void UltraCanvasRichTextEdit::SetTrackChanges(bool enabled) {
    if (furnitureEdit) FinishHeaderFooterEditing();
    editor.SetRevisionAuthor(commentAuthor, CurrentIsoTime());
    editor.SetTrackChanges(enabled);
}

bool UltraCanvasRichTextEdit::AcceptAllChanges() {
    if (readOnly || furnitureEdit || !editor.AcceptAllChanges()) return false;
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::RejectAllChanges() {
    if (readOnly || furnitureEdit || !editor.RejectAllChanges()) return false;
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::AcceptChangeAtCaret() {
    if (readOnly || furnitureEdit || !editor.AcceptChangeAt(editor.GetCaret())) return false;
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::RejectChangeAtCaret() {
    if (readOnly || furnitureEdit || !editor.RejectChangeAt(editor.GetCaret())) return false;
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::GoToNextChange() {
    if (furnitureEdit) FinishHeaderFooterEditing();
    RichDocRange change;
    if (!editor.NextChange(editor.GetCaret(), change)) return false;
    editor.SetSelection(change.start, change.end);
    caretMoved = true;
    AfterSelectionChange();
    return true;
}

int UltraCanvasRichTextEdit::AddComment(const std::string& text) {
    if (readOnly) return -1;
    if (furnitureEdit) FinishHeaderFooterEditing();
    const int index = editor.AddComment(text, commentAuthor, CurrentIsoTime());
    if (index >= 0) AfterEdit();
    return index;
}

bool UltraCanvasRichTextEdit::RemoveComment(int index) {
    if (readOnly || furnitureEdit || !editor.RemoveComment(index)) return false;
    AfterEdit();
    return true;
}

bool UltraCanvasRichTextEdit::SetCommentText(int index, const std::string& text) {
    if (readOnly || !(furnitureEdit ? furnitureEdit->bodyEditor : editor).SetCommentText(index, text)) return false;
    RequestRedraw();
    return true;
}

bool UltraCanvasRichTextEdit::SetCommentResolved(int index, bool resolved) {
    if (readOnly || furnitureEdit || !editor.SetCommentResolved(index, resolved)) return false;
    // The shading goes with the comment being open.
    for (auto& bl : blockLayouts) bl.valid = false;
    layoutsDirty = true;
    RequestRedraw();
    return true;
}

void UltraCanvasRichTextEdit::SetShowComments(bool show) {
    if (showComments == show) return;
    showComments = show;
    InvalidateDocument();
}

float UltraCanvasRichTextEdit::CommentPaneLeft() const {
    return visibleArea.x + visibleArea.width * zoom + style.padding;
}

// Each comment in a box in the pane, level with the start of its text (or
// just below the box above it), joined to the text by a line.
void UltraCanvasRichTextEdit::RenderCommentPane(IRenderContext* ctx) {
    commentBoxes.clear();
    const Rect2Df bounds = GetLocalBounds();
    const float left = CommentPaneLeft();
    const float width = style.commentPaneWidth;
    const float top = bounds.y, bottom = bounds.y + bounds.height;
    ctx->PushState();
    ctx->ClipRect(Rect2Dd(left, top, width, bounds.height));
    ctx->DrawFilledRectangle(Rect2Dd(left, top, width, bounds.height), style.commentPaneColor, 0.0f, Colors::Transparent);

    const UCRichDocument& document = *editor.GetDocument();
    const std::vector<int> caretComments = editor.CommentsAt(editor.GetCaret());
    FontStyle textFont = style.baseFont;
    textFont.fontSize = std::max(8.0, style.baseFont.fontSize * 0.85);
    FontStyle authorFont = textFont;
    authorFont.fontWeight = FontWeight::Bold;
    const float inner = width - 20.0f;
    float nextFree = top + 6.0f;
    std::vector<std::pair<Point2Dd, Point2Dd>> connectors;
    for (int index : document.ActiveComments()) {
        RichDocRange range;
        if (!editor.CommentRange(index, range)) continue;
        const Rect2Df anchor = ToElement(PositionRect(range.start));
        const RichComment& comment = document.comments[static_cast<size_t>(index)];
        auto layout = ctx->CreateTextLayout(comment.text.empty() ? " " : comment.text, false);
        layout->SetFontStyle(textFont);
        layout->SetExplicitWidth(inner);
        layout->SetWrap(TextWrap::WrapWordChar);
        const float textHeight = static_cast<float>(layout->GetLayoutExtents().logical.height);
        const float header = static_cast<float>(textFont.fontSize) * 1.5f;
        const float height = header + textHeight + 10.0f;
        const float y = std::max(anchor.y, nextFree);
        nextFree = y + height + 6.0f;
        if (y > bottom || y + height < top) continue;
        const Rect2Df box(left + 6.0f, y, width - 12.0f, height);
        const bool current = std::find(caretComments.begin(), caretComments.end(), index) != caretComments.end();
        Color border = style.commentBorderColor;
        Color text = style.textColor;
        if (comment.resolved) {
            border.a = 90;
            text = style.pageBreakColor;
        }
        // The comment the caret is in is joined to its text by a line, drawn
        // once the pane is (over the text, so outside the pane's clip).
        if (current && anchor.height > 0.0f) {
            connectors.push_back({Point2Dd(anchor.x, anchor.y + anchor.height),
                                  Point2Dd(box.x, box.y + header * 0.5f)});
        }
        ctx->DrawFilledRectangle(Rect2Dd(box.x, box.y, box.width, box.height), style.commentBoxColor,
                                 current ? 2.0f : 1.0f, border);
        ctx->PushState();
        ctx->SetFontStyle(authorFont);
        ctx->SetTextPaint(comment.resolved ? style.pageBreakColor : style.commentAuthorColor);
        std::string title = comment.author.empty() ? "Comment" : comment.author;
        if (comment.resolved) title += " (resolved)";
        ctx->DrawText(title, Point2Dd(box.x + 4.0f, box.y + 3.0f));
        ctx->PopState();
        ctx->SetTextPaint(text);
        ctx->DrawTextLayout(*layout, Point2Dd(box.x + 4.0f, box.y + header));
        commentBoxes.push_back({index, box});
    }
    ctx->PopState();
    for (const auto& [from, to] : connectors) {
        ctx->PushState();
        ctx->SetLineDash(UCDashPattern({3.0, 2.0}));
        ctx->DrawLine(from, Point2Dd(left, from.y), style.commentBorderColor);
        ctx->DrawLine(Point2Dd(left, from.y), to, style.commentBorderColor);
        ctx->PopState();
    }
}

int UltraCanvasRichTextEdit::CommentBoxAt(const Point2Di& elementPoint) const {
    if (!commentPaneShown) return -1;
    const Point2Df point(static_cast<float>(elementPoint.x), static_cast<float>(elementPoint.y));
    for (const CommentBox& box : commentBoxes) {
        if (box.rect.Contains(point)) return box.index;
    }
    return -1;
}

// ===== PDF =====

bool UltraCanvasRichTextEdit::ExportToPdf(const std::string& utf8Path, std::string& error) {
    const RichPageSetup page = EffectivePageSetup();
    auto pdf = UltraCanvasPdfSurface::CreateForFile(utf8Path, page.widthPt, page.heightPt, error);
    if (!pdf) return false;
    if (!ExportPdfPages(*pdf, error)) return false;
    return pdf->Finish(error);
}

bool UltraCanvasRichTextEdit::ExportToPdf(std::vector<uint8_t>& pdfBytes, std::string& error) {
    const RichPageSetup page = EffectivePageSetup();
    auto pdf = UltraCanvasPdfSurface::CreateInMemory(page.widthPt, page.heightPt, error);
    if (!pdf) return false;
    if (!ExportPdfPages(*pdf, error)) return false;
    if (!pdf->Finish(error)) return false;
    pdfBytes = pdf->GetBytes();
    return true;
}

// Exported as print draws it: laid out with the PDF's own context (its text
// is measured with the fonts it is drawn with), then page after page.
bool UltraCanvasRichTextEdit::ExportPdfPages(UltraCanvasPdfSurface& pdf, std::string& error) {
    IRenderContext* ctx = pdf.GetContext();
    if (!ctx) {
        error = "The PDF has no page to draw on";
        return false;
    }
    const std::shared_ptr<UCRichDocument>& document = editor.GetDocument();
    if (document) pdf.SetMetadata(document->metadata.title, document->metadata.author, document->metadata.description);

    const int count = BeginPrintLayout(ctx);
    const float pointsPerPixel = 1.0f / kPixelsPerPoint;
    for (int index = 0; index < count; index++) {
        if (index > 0) pdf.NextPage();
        ctx->PushState();
        ctx->Scale(pointsPerPixel, pointsPerPixel);
        RenderPrintPage(ctx, index);
        ctx->PopState();
    }
    EndPrintLayout();
    return true;
}

// ===== PRINTING =====

// Lays the document out as page view does, measuring with `ctx`, in the
// output state: page view at zoom 1, no editing marks. The view state is kept
// for EndPrintLayout().
int UltraCanvasRichTextEdit::BeginPrintLayout(IRenderContext* ctx) {
    if (!ctx) return 0;
    if (furnitureEdit) FinishHeaderFooterEditing();
    if (!printing) {
        savedOutputView.pageView = pageView;
        savedOutputView.zoom = zoom;
        savedOutputView.scrollOffset = scrollOffset;
        savedOutputView.hScrollOffset = hScrollOffset;
        savedOutputView.visibleArea = visibleArea;
    }

    printing = true;
    pageView = true;
    zoom = 1.0f;
    hScrollOffset = 0.0f;
    RecalculateVisibleArea();
    // Tall enough that no block of a page is culled while it is drawn.
    const RichPageSetup page = EffectivePageSetup();
    visibleArea.height = std::max(visibleArea.height, Px(page.heightPt) + 2.0f * style.pageGap);
    blockLayouts.clear();
    furnitureCache.clear();
    layoutsDirty = true;
    EnsureLayouts(ctx);
    return static_cast<int>(pages.size());
}

// One page of the print layout, its top-left corner at the context's origin.
void UltraCanvasRichTextEdit::RenderPrintPage(IRenderContext* ctx, int pageIndex) {
    if (!ctx || !printing || pageIndex < 0 || pageIndex >= static_cast<int>(pages.size())) return;
    const PageFrame frame = pages[static_cast<size_t>(pageIndex)];
    const float savedScroll = scrollOffset;
    scrollOffset = frame.top;
    ctx->PushState();
    // The page's top-left corner to the context's origin.
    ctx->Translate(-(visibleArea.x + pageLeftX), -visibleArea.y);
    ctx->ClipRect(Rect2Dd(visibleArea.x + pageLeftX, visibleArea.y, pageWidthPx, pageHeightPx));
    const std::vector<PageFrame> onePage{frame};
    std::vector<PageFrame> allPages;
    allPages.swap(pages);
    pages = onePage;
    RenderPages(ctx);
    pages.swap(allPages);
    DrawFloats(ctx, true);
    const float top = frame.top, bottom = frame.top + pageHeightPx;
    for (int i = 0; i < static_cast<int>(blockLayouts.size()); i++) {
        const BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
        if (BlockVisualBottom(bl) < top) continue;
        if (bl.bounds.y > bottom) break;
        RenderBlock(ctx, i, bl);
    }
    DrawFloats(ctx, false);
    RenderNotes(ctx);
    ctx->PopState();
    scrollOffset = savedScroll;
}

// Back to the screen: its own context lays everything out again.
void UltraCanvasRichTextEdit::EndPrintLayout() {
    if (!printing) return;
    printing = false;
    pageView = savedOutputView.pageView;
    zoom = savedOutputView.zoom;
    scrollOffset = savedOutputView.scrollOffset;
    hScrollOffset = savedOutputView.hScrollOffset;
    visibleArea = savedOutputView.visibleArea;
    visibleAreaDirty = true;
    pages.clear();
    InvalidateDocument();
}

} // namespace UltraCanvas
