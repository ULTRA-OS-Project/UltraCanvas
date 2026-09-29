// core/UltraCanvasRichTextEdit.cpp
// The WYSIWYG editing element. See UltraCanvasRichTextEdit.h for what it is
// and how it relates to UltraCanvasTextArea.
//
// Rendering model: one ITextLayout per block, holding the block's concatenated
// run text with each run's formatting applied as text attributes. Because the
// layout text and the editor's byte offsets are the same string, hit testing,
// caret geometry and selection painting need no translation layer.
//
// Version: 1.0.0
// Author: UltraCanvas Framework

#include "UltraCanvasRichTextEdit.h"
#include "UltraCanvasUI.h"
#include "UltraCanvasCaret.h"
#include "UltraCanvasClipboard.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPdfSurface.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace UltraCanvas {

std::vector<RichDocBlock> UltraCanvasRichTextEdit::internalClipboard;
std::string UltraCanvasRichTextEdit::internalClipboardText;

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
    const float availableWidth = bounds.width - 2 * style.padding - vertical;
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
        std::vector<PlacedFloat> pageFloats;      // floats of the current page
        auto newPage = [&]() {
            placedFloats.insert(placedFloats.end(), pageFloats.begin(), pageFloats.end());
            pageFloats.clear();
            pages.push_back(frameFor(static_cast<int>(pages.size()), count));
            y = pages.back().bodyTop;
            pageEmpty = true;
        };
        for (int i = 0; i < blockCount; i++) {
            BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
            bl.slices.clear();
            const float before = (!pageEmpty && i > 0) ? GapAfterBlock(i - 1) : 0.0f;
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
            if (top + bl.bounds.height + keepWithNext > bodyBottom) {
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
            y = BlockVisualBottom(bl);
            pageEmpty = false;
            if (editor.GetBlock(i).type == RichBlockType::PageBreak) breakPending = true;
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
        bl.bounds.y = y;
        y += bl.bounds.height + GapAfterBlock(i);
    }
    // The column ends below its last picture too.
    for (const PlacedFloat& placed : floats) y = std::max(y, placed.rect.y + placed.rect.height);
    placedFloats = std::move(floats);
    frame.bodyBottom = y;
    if (frame.footer) {
        frame.footerTop = y + separation;
        y = frame.footerTop + frame.footer->height;
    }
    if (frame.header || frame.footer) pages.push_back(std::move(frame));
    return y;
}

