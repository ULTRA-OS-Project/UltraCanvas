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

// Ordinal of a list item among its siblings at the same level, so numbering
// restarts after a paragraph and after a deeper sublist closes.
int OrderedItemNumber(const UCRichDocumentEditor& editor, int blockIndex) {
    const RichDocBlock& block = editor.GetBlock(blockIndex);
    int number = 1;
    for (int i = blockIndex - 1; i >= 0; i--) {
        const RichDocBlock& previous = editor.GetBlock(i);
        if (previous.type != RichBlockType::ListItem) break;
        if (previous.listLevel < block.listLevel) break;
        if (previous.listLevel > block.listLevel) continue;
        if (previous.orderedList != block.orderedList) break;
        number++;
    }
    return number;
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
    switch (block.type) {
        case RichBlockType::ListItem:
            return style.listIndent * static_cast<float>(block.listLevel + 1);
        case RichBlockType::BlockQuote:
            return style.quoteIndent;
        case RichBlockType::CodeBlock:
            return style.codeIndent;
        default:
            return 0.0f;
    }
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
                                                  int blockIndex) const {
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

void UltraCanvasRichTextEdit::ApplySelectionAttributes(ITextLayout* layout, int blockIndex) const {
    if (!layout || !editor.HasSelection()) return;
    RichDocRange range = editor.GetSelectionRange();
    if (blockIndex < range.start.blockIndex || blockIndex > range.end.blockIndex) return;

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
        float wrapWidth, std::vector<RichTextHitRect>* outHits, int blockIndex) const {
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
    ApplyRunAttributes(layout.get(), block, runs, outHits, blockIndex);
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
    bl.textLeft = indent;
    bl.markerLeft = std::max(0.0f, indent - style.listIndent * 0.8f);
    float wrapWidth = std::max(1.0f, visibleArea.width - indent);

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
            // Columns share the width evenly; each cell lays out inside its own
            // column and the row takes the tallest cell's height.
            size_t columnCount = 0;
            for (const auto& row : block.tableRows) columnCount = std::max(columnCount, row.cells.size());
            if (columnCount == 0) {
                bl.bounds.width = visibleArea.width;
                bl.bounds.height = static_cast<float>(style.baseFont.fontSize);
                break;
            }
            float columnWidth = std::max(24.0f, (visibleArea.width - indent) / static_cast<float>(columnCount));
            float y = 0.0f;
            for (size_t r = 0; r < block.tableRows.size(); r++) {
                const RichTableRow& row = block.tableRows[r];
                float rowHeight = 0.0f;
                for (size_t c = 0; c < row.cells.size(); c++) {
                    RichDocBlock cellBlock;
                    cellBlock.type = RichBlockType::Paragraph;
                    auto cell = std::make_unique<BlockLayout>();
                    cell->layout = MakeRunsLayout(ctx, cellBlock, row.cells[c].runs,
                                                  columnWidth - 8.0f, nullptr, blockIndex);
                    cell->bounds = Rect2Df(indent + static_cast<float>(c) * columnWidth, y,
                                           columnWidth,
                                           static_cast<float>(cell->layout->GetLayoutHeight()));
                    rowHeight = std::max(rowHeight, cell->bounds.height);
                    bl.cellColumns.push_back(static_cast<int>(c));
                    bl.cellRows.push_back(static_cast<int>(r));
                    bl.cells.push_back(std::move(cell));
                }
                for (auto& cell : bl.cells) {
                    if (std::abs(cell->bounds.y - y) < 0.01f) cell->bounds.height = rowHeight;
                }
                y += rowHeight + 4.0f;
            }
            bl.bounds.width = columnWidth * static_cast<float>(columnCount);
            bl.bounds.height = y;
            break;
        }

        default: {
            bl.layout = MakeRunsLayout(ctx, block, block.runs, wrapWidth, &bl.hitRects, blockIndex);
            ApplySelectionAttributes(bl.layout.get(), blockIndex);
            bl.bounds.width = static_cast<float>(bl.layout->GetLayoutWidth());
            bl.bounds.height = static_cast<float>(bl.layout->GetLayoutHeight()) + style.paragraphLeading;
            if (block.type == RichBlockType::ListItem) {
                bl.markerText = block.orderedList
                        ? std::to_string(OrderedItemNumber(editor, blockIndex)) + "."
                        : style.bulletCharacters[static_cast<size_t>(
                              std::min<int>(block.listLevel, static_cast<int>(style.bulletCharacters.size()) - 1))];
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
            bl.textLeft = BlockIndentFor(block);
        }
        bl.bounds.x = bl.textLeft;
        bl.bounds.y = y;
        y += bl.bounds.height + style.blockSpacing;
    }
    contentHeight = std::max(0.0f, y - style.blockSpacing);
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
                ctx->DrawFilledRectangle(cellRect, Colors::Transparent, 1.0f, style.tableBorderColor);
                ctx->SetCurrentPaint(style.textColor);
                ctx->DrawTextLayout(*cell->layout, Point2Dd(cellRect.x + 4.0, cellRect.y + 2.0));
            }
            DrawSelectionForNonTextBlock(ctx, blockIndex, bl);
            return;
        }
        default:
            break;
    }

    if (block.type == RichBlockType::BlockQuote) {
        ctx->DrawFilledRectangle(Rect2Dd(originX, originY, 3.0, bl.bounds.height),
                                 style.quoteBarColor, 0.0f, Colors::Transparent);
    } else if (block.type == RichBlockType::CodeBlock) {
        ctx->DrawFilledRectangle(Rect2Dd(originX, originY, visibleArea.width, bl.bounds.height),
                                 style.codeBackgroundColor, 1.0f, style.codeBorderColor);
    }

    if (!bl.markerText.empty()) {
        ctx->PushState();
        ctx->SetFontStyle(style.baseFont);
        ctx->SetTextPaint(style.listMarkerColor);
        ctx->DrawText(bl.markerText, Point2Dd(originX + bl.markerLeft, originY));
        ctx->PopState();
    }

    if (bl.layout) {
        ctx->SetCurrentPaint(style.textColor);
        ctx->DrawTextLayout(*bl.layout, Point2Dd(textX, originY));
    }
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

RichDocPosition UltraCanvasRichTextEdit::PositionFromPoint(const Point2Df& localPoint) const {
    if (blockLayouts.empty()) return RichDocPosition(0, 0);

    float contentY = localPoint.y - visibleArea.y + scrollOffset;
    float contentX = localPoint.x - visibleArea.x;

    int blockIndex = static_cast<int>(blockLayouts.size()) - 1;
    for (int i = 0; i < static_cast<int>(blockLayouts.size()); i++) {
        const BlockLayout& bl = blockLayouts[static_cast<size_t>(i)];
        if (contentY < bl.bounds.y + bl.bounds.height + style.blockSpacing * 0.5f) {
            blockIndex = i;
            break;
        }
    }
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(blockIndex)];
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
    int blockIndex = editor.GetCaret().blockIndex;
    if (blockIndex < 0 || blockIndex >= static_cast<int>(blockLayouts.size())) return;
    const BlockLayout& bl = blockLayouts[static_cast<size_t>(blockIndex)];

    float top = bl.bounds.y;
    float bottom = bl.bounds.y + bl.bounds.height;
    if (bl.layout) {
        Rect2Di cursor = bl.layout->GetCursorPos(editor.GetCaret().byteOffset).strongPos;
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
            // Tab restructures lists; elsewhere it is left to focus traversal.
            if (editor.GetBlock(caret.blockIndex).type == RichBlockType::ListItem) {
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
