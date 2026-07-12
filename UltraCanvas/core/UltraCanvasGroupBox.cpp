// core/UltraCanvasGroupBox.cpp
// Titled container ("group box" / "fieldset"). See UltraCanvasGroupBox.h.
// The frame, caption and indicators are drawn in element-local coordinates
// (the render context is already translated to this element's origin). The
// border line, padding and caption strip are kept in sync through
// RecalculatePadding() so the inherited container lays the children out
// inside the visible frame, below the caption.
// Version: 1.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework
#include "UltraCanvasGroupBox.h"
#include "UltraCanvasTooltipManager.h"
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

// ===== ACTIVATOR =====
    void UltraCanvasGroupBox::SetCheckable(bool value) {
        if (checkable == value) return;
        checkable = value;
        ApplyCheckStateToChildren();
        RecalculatePadding();
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetChecked(bool value) {
        if (checked == value) return;
        checked = value;
        ApplyCheckStateToChildren();
        if (onCheckedChanged) onCheckedChanged(checked);
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetActivatorStyle(GroupBoxActivatorStyle style) {
        if (activatorStyle == style) return;
        activatorStyle = style;
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetActivatorSide(GroupBoxIndicatorSide side) {
        if (activatorSide == side) return;
        activatorSide = side;
        RequestRedraw();
    }

    void UltraCanvasGroupBox::EnableActivatorSwitch(GroupBoxIndicatorSide side) {
        activatorStyle = GroupBoxActivatorStyle::Switch;
        activatorSide = side;
        SetCheckable(true);
    }

    void UltraCanvasGroupBox::ApplyCheckStateToChildren() {
        // Only a checkable box gates its children; otherwise leave them alone.
        bool disable = checkable && !checked;
        for (auto& c : Children()) {
            asUI(c)->SetDisabled(disable);
        }
    }

// ===== INFO ICON / HELP TEXT =====
    void UltraCanvasGroupBox::SetInfoIcon(bool show) {
        if (showInfoIcon == show) return;
        showInfoIcon = show;
        RecalculatePadding();
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetInfoIconSide(GroupBoxIndicatorSide side) {
        if (infoIconSide == side) return;
        infoIconSide = side;
        RequestRedraw();
    }

    void UltraCanvasGroupBox::SetHelpText(const std::string& text) {
        helpText = text;
        SetInfoIcon(!text.empty());
        RequestRedraw();
    }

// ===== COLLAPSIBLE =====
    void UltraCanvasGroupBox::SetCollapsible(bool value) {
        if (collapsible == value) return;
        collapsible = value;
        if (!collapsible && collapsed) {
            SetCollapsed(false);
        }
        RecalculatePadding();
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

    bool UltraCanvasGroupBox::HasTitleContent() const {
        return !title.empty() || checkable || collapsible || showInfoIcon;
    }

    float UltraCanvasGroupBox::TitleAreaHeight() const {
        if (!HasTitleContent()) {
            return EffectiveBorderWidth();
        }
        // Approximate text height from the font size (avoids needing a render
        // context here); padding is added above and below. The strip must also
        // fit a switch-style activator if one is in use.
        float textHeight = std::ceil(visualStyle.titleFont.fontSize * 1.35f);
        float h = textHeight + 2.0f * visualStyle.titleVerticalPadding;
        if (checkable && activatorStyle == GroupBoxActivatorStyle::Switch) {
            h = std::max(h, ActivatorSize().height + 2.0f * visualStyle.titleVerticalPadding);
        }
        return std::max(h, EffectiveBorderWidth() + 2.0f);
    }

    Rect2Df UltraCanvasGroupBox::TitleBarRect() const {
        return Rect2Df(0.0f, 0.0f, GetWidth(), TitleAreaHeight());
    }

    float UltraCanvasGroupBox::IndicatorSize() const {
        return std::max(12.0f, std::round(static_cast<float>(visualStyle.titleFont.fontSize)));
    }

    Size2Df UltraCanvasGroupBox::ActivatorSize() const {
        float s = IndicatorSize();
        if (activatorStyle == GroupBoxActivatorStyle::Switch) {
            float hgt = std::round(s * 0.95f);
            return Size2Df(std::round(hgt * 1.8f), hgt);
        }
        return Size2Df(s, s);
    }

    void UltraCanvasGroupBox::RecalculatePadding() {
        const float bw  = EffectiveBorderWidth();
        const float cp  = visualStyle.contentPadding;
        const float top = HasTitleContent() ? (TitleAreaHeight() + cp) : (bw + cp);

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
        tl.hasContent = HasTitleContent();
        if (!tl.hasContent) return tl;

        const float bw = EffectiveBorderWidth();
        const float th = TitleAreaHeight();
        const float centerY = th * 0.5f;
        const float spacing = 6.0f;
        const float indSize = IndicatorSize();
        const Size2Df actSize = ActivatorSize();
        const float w = GetWidth();

        const float leftEdge  = bw + visualStyle.titleIndent;
        const float rightEdge = w - bw - visualStyle.titleIndent;

        auto vcenter = [&](float h) { return centerY - h * 0.5f; };

        // ----- Left cluster (disclosure arrow, then a left-side activator/info) -----
        float lx = leftEdge;
        if (collapsible) {
            tl.hasArrow = true;
            tl.arrowRect = Rect2Df(lx, vcenter(indSize), indSize, indSize);
            lx += indSize + spacing;
        }
        if (checkable && activatorSide == GroupBoxIndicatorSide::Left) {
            tl.hasActivator = true;
            tl.activatorRect = Rect2Df(lx, vcenter(actSize.height), actSize.width, actSize.height);
            lx += actSize.width + spacing;
        }
        if (showInfoIcon && infoIconSide == GroupBoxIndicatorSide::Left) {
            tl.hasInfo = true;
            tl.infoRect = Rect2Df(lx, vcenter(indSize), indSize, indSize);
            lx += indSize + spacing;
        }

        // ----- Right cluster (right-side activator and/or info icon) -----
        float rx = rightEdge;
        if (checkable && activatorSide == GroupBoxIndicatorSide::Right) {
            rx -= actSize.width;
            tl.hasActivator = true;
            tl.activatorRect = Rect2Df(rx, vcenter(actSize.height), actSize.width, actSize.height);
            rx -= spacing;
        }
        if (showInfoIcon && infoIconSide == GroupBoxIndicatorSide::Right) {
            rx -= indSize;
            tl.hasInfo = true;
            tl.infoRect = Rect2Df(rx, vcenter(indSize), indSize, indSize);
            rx -= spacing;
        }

        // ----- Caption text between the two clusters -----
        Size2Di textSize{0, 0};
        if (!title.empty()) {
            ctx->SetFontStyle(visualStyle.titleFont);
            textSize = ctx->GetTextLineDimensions(title);
        }
        tl.textSize = textSize;

        const float gap = visualStyle.titleGap;
        float regionStart = lx;
        float regionEnd   = rx;
        float avail = std::max(0.0f, regionEnd - regionStart);
        float textW = std::min(static_cast<float>(textSize.width), avail);

        float textX;
        switch (titleAlignment) {
            case GroupBoxTitleAlignment::Center:
                textX = regionStart + (avail - textW) * 0.5f;
                break;
            case GroupBoxTitleAlignment::Right:
                textX = regionEnd - textW - gap;
                break;
            case GroupBoxTitleAlignment::Left:
            default:
                textX = regionStart + (lx > leftEdge ? 0.0f : gap);
                break;
        }
        textX = std::max(textX, regionStart);
        tl.textPos = Point2Df(textX, centerY - textSize.height * 0.5f);

        // ----- Border gaps (Framed): one per on-border item, then merged -----
        if (frameStyle == GroupBoxFrameStyle::Framed) {
            auto addGap = [&](float x0, float x1) {
                if (x1 > x0) tl.borderGaps.emplace_back(x0 - gap, x1 + gap);
            };
            if (tl.hasArrow)     addGap(tl.arrowRect.x, tl.arrowRect.Right());
            if (tl.hasActivator) addGap(tl.activatorRect.x, tl.activatorRect.Right());
            if (tl.hasInfo)      addGap(tl.infoRect.x, tl.infoRect.Right());
            if (textW > 0)       addGap(textX, textX + textW);
        }
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
        float frameTop    = (framed && tl.hasContent) ? th * 0.5f : half;
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
        if (frameStyle == GroupBoxFrameStyle::Header && tl.hasContent &&
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

            // Rounded corners as individual arc strokes (so the top edge can be
            // broken into several segments for the caption gap(s)).
            auto strokeCorner = [&](Point2Dd a, Point2Dd corner, Point2Dd b) {
                ctx->ClearPath();
                ctx->MoveTo(a.x, a.y);
                ctx->ArcTo(corner.x, corner.y, b.x, b.y, r);
                ctx->Stroke();
                ctx->ClearPath();
            };
            if (r > 0.0f) {
                strokeCorner({frameLeft, frameTop + r}, {frameLeft, frameTop}, {frameLeft + r, frameTop});           // TL
                strokeCorner({frameRight - r, frameTop}, {frameRight, frameTop}, {frameRight, frameTop + r});         // TR
                strokeCorner({frameRight, frameBottom - r}, {frameRight, frameBottom}, {frameRight - r, frameBottom}); // BR
                strokeCorner({frameLeft + r, frameBottom}, {frameLeft, frameBottom}, {frameLeft, frameBottom - r});    // BL
            }

            // Left / right / bottom straight edges.
            ctx->DrawLine(Point2Dd(frameLeft, frameTop + r),  Point2Dd(frameLeft, frameBottom - r));
            ctx->DrawLine(Point2Dd(frameRight, frameTop + r), Point2Dd(frameRight, frameBottom - r));
            ctx->DrawLine(Point2Dd(frameLeft + r, frameBottom), Point2Dd(frameRight - r, frameBottom));

            // Top edge, skipping the caption gap intervals.
            const float segStart = frameLeft + r;
            const float segEnd   = frameRight - r;
            std::vector<std::pair<float, float>> gaps;
            for (auto g : tl.borderGaps) {
                float x0 = std::max(g.first, segStart);
                float x1 = std::min(g.second, segEnd);
                if (x1 > x0) gaps.emplace_back(x0, x1);
            }
            std::sort(gaps.begin(), gaps.end());
            // Merge overlapping intervals.
            std::vector<std::pair<float, float>> merged;
            for (auto& g : gaps) {
                if (!merged.empty() && g.first <= merged.back().second) {
                    merged.back().second = std::max(merged.back().second, g.second);
                } else {
                    merged.push_back(g);
                }
            }
            float cursor = segStart;
            for (auto& g : merged) {
                if (g.first > cursor) {
                    ctx->DrawLine(Point2Dd(cursor, frameTop), Point2Dd(g.first, frameTop));
                }
                cursor = std::max(cursor, g.second);
            }
            if (cursor < segEnd) {
                ctx->DrawLine(Point2Dd(cursor, frameTop), Point2Dd(segEnd, frameTop));
            }

            // Header separator under the caption strip.
            if (frameStyle == GroupBoxFrameStyle::Header && tl.hasContent &&
                visualStyle.showHeaderSeparator) {
                ctx->SetStrokePaint(visualStyle.borderColor);
                ctx->SetStrokeWidth(bw);
                ctx->DrawLine(Point2Dd(frameLeft, th), Point2Dd(frameRight, th));
            }
        }

        // Flat style: optional separator below the caption strip.
        if (flat && tl.hasContent && visualStyle.showHeaderSeparator &&
            visualStyle.borderColor.a > 0) {
            ctx->SetStrokePaint(visualStyle.borderColor);
            ctx->SetStrokeWidth(std::max(1.0f, visualStyle.borderWidth));
            ctx->DrawLine(Point2Dd(0, th), Point2Dd(w, th));
        }
    }

    void UltraCanvasGroupBox::DrawTitle(IRenderContext* ctx, const TitleLayout& tl) {
        if (!tl.hasContent) return;

        const Color titleColor = (checkable && !checked)
                                 ? visualStyle.disabledTitleColor
                                 : visualStyle.titleColor;

        if (tl.hasArrow) {
            DrawDisclosureArrow(ctx, tl.arrowRect);
        }
        if (tl.hasActivator) {
            if (activatorStyle == GroupBoxActivatorStyle::Switch) {
                DrawSwitchIndicator(ctx, tl.activatorRect);
            } else {
                DrawCheckIndicator(ctx, tl.activatorRect);
            }
        }
        if (tl.hasInfo) {
            DrawInfoIcon(ctx, tl.infoRect);
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

    void UltraCanvasGroupBox::DrawSwitchIndicator(IRenderContext* ctx, const Rect2Df& rect) {
        // Pill-shaped track with a circular knob that slides on/off.
        const float radius = rect.height * 0.5f;
        const Color trackColor = checked ? visualStyle.activatorOnColor
                                         : visualStyle.activatorOffColor;
        ctx->SetFillPaint(trackColor);
        ctx->FillRoundedRectangle(Rect2Dd(rect.x, rect.y, rect.width, rect.height), radius);

        const float knobR = radius - 2.0f;
        const float knobY = rect.y + radius;
        const float knobX = checked ? (rect.Right() - radius) : (rect.x + radius);
        ctx->DrawFilledCircle(Point2Dd(knobX, knobY), knobR,
                              visualStyle.activatorKnobColor,
                              Color(0, 0, 0, 30), 1.0f);
    }

    void UltraCanvasGroupBox::DrawInfoIcon(IRenderContext* ctx, const Rect2Df& rect) {
        const Color color = infoIconHovered ? visualStyle.infoIconHoverColor
                                            : visualStyle.infoIconColor;
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        const float radius = rect.width * 0.5f - 0.5f;

        // Outlined circle.
        ctx->DrawFilledCircle(Point2Dd(cx, cy), radius, Colors::Transparent, color, 1.5f);

        // Lowercase "i": a dot and a stem.
        ctx->SetFillPaint(color);
        float dotR = std::max(1.0f, rect.width * 0.07f);
        ctx->FillCircle(Point2Dd(cx, cy - radius * 0.42f), dotR);
        float stemW = std::max(1.5f, rect.width * 0.12f);
        ctx->FillRectangle(Rect2Dd(cx - stemW * 0.5f, cy - radius * 0.12f,
                                   stemW, radius * 0.78f));
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

// ===== HELP TOOLTIP =====
    void UltraCanvasGroupBox::ShowHelpTooltip() {
        if (helpText.empty() || !GetWindow()) return;
        IRenderContext* ctx = GetRenderContext();
        if (!ctx) return;
        TitleLayout tl = ComputeTitleLayout(ctx);
        if (!tl.hasInfo) return;

        Point2Df winPos = GetPositionInWindow();
        Point2Di at(static_cast<int>(winPos.x + tl.infoRect.x),
                    static_cast<int>(winPos.y + tl.infoRect.Bottom()));
        UltraCanvasTooltipManager::UpdateAndShowTooltip(GetWindow(), helpText, at);
    }

    void UltraCanvasGroupBox::HideHelpTooltip() {
        UltraCanvasTooltipManager::HideTooltip();
    }

// ===== EVENT HANDLING =====
    bool UltraCanvasGroupBox::OnEvent(const UCEvent& event) {
        if (event.type == UCEventType::MouseLeave) {
            if (infoIconHovered) {
                infoIconHovered = false;
                HideHelpTooltip();
                RequestRedraw();
            }
        }

        if (event.type == UCEventType::MouseMove) {
            Point2Df p(event.pointer.x, event.pointer.y);

            bool nowHovered = TitleBarRect().Contains(p);
            if (nowHovered != titleHovered) {
                titleHovered = nowHovered;
                if (collapsible || checkable) RequestRedraw();
            }

            // Info-icon hover → reveal/hide the help tooltip.
            if (showInfoIcon) {
                IRenderContext* ctx = GetRenderContext();
                bool overInfo = false;
                if (ctx) {
                    TitleLayout tl = ComputeTitleLayout(ctx);
                    overInfo = tl.hasInfo && tl.infoRect.Contains(p);
                }
                if (overInfo != infoIconHovered) {
                    infoIconHovered = overInfo;
                    if (overInfo) ShowHelpTooltip();
                    else          HideHelpTooltip();
                    RequestRedraw();
                }
            }
        }

        // A rapid second click arrives as a double-click instead of a
        // MouseDown; the disclosure arrow must toggle on every click.
        if ((event.type == UCEventType::MouseDown ||
             event.type == UCEventType::MouseDoubleClick) &&
            event.button == UCMouseButton::Left) {
            Point2Df p(event.pointer.x, event.pointer.y);

            // The caption layout depends on measured text, which needs a render
            // context; reuse this element's render context for the hit-test.
            IRenderContext* ctx = GetRenderContext();
            if (ctx) {
                TitleLayout tl = ComputeTitleLayout(ctx);
                if (tl.hasActivator && tl.activatorRect.Contains(p)) {
                    SetChecked(!checked);
                    return true;
                }
                if (tl.hasInfo && tl.infoRect.Contains(p)) {
                    ShowHelpTooltip();
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
