// core/CSSLayout/Element.cpp
// Element base: measure-cache wrapper, default block layout, arrange dispatch.
// Version: 1.2.1
// Last Modified: 2026-05-28
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"
#include "CSSLayout/LayoutUtils.h"
#include "LayoutAlgorithms.h"

#include <algorithm>
#include <cmath>

namespace UltraCanvas {
    namespace CSSLayout {

        // -------------------- helpers --------------------

        namespace {

            // True if this child participates in normal in-flow layout.
            bool isInFlow(const Element& e) {
                if (e.layout.display == DisplayType::NoDisplay) return false;
                auto pos = e.layoutItem.positionType;
                return pos == PositionType::Static || pos == PositionType::Relative;
            }

            // Resolve the inline (width) and block (height) extents the element
            // *itself* occupies as a content-box, given its constraints and its
            // own size property. Returns {std::nullopt} on either axis when the
            // size is to be derived from children (auto).
            struct ResolvedOwnSize {
                std::optional<float> contentWidth;
                std::optional<float> contentHeight;
            };

            ResolvedOwnSize resolveOwnContentSize(const Element& e,
                                                  const MeasureConstraints& c,
                                                  const LayoutContext& ctx) {
                ResolvedOwnSize out;

                std::optional<float> parentInline =
                    (c.horizontal.mode == ConstraintMode::Unbounded)
                        ? std::nullopt
                        : std::optional<float>{c.horizontal.available};
                std::optional<float> parentBlock =
                    (c.vertical.mode == ConstraintMode::Unbounded)
                        ? std::nullopt
                        : std::optional<float>{c.vertical.available};

                // Resolve padding+border on each axis against parent inline size
                // (this matches CSS — padding/border percentages always use inline).
                float padH = 0.f, padV = 0.f, bordH = 0.f, bordV = 0.f;
                {
                    auto pad = resolveEdgeSizes(e.box.padding,
                                                parentInline.value_or(0.f), ctx);
                    auto bor = resolveEdgeSizes(e.box.border,
                                                parentInline.value_or(0.f), ctx);
                    padH = pad.horizontal();  padV = pad.vertical();
                    bordH = bor.horizontal(); bordV = bor.vertical();
                }

                // Width
                {
                    auto specW = resolveDimension(e.size.width, parentInline, ctx);
                    if (specW.has_value()) {
                        float cw = (e.box.boxSizing == BoxSizing::BorderBox)
                            ? borderBoxToContent(*specW, padH, bordH)
                            : *specW;
                        cw = clampToConstraints(cw, e.constraints, true, parentInline, ctx);
                        out.contentWidth = std::max(0.f, cw);
                    } else if (c.horizontal.mode == ConstraintMode::Exact) {
                        float bb = c.horizontal.available;
                        float cw = borderBoxToContent(bb, padH, bordH);
                        cw = clampToConstraints(cw, e.constraints, true, parentInline, ctx);
                        out.contentWidth = std::max(0.f, cw);
                    } else {
                        // AtMost / Unbounded → derive from children, but clamp later
                        // to (available - padding/border) if AtMost.
                        out.contentWidth = std::nullopt;
                    }
                }

                // Height
                {
                    auto specH = resolveDimension(e.size.height, parentBlock, ctx);
                    if (specH.has_value()) {
                        float ch = (e.box.boxSizing == BoxSizing::BorderBox)
                            ? borderBoxToContent(*specH, padV, bordV)
                            : *specH;
                        ch = clampToConstraints(ch, e.constraints, false, parentBlock, ctx);
                        out.contentHeight = std::max(0.f, ch);
                    } else if (c.vertical.mode == ConstraintMode::Exact) {
                        float bb = c.vertical.available;
                        float ch = borderBoxToContent(bb, padV, bordV);
                        ch = clampToConstraints(ch, e.constraints, false, parentBlock, ctx);
                        out.contentHeight = std::max(0.f, ch);
                    } else {
                        out.contentHeight = std::nullopt;
                    }
                }

                return out;
            }

        } // namespace

        // -------------------- Element basics --------------------

        void Element::AddChild(std::shared_ptr<Element> kid) {
            if (!kid) return;
            // Re-parenting an element that already has a parent would silently
            // leak the old parent's invariants; require an explicit detach first.
            if (kid->parent && kid->parent != this) {
                kid->parent->RemoveChild(kid.get());
            }
            kid->parent = this;
            children.push_back(std::move(kid));
            InvalidateLayout();
        }

        void Element::RemoveChild(Element* kid) {
            if (!kid) return;
            auto it = std::find_if(children.begin(), children.end(),
                                   [&](const std::shared_ptr<Element>& c) {
                                       return c.get() == kid;
                                   });
            if (it == children.end()) return;
            (*it)->parent = nullptr;
            children.erase(it);
            InvalidateLayout();
        }

