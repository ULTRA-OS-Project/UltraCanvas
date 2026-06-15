// core/UltraCanvasGroupBox.cpp
// Titled container ("group box" / "fieldset"). See UltraCanvasGroupBox.h.
// The frame, caption and indicators are drawn in element-local coordinates
// (the render context is already translated to this element's origin). The
// border line, padding and caption strip are kept in sync through
// RecalculatePadding() so the inherited container lays the children out
// inside the visible frame, below the caption.
// Version: 1.0.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework
#include "UltraCanvasGroupBox.h"
#include <algorithm>
#include <cmath>

namespace UltraCanvas {

    // Every child of a container is a UI element.
    static inline UltraCanvasUIElement* asUI(const std::shared_ptr<CSSLayout::Element>& c) {
        return static_cast<UltraCanvasUIElement*>(c.get());
    }

    GroupBoxVisualStyle GroupBoxVisualStyle::Default() {
        GroupBoxVisualStyle s;
        s.titleFont.fontSize = 13.0f;
        s.titleFont.fontWeight = FontWeight::Bold;
        return s;
    }

    GroupBoxVisualStyle GroupBoxVisualStyle::Card() {
        GroupBoxVisualStyle s;
        s.backgroundColor = Color(245, 245, 247, 255);
        s.borderColor     = Color(210, 210, 214, 255);
        s.borderWidth     = 1.0f;
        s.cornerRadius    = 10.0f;
        s.titleFont.fontSize = 13.0f;
        s.titleFont.fontWeight = FontWeight::Bold;
        s.titleColor      = Color(90, 90, 95, 255);
        s.headerBackgroundColor = Colors::Transparent;
        s.showHeaderSeparator   = true;
        s.contentPadding  = 12.0f;
        return s;
    }

