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
    editor.SetDocument(std::move(document));
    blockLayouts.clear();
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

void UltraCanvasRichTextEdit::SetStyle(const RichTextEditStyle& s) {
    style = s;
    InvalidateDocument();
}

// ===== LAYOUT =====

void UltraCanvasRichTextEdit::Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) {
    UltraCanvasUIElement::Arrange(finalRect, ctx);
    visibleAreaDirty = true;
}

void UltraCanvasRichTextEdit::RecalculateVisibleArea() {
    Rect2Df bounds = GetLocalBounds();
    float scrollbar = (contentHeight > bounds.height) ? style.scrollbarWidth : 0.0f;
    visibleArea = Rect2Df(bounds.x + style.padding,
                          bounds.y + style.padding,
                          std::max(1.0f, bounds.width - 2 * style.padding - scrollbar),
                          std::max(1.0f, bounds.height - 2 * style.padding));
    visibleAreaDirty = false;
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

// Width of the widest label among the ordered items of `blockIndex`'s list
// level - the siblings before and after it in the same list, up to a
// shallower item or the end of the list. Bounded, so a very long list costs
// no more than a few hundred measurements.
float UltraCanvasRichTextEdit::WidestSiblingLabel(IRenderContext* ctx, int blockIndex) const {
    const std::vector<RichDocBlock>& blocks = editor.GetDocument()->blocks;
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
    const int count = editor.GetBlockCount();
    if (index < 0 || index + 1 >= count) return 0.0f;
    const RichDocBlock& block = editor.GetBlock(index);
    const RichDocBlock& next = editor.GetBlock(index + 1);
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
    }
    return font;
}