        void Element::ClearChildren() {
            for (auto& c : children) if (c) c->parent = nullptr;
            children.clear();
            InvalidateLayout();
        }

        void Element::SortChildren(const ChildComparator& cmp) {
            std::stable_sort(children.begin(), children.end(), cmp);
        }

        void Element::InvalidateLayout() {
            measured.valid  = false;
            intrinsic.valid = false;
            arrangeValid    = false;
            if (layoutComputed) layoutComputed->valid = false;
            if (parent) parent->InvalidateLayout();
        }

        void Element::InvalidateSubtree() {
            measured.valid  = false;
            intrinsic.valid = false;
            arrangeValid    = false;
            if (layoutComputed) layoutComputed->valid = false;
            for (auto& c : children) if (c) c->InvalidateSubtree();
        }

        void Element::Measure(const MeasureConstraints& c, const LayoutContext& ctx) {
            if (measured.valid && measured.key == c) return;
            if (!intrinsic.valid) {
                ComputeIntrinsicSizes(ctx);
                intrinsic.valid = true;
            }
            MeasureCore(c, ctx);
            measured.key = c;
            measured.valid = true;
        }

        void Element::ComputeIntrinsicSizes(const LayoutContext& /*ctx*/) {
            // Base Element has no intrinsic content; leaf subclasses override.
            intrinsic.minContentWidth  = 0;
            intrinsic.maxContentWidth  = 0;
            intrinsic.minContentHeight = 0;
            intrinsic.maxContentHeight = 0;
        }

        void Element::MeasureCore(const MeasureConstraints& c, const LayoutContext& ctx) {
            if (visibility == Visibility::Hidden && layout.display == DisplayType::NoDisplay) {
                measured.measuredWidth = measured.measuredHeight = 0;
                return;
            }
            switch (layout.display) {
                case DisplayType::Flex:   MeasureFlex (*this, c, ctx); return;
                case DisplayType::Grid:   MeasureGrid (*this, c, ctx); return;
                case DisplayType::NoDisplay:
                    measured.measuredWidth = measured.measuredHeight = 0;
                    return;
                case DisplayType::Block:
                case DisplayType::Inline:       // TODO: real inline formatting context
                case DisplayType::InlineBlock:  // TODO: shrink-to-fit subtlety
                default:
                    MeasureBlock(*this, c, ctx);
                    return;
            }
        }

        void Element::Arrange(const Rect2Df& finalRect, const LayoutContext& ctx) {
            finalBounds = finalRect;
            if (layout.display != DisplayType::NoDisplay) {
                switch (layout.display) {
                    case DisplayType::Flex:   ArrangeFlex (*this, finalRect, ctx); break;
                    case DisplayType::Grid:   ArrangeGrid (*this, finalRect, ctx); break;
                    case DisplayType::Block:
                    case DisplayType::Inline:
                    case DisplayType::InlineBlock:
                    default:
                        ArrangeBlock(*this, finalRect, ctx);
                        break;
                }
            }
            // finalBounds and all in-flow / positioned children are now placed.
            // Mark layout valid and run the post-layout hook so subclasses can
            // do their internal, geometry-dependent setup.
            arrangeValid = true;
        }

        // -------------------- Block layout --------------------
        //
        // Default behavior for display: block. Children stack vertically; each
        // receives the parent's content width as an Exact constraint (or AtMost
        // when parent width is itself indefinite) and Unbounded height.
        // Absolute / Fixed children are skipped during in-flow stacking and are
        // laid out separately against this element's border-box.
        // Relative children are placed in-flow then offset post-hoc.