    UltraCanvasGroupBox::UltraCanvasGroupBox(const std::string& identifier,
                                             float x, float y, float w, float h,
                                             const std::string& titleText)
            : UltraCanvasContainer(identifier, x, y, w, h), title(titleText) {
        visualStyle = GroupBoxVisualStyle::Default();
        // The frame draws its own background/border; keep the base element's
        // background transparent and its engine border at zero so the inherited
        // Render() doesn't paint a second (square) box underneath the frame.
        SetBackgroundColor(Colors::Transparent);
        expandedHeight = size.height;
        RecalculatePadding();
    }

// ===== TITLE =====
    void UltraCanvasGroupBox::SetTitle(const std::string& newTitle) {
        if (title == newTitle) return;
        title = newTitle;
        RecalculatePadding();
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetTitleAlignment(GroupBoxTitleAlignment alignment) {
        titleAlignment = alignment;
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetFrameStyle(GroupBoxFrameStyle newStyle) {
        if (frameStyle == newStyle) return;
        frameStyle = newStyle;
        RecalculatePadding();
        RequestRedraw();
    }

// ===== STYLE =====
    void UltraCanvasGroupBox::SetVisualStyle(const GroupBoxVisualStyle& newStyle) {
        visualStyle = newStyle;
        RecalculatePadding();
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetTitleFont(const std::string& family, float fontSize, FontWeight weight) {
        visualStyle.titleFont.fontFamily = family;
        visualStyle.titleFont.fontSize = fontSize;
        visualStyle.titleFont.fontWeight = weight;
        RecalculatePadding();
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetTitleColor(const Color& color) {
        visualStyle.titleColor = color;
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetBorderColor(const Color& color) {
        visualStyle.borderColor = color;
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetBorderWidth(float width) {
        visualStyle.borderWidth = std::max(0.0f, width);
        RecalculatePadding();
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetCornerRadius(float radius) {
        visualStyle.cornerRadius = std::max(0.0f, radius);
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetGroupBackgroundColor(const Color& color) {
        visualStyle.backgroundColor = color;
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetContentPadding(float padding) {
        visualStyle.contentPadding = std::max(0.0f, padding);
        RecalculatePadding();
    }

// ===== CHECKABLE =====
    void UltraCanvasGroupBox::SetCheckable(bool value) {
        if (checkable == value) return;
        checkable = value;
        ApplyCheckStateToChildren();
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetChecked(bool value) {
        if (checked == value) return;
        checked = value;
        ApplyCheckStateToChildren();
        if (onCheckedChanged) onCheckedChanged(checked);
        RequestRedraw();
    }

    void UltraCanvasGroupBox::ApplyCheckStateToChildren() {
        // Only a checkable box gates its children; otherwise leave them alone.
        bool disable = checkable && !checked;
        for (auto& c : Children()) {
            asUI(c)->SetDisabled(disable);
        }
    }

// ===== COLLAPSIBLE =====
    void UltraCanvasGroupBox::SetCollapsible(bool value) {
        if (collapsible == value) return;
        collapsible = value;
        if (!collapsible && collapsed) {
            SetCollapsed(false);
        }
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetCollapsed(bool value) {
        if (collapsed == value) return;
        collapsed = value;
        if (collapsed) {
            // Remember the expanded height, then shrink to just the title bar.
            expandedHeight = size.height;
            size.height = CSSLayout::Dimension::Px(TitleAreaHeight() + EffectiveBorderWidth());
        } else {
            size.height = expandedHeight;
        }
        InvalidateLayout();
        if (onCollapsedChanged) onCollapsedChanged(collapsed);
        RequestRedraw();
    }

// ===== GEOMETRY HELPERS =====
    float UltraCanvasGroupBox::EffectiveBorderWidth() const {
        return frameStyle == GroupBoxFrameStyle::Flat ? 0.0f : visualStyle.borderWidth;
    }

    float UltraCanvasGroupBox::TitleAreaHeight() const {
        // No caption → the strip is just the (top) border.
        if (title.empty() && !checkable && !collapsible) {
            return EffectiveBorderWidth();
        }
        // Approximate text height from the font size (avoids needing a render
        // context here); padding is added above and below.
        float textHeight = std::ceil(visualStyle.titleFont.fontSize * 1.35f);
        float h = textHeight + 2.0f * visualStyle.titleVerticalPadding;
        return std::max(h, EffectiveBorderWidth() + 2.0f);
    }

    Rect2Df UltraCanvasGroupBox::TitleBarRect() const {
        return Rect2Df(0.0f, 0.0f, GetWidth(), TitleAreaHeight());
    }

    float UltraCanvasGroupBox::IndicatorSize() const {
        return std::max(10.0f, static_cast<float>(std::round(visualStyle.titleFont.fontSize)));
    }

    void UltraCanvasGroupBox::RecalculatePadding() {
        const float bw  = EffectiveBorderWidth();
        const float cp  = visualStyle.contentPadding;
        const float top = (title.empty() && !checkable && !collapsible)
                          ? (bw + cp)
                          : (TitleAreaHeight() + cp);

        // Fold the visual border thickness into the padding so children sit
        // inside the frame. The engine border stays at zero (the frame is drawn
        // by this class), so content area = bounds minus this padding.
        box.padding.top    = CSSLayout::Dimension::Px(top);
        box.padding.left   = CSSLayout::Dimension::Px(bw + cp);
        box.padding.right  = CSSLayout::Dimension::Px(bw + cp);
        box.padding.bottom = CSSLayout::Dimension::Px(bw + cp);
        InvalidateLayout();
    }

// ===== TITLE LAYOUT =====
    UltraCanvasGroupBox::TitleLayout
    UltraCanvasGroupBox::ComputeTitleLayout(IRenderContext* ctx) const {
        TitleLayout tl;
        tl.hasTitle = !title.empty() || checkable || collapsible;
        if (!tl.hasTitle) return tl;

        const float bw = EffectiveBorderWidth();
        const float th = TitleAreaHeight();
        const float centerY = th * 0.5f;
        const float spacing = 4.0f;
        const float indSize = IndicatorSize();

        // Measure the caption text with the title font.
        Size2Di textSize{0, 0};
        if (!title.empty()) {
            ctx->SetFontStyle(visualStyle.titleFont);
            textSize = ctx->GetTextLineDimensions(title);
        }
        tl.textSize = textSize;

        float contentW = 0.0f;
        if (collapsible) contentW += indSize + spacing;
        if (checkable)   contentW += indSize + spacing;
        contentW += static_cast<float>(textSize.width);

        // Resolve the leading X according to alignment.
        const float w = GetWidth();
        float leadingX;
        switch (titleAlignment) {
            case GroupBoxTitleAlignment::Center:
                leadingX = (w - contentW) * 0.5f;
                break;
            case GroupBoxTitleAlignment::Right:
                leadingX = w - bw - visualStyle.titleIndent - visualStyle.titleGap - contentW;
                break;
            case GroupBoxTitleAlignment::Left:
            default:
                leadingX = bw + visualStyle.titleIndent + visualStyle.titleGap;
                break;
        }
        leadingX = std::max(leadingX, bw + visualStyle.titleGap);

        float x = leadingX;
        if (collapsible) {
            tl.arrowRect = Rect2Df(x, centerY - indSize * 0.5f, indSize, indSize);
            x += indSize + spacing;
        }
        if (checkable) {
            tl.checkRect = Rect2Df(x, centerY - indSize * 0.5f, indSize, indSize);
            x += indSize + spacing;
        }
        tl.textPos = Point2Df(x, centerY - textSize.height * 0.5f);

        tl.gapX0 = leadingX - visualStyle.titleGap;
        tl.gapX1 = leadingX + contentW + visualStyle.titleGap;
        return tl;
    }

// ===== RENDERING =====
    void UltraCanvasGroupBox::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        ctx->PushState();
        TitleLayout tl = ComputeTitleLayout(ctx);
        DrawFrame(ctx, tl);
        DrawTitle(ctx, tl);
        ctx->PopState();

        // Collapsed: only the title bar is shown; skip children and scrollbars.
        if (collapsed) return;

        // The inherited container renders the children (and scrollbars). The base
        // UltraCanvasUIElement::Render it calls is a no-op here because the engine
        // border is zero and the base background is transparent.
        UltraCanvasContainer::Render(ctx, dirtyRect);
    }

    void UltraCanvasGroupBox::DrawFrame(IRenderContext* ctx, const TitleLayout& tl) {
        const float w  = GetWidth();
        const float h  = GetHeight();
        const float bw = EffectiveBorderWidth();
        const float th = TitleAreaHeight();

        const bool framed = frameStyle == GroupBoxFrameStyle::Framed;
        const bool flat   = frameStyle == GroupBoxFrameStyle::Flat;

        // Frame rectangle (stroke is centred on the path → inset by half width).
        const float half = bw * 0.5f;
        float frameLeft   = half;
        float frameRight  = w - half;
        float frameTop    = (framed && tl.hasTitle) ? th * 0.5f : half;
        float frameBottom = h - half;
        float frameW = std::max(0.0f, frameRight - frameLeft);
        float frameH = std::max(0.0f, frameBottom - frameTop);
        float r = std::max(0.0f, std::min(visualStyle.cornerRadius,
                                          std::min(frameW, frameH) * 0.5f));

        // ----- Background fill -----
        if (visualStyle.backgroundColor.a > 0 && frameW > 0 && frameH > 0) {
            ctx->SetFillPaint(visualStyle.backgroundColor);
            ctx->FillRoundedRectangle(Rect2Dd(frameLeft, frameTop, frameW, frameH), r);
        }

        // ----- Header strip background (Header style) -----
        if (frameStyle == GroupBoxFrameStyle::Header && tl.hasTitle &&
            visualStyle.headerBackgroundColor.a > 0) {
            ctx->SetFillPaint(visualStyle.headerBackgroundColor);
            ctx->FillRectangle(Rect2Dd(frameLeft + half, frameTop + half,
                                       std::max(0.0f, frameW - bw),
                                       std::max(0.0f, th - bw)));
        }

        // ----- Border -----
        if (bw > 0.0f && frameW > 0 && frameH > 0) {
            ctx->SetStrokePaint(visualStyle.borderColor);
            ctx->SetStrokeWidth(bw);

            bool drawGap = framed && tl.hasTitle &&
                           tl.gapX1 > tl.gapX0 && r >= 0.0f;
            // Keep the gap within the straight part of the top edge.
            float gx0 = std::max(tl.gapX0, frameLeft + r);
            float gx1 = std::min(tl.gapX1, frameRight - r);
            drawGap = drawGap && (gx1 > gx0);

            if (drawGap) {
                // Open rounded-rect path with a gap cut into the top edge for the
                // caption. Walk clockwise from the right side of the gap.
                ctx->ClearPath();
                ctx->MoveTo(gx1, frameTop);
                ctx->LineTo(frameRight - r, frameTop);
                ctx->ArcTo(frameRight, frameTop, frameRight, frameTop + r, r);
                ctx->LineTo(frameRight, frameBottom - r);
                ctx->ArcTo(frameRight, frameBottom, frameRight - r, frameBottom, r);
                ctx->LineTo(frameLeft + r, frameBottom);
                ctx->ArcTo(frameLeft, frameBottom, frameLeft, frameBottom - r, r);
                ctx->LineTo(frameLeft, frameTop + r);
                ctx->ArcTo(frameLeft, frameTop, frameLeft + r, frameTop, r);
                ctx->LineTo(gx0, frameTop);
                ctx->Stroke();
                ctx->ClearPath();
            } else {
                ctx->DrawRoundedRectangle(Rect2Dd(frameLeft, frameTop, frameW, frameH), r);
            }

            // Header separator under the caption strip.
            if (frameStyle == GroupBoxFrameStyle::Header && tl.hasTitle &&
                visualStyle.showHeaderSeparator) {
                ctx->SetStrokePaint(visualStyle.borderColor);
                ctx->SetStrokeWidth(bw);
                ctx->DrawLine(Point2Dd(frameLeft, th), Point2Dd(frameRight, th));
            }
        }

        // Flat style: optional separator below the caption strip.
        if (flat && tl.hasTitle && visualStyle.showHeaderSeparator &&
            visualStyle.borderColor.a > 0) {
            ctx->SetStrokePaint(visualStyle.borderColor);
            ctx->SetStrokeWidth(std::max(1.0f, visualStyle.borderWidth));
            ctx->DrawLine(Point2Dd(0, th), Point2Dd(w, th));
        }
    }

    void UltraCanvasGroupBox::DrawTitle(IRenderContext* ctx, const TitleLayout& tl) {
        if (!tl.hasTitle) return;

        const Color titleColor = (checkable && !checked)
                                 ? visualStyle.disabledTitleColor
                                 : visualStyle.titleColor;

        if (collapsible) {
            DrawDisclosureArrow(ctx, tl.arrowRect);
        }
        if (checkable) {
            DrawCheckIndicator(ctx, tl.checkRect);
        }
        if (!title.empty()) {
            ctx->SetFontStyle(visualStyle.titleFont);
            ctx->SetTextPaint(titleColor);
            ctx->DrawText(title, Point2Dd(tl.textPos.x, tl.textPos.y));
        }
    }

    void UltraCanvasGroupBox::DrawCheckIndicator(IRenderContext* ctx, const Rect2Df& rect) {
        const Color boxColor    = Colors::White;
        const Color borderColor = checked ? visualStyle.titleColor
                                          : Color(150, 150, 150, 255);

        ctx->DrawFilledRectangle(Rect2Dd(rect.x, rect.y, rect.width, rect.height),
                                 boxColor, 1.0f, borderColor, 2.0f);

        if (checked) {
            // Classic two-stroke checkmark inside the box.
            ctx->SetStrokePaint(visualStyle.titleColor);
            ctx->SetStrokeWidth(std::max(1.5f, rect.width * 0.12f));
            ctx->ClearPath();
            ctx->MoveTo(rect.x + rect.width * 0.22f, rect.y + rect.height * 0.52f);
            ctx->LineTo(rect.x + rect.width * 0.42f, rect.y + rect.height * 0.72f);
            ctx->LineTo(rect.x + rect.width * 0.78f, rect.y + rect.height * 0.28f);
            ctx->Stroke();
            ctx->ClearPath();
        }
    }

    void UltraCanvasGroupBox::DrawDisclosureArrow(IRenderContext* ctx, const Rect2Df& rect) {
        ctx->SetFillPaint(visualStyle.titleColor);
        ctx->ClearPath();
        if (collapsed) {
            // Right-pointing triangle ▶
            float x = rect.x + rect.width * 0.30f;
            float yTop = rect.y + rect.height * 0.22f;
            float yBot = rect.y + rect.height * 0.78f;
            float xTip = rect.x + rect.width * 0.74f;
            ctx->MoveTo(x, yTop);
            ctx->LineTo(xTip, rect.y + rect.height * 0.5f);
            ctx->LineTo(x, yBot);
        } else {
            // Down-pointing triangle ▼
            float yTop = rect.y + rect.height * 0.32f;
            float xLeft = rect.x + rect.width * 0.22f;
            float xRight = rect.x + rect.width * 0.78f;
            float yTip = rect.y + rect.height * 0.70f;
            ctx->MoveTo(xLeft, yTop);
            ctx->LineTo(xRight, yTop);
            ctx->LineTo(rect.x + rect.width * 0.5f, yTip);
        }
        ctx->ClosePath();
        ctx->Fill();
        ctx->ClearPath();
    }

// ===== EVENT HANDLING =====
    bool UltraCanvasGroupBox::OnEvent(const UCEvent& event) {
        if (event.type == UCEventType::MouseMove) {
            bool nowHovered = TitleBarRect().Contains(Point2Df(event.pointer.x, event.pointer.y));
            if (nowHovered != titleHovered) {
                titleHovered = nowHovered;
                if (collapsible || checkable) RequestRedraw();
            }
        }

        if (event.type == UCEventType::MouseDown && event.button == UCMouseButton::Left) {
            Point2Df p(event.pointer.x, event.pointer.y);

            // The caption layout depends on measured text, which needs a render
            // context; reuse this element's render context for the hit-test.
            IRenderContext* ctx = GetRenderContext();
            if (ctx) {
                TitleLayout tl = ComputeTitleLayout(ctx);
                if (checkable && tl.checkRect.Contains(p)) {
                    SetChecked(!checked);
                    return true;
                }
            }

            if ((collapsible || checkable) && TitleBarRect().Contains(p)) {
                if (collapsible) {
                    SetCollapsed(!collapsed);
                } else if (checkable) {
                    SetChecked(!checked);
                }
                return true;
            }
        }

        return UltraCanvasContainer::OnEvent(event);
    }

} // namespace UltraCanvas