void UltraCanvasRichTextEdit::ApplyRunAttributes(ITextLayout* layout, const RichDocBlock& block,
                                                  const std::vector<RichTextRun>& runs,
                                                  std::vector<RichTextHitRect>* outHits,
                                                  int blockIndex,
                                                  std::vector<BlockLayout::InlineImage>* outInlineImages) const {
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

        // A picture in the line: reserve a box over its placeholder so the text
        // flows around it, and record where to draw it once the layout is laid.
        if (run.IsInlineImage()) {
            std::shared_ptr<UCImage> image;
            const UCRichDocument& document = *editor.GetDocument();
            if (run.mediaIndex >= 0 && run.mediaIndex < static_cast<int>(document.media.size())) {
                image = UCImage::LoadFromMemory(document.media[static_cast<size_t>(run.mediaIndex)].data);
            }
            float width = run.imageWidthPt > 0.0f ? run.imageWidthPt
                        : (image ? static_cast<float>(image->GetWidth()) : 16.0f);
            float height = run.imageHeightPt > 0.0f ? run.imageHeightPt
                         : (image ? static_cast<float>(image->GetHeight()) : 16.0f);
            // Never wider than the column it sits in; keep the aspect ratio.
            const float maxWidth = std::max(16.0f, visibleArea.width);
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

void UltraCanvasRichTextEdit::ApplySelectionAttributes(ITextLayout* layout, int blockIndex,
                                                       int cellRow, int cellColumn) const {
    if (!layout || !editor.HasSelection()) return;
    RichDocRange range = editor.GetSelectionRange();
    if (blockIndex < range.start.blockIndex || blockIndex > range.end.blockIndex) return;

    // A cell's layout only carries the highlight when the selection is in that
    // cell — and a selection never spans cells, so start and end agree.
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
        std::vector<BlockLayout::InlineImage>* outInlineImages, float paragraphOriginX) const {
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
    ApplyRunAttributes(layout.get(), block, runs, outHits, blockIndex, outInlineImages);
    return layout;
}

void UltraCanvasRichTextEdit::BuildBlockLayout(IRenderContext* ctx, int blockIndex) {
    BlockLayout& bl = blockLayouts[static_cast<size_t>(blockIndex)];
    const RichDocBlock& block = editor.GetBlock(blockIndex);

    bl.layout.reset();
    bl.cells.clear();
    bl.cellColumns.clear();
    bl.cellRows.clear();
    bl.hitRects.clear();
    bl.markerText.clear();
    bl.image.reset();

    float indent = BlockIndentFor(block);
    bl.markerLeft = std::max(0.0f, indent - style.listIndent * 0.8f);
    if (block.type == RichBlockType::ListItem && block.orderedList) {
        // A level's text starts after its widest label, as in a word
        // processor: "(III)" and "1.2.10." need more room than "1.", and
        // every item of the level lines up behind the widest one.
        const float labels = WidestSiblingLabel(ctx, blockIndex);
        const float needed = style.listIndent * static_cast<float>(block.listLevel) + labels
                           + std::max(4.0f, static_cast<float>(MarkerFontFor(block).fontSize) * 0.4f);
        indent = std::max(indent, needed);
    }
    bl.textLeft = indent;
    float wrapWidth = std::max(1.0f, visibleArea.width - indent);
    if (block.type != RichBlockType::ListItem) {
        // A hanging indent puts the first line left of the others: the layout
        // starts there, and its (negative) indent moves the rest back in.
        const float hang = Px(std::min(0.0f, block.firstLineIndentPt));
        bl.textLeft = std::max(0.0f, indent + hang);
        wrapWidth = std::max(1.0f, visibleArea.width - bl.textLeft - Px(std::max(0.0f, block.rightIndentPt)));
    }

    switch (block.type) {
        case RichBlockType::HorizontalRule:
            bl.bounds.width = visibleArea.width;
            bl.bounds.height = std::max(8.0f, static_cast<float>(style.baseFont.fontSize));
            break;

        case RichBlockType::PageBreak:
            bl.bounds.width = visibleArea.width;
            bl.bounds.height = std::max(12.0f, static_cast<float>(style.baseFont.fontSize) * 1.2f);
            break;

        case RichBlockType::Image: {
            const UCRichDocument& document = *editor.GetDocument();
            if (block.mediaIndex >= 0
                && block.mediaIndex < static_cast<int>(document.media.size())) {
                bl.image = UCImage::LoadFromMemory(document.media[static_cast<size_t>(block.mediaIndex)].data);
            }
            float width = block.imageWidthPt > 0 ? block.imageWidthPt
                                                 : (bl.image ? static_cast<float>(bl.image->GetWidth()) : 160.0f);
            float height = block.imageHeightPt > 0 ? block.imageHeightPt
                                                   : (bl.image ? static_cast<float>(bl.image->GetHeight()) : 120.0f);
            // Never wider than the text column; keep the aspect ratio.
            if (width > visibleArea.width && width > 0) {
                height *= visibleArea.width / width;
                width = visibleArea.width;
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
                bl.bounds.width = visibleArea.width;
                bl.bounds.height = static_cast<float>(style.baseFont.fontSize);
                break;
            }
            // The table's own width and place in the column: a fixed or
            // relative width from the document, else the whole column.
            const float columnSpace = std::max(24.0f * static_cast<float>(columnCount), visibleArea.width - indent);
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
                float rowHeight = static_cast<float>(style.baseFont.fontSize) * 1.3f;
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
                                                  blockIndex, &cell->inlineImages);
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

            bl.bounds.width = tableLeft + columnLeft[columnCount];
            bl.bounds.height = y;
            break;
        }

        default: {
            bl.layout = MakeRunsLayout(ctx, block, block.runs, wrapWidth, &bl.hitRects, blockIndex,
                                       &bl.inlineImages, bl.textLeft);
            ApplySelectionAttributes(bl.layout.get(), blockIndex);
            bl.bounds.width = static_cast<float>(bl.layout->GetLayoutWidth());
            bl.bounds.height = static_cast<float>(bl.layout->GetLayoutHeight()) + style.paragraphLeading;
            if (block.type == RichBlockType::ListItem) {
                // The document's own label ("b)", "1.2.", "(iv)") or bullet
                // when it has one; the view's otherwise.
                if (block.orderedList) {
                    bl.markerText = RichDocListLabel(editor.GetDocument()->blocks,
                                                     static_cast<size_t>(blockIndex));
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
    if (lastWrapWidth != visibleArea.width) {
        for (auto& bl : blockLayouts) bl.valid = false;
        lastWrapWidth = visibleArea.width;
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

    // Two passes: heights first (so scrolling and the scrollbar are correct),
    // then full layouts only for the blocks the viewport shows. Blocks outside
    // it keep the height they had, so a long document never pays for layouts
    // nobody is looking at.
    float y = 0.0f;
    float viewTop = scrollOffset - visibleArea.height;
    float viewBottom = scrollOffset + 2 * visibleArea.height;

    for (int i = 0; i < blockCount; i++) {
        BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
        bool visible = (y + bl.bounds.height >= viewTop) && (y <= viewBottom);
        if (!bl.valid && visible) {
            BuildBlockLayout(ctx, i);
        } else if (!bl.valid) {
            // Estimate: one line per block until it scrolls into view.
            const RichDocBlock& block = editor.GetBlock(i);
            bl.bounds.width = visibleArea.width;
            bl.bounds.height = static_cast<float>(style.baseFont.fontSize)
                             * (block.type == RichBlockType::Heading ? 2.0f : 1.4f);
            bl.textLeft = std::max(0.0f, BlockIndentFor(block) + Px(std::min(0.0f, block.firstLineIndentPt)));
        }
        bl.bounds.x = bl.textLeft;
        bl.bounds.y = y;
        y += bl.bounds.height + GapAfterBlock(i);
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
                             style.backgroundColor,
                             style.drawBorder ? 1.0f : 0.0f,
                             style.drawBorder ? style.borderColor : Colors::Transparent);

    ctx->PushState();
    ctx->ClipRect(Rect2Dd(visibleArea.x, visibleArea.y, visibleArea.width, visibleArea.height));

    float viewTop = scrollOffset;
    float viewBottom = scrollOffset + visibleArea.height;
    for (int i = 0; i < static_cast<int>(blockLayouts.size()); i++) {
        const BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
        if (bl.bounds.y + bl.bounds.height < viewTop) continue;
        if (bl.bounds.y > viewBottom) break;
        RenderBlock(ctx, i, bl);
    }
    DrawSpellErrorMarks(ctx);
    ctx->PopState();

    if (IsFocused() && !readOnly) {
        UpdateCaret();
    } else {
        UltraCanvasCaret::GetInstance().Hide(this);
    }

    DrawScrollbar(ctx);
}

// Draws the pictures sitting inside a laid-out text. The layout reserved a box
// for each over its placeholder, so IndexToPos() gives the box's top-left and
// the picture goes straight into it.
void UltraCanvasRichTextEdit::DrawInlineImages(IRenderContext* ctx, const BlockLayout& bl,
                                               float originX, float originY) const {
    if (bl.inlineImages.empty() || !bl.layout) return;
    for (const BlockLayout::InlineImage& placed : bl.inlineImages) {
        Rect2Di box = bl.layout->IndexToPos(placed.byteOffset);
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

void UltraCanvasRichTextEdit::RenderBlock(IRenderContext* ctx, int blockIndex,
                                          const BlockLayout& bl) {
    const RichDocBlock& block = editor.GetBlock(blockIndex);
    float originX = visibleArea.x;
    float originY = visibleArea.y + bl.bounds.y - scrollOffset;
    float textX = originX + bl.textLeft;

    switch (block.type) {
        case RichBlockType::HorizontalRule: {
            float centerY = originY + bl.bounds.height / 2.0f;
            ctx->DrawLine(Point2Dd(originX, centerY),
                          Point2Dd(originX + visibleArea.width, centerY), style.ruleColor);
            DrawSelectionForNonTextBlock(ctx, blockIndex, bl);
            return;
        }
        case RichBlockType::PageBreak: {
            float centerY = originY + bl.bounds.height / 2.0f;
            ctx->PushState();
            ctx->SetLineDash(UCDashPattern({4.0, 3.0}));
            ctx->DrawLine(Point2Dd(originX, centerY),
                          Point2Dd(originX + visibleArea.width, centerY), style.pageBreakColor);
            ctx->PopState();
            DrawSelectionForNonTextBlock(ctx, blockIndex, bl);
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
            DrawSelectionForNonTextBlock(ctx, blockIndex, bl);
            return;
        }
        case RichBlockType::Table: {
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
                ctx->SetCurrentPaint(style.textColor);
                const Point2Dd textAt(cellRect.x + cell->textLeft, cellRect.y + cell->textTop);
                ctx->DrawTextLayout(*cell->layout, textAt);
                DrawInlineImages(ctx, *cell, static_cast<float>(textAt.x), static_cast<float>(textAt.y));
            }
            DrawSelectionForNonTextBlock(ctx, blockIndex, bl);
            return;
        }
        default:
            break;
    }

    if (block.HasParagraphFrame()) DrawParagraphFrame(ctx, blockIndex, bl, originX, originY);

    if (block.type == RichBlockType::BlockQuote) {
        ctx->DrawFilledRectangle(Rect2Dd(originX, originY, 3.0, bl.bounds.height),
                                 style.quoteBarColor, 0.0f, Colors::Transparent);
    } else if (block.type == RichBlockType::CodeBlock) {
        ctx->DrawFilledRectangle(Rect2Dd(originX, originY, visibleArea.width, bl.bounds.height),
                                 style.codeBackgroundColor, 1.0f, style.codeBorderColor);
    }

    if (!bl.markerText.empty()) {
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
            ctx->PushState();
            ctx->SetStrokeWidth(std::max(1.0, static_cast<double>(Px(border.widthPt))));
            ctx->DrawLine(from, to, ParseHexColor(border.color, style.textColor));
            ctx->PopState();
        } else if (!readOnly) {
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
void UltraCanvasRichTextEdit::DrawParagraphFrame(IRenderContext* ctx, int blockIndex, const BlockLayout& bl,
                                                 float originX, float originY) const {
    const RichDocBlock& block = editor.GetBlock(blockIndex);
    const int count = editor.GetBlockCount();
    const bool withPrevious = blockIndex > 0 && editor.GetBlock(blockIndex - 1).HasParagraphFrame()
                              && editor.GetBlock(blockIndex - 1).SameParagraphFrame(block);
    const bool withNext = blockIndex + 1 < count && editor.GetBlock(blockIndex + 1).HasParagraphFrame()
                          && editor.GetBlock(blockIndex + 1).SameParagraphFrame(block);
    const float padding = kParagraphFramePadding;
    const double left = originX + Px(std::max(0.0f, block.leftIndentPt)) - padding;
    const double right = originX + visibleArea.width - Px(std::max(0.0f, block.rightIndentPt));
    // Grouped paragraphs meet halfway across the gap between them.
    const float gapBelow = GapAfterBlock(blockIndex);
    const float gapAbove = blockIndex > 0 ? GapAfterBlock(blockIndex - 1) : 0.0f;
    const double top = originY - (withPrevious ? gapAbove * 0.5f : padding);
    const double bottom = originY + bl.bounds.height + (withNext ? gapBelow * 0.5f : padding);

    if (!block.paragraphBackground.empty()) {
        ctx->DrawFilledRectangle(Rect2Dd(left, top, right - left, bottom - top),
                                 ParseHexColor(block.paragraphBackground, Colors::Transparent),
                                 0.0f, Colors::Transparent);
    }
    auto line = [&](const RichBorder& border, const Point2Dd& from, const Point2Dd& to) {
        if (!border.IsVisible()) return;
        ctx->PushState();
        ctx->SetStrokeWidth(std::max(1.0, static_cast<double>(Px(border.widthPt))));
        ctx->DrawLine(from, to, ParseHexColor(border.color, style.textColor));
        ctx->PopState();
    };
    if (!withPrevious) line(block.paragraphBorderTop, Point2Dd(left, top), Point2Dd(right, top));
    if (!withNext) line(block.paragraphBorderBottom, Point2Dd(left, bottom), Point2Dd(right, bottom));
    line(block.paragraphBorderLeft, Point2Dd(left, top), Point2Dd(left, bottom));
    line(block.paragraphBorderRight, Point2Dd(right, top), Point2Dd(right, bottom));
}

void UltraCanvasRichTextEdit::DrawSelectionForNonTextBlock(IRenderContext* ctx, int blockIndex,
                                                            const BlockLayout& bl) {
    // Blocks with no text cannot carry a selection attribute, so a selection
    // that swallows one is shown as a translucent wash over its box.
    if (!editor.HasSelection()) return;
    RichDocRange range = editor.GetSelectionRange();
    RichDocPosition here(blockIndex, 0);
    if (here < range.start || !(here < range.end)) return;
    Color wash = style.selectionColor;
    wash.a = 110;
    ctx->DrawFilledRectangle(Rect2Dd(visibleArea.x, visibleArea.y + bl.bounds.y - scrollOffset,
                                     visibleArea.width, bl.bounds.height),
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

void UltraCanvasRichTextEdit::UpdateCaret() {
    auto& caret = UltraCanvasCaret::GetInstance();
    Rect2Df rect = CaretRect();
    if (rect.width <= 0 && rect.height <= 0) { caret.Hide(this); return; }

    if (rect.y + rect.height < visibleArea.y || rect.y > visibleArea.y + visibleArea.height) {
        caret.Hide(this);
        return;
    }
    Point2Df windowPos = GetPositionInWindow();
    Rect2Di rectInWindow(static_cast<int>(windowPos.x + rect.x),
                         static_cast<int>(windowPos.y + rect.y),
                         2, static_cast<int>(std::max(4.0f, rect.height)));
    caret.Show(this, rectInWindow, style.cursorColor);
}

// ===== HIT TESTING =====

Rect2Df UltraCanvasRichTextEdit::CaretRect() const {
    RichDocPosition position = editor.GetCaret();
    if (position.blockIndex < 0 || position.blockIndex >= static_cast<int>(blockLayouts.size())) {
        return Rect2Df(0, 0, 0, 0);
    }
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(position.blockIndex)];
    float x = visibleArea.x + bl.textLeft;
    float y = visibleArea.y + bl.bounds.y - scrollOffset;

    // Inside a table the caret belongs to a cell's layout, positioned at that
    // cell's origin rather than the table's.
    if (const BlockLayout* cell = CellLayoutFor(position)) {
        // Where the cell's text starts, as it is drawn (padding, vertical
        // alignment).
        const float cellX = visibleArea.x + cell->bounds.x + cell->textLeft;
        const float cellY = visibleArea.y + bl.bounds.y + cell->bounds.y + cell->textTop - scrollOffset;
        if (!cell->layout) return Rect2Df(cellX, cellY, 2.0f, cell->bounds.height);
        int cellLength = static_cast<int>(cell->layout->GetText().size());
        int cellOffset = std::max(0, std::min(position.byteOffset, cellLength));
        Rect2Di cursor = cell->layout->GetCursorPos(cellOffset).strongPos;
        float cellHeight = cursor.height > 0 ? static_cast<float>(cursor.height)
                                             : static_cast<float>(style.baseFont.fontSize) * 1.3f;
        return Rect2Df(cellX + static_cast<float>(cursor.x),
                       cellY + static_cast<float>(cursor.y), 2.0f, cellHeight);
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
    return Rect2Df(x + static_cast<float>(cursor.x), y + static_cast<float>(cursor.y), 2.0f, height);
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
    float contentX = localPoint.x - visibleArea.x;

    int blockIndex = static_cast<int>(blockLayouts.size()) - 1;
    for (int i = 0; i < static_cast<int>(blockLayouts.size()); i++) {
        const BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
        // The gap below a block belongs half to it, half to the next one.
        const float bottom = bl.bounds.y + bl.bounds.height;
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
    if (!bl.cells.empty()) {
        const float cellY = contentY - bl.bounds.y;
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
    int layoutY = static_cast<int>(contentY - bl.bounds.y);
    UCLayoutHitResult hit = bl.layout->XYToIndex(std::max(0, layoutX), std::max(0, layoutY));

    std::string text = bl.layout->GetText();
    int offset = hit.index;
    if (hit.trailing > 0) offset = UCRichDocumentEditor::NextCharOffset(text, offset);
    offset = std::max(0, std::min(offset, static_cast<int>(text.size())));
    return RichDocPosition(blockIndex, offset);
}

const RichTextHitRect* UltraCanvasRichTextEdit::LinkAtPoint(const Point2Df& localPoint) const {
    float contentY = localPoint.y - visibleArea.y + scrollOffset;
    float contentX = localPoint.x - visibleArea.x;
    for (const BlockLayout& bl : blockLayouts) {
        if (contentY < bl.bounds.y || contentY > bl.bounds.y + bl.bounds.height) continue;
        for (const RichTextHitRect& hit : bl.hitRects) {
            Rect2Df box(bl.textLeft + hit.bounds.x, bl.bounds.y + hit.bounds.y,
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
    float bottom = bl.bounds.y + bl.bounds.height;
    if (const BlockLayout* cell = CellLayoutFor(caret)) {
        // Scroll to the line inside the cell, not to the whole table — a tall
        // table would otherwise jump the view to its top on every keystroke.
        top = bl.bounds.y + cell->bounds.y;
        bottom = top + cell->bounds.height;
        if (cell->layout) {
            Rect2Di cursor = cell->layout->GetCursorPos(caret.byteOffset).strongPos;
            top = bl.bounds.y + cell->bounds.y + static_cast<float>(cursor.y);
            bottom = top + static_cast<float>(std::max(cursor.height, 4));
        }
    } else if (bl.layout) {
        Rect2Di cursor = bl.layout->GetCursorPos(caret.byteOffset).strongPos;
        top = bl.bounds.y + static_cast<float>(cursor.y);
        bottom = top + static_cast<float>(std::max(cursor.height, 4));
    }
    if (top < scrollOffset) {
        SetScrollOffset(top);
    } else if (bottom > scrollOffset + visibleArea.height) {
        SetScrollOffset(bottom - visibleArea.height);
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

    const RichDocPosition hit = PositionFromPoint(Point2Df(static_cast<float>(x),
                                                           static_cast<float>(y)));
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

    const float originX = visibleArea.x + bl.textLeft;
    const float originY = visibleArea.y + bl.bounds.y - scrollOffset;

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

        rects.emplace_back(originX + left, originY + static_cast<float>(head.y),
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
        if (!bl.valid || bl.bounds.y + bl.bounds.height < viewTop) continue;
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

bool UltraCanvasRichTextEdit::HandleMouseDown(const UCEvent& event) {
    if (!Contains(event.pointer)) return false;

    if (event.button == UCMouseButton::Right) {
        if (!IsFocused()) SetFocus(true);
        // A click inside the selection keeps it, so a host menu's Cut and Copy
        // still act on what is highlighted; a click outside moves the caret so
        // that Paste lands where the user clicked. The hit test runs before
        // either, while the layouts still describe what was on screen.
        const RichDocPosition hit = PositionFromPoint(event.pointer);
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

    if (thumbRect.width > 0 && thumbRect.Contains(event.pointer)) {
        draggingThumb = true;
        thumbGrabOffset = static_cast<float>(event.pointerGlobal.y) - GetYInWindow() - thumbRect.y;
        UltraCanvasApplication::GetInstance()->CaptureMouse(this);
        return true;
    }
    if (!IsFocused()) SetFocus(true);

    if (const RichTextHitRect* link = LinkAtPoint(event.pointer)) {
        // Ctrl+click follows a link; a plain click places the caret, so a link
        // stays editable text rather than a trap.
        if (event.ctrl && onLinkClicked && onLinkClicked(link->linkTarget)) return true;
    }

    RichDocPosition position = PositionFromPoint(event.pointer);
    editor.SetCaret(position, event.shift);
    goalColumnX = -1.0f;
    selecting = true;
    UltraCanvasApplication::GetInstance()->CaptureMouse(this);
    AfterSelectionChange();
    return true;
}

bool UltraCanvasRichTextEdit::HandleMouseUp(const UCEvent& event) {
    (void)event;
    if (draggingThumb || selecting) {
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
    if (selecting) {
        editor.SetCaret(PositionFromPoint(event.pointer), /*extend*/ true);
        goalColumnX = -1.0f;
        AfterSelectionChange();
        return true;
    }
    return false;
}

bool UltraCanvasRichTextEdit::HandleDoubleClick(const UCEvent& event) {
    if (!Contains(event.pointer)) return false;
    editor.SelectWordAt(PositionFromPoint(event.pointer));
    AfterSelectionChange();
    return true;
}

bool UltraCanvasRichTextEdit::HandleMouseWheel(const UCEvent& event) {
    if (!Contains(event.pointer)) return false;
    float step = static_cast<float>(style.baseFont.fontSize) * 3.0f;
    SetScrollOffset(scrollOffset - (event.wheelDelta > 0 ? step : -step));
    return true;
}

bool UltraCanvasRichTextEdit::HandleKeyDown(const UCEvent& event) {
    UltraCanvasCaret::GetInstance().ResetBlink(this);
    bool handled = true;
    bool keepGoalColumn = false;

    const RichDocPosition caret = editor.GetCaret();

    switch (event.virtualKey) {
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
            Point2Df probe(visibleArea.x + 4.0f,
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
                editor.SplitBlock();
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
        editor.InsertText(event.text);
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
    std::ifstream in(path, std::ios::binary);
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
    std::ifstream in(path, std::ios::binary);
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

} // namespace UltraCanvas