        void MeasureBlock(Element& e,
                          const MeasureConstraints& c,
                          const LayoutContext& ctx) {
            ResolvedOwnSize own = resolveOwnContentSize(e, c, ctx);

            std::optional<float> parentInline =
                (c.horizontal.mode == ConstraintMode::Unbounded)
                    ? std::nullopt
                    : std::optional<float>{c.horizontal.available};

            auto padIns  = resolveEdgeSizes(e.box.padding, parentInline.value_or(0.f), ctx);
            auto bordIns = resolveEdgeSizes(e.box.border,  parentInline.value_or(0.f), ctx);
            float padH  = padIns.horizontal(),  padV  = padIns.vertical();
            float bordH = bordIns.horizontal(), bordV = bordIns.vertical();

            // Content-area inline extent to hand down to in-flow children.
            // - Width known   → Exact child constraint.
            // - Width unknown → AtMost (parent's available - padding/border) when finite,
            //                   otherwise Unbounded.
            AxisConstraint childH;
            if (own.contentWidth.has_value()) {
                childH = { ConstraintMode::Exact, *own.contentWidth };
            } else if (c.horizontal.mode == ConstraintMode::Unbounded) {
                childH = { ConstraintMode::Unbounded, INFINITY };
            } else {
                float avail = std::max(0.f, c.horizontal.available - padH - bordH);
                childH = { ConstraintMode::AtMost, avail };
            }

            // Children get unbounded block-axis space; the parent grows to fit.
            // (If we honored parent's vertical Exact constraint here it would
            // create clipping behavior; CSS handles overflow separately.)
            MeasureConstraints childC{ childH, { ConstraintMode::Unbounded, INFINITY } };

            float stackedHeight = 0.f;
            float maxChildWidth = 0.f;
            for (auto& kid : e.Children()) {
                if (!kid) continue;
                if (!isInFlow(*kid)) continue;
                kid->Measure(childC, ctx);
                stackedHeight += kid->measured.measuredHeight;
                maxChildWidth  = std::max(maxChildWidth, kid->measured.measuredWidth);
            }

            float contentW = own.contentWidth.value_or(maxChildWidth);
            if (c.horizontal.mode == ConstraintMode::AtMost) {
                float maxContent = std::max(0.f, c.horizontal.available - padH - bordH);
                contentW = std::min(contentW, maxContent);
            }
            contentW = clampToConstraints(contentW, e.constraints, true, parentInline, ctx);

            float contentH = own.contentHeight.value_or(stackedHeight);
            std::optional<float> parentBlock =
                (c.vertical.mode == ConstraintMode::Unbounded)
                    ? std::nullopt
                    : std::optional<float>{c.vertical.available};
            contentH = clampToConstraints(contentH, e.constraints, false, parentBlock, ctx);

            e.measured.measuredWidth  = contentW + padH + bordH;
            e.measured.measuredHeight = contentH + padV + bordV;
        }

        void ArrangeBlock(Element& e,
                          const Rect2Df& finalRect,
                          const LayoutContext& ctx) {
            // finalRect is this element's border-box.
            float parentInline = finalRect.width;

            auto padIns  = resolveEdgeSizes(e.box.padding, parentInline, ctx);
            auto bordIns = resolveEdgeSizes(e.box.border,  parentInline, ctx);

            // Child positions are expressed in this element's content-box
            // frame (parent-relative), so the local base is just border+padding;
            // we must NOT add finalRect.x/y (that would inherit the parent's
            // own position and yield window-absolute coords). contentW/H still
            // need the absolute-style size calc because they describe extent.
            float localBaseX = bordIns.left + padIns.left;
            float localBaseY = bordIns.top  + padIns.top;
            float contentW = std::max(0.f,
                finalRect.width  - bordIns.horizontal() - padIns.horizontal());
            float contentH = std::max(0.f,
                finalRect.height - bordIns.vertical()   - padIns.vertical());

            // Stack in-flow children vertically inside the content area.
            // position: relative shifts the rect we hand to Arrange (so finalBounds
            // is correct the moment Arrange returns) but does NOT advance cursorY —
            // siblings ignore the offset.
            float cursorY = localBaseY;
            for (auto& kid : e.Children()) {
                if (!kid) continue;
                if (!isInFlow(*kid)) continue;

                // Re-measure to make the child's width Exact = contentW. The cache
                // hit-rate stays high because the second-pass constraints typically
                // match what the measure pass already used.
                MeasureConstraints kc{
                    { ConstraintMode::Exact, contentW },
                    { ConstraintMode::Unbounded, INFINITY }
                };
                kid->Measure(kc, ctx);

                Rect2Df kr{ localBaseX, cursorY,
                               kid->measured.measuredWidth,
                               kid->measured.measuredHeight };
                if (kid->layoutItem.positionType == PositionType::Relative) {
                    auto [dx, dy] = computeRelativeOffset(*kid, contentW, contentH, ctx);
                    kr.x += dx;
                    kr.y += dy;
                }
                kid->Arrange(kr, ctx);
                cursorY += kid->measured.measuredHeight;
            }

            // Out-of-flow (absolute / fixed) children: lay each one out against
            // the appropriate containing block. The CB for an absolutely positioned
            // child is the *padding-box* of the nearest positioned ancestor (CSS
            // 2.1 §10.1). We pass our own padding-box; correctness depends on the
            // caller using this Element as the abspos CB (position != Static).
            // TODO: proper containing-block walk for nested abspos through non-CB parents.
            Rect2Df paddingBox{
                finalRect.x + bordIns.left,
                finalRect.y + bordIns.top,
                std::max(0.f, finalRect.width  - bordIns.horizontal()),
                std::max(0.f, finalRect.height - bordIns.vertical())
            };
            for (auto& kid : e.Children()) {
                if (!kid) continue;
                auto pos = kid->layoutItem.positionType;
                if (pos == PositionType::Absolute) {
                    ArrangePositionedChild(*kid, paddingBox, ctx);
                } else if (pos == PositionType::Fixed) {
                    Rect2Df viewport{ 0, 0, ctx.viewportWidth, ctx.viewportHeight };
                    ArrangePositionedChild(*kid, viewport, ctx);
                }
            }
        }
    }
}
