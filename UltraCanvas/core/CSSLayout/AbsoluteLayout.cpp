// core/CSSLayout/AbsoluteLayout.cpp
// Out-of-flow positioning: position: absolute / fixed (CSS 2.1 §10.3.7, §10.6.4).
// Position: relative offsets are applied post-layout (§9.4.3).
// Version: 1.1.1
// Last Modified: 2026-05-28
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"
#include "CSSLayout/LayoutUtils.h"
#include "LayoutAlgorithms.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {
    namespace CSSLayout {

        namespace {

            // Resolve an axis of an absolutely positioned box against its containing
            // block (size = cb_size). Implements the constraint:
            //
            //   start + margin_s + border_s + padding_s + width
            //         + padding_e + border_e + margin_e + end = cb_size
            //
            // Any of {start, end, size, margin_s, margin_e} may be Auto.
            //
            // Returns the in-cb position (offset from cb origin) and the border-box
            // extent of the element on this axis.
            struct AxisResult {
                float position = 0;     // offset within CB (i.e. CB-relative)
                float borderBox = 0;    // border-box extent on this axis
            };

            AxisResult solveAbsoluteAxis(float cb_size,
                                         const Dimension& start_d,
                                         const Dimension& end_d,
                                         const Dimension& size_d,
                                         BoxSizing boxSizing,
                                         const Dimension& margin_s_d,
                                         const Dimension& margin_e_d,
                                         float padding_total,
                                         float border_total,
                                         float intrinsic_borderbox_size,
                                         float static_pos,
                                         const LayoutContext& ctx) {
                std::optional<float> cb = cb_size;

                auto start  = resolveDimension(start_d,    cb, ctx);
                auto end    = resolveDimension(end_d,      cb, ctx);
                auto size   = resolveDimension(size_d,     cb, ctx);     // content-box value
                auto ms     = resolveDimension(margin_s_d, cb, ctx);
                auto me     = resolveDimension(margin_e_d, cb, ctx);

                // Convert specified content-box size to border-box if needed.
                std::optional<float> bb_size;
                if (size.has_value()) {
                    bb_size = (boxSizing == BoxSizing::BorderBox)
                        ? *size
                        : *size + padding_total + border_total;
                }

                const bool start_auto = !start.has_value();
                const bool end_auto   = !end.has_value();
                const bool size_auto  = !bb_size.has_value();

                if (start_auto && end_auto && size_auto) {
                    // Static position; size collapses to intrinsic (here: shrink-to-fit).
                    AxisResult r;
                    r.borderBox = intrinsic_borderbox_size;
                    r.position  = static_pos + ms.value_or(0.f);
                    return r;
                }

                // Section 10.3.7 - rules collapsing two autos
                if (start_auto && size_auto) {
                    // 'start' auto, 'size' auto, 'end' resolved → element keeps intrinsic
                    // size, position derived from end.
                    AxisResult r;
                    r.borderBox = intrinsic_borderbox_size;
                    r.position  = cb_size - *end - me.value_or(0.f)
                                - r.borderBox - ms.value_or(0.f);
                    return r;
                }
                if (end_auto && size_auto) {
                    AxisResult r;
                    r.borderBox = intrinsic_borderbox_size;
                    r.position  = start.value_or(0.f) + ms.value_or(0.f);
                    return r;
                }
                if (start_auto && end_auto) {
                    // size resolved; position is static position.
                    AxisResult r;
                    r.borderBox = *bb_size;
                    r.position  = static_pos + ms.value_or(0.f);
                    return r;
                }

                // One auto among {start, end, size}, or none.
                if (size_auto) {
                    // start + end both resolved, size auto → stretch to fill.
                    AxisResult r;
                    float used_ms = ms.value_or(0.f);
                    float used_me = me.value_or(0.f);
                    r.borderBox = std::max(0.f,
                        cb_size - *start - *end - used_ms - used_me);
                    r.position  = *start + used_ms;
                    return r;
                }
                if (start_auto) {
                    AxisResult r;
                    r.borderBox = *bb_size;
                    float used_me = me.value_or(0.f);
                    r.position  = cb_size - *end - used_me - r.borderBox;
                    return r;
                }
                if (end_auto) {
                    AxisResult r;
                    r.borderBox = *bb_size;
                    r.position  = *start + ms.value_or(0.f);
                    return r;
                }

                // Over-constrained: all four resolved. Distribute slack to auto margins,
                // else (per CSS) ignore 'end' in LTR.
                AxisResult r;
                r.borderBox = *bb_size;
                float slack = cb_size - *start - *end - r.borderBox;
                bool ms_auto = (margin_s_d.unit == DimensionUnit::Auto);
                bool me_auto = (margin_e_d.unit == DimensionUnit::Auto);
                float used_ms, used_me;
                if (ms_auto && me_auto) {
                    used_ms = slack / 2.f;
                    used_me = slack - used_ms;
                } else if (ms_auto) {
                    used_ms = slack - me.value_or(0.f);
                    used_me = me.value_or(0.f);
                } else if (me_auto) {
                    used_ms = ms.value_or(0.f);
                    used_me = slack - used_ms;
                } else {
                    // Over-constrained: drop 'end' (LTR/TB).
                    used_ms = ms.value_or(0.f);
                    used_me = me.value_or(0.f);
                }
                r.position = *start + used_ms;
                (void)used_me;
                return r;
            }

        } // namespace

        void ArrangePositionedChild(Element& child,
                                    const Rect2Df& containingBlock,
                                    const LayoutContext& ctx) {
            // containingBlock is the padding-box rect of the CB (passed by caller).
            // First, measure the child with unbounded constraints to get its
            // intrinsic preferred size (used when sides/size combine to auto).
            MeasureConstraints unconstrained{
                { ConstraintMode::AtMost, containingBlock.width },
                { ConstraintMode::AtMost, containingBlock.height }
            };
            child.Measure(unconstrained, ctx);
            float intrinsicW = child.measured.measuredWidth;
            float intrinsicH = child.measured.measuredHeight;

            // Padding + border totals (resolved against CB inline size).
            auto pad  = resolveEdgeSizes(child.box.padding, containingBlock.width, ctx);
            auto bord = resolveEdgeSizes(child.box.border,  containingBlock.width, ctx);

            Position insets = child.layoutItem.position.value_or(Position{});

            AxisResult xr = solveAbsoluteAxis(
                containingBlock.width,
                insets.left, insets.right, child.size.width,
                child.box.boxSizing,
                child.box.margin.left, child.box.margin.right,
                pad.horizontal(), bord.horizontal(),
                intrinsicW,
                0.f /* static_pos: TODO - track real static-position from in-flow pass */,
                ctx);

            AxisResult yr = solveAbsoluteAxis(
                containingBlock.height,
                insets.top, insets.bottom, child.size.height,
                child.box.boxSizing,
                child.box.margin.top, child.box.margin.bottom,
                pad.vertical(), bord.vertical(),
                intrinsicH,
                0.f /* static_pos */,
                ctx);

            // Re-measure with the exact resolved border-box so the child lays out
            // its own children at the right size.
            MeasureConstraints exact{
                { ConstraintMode::Exact, xr.borderBox },
                { ConstraintMode::Exact, yr.borderBox }
            };
            child.Measure(exact, ctx);

            // finalBounds is parent-relative (offset within the DOM parent's
            // content-box), per the renderer's expectation. xr.position is the
            // offset within the containing block. This is correct only while
            // every caller passes its own padding-box as `containingBlock` —
            // i.e. CB == DOM parent. Cross-ancestor CBs (per CSS 2.1 §10.1)
            // will need a separate arrange pass against the real CB; see TODO
            // in ArrangeBlock for the containing-block walk.
            Rect2Df r{ xr.position,
                          yr.position,
                          xr.borderBox,
                          yr.borderBox };
            child.Arrange(r, ctx);
        }

        std::pair<float, float> computeRelativeOffset(const Element& child,
                                                      float cbInline,
                                                      float cbBlock,
                                                      const LayoutContext& ctx) {
            if (!child.layoutItem.position.has_value()) return {0.f, 0.f};
            const Position& offs = *child.layoutItem.position;

            auto left   = resolveDimension(offs.left,   cbInline, ctx);
            auto right  = resolveDimension(offs.right,  cbInline, ctx);
            auto top    = resolveDimension(offs.top,    cbBlock,  ctx);
            auto bottom = resolveDimension(offs.bottom, cbBlock,  ctx);

            float dx = 0.f, dy = 0.f;
            if (left.has_value())        dx =  *left;
            else if (right.has_value())  dx = -*right;
            if (top.has_value())         dy =  *top;
            else if (bottom.has_value()) dy = -*bottom;
            return {dx, dy};
        }

    }
}
