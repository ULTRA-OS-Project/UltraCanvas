// core/UltraCanvasLabel.cpp
// Reference implementation of the CSSLayout intrinsic-sizing protocol for
// a UI widget. The pattern, transcribed for future widget migrations:
//
//   1. EnsureTextLayout() builds the underlying text/content cache on
//      demand. It applies font/wrap/alignment but NOT the explicit width
//      — width is set by the caller (Measure/Intrinsic).
//   2. ComputeIntrinsicSizes() publishes min/max-content in BORDER-BOX
//      units (content + padding + border) so Flex/Grid can short-circuit
//      the extra Measure(Unbounded) pass.
//   3. MeasureOwnContent() returns the text's content-box size: nullopt
//      width → max-content width; a definite width → height at that width
//      (wrapping). The block layout adds padding/border + size.*/constraints.
//   4. Property setters call textLayout.reset() + InvalidateLayout()
//      (bubbles engine caches up) + RequestRedraw() (damage).
//
// Version: 2.7.0 - an inline image's border drawn per side (mitred corners)
// Version: 2.6.0 - inline images drawn in their frame (margin, border,
//                 padding, background, rounded corners)
// Version: 2.5.0 - inline images fitted and placed by their fit / position
// Version: 2.4.0 - min-content width is the widest unbreakable run
// Version: 2.3.0 - inline images at U+FFFC placeholders (LabelInlineImage)
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework

#include "UltraCanvasLabel.h"
#include "CSSLayout/LayoutUtils.h"
#include <algorithm>

namespace UltraCanvas {

    LabelStyle LabelStyle::HeaderStyle() {
        LabelStyle style;
        style.fontStyle.fontSize = 18.0f;
        style.fontStyle.fontWeight = FontWeight::Bold;
        style.textColor = Color(40, 40, 40, 255);
        return style;
    }

    LabelStyle LabelStyle::SubHeaderStyle() {
        LabelStyle style;
        style.fontStyle.fontSize = 14.0f;
        style.fontStyle.fontWeight = FontWeight::Bold;
        style.textColor = Color(60, 60, 60, 255);
        return style;
    }

    LabelStyle LabelStyle::CaptionStyle() {
        LabelStyle style;
        style.fontStyle.fontSize = 10.0f;
        style.textColor = Color(120, 120, 120, 255);
        return style;
    }

    LabelStyle LabelStyle::StatusStyle() {
        LabelStyle style;
        style.fontStyle.fontSize = 11.0f;
        style.textColor = Color(100, 100, 100, 255);
        return style;
    }

    UltraCanvasLabel::UltraCanvasLabel(const std::string &identifier, float x, float y, float w, float h,
                                       const std::string &labelText)
            : UltraCanvasUIElement(identifier, x, y, w, h), text(labelText) {

        // Initialize style
        style = LabelStyle::DefaultStyle();
        SetText(labelText);
    }


    // Property setters: invalidate the text cache (so EnsureTextLayout
    // rebuilds), invalidate the engine layout cache (so the next Measure
    // gets fresh dimensions and ancestors that auto-sized to us drop their
    // own caches), and request a redraw.
    void UltraCanvasLabel::SetText(const std::string &newText) {
        if (text != newText) {
            text = newText;
            textLayout.reset();
            InvalidateLayout();
            RequestRedraw();

            if (onTextChanged) {
                onTextChanged(text);
            }
        }
    }

