// core/UltraCanvasWaveSeparator.cpp
// The S-curve between two groups on one bar. See the header.
//
// The whole element is first filled with the colour of the group after it;
// then the region on the near side of the curve is filled with the colour of
// the group before it. Two fills and one path, so there is never a seam
// between the separator and either neighbour whatever the pixel rounding.
// Version: 1.0.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "UltraCanvasWaveSeparator.h"
#include "UltraCanvasRenderContext.h"

namespace UltraCanvas {

    UltraCanvasWaveSeparator::UltraCanvasWaveSeparator(const std::string& identifier,
                                                       bool isVerticalBar, float len)
        : UltraCanvasUIElement(identifier, 0, 0, 0, 0),
          verticalBar(isVerticalBar),
          length(len > 1.0f ? len : 1.0f),
          beforeColor(Color(74, 74, 74, 255)),
          afterColor(Color(20, 20, 20, 255)) {
        ApplySize();
    }

    void UltraCanvasWaveSeparator::ApplySize() {
        // Fixed along the bar's main axis; the cross axis is the container's
        // to stretch (align-items: stretch is what a bar uses).
        if (verticalBar) {
            size.height = CSSLayout::Dimension::Px(length);
            size.width  = CSSLayout::Dimension::Auto();
        } else {
            size.width  = CSSLayout::Dimension::Px(length);
            size.height = CSSLayout::Dimension::Auto();
        }
        layoutItem.SetFlexShrink(0.0f);
        layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        InvalidateLayout();
    }

    void UltraCanvasWaveSeparator::SetColors(const Color& before, const Color& after) {
        beforeColor = before;
        afterColor  = after;
        RequestRedraw();
    }

    void UltraCanvasWaveSeparator::SetFlipped(bool flip) {
        flipped = flip;
        RequestRedraw();
    }

    void UltraCanvasWaveSeparator::SetVerticalBar(bool vertical) {
        if (verticalBar == vertical) return;
        verticalBar = vertical;
        ApplySize();
        RequestRedraw();
    }

    void UltraCanvasWaveSeparator::SetLength(float px) {
        length = px > 1.0f ? px : 1.0f;
        ApplySize();
        RequestRedraw();
    }

    void UltraCanvasWaveSeparator::Render(IRenderContext* ctx, const Rect2Df& /*dirtyRect*/) {
        const double w = GetWidth();
        const double h = GetHeight();
        if (w <= 0 || h <= 0) return;

        // The far group's colour under everything.
        ctx->SetFillPaint(afterColor);
        ctx->FillRectangle(Rect2Dd(0, 0, w, h));

        // The near group's colour up to the curve. The curve is a cubic whose
        // control points sit halfway along the main axis, on the two edges,
        // which is the classic S; it enters and leaves the neighbours at a
        // tangent, so the transition has no corner at either end.
        ctx->ClearPath();
        if (verticalBar) {
            // Group above. The curve runs from one side edge at the top to the
            // other at the bottom; the region above it is closed along the
            // top edge.
            const double x0 = flipped ? w : 0.0;
            const double x1 = flipped ? 0.0 : w;
            ctx->MoveTo(x0, 0.0);
            ctx->BezierCurveTo(x0, h * 0.5, x1, h * 0.5, x1, h);
            ctx->LineTo(x1, 0.0);
            ctx->ClosePath();
        } else {
            // Group to the left. The curve runs from one edge at the left to
            // the other at the right; the region left of it closes along the
            // left edge.
            const double y0 = flipped ? h : 0.0;
            const double y1 = flipped ? 0.0 : h;
            ctx->MoveTo(0.0, y0);
            ctx->BezierCurveTo(w * 0.5, y0, w * 0.5, y1, w, y1);
            ctx->LineTo(0.0, y1);
            ctx->ClosePath();
        }
        ctx->SetFillPaint(beforeColor);
        ctx->FillPathPreserve();
        ctx->ClearPath();
    }

} // namespace UltraCanvas
