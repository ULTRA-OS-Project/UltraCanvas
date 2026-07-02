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
// Version: 2.2.2
// Last Modified: 2026-07-02
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
    bool UltraCanvasLabel::EnsureTextLayout() {
        if (textLayout) return true;
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return false;
        textLayout = ctx->CreateTextLayout(text, isMarkup);
        if (!textLayout) return false;
        textLayout->SetFontStyle(style.fontStyle);
        textLayout->SetWrap(style.wrap);
        textLayout->SetAlignment(style.horizontalAlign);
        textLayout->SetVerticalAlignment(style.verticalAlign);
        return true;
    }

    // ===== Engine entry points =====

    void UltraCanvasLabel::ComputeIntrinsicSizes(const CSSLayout::LayoutContext& /*ctx*/) {
        if (!EnsureTextLayout()) {
            intrinsic.minContentWidth = intrinsic.maxContentWidth = 0;
            intrinsic.minContentHeight = intrinsic.maxContentHeight = 0;
            return;
        }
        // max-content: unbounded width → single-line natural width.
        textLayout->SetExplicitWidth(-1);
        const float maxW = (float)textLayout->GetLayoutWidth();
        const float maxH = (float)textLayout->GetLayoutHeight();

        // min-content (rough): one-line layout height as a lower bound on
        // width AND height. Proper word-by-word measurement is a follow-up.
        const float minW = maxH;        // ~one cap-line wide
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
    }

    Size2Df UltraCanvasLabel::MeasureOwnContent(std::optional<float> definiteContentWidth,
                                                const CSSLayout::LayoutContext& /*ctx*/) {
        if (!EnsureTextLayout()) {
            // No render context — report no own content; the block path then
            // sizes from size.*/constraints (matching the old base fallback).
            return Size2Df(0.f, 0.f);
        }

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

    // ===== EVENT HANDLING =====
    bool UltraCanvasLabel::OnEvent(const UCEvent &event) {
        if (UltraCanvasUIElement::OnEvent(event)) {
            return true;
        }        

        switch (event.type) {
            case UCEventType::MouseDown:
                if (Contains(event.pointer)) {
                    SetFocus(true);
                    if (onClick) {
                        onClick();
                    }
                    return true;
                }
                break;

            case UCEventType::MouseMove:
                if (Contains(event.pointer)) {
                    if (!IsHovered()) {
                        SetHovered(true);
                        if (onHoverEnter) {
                            onHoverEnter();
                        }
                    }
                } else {
                    if (IsHovered()) {
                        SetHovered(false);
                        if (onHoverLeave) {
                            onHoverLeave();
                        }
                    }
                }
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
        if (!EnsureTextLayout()) return;

        auto crect = GetLocalContentRect();
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

        if (!text.empty()) {
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