    void UltraCanvasLabel::SetStyle(const LabelStyle &newStyle) {
        style = newStyle;
        textLayout.reset();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetFont(const std::string &fontFamily, float fontSize, FontWeight weight) {
        style.fontStyle.fontFamily = fontFamily;
        style.fontStyle.fontSize = fontSize;
        style.fontStyle.fontWeight = weight;
        textLayout.reset();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetFontSize(float fontSize) {
        style.fontStyle.fontSize = fontSize;
        textLayout.reset();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetFontWeight(const FontWeight w) {
        style.fontStyle.fontWeight = w;
        textLayout.reset();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetTextColor(const Color &color) {
        // Color doesn't affect layout — just damage the paint.
        style.textColor = color;
        RequestRedraw();
    }

    void UltraCanvasLabel::SetAlignment(TextAlignment horizontal, VerticalAlignment vertical) {
        style.horizontalAlign = horizontal;
        style.verticalAlign = vertical;
        textLayout.reset();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetWrap(TextWrap wrap) {
        style.wrap = wrap;
        textLayout.reset();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasLabel::SetTextIsMarkup(bool markup) {
        isMarkup = markup;
        textLayout.reset();
        InvalidateLayout();
        RequestRedraw();
    }

    // ===== Internal: ensure the text layout exists and reflects current
    // style/font/wrap/alignment. Does NOT touch the explicit width — the
    // caller (Measure/Intrinsic) sets that according to its
    // own context. Returns false if no render context is reachable yet
    // (element not attached to a window), in which case callers should
    // bail gracefully without crashing.
    bool UltraCanvasLabel::EnsureTextLayout(IRenderContext* context) {
        if (textLayout) return true;
        // The context we are about to draw into first, the element's window
        // second: a label rendered into an offscreen surface has no window,
        // and asking one it does not have is what left the layout null.
        IRenderContext* ctx = context ? context : GetRenderContext();
        if (!ctx) return false;
        textLayout = ctx->CreateTextLayout(text, isMarkup);
        if (!textLayout) return false;
        textLayout->SetFontStyle(style.fontStyle);
        textLayout->SetWrap(style.wrap);
        textLayout->SetAlignment(style.horizontalAlign);
        textLayout->SetVerticalAlignment(style.verticalAlign);
        // Reserve each inline image's box on its placeholder, above and below
        // the baseline as its alignment asks; the line grows to hold it.
        inlineAscents.assign(inlineImages.size(), 0.f);
        if (!inlineImages.empty()) {
            // The font's own ascent and descent, from a probe line in the
            // label's font; the x-height (for "middle") is taken as a share of
            // the ascent, which holds for common text faces.
            float fontAscent = 0.f, fontDescent = 0.f;
            if (auto probe = ctx->CreateTextLayout("Hxg", false)) {
                probe->SetFontStyle(style.fontStyle);
                fontAscent  = static_cast<float>(probe->GetBaseline());
                fontDescent = std::max(0.f, static_cast<float>(probe->GetLayoutHeight()) - fontAscent);
            }
            const float xHeight = fontAscent * 0.55f;
            for (size_t i = 0; i < inlineImages.size(); ++i) {
                const LabelInlineImage& img = inlineImages[i];
                const Size2Df size = InlineImageSize(img);
                // The whole margin box stands on the line, as CSS places an
                // inline replaced element.
                const float w = size.width + img.frame.Horizontal();
                const float h = size.height + img.frame.Vertical();
                float ascent = h;                                  // baseline
                switch (img.align) {
                    case LabelInlineImageAlign::Middle: ascent = (h + xHeight) / 2.f; break;
                    case LabelInlineImageAlign::Top:    ascent = fontAscent;           break;
                    case LabelInlineImageAlign::Bottom: ascent = h - fontDescent;      break;
                    case LabelInlineImageAlign::Baseline: break;
                }
                inlineAscents[i] = ascent;
                auto shape = TextAttributeFactory::CreateShape(w,
                                                               std::max(0.f, ascent),
                                                               std::max(0.f, h - ascent));
                shape->SetRange(img.byteOffset, img.byteOffset + 3);   // U+FFFC is 3 bytes
                textLayout->InsertAttribute(std::move(shape));
            }
        }
        return true;
    }

    // ===== INLINE IMAGES =====

    void UltraCanvasLabel::SetInlineImages(std::vector<LabelInlineImage> images) {
        inlineImages = std::move(images);
        inlineFitWidth = -1.f;
        textLayout.reset();
        InvalidateLayout();
        RequestRedraw();
    }

    Size2Df UltraCanvasLabel::InlineImageSize(const LabelInlineImage& image) const {
        // The picture shrinks so that it and its frame fit the line.
        const float room = inlineFitWidth - image.frame.Horizontal();
        if (inlineFitWidth > 0.f && image.width > room && image.width > 0.f) {
            const float fitted = std::max(0.f, room);
            const float scale = fitted / image.width;
            return Size2Df(fitted, image.height * scale);
        }
        return Size2Df(image.width, image.height);
    }

    void UltraCanvasLabel::FitInlineImages(float width) {
        if (inlineImages.empty()) return;
        const float fit = width > 0.f ? width : -1.f;
        if (fit == inlineFitWidth) return;
        bool changes = false;
        for (const auto& img : inlineImages) {
            const float w = img.width + img.frame.Horizontal();
            const bool wasScaled = inlineFitWidth > 0.f && w > inlineFitWidth;
            const bool isScaled  = fit > 0.f && w > fit;
            if (wasScaled || isScaled) { changes = true; break; }
        }
        inlineFitWidth = fit;
        if (changes) textLayout.reset();
    }

    Rect2Df UltraCanvasLabel::InlineImageRect(size_t index) {
        const Rect2Df box = InlineImageBoxRect(index);
        if (index >= inlineImages.size() || box.width <= 0.f) return box;
        const LabelInlineImageFrame& f = inlineImages[index].frame;
        return Rect2Df(box.x + f.borderLeft.width + f.paddingLeft,
                       box.y + f.borderTop.width + f.paddingTop,
                       std::max(0.f, box.width - f.borderLeft.width - f.borderRight.width -
                                     f.paddingLeft - f.paddingRight),
                       std::max(0.f, box.height - f.borderTop.width - f.borderBottom.width -
                                     f.paddingTop - f.paddingBottom));
    }

    Rect2Df UltraCanvasLabel::InlineImageBoxRect(size_t index) {
        const Rect2Df m = InlineImageMarginRect(index);
        if (index >= inlineImages.size() || m.width <= 0.f) return m;
        const LabelInlineImageFrame& f = inlineImages[index].frame;
        return Rect2Df(m.x + f.marginLeft, m.y + f.marginTop,
                       std::max(0.f, m.width - f.marginLeft - f.marginRight),
                       std::max(0.f, m.height - f.marginTop - f.marginBottom));
    }

    Rect2Df UltraCanvasLabel::InlineImageMarginRect(size_t index) {
        if (index >= inlineImages.size()) return {};
        if (!internalLayoutValid || !textLayout) {
            UpdateInternalLayout(GetRenderContext());
            if (!internalLayoutValid || !textLayout) return {};
        }
        const LabelInlineImage& img = inlineImages[index];
        const Size2Df size = InlineImageSize(img);
        const Rect2Di pos = textLayout->IndexToPos(img.byteOffset);
        const double baseline = textLayout->IndexToBaseline(img.byteOffset);
        const float x = static_cast<float>(GetBorderLeftWidth() + GetPaddingLeft() + pos.x);
        const float w = size.width + img.frame.Horizontal();
        const float h = size.height + img.frame.Vertical();
        const float ascent = index < inlineAscents.size() ? inlineAscents[index] : h;
        const float y = static_cast<float>(GetBorderTopWidth() + GetPaddingTop() +
                                           textLayout->GetLayoutVerticalOffset() +
                                           baseline - ascent);
        return Rect2Df(x, y, w, h);
    }

    // ===== Engine entry points =====

    void UltraCanvasLabel::ComputeIntrinsicSizes(const CSSLayout::LayoutContext& /*ctx*/) {
        FitInlineImages(-1.f);   // max-content: images at their own size
        if (!EnsureTextLayout()) {
            intrinsic.minContentWidth = intrinsic.maxContentWidth = 0;
            intrinsic.minContentHeight = intrinsic.maxContentHeight = 0;
            return;
        }
        // max-content: unbounded width → single-line natural width.
        textLayout->SetExplicitWidth(-1);
        const float maxW = (float)textLayout->GetLayoutWidth();
        const float maxH = (float)textLayout->GetLayoutHeight();

        // min-content: the widest unbreakable run - the layout at a one-pixel
        // width, where every break opportunity is taken (a table sizes its
        // columns from it). Text that does not wrap is as wide as its line.
        float minW = maxW;
        if (style.wrap != TextWrap::WrapNone && maxW > 1.f) {
            textLayout->SetExplicitWidth(1);
            minW = std::min(maxW, (float)textLayout->GetLayoutWidth());
            textLayout->SetExplicitWidth(-1);
        }
        const float minH = maxH;

        const float padH = (float)(GetTotalPaddingHorizontal() + GetTotalBorderHorizontal());
        const float padV = (float)(GetTotalPaddingVertical()   + GetTotalBorderVertical());

        // Publish in BORDER-BOX units so the engine can use them as-is when
        // computing flex bases / grid intrinsic tracks.
        intrinsic.valid = true;
        intrinsic.maxContentWidth  = maxW + padH;
        intrinsic.maxContentHeight = maxH + padV;
        intrinsic.minContentWidth  = minW + padH;
        intrinsic.minContentHeight = minH + padV;

        // The max-content probe above left the shared text layout at wrap width
        // -1 (no wrap). Force the next Render() to re-sync the real box width via
        // UpdateInternalLayout(); otherwise a paint that follows this measure
        // pass would draw the text unwrapped. (See MeasureOwnContent — same
        // shared-state hazard; this is what made wrapping intermittent.)
        internalLayoutValid = false;
    }

    Size2Df UltraCanvasLabel::MeasureOwnContent(std::optional<float> definiteContentWidth,
                                                const CSSLayout::LayoutContext& /*ctx*/) {
        FitInlineImages(definiteContentWidth ? *definiteContentWidth : -1.f);
        if (!EnsureTextLayout()) {
            // No render context — report no own content; the block path then
            // sizes from size.*/constraints (matching the old base fallback).
            return Size2Df(0.f, 0.f);
        }

        // Both branches below set the shared text layout's explicit width as a
        // measurement side effect. Force the next Render() to re-sync the
        // arranged box width via UpdateInternalLayout(); otherwise a stray
        // max-content measure pass could leave the label painting unwrapped.
        internalLayoutValid = false;

        if (definiteContentWidth.has_value()) {
            // Height at the resolved content width (reflects any wrapping).
            float w = std::max(0.f, *definiteContentWidth);
            textLayout->SetExplicitWidth(w);
            return Size2Df(w, (float)textLayout->GetLayoutHeight());
        }

        // Max-content: natural (unwrapped) width and its height.
        textLayout->SetExplicitWidth(-1);
        float w = (float)textLayout->GetLayoutWidth();
        return Size2Df(w, (float)textLayout->GetLayoutHeight());
    }

    // ===== TEXT LINK HIT TESTING =====
    int UltraCanvasLabel::LinkIndexAtPoint(const Point2Di& localPoint) {
        if (textLinks.empty()) return -1;
        // A click can arrive between a layout invalidation (resize, font
        // change) and the next render; sync the text layout on demand so the
        // hit test never runs against stale wrap widths — or silently fails
        // before the first paint.
        if (!internalLayoutValid || !textLayout) {
            UpdateInternalLayout(GetRenderContext());
            if (!internalLayoutValid || !textLayout) return -1;
        }

        // The layout is drawn at the content origin plus its vertical
        // alignment offset (see Render); undo both to get layout-local pixels.
        int layoutX = localPoint.x - (GetBorderLeftWidth() + GetPaddingLeft());
        int layoutY = localPoint.y - (GetBorderTopWidth() + GetPaddingTop())
                      - static_cast<int>(textLayout->GetLayoutVerticalOffset());
        if (layoutX < 0 || layoutY < 0) return -1;

        UCLayoutHitResult hit = textLayout->XYToIndex(layoutX, layoutY);
        if (!hit.inside) return -1;
        for (size_t i = 0; i < textLinks.size(); ++i) {
            if (hit.index >= textLinks[i].startByte &&
                hit.index < textLinks[i].endByte) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    // ===== EVENT HANDLING =====
    bool UltraCanvasLabel::OnEvent(const UCEvent &event) {
        if (UltraCanvasUIElement::OnEvent(event)) {
            return true;
        }

        switch (event.type) {
            case UCEventType::MouseDown:
                if (Contains(event.pointer)) {
                    SetFocus(true);
                    if (onLinkActivated) {
                        int link = LinkIndexAtPoint(event.pointer);
                        if (link >= 0) {
                            onLinkActivated(textLinks[static_cast<size_t>(link)].href);
                            return true;
                        }
                    }
                    if (onClick) {
                        onClick();
                        return true;
                    }
                }
                break;

            case UCEventType::MouseMove:
                if (Contains(event.pointer)) {
                    if (!textLinks.empty()) {
                        hoveredLink = LinkIndexAtPoint(event.pointer);
                    }
                    if (!IsHovered()) {
                        SetHovered(true);
                        if (onHoverEnter) {
                            onHoverEnter();
                        }
                    }
                } else {
                    hoveredLink = -1;
                    if (IsHovered()) {
                        SetHovered(false);
                        if (onHoverLeave) {
                            onHoverLeave();
                        }
                    }
                }
                break;

            case UCEventType::MouseLeave:
                hoveredLink = -1;
                break;

            default:
                break;
        }

        return false;
    }

    void UltraCanvasLabel::InvalidateLayout() {
        CSSLayout::Element::InvalidateLayout();
        internalLayoutValid = false;
    }

    void UltraCanvasLabel::Arrange(const Rect2Df& finalRect,
                                   const CSSLayout::LayoutContext& ctx) {
        // Capture BEFORE the base call overwrites finalBounds.
        const bool sizeChanged = (finalRect.width  != finalBounds.width) ||
                                 (finalRect.height != finalBounds.height);
        UltraCanvasUIElement::Arrange(finalRect, ctx);
        // The text layout's wrap width is derived from finalBounds in
        // UpdateInternalLayout(); when the engine re-arranges us to a new size
        // (e.g. a window resize growing a grid/flex cell) the cached layout
        // must be re-synced, or the text keeps its previous wrap width.
        if (sizeChanged) internalLayoutValid = false;
    }

    void UltraCanvasLabel::UpdateInternalLayout(IRenderContext *ctx) {
        // finalBounds is owned by the engine (set during Arrange).
        auto crect = GetLocalContentRect();
        FitInlineImages(crect.width);
        if (!EnsureTextLayout(ctx)) return;

        // When a non-zero content area exists, point the text layout at it.
        // Negative or zero collapses to "no explicit width" (max-content).
        textLayout->SetExplicitWidth(crect.width  > 0 ? crect.width  : -1);
        textLayout->SetExplicitHeight(crect.height > 0 ? crect.height : -1);
        // Ellipsize when we'd overflow a fixed width with single-line text.
        if (style.wrap == TextWrap::WrapNone && crect.width > 0) {
            textLayout->SetEllipsize(EllipsizeMode::EllipsizeEnd);
        }
        internalLayoutValid = true;
    }

    void UltraCanvasLabel::Render(IRenderContext *ctx, const Rect2Df& dirtyRect) {
        if (!internalLayoutValid) {
            UpdateInternalLayout(ctx);
        }

        UltraCanvasUIElement::Render(ctx, dirtyRect);

        // The background, border and any decoration are drawn above whatever
        // happens to the text. A layout that could not be built is a label
        // without its words, not a crash - the same call the framework makes
        // in IRenderContext::DrawText, which checks before it dereferences.
        if (!text.empty() && textLayout) {
            // Element-local content rect: ctx is already translated to element origin
            int contentX = GetBorderLeftWidth() + GetPaddingLeft();
            int contentY = GetBorderTopWidth() + GetPaddingTop();
            if (style.hasShadow) {
                ctx->SetCurrentPaint(style.shadowColor);
                //textLayout->ChangeAttribute(TextAttributeFactory::CreateForeground(style.shadowColor));
                ctx->DrawTextLayout(*textLayout, {contentX + style.shadowOffset.x, contentY + style.shadowOffset.y});
            }
//            textLayout->ChangeAttribute(TextAttributeFactory::CreateForeground(style.textColor));
            ctx->SetCurrentPaint(IsDisabled() ? style.disabledTextColor : style.textColor);
            ctx->DrawTextLayout(*textLayout, Point2Di(contentX, contentY));

            // Inline images, into the boxes their placeholders reserved.
            for (size_t i = 0; i < inlineImages.size(); ++i) {
                if (!inlineImages[i].image) continue;
                const LabelInlineImage& img = inlineImages[i];
                const LabelInlineImageFrame& f = img.frame;
                // The frame: background over the border box, border inside it.
                const Rect2Df box = InlineImageBoxRect(i);
                const Rect2Dd boxD(box.x, box.y, box.width, box.height);
                if (box.width > 0.f && box.height > 0.f && f.HasBorder()) {
                    // Each side its own, meeting on the corners' diagonals.
                    if (f.background.a > 0) ctx->SetFillPaint(f.background);
                    const double rad = f.borderRadius;
                    ctx->DrawRoundedRectangleWidthBorders(
                        boxD, f.background.a > 0,
                        f.borderLeft.width, f.borderRight.width, f.borderTop.width, f.borderBottom.width,
                        f.borderLeft.color, f.borderRight.color, f.borderTop.color, f.borderBottom.color,
                        rad, rad, rad, rad,
                        f.borderLeft.dash, f.borderRight.dash, f.borderTop.dash, f.borderBottom.dash);
                } else if (box.width > 0.f && box.height > 0.f && f.background.a > 0) {
                    ctx->DrawFilledRectangle(boxD, f.background, 0.f, Colors::Transparent, f.borderRadius);
                }
                const Rect2Df r = InlineImageRect(i);
                if (r.width <= 0.f || r.height <= 0.f) continue;
                // The rounded corners inside the border: what each side leaves
                // of the radius.
                const float lw = f.borderLeft.width, rw = f.borderRight.width;
                const float tw = f.borderTop.width, bw = f.borderBottom.width;
                const double itl = std::max(0.f, f.borderRadius - std::max(lw, tw));
                const double itr = std::max(0.f, f.borderRadius - std::max(rw, tw));
                const double ibr = std::max(0.f, f.borderRadius - std::max(rw, bw));
                const double ibl = std::max(0.f, f.borderRadius - std::max(lw, bw));
                const bool rounded = itl > 0 || itr > 0 || ibr > 0 || ibl > 0;
                if (img.fit == ImageFitMode::Fill && !rounded) {
                    ctx->DrawImage(*img.image, Rect2Dd(r.x, r.y, r.width, r.height), ImageFitMode::Fill);
                    continue;
                }
                // Fitted inside its box and placed there (object-fit /
                // object-position), clipped to the box and to the rounded
                // corners inside the border.
                const Size2Df natural(static_cast<float>(img.image->GetWidth()),
                                      static_cast<float>(img.image->GetHeight()));
                const Rect2Df d = FitImageRect(natural, r, img.fit, img.position);
                if (d.width <= 0.f || d.height <= 0.f) continue;
                ctx->PushState();
                if (rounded) {
                    ctx->ClipRoundedRectangle(Rect2Dd(box.x + lw, box.y + tw,
                                                      box.width - lw - rw, box.height - tw - bw),
                                              itl, itr, ibr, ibl);
                }
                ctx->ClipRect(Rect2Dd(r.x, r.y, r.width, r.height));
                ctx->DrawImage(*img.image, Rect2Dd(d.x, d.y, d.width, d.height), ImageFitMode::Fill);
                ctx->PopState();
            }
        }
    }

    LabelBuilder::LabelBuilder(const std::string &identifier) {
        label = CreateLabel(identifier);
    }

    LabelBuilder &LabelBuilder::SetText(const std::string &text) {
        label->SetText(text);
        return *this;
    }

    LabelBuilder &
    LabelBuilder::SetFont(const std::string &fontFamily, float fontSize) {
        label->SetFont(fontFamily, fontSize);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetTextColor(const Color &color) {
        label->SetTextColor(color);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetBackgroundColor(const Color &color) {
        label->SetBackgroundColor(color);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetAlignment(TextAlignment align) {
        label->SetAlignment(align);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetPadding(float padding) {
        label->SetPadding(padding);
        return *this;
    }

    LabelBuilder &LabelBuilder::SetStyle(const LabelStyle &style) {
        label->SetStyle(style);
        return *this;
    }

    LabelBuilder &LabelBuilder::OnClick(std::function<void()> callback) {
        label->onClick = callback;
        return *this;
    }

}