float UltraCanvasRichTextEdit::BlockIndentFor(const RichDocBlock& block) const {
    // A document's own left indent adds to the view's indent for the block's
    // kind. List items keep the view's list indentation only (see the model).
    const float documentIndent = Px(std::max(0.0f, block.leftIndentPt));
    switch (block.type) {
        case RichBlockType::ListItem:
            return style.listIndent * static_cast<float>(block.listLevel + 1);
        case RichBlockType::BlockQuote:
            return style.quoteIndent + documentIndent;
        case RichBlockType::CodeBlock:
            return style.codeIndent + documentIndent;
        case RichBlockType::Paragraph:
        case RichBlockType::Heading:
        case RichBlockType::MathBlock:
            return documentIndent;
        default:
            return 0.0f;
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
            // Never wider than the column it sits in; keep the aspect ratio.
            const float maxWidth = std::max(16.0f, ColumnWidth());
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
        if (!run.highlightColor.empty()) {
            add(TextAttributeFactory::CreateBackground(ParseHexColor(run.highlightColor, Colors::Transparent)));
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
    if (block.align != RichTextAlign::Default) {
        layout->SetAlignment(ToTextAlignment(block.align));
    }
    if (paragraphOriginX >= 0.0f) {
        ApplyParagraphGeometry(layout.get(), block, text, paragraphOriginX, wrapWidth);
    }
    ApplyRunAttributes(layout.get(), block, runs, outHits, blockIndex, outInlineImages, cellRow, cellColumn);
    return layout;
}

void UltraCanvasRichTextEdit::BuildBlockLayout(IRenderContext* ctx, int blockIndex) {
    BuildBlockLayout(ctx, editor.GetDocument()->blocks, blockIndex,
                     blockLayouts[static_cast<size_t>(blockIndex)], blockIndex);
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
            if (width > columnSpan && width > 0) {
                height *= columnSpan / width;
                width = columnSpan;
            }
            bl.bounds.width = std::max(8.0f, width);
            bl.bounds.height = std::max(8.0f, height);
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
    y = furnitureEdit ? PlaceFurnitureBeingEdited(ctx)
                      : pageView ? PlaceBlocksOnPages(ctx) : PlaceBlocksInColumn(ctx);
    // A page number in the body shows the page its block landed on, which is
    // only known now. Renumbering can change a block's width ("9" to "10"),
    // so the renumbered blocks are laid out and the pages placed again.
    if (pageView && !furnitureEdit && UpdateBodyPageFields()) {
        for (int i = 0; i < blockCount; i++) {
            BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
            if (!bl.valid) BuildBlockLayout(ctx, i);
        }
        y = PlaceBlocksOnPages(ctx);
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
        if (bl.bounds.y > viewBottom) break;
        RenderBlock(ctx, i, bl);
    }
    DrawFloats(ctx, /*behindText*/ false);
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
        const bool editedPage = furnitureEdit && static_cast<int>(&frame - pages.data()) == furnitureEdit->pageIndex;
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
        RenderBlock(ctx, blocks, blockIndex, bl, ColumnLeft(),
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
            RenderBlock(ctx, blocks, blockIndex, bl, ColumnLeft(),
                        visibleArea.y + slice.top - scrollOffset, selectionIndex);
            ctx->PopState();
        }
        ctx->PushState();
        ctx->ClipRect(Rect2Dd(left, visibleArea.y + slice.top + slice.headerHeight - scrollOffset, width, height));
        RenderBlock(ctx, blocks, blockIndex, bl, ColumnLeft(),
                    visibleArea.y + slice.top + slice.headerHeight - slice.from - scrollOffset, selectionIndex);
        ctx->PopState();
    }
}

void UltraCanvasRichTextEdit::RenderBlock(IRenderContext* ctx, const std::vector<RichDocBlock>& blocks,
                                          int index, const BlockLayout& bl,
                                          float originX, float originY, int blockIndex) {
    const RichDocBlock& block = blocks[static_cast<size_t>(index)];
    float textX = originX + bl.textLeft;

    switch (block.type) {
        case RichBlockType::HorizontalRule: {
            float centerY = originY + bl.bounds.height / 2.0f;
            ctx->DrawLine(Point2Dd(originX, centerY),
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
            ctx->DrawLine(Point2Dd(originX, centerY),
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
        const double markerX = std::max(static_cast<double>(originX),
                                        std::min(static_cast<double>(originX + bl.markerLeft),
                                                 static_cast<double>(originX + bl.textLeft) - gap - width));
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
    const double left = originX + Px(std::max(0.0f, block.leftIndentPt)) - padding;
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
    ctx->DrawFilledRectangle(Rect2Dd(ColumnLeft(), originY, ColumnWidth(), bl.bounds.height),
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
            visit(RichDocPosition(index, 0), Rect2Df(ColumnLeft() + bl.textLeft, top, bl.bounds.width, bl.bounds.height));
            continue;
        }
        inlineImages(bl, bl, ColumnLeft() + bl.textLeft, 0.0f, RichDocPosition(index, 0));
        for (size_t c = 0; c < bl.cells.size(); c++) {
            const BlockLayout& cell = *bl.cells[c];
            inlineImages(bl, cell, ColumnLeft() + cell.bounds.x + cell.textLeft, cell.bounds.y + cell.textTop,
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
    return PositionRect(editor.GetCaret());
}

Rect2Df UltraCanvasRichTextEdit::PositionRect(const RichDocPosition& position) const {
    if (position.blockIndex < 0 || position.blockIndex >= static_cast<int>(blockLayouts.size())) {
        return Rect2Df(0, 0, 0, 0);
    }
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(position.blockIndex)];
    float x = ColumnLeft() + bl.textLeft;
    float y = visibleArea.y + bl.bounds.y - scrollOffset;

    // Inside a table the caret belongs to a cell's layout, positioned at that
    // cell's origin rather than the table's.
    if (const BlockLayout* cell = CellLayoutFor(position)) {
        // Where the cell's text starts, as it is drawn (padding, vertical
        // alignment).
        const float cellX = ColumnLeft() + cell->bounds.x + cell->textLeft;
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
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(blockIndex)];

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
            Rect2Df box(bl.textLeft + hit.bounds.x, BlockToContentY(bl, hit.bounds.y),
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

    const float originX = ColumnLeft() + bl.textLeft;

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
        if (bl.bounds.y > viewBottom) break;

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
        case UCEventType::KeyDown:          return readOnly ? false : HandleKeyDown(event);
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
        // stays editable text rather than a trap.
        if (event.ctrl && onLinkClicked && onLinkClicked(link->linkTarget)) return true;
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
    // A double-click on a header or footer (or the margin where one would
    // be) edits it; one on the body while editing one goes back.
    if (!readOnly) {
        const float contentY = ToDocument(event.pointer).y - visibleArea.y + scrollOffset;
        int page = 0;
        bool footer = false;
        const bool inFurniture = FurnitureRegionAt(contentY, page, footer);
        if (!furnitureEdit && inFurniture && (pageView || !pages.empty())) {
            if (BeginFurnitureEditing(page, footer)) return true;
        }
        if (furnitureEdit && !(inFurniture && page == furnitureEdit->pageIndex && footer == furnitureEdit->footer)) {
            FinishHeaderFooterEditing();
            return true;
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
            editor.SetCaret(event.ctrl ? editor.PreviousWord(caret) : editor.PreviousCharacter(caret),
                            event.shift);
            break;
        case UCKeys::Right:
            editor.SetCaret(event.ctrl ? editor.NextWord(caret) : editor.NextCharacter(caret),
                            event.shift);
            break;
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
    SetClipboardText(internalClipboardText);
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
        editor.InsertBlocks(internalClipboard);
    } else if (!text.empty()) {
        editor.InsertText(text);
    }
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
        if (furnitureEdit->pageIndex == pageIndex && furnitureEdit->footer == footer) return true;
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
    state->bodyContentHeight = contentHeight;
    state->bodyEditor = std::move(editor);
    editor = UCRichDocumentEditor(furnitureDoc);
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
    return true;
}

void UltraCanvasRichTextEdit::SyncFurnitureToDocument() {
    if (!furnitureEdit) return;
    const std::shared_ptr<UCRichDocument>& body = furnitureEdit->bodyEditor.GetDocument();
    const std::shared_ptr<UCRichDocument>& edited = editor.GetDocument();
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
    const float bodyHeight = pageView ? PlaceBlocksOnPages(ctx) : PlaceBlocksInColumn(ctx);
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
    if (!pages.empty()) {
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
        if (bl.bounds.y > viewBottom) break;
        RenderBlockPieces(ctx, blocks, static_cast<int>(i), bl, -1);
    }
    for (const PlacedFloat& placed : parkedFloats) {
        if (!placed.image) continue;
        ctx->DrawImage(*placed.image, Rect2Dd(ColumnLeft() + placed.rect.x, visibleArea.y + placed.rect.y - scrollOffset,
                                              placed.rect.width, placed.rect.height), ImageFitMode::Contain);
    }
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
        const float edge = furnitureEdit->footer ? first.bounds.y - 4.0f : last.bounds.y + last.bounds.height + 4.0f;
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
        const std::string name = std::string(furnitureEdit->firstPage ? "First Page " : "")
                               + (furnitureEdit->footer ? "Footer" : "Header");
        // At the rule's right end, on the header's side of it.
        const double textY = furnitureEdit->footer ? y + 2.0 : y - label.fontSize * 1.6;
        const double textX = ColumnLeft() + ColumnWidth() - ctx->GetTextLineWidth(name);
        ctx->DrawText(name, Point2Dd(textX, textY));
        ctx->PopState();
    }
}

// ===== PDF =====

bool UltraCanvasRichTextEdit::ExportToPdf(const std::string& utf8Path, std::string& error) {
    const RichPageSetup page = EffectivePageSetup();
    auto pdf = UltraCanvasPdfSurface::CreateFile(utf8Path, page.widthPt, page.heightPt, error);
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

// Lays the document out as page view does, with the PDF's own context (its
// text is measured with the fonts it is drawn with), and draws page after
// page into it. The element's view state is put back afterwards.
bool UltraCanvasRichTextEdit::ExportPdfPages(UltraCanvasPdfSurface& pdf, std::string& error) {
    if (furnitureEdit) FinishHeaderFooterEditing();
    IRenderContext* ctx = pdf.GetContext();
    if (!ctx) {
        error = "The PDF has no page to draw on";
        return false;
    }
    const std::shared_ptr<UCRichDocument>& document = editor.GetDocument();
    if (document) pdf.SetMetadata(document->metadata.title, document->metadata.author, document->metadata.description);

    const bool savedPageView = pageView;
    const float savedZoom = zoom, savedScroll = scrollOffset, savedHScroll = hScrollOffset;
    const Rect2Df savedArea = visibleArea;

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

    const float pointsPerPixel = 1.0f / kPixelsPerPoint;
    for (size_t index = 0; index < pages.size(); index++) {
        if (index > 0) pdf.NextPage();
        const PageFrame& frame = pages[index];
        scrollOffset = frame.top;
        ctx->PushState();
        ctx->Scale(pointsPerPixel, pointsPerPixel);
        // The page's top-left corner to the PDF page's origin.
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
        ctx->PopState();
    }

    // Back to the screen: its own context lays everything out again.
    printing = false;
    pageView = savedPageView;
    zoom = savedZoom;
    scrollOffset = savedScroll;
    hScrollOffset = savedHScroll;
    visibleArea = savedArea;
    visibleAreaDirty = true;
    pages.clear();
    InvalidateDocument();
    return true;
}

} // namespace UltraCanvas
