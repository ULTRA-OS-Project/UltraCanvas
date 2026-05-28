// core/CSSLayout/FlexLayout.cpp
// CSS Flexbox layout: https://www.w3.org/TR/css-flexbox-1/#layout-algorithm
// Implemented: row/column/reverse, wrap, grow, shrink, basis, gap,
// justify-content, align-items, align-self, align-content (no Baseline).
// Version: 1.2.1
// Last Modified: 2026-05-28
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"
#include "CSSLayout/LayoutUtils.h"
#include "LayoutAlgorithms.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace UltraCanvas {
    namespace CSSLayout {

        namespace {

            // ---- Axis abstraction ----------------------------------------------

            struct Axis {
                bool isRow;            // main axis is horizontal
                bool mainReverse;
                bool wrapReverse;

                float mainOf  (float w, float h) const { return isRow ? w : h; }
                float crossOf (float w, float h) const { return isRow ? h : w; }

                MeasureConstraints constraints(ConstraintMode mainMode, float mainAvail,
                                               ConstraintMode crossMode, float crossAvail) const {
                    AxisConstraint m{mainMode,  mainAvail};
                    AxisConstraint x{crossMode, crossAvail};
                    return isRow ? MeasureConstraints{m, x}
                                 : MeasureConstraints{x, m};
                }
            };

            Axis axisOf(const FlexLayout& fl) {
                Axis a{};
                a.isRow       = (fl.direction == FlexDirection::Row ||
                                 fl.direction == FlexDirection::RowReverse);
                a.mainReverse = (fl.direction == FlexDirection::RowReverse ||
                                 fl.direction == FlexDirection::ColumnReverse);
                a.wrapReverse = (fl.wrap == FlexWrap::WrapReverse);
                return a;
            }

            // ---- Per-item bookkeeping ------------------------------------------

            struct Item {
                Element* el = nullptr;
                FlexItem fi;
                float baseSize = 0;     // flex base size in main axis (border-box)
                float hypoMain = 0;     // base clamped by min/max
                float minMain  = 0;
                float maxMain  = INFINITY;
                float mainSize = 0;     // resolved main extent (border-box)
                float crossSize = 0;    // resolved cross extent (border-box)
                float mainPos = 0;
                float crossPos = 0;
                float marginMainStart = 0, marginMainEnd = 0;
                float marginCrossStart = 0, marginCrossEnd = 0;
                bool  frozen = false;
            };

            FlexItem itemPropsOf(const Element& e) {
                if (std::holds_alternative<FlexItem>(e.layoutItem.data))
                    return std::get<FlexItem>(e.layoutItem.data);
                return FlexItem{};
            }

            // Resolve the four-edge margin of an item, in main/cross terms.
            void resolveItemMargins(Item& it, const Axis& axis,
                                    float containerInline,
                                    const LayoutContext& ctx) {
                auto m = resolveEdgeSizes(it.el->box.margin, containerInline, ctx);
                if (axis.isRow) {
                    it.marginMainStart  = m.left;
                    it.marginMainEnd    = m.right;
                    it.marginCrossStart = m.top;
                    it.marginCrossEnd   = m.bottom;
                } else {
                    it.marginMainStart  = m.top;
                    it.marginMainEnd    = m.bottom;
                    it.marginCrossStart = m.left;
                    it.marginCrossEnd   = m.right;
                }
            }

            // Compute the flex base size in the main axis (border-box).
            //   FlexItem::basis: Auto → use the item's preferred main size
            //                    Px   → that value
            //                    Pct  → % of container's main content extent
            // Returns the border-box main extent.
            float computeBaseSize(Element& el, const FlexItem& fi, const Axis& axis,
                                  std::optional<float> mainContent,
                                  std::optional<float> crossContent,
                                  const LayoutContext& ctx) {
                auto basis = resolveDimension(fi.basis, mainContent, ctx);
                if (basis.has_value()) return *basis;

                // basis: auto → use the item's own size in main axis, if definite;
                // else fall through to intrinsic / max-content measurement.
                const Dimension& mainSizeDim = axis.isRow ? el.size.width : el.size.height;
                auto own = resolveDimension(mainSizeDim, mainContent, ctx);
                if (own.has_value()) return *own;

                // Prefer a published intrinsic.maxContent* (border-box units).
                // Widgets like UltraCanvasLabel publish this via ComputeIntrinsicSizes;
                // for those, we skip the otherwise-redundant Measure(Unbounded) pass.
                if (el.intrinsic.valid) {
                    float ic = axis.isRow ? el.intrinsic.maxContentWidth
                                          : el.intrinsic.maxContentHeight;
                    if (ic > 0) return ic;
                }

                // Fallback: Measure with Unbounded main, cross from container.
                MeasureConstraints mc = axis.constraints(
                    ConstraintMode::Unbounded, INFINITY,
                    crossContent.has_value() ? ConstraintMode::AtMost : ConstraintMode::Unbounded,
                    crossContent.value_or(INFINITY));
                el.Measure(mc, ctx);
                return axis.isRow ? el.measured.measuredWidth
                                  : el.measured.measuredHeight;
            }

            float resolveMinMax(const Dimension& d,
                                std::optional<float> ref,
                                const LayoutContext& ctx,
                                float fallback) {
                auto r = resolveDimension(d, ref, ctx);
                return r.value_or(fallback);
            }

            // ---- Step 5: resolve flexible lengths ------------------------------

            void resolveFlexibleLengths(std::vector<Item*>& line,
                                        float availableMain,
                                        float mainGap) {
                if (line.empty()) return;

                float used = mainGap * (float)(line.size() - 1);
                float sumHypo = 0.f;
                for (auto* it : line) sumHypo += it->hypoMain;

                bool growing = (sumHypo + used) < availableMain;

                // Initialize target sizes = hypothetical.
                for (auto* it : line) {
                    it->mainSize = it->hypoMain;
                    it->frozen = false;
                }

                // Items with grow=0 (or shrink=0 in the shrink case) cannot flex.
                for (auto* it : line) {
                    if (growing  && it->fi.grow   == 0.f) it->frozen = true;
                    if (!growing && it->fi.shrink == 0.f) it->frozen = true;
                }

                // Fixed-point iteration: distribute remaining free space until
                // all items are frozen by min/max clamping or the budget is spent.
                for (int iter = 0; iter < 64; ++iter) {
                    float sumSize = 0.f;
                    for (auto* it : line) sumSize += it->mainSize;
                    float free = availableMain - used - sumSize;
                    if (std::fabs(free) < 0.001f) break;

                    float totalFactor = 0.f;
                    for (auto* it : line) {
                        if (it->frozen) continue;
                        totalFactor += growing ? it->fi.grow
                                               : it->fi.shrink * std::max(1.f, it->baseSize);
                    }
                    if (totalFactor <= 0.f) break;

                    bool anyClamped = false;
                    for (auto* it : line) {
                        if (it->frozen) continue;
                        float factor = growing
                            ? it->fi.grow
                            : it->fi.shrink * std::max(1.f, it->baseSize);
                        float delta = free * (factor / totalFactor);
                        float target = it->mainSize + delta;
                        float clamped = std::clamp(target, it->minMain, it->maxMain);
                        if (clamped != target) {
                            anyClamped = true;
                            it->frozen = true;
                        }
                        it->mainSize = clamped;
                    }
                    if (!anyClamped) break;
                }
            }

            // ---- Cross-axis position from align-items / align-self -------------

            float alignCross(float lineCrossSize, float itemCrossSize,
                             AlignSelf as) {
                switch (as) {
                    case AlignSelf::End:    return lineCrossSize - itemCrossSize;
                    case AlignSelf::Center: return (lineCrossSize - itemCrossSize) * 0.5f;
                    case AlignSelf::Start:
                    case AlignSelf::Stretch:
                    case AlignSelf::Baseline:  // TODO baseline alignment
                    case AlignSelf::Auto:
                    default: return 0.f;
                }
            }

            AlignSelf effectiveAlignSelf(const FlexItem& fi, AlignItems containerAI) {
                if (fi.alignSelf != AlignSelf::Auto) return fi.alignSelf;
                switch (containerAI) {
                    case AlignItems::End:      return AlignSelf::End;
                    case AlignItems::Center:   return AlignSelf::Center;
                    case AlignItems::Baseline: return AlignSelf::Baseline;
                    case AlignItems::Stretch:  return AlignSelf::Stretch;
                    case AlignItems::Start:
                    case AlignItems::FirstBaseline:
                    case AlignItems::LastBaseline:
                    default: return AlignSelf::Start;
                }
            }

            // ---- Main-axis distribution per justify-content --------------------

            struct MainLayout {
                float leadingSpace;
                float between;
            };

            MainLayout distributeMain(JustifyContent jc, float free, int n) {
                MainLayout r{0.f, 0.f};
                if (n <= 0) return r;
                switch (jc) {
                    case JustifyContent::End:
                    case JustifyContent::FlexEnd:
                        r.leadingSpace = free;
                        break;
                    case JustifyContent::Center:
                        r.leadingSpace = free * 0.5f;
                        break;
                    case JustifyContent::SpaceBetween:
                        if (n > 1) r.between = free / (float)(n - 1);
                        else       r.leadingSpace = free * 0.5f;
                        break;
                    case JustifyContent::SpaceAround: {
                        float slot = free / (float)n;
                        r.leadingSpace = slot * 0.5f;
                        r.between = slot;
                        break;
                    }
                    case JustifyContent::SpaceEvenly: {
                        float slot = free / (float)(n + 1);
                        r.leadingSpace = slot;
                        r.between = slot;
                        break;
                    }
                    case JustifyContent::Stretch:
                    case JustifyContent::Start:
                    case JustifyContent::FlexStart:
                    default:
                        break;
                }
                return r;
            }

            // ---- Cross-axis distribution of lines per align-content ------------

            MainLayout distributeCross(AlignContent ac, float free, int n) {
                MainLayout r{0.f, 0.f};
                if (n <= 0) return r;
                switch (ac) {
                    case AlignContent::End:    r.leadingSpace = free; break;
                    case AlignContent::Center: r.leadingSpace = free * 0.5f; break;
                    case AlignContent::SpaceBetween:
                        if (n > 1) r.between = free / (float)(n - 1);
                        else       r.leadingSpace = free * 0.5f;
                        break;
                    case AlignContent::SpaceAround: {
                        float slot = free / (float)n;
                        r.leadingSpace = slot * 0.5f;
                        r.between = slot;
                        break;
                    }
                    case AlignContent::SpaceEvenly: {
                        float slot = free / (float)(n + 1);
                        r.leadingSpace = slot;
                        r.between = slot;
                        break;
                    }
                    case AlignContent::Stretch:
                    case AlignContent::Start:
                    default: break;
                }
                return r;
            }

            // ---- Shared core: build per-item state and per-line groupings ------

            struct Line {
                std::vector<Item*> items;
                float crossSize = 0;       // max item cross size (pre-stretch)
                float crossPos  = 0;       // position along cross axis
                float lineCrossSize = 0;   // final cross size assigned to the line
            };

            struct FlexState {
                FlexLayout fl;
                Axis axis;
                std::vector<Item>      items;   // owns
                std::vector<Line>      lines;
                float availableMain  = 0;      // content-area main extent
                float availableCross = 0;
                bool  mainKnown  = false;
                bool  crossKnown = false;
                float mainGap   = 0;
                float crossGap  = 0;
                float padMain = 0, padCross = 0;
                float bordMain = 0, bordCross = 0;
            };

            // Lay out (measure + group + flex + cross size). Common to MeasureFlex / ArrangeFlex.
            FlexState computeFlex(Element& e,
                                  const MeasureConstraints& c,
                                  const LayoutContext& ctx) {
                FlexState s;
                s.fl   = std::get<FlexLayout>(e.layout.data);
                s.axis = axisOf(s.fl);

                // Padding / border on the container.
                std::optional<float> parentInline =
                    (c.horizontal.mode == ConstraintMode::Unbounded)
                        ? std::nullopt
                        : std::optional<float>{c.horizontal.available};
                auto padIns  = resolveEdgeSizes(e.box.padding, parentInline.value_or(0.f), ctx);
                auto bordIns = resolveEdgeSizes(e.box.border,  parentInline.value_or(0.f), ctx);

                if (s.axis.isRow) {
                    s.padMain   = padIns.horizontal();  s.padCross  = padIns.vertical();
                    s.bordMain  = bordIns.horizontal(); s.bordCross = bordIns.vertical();
                } else {
                    s.padMain   = padIns.vertical();    s.padCross  = padIns.horizontal();
                    s.bordMain  = bordIns.vertical();   s.bordCross = bordIns.horizontal();
                }

                // Container main/cross content extents from constraints (Exact only).
                auto inlineAvail = parentInline;
                auto blockAvail  = (c.vertical.mode == ConstraintMode::Unbounded)
                                    ? std::nullopt
                                    : std::optional<float>{c.vertical.available};
                std::optional<float> mainOuter = s.axis.isRow ? inlineAvail : blockAvail;
                std::optional<float> crossOuter= s.axis.isRow ? blockAvail  : inlineAvail;

                // Honor an explicit size on the container if set (overrides AtMost).
                const Dimension& mainDim  = s.axis.isRow ? e.size.width  : e.size.height;
                const Dimension& crossDim = s.axis.isRow ? e.size.height : e.size.width;
                auto ownMain  = resolveDimension(mainDim,  mainOuter,  ctx);
                auto ownCross = resolveDimension(crossDim, crossOuter, ctx);

                // Treat the main extent as "known" when constraints are Exact OR an
                // explicit size resolves; AtMost is also acceptable as an upper bound
                // for wrapping decisions.
                if (ownMain.has_value()) {
                    s.availableMain = std::max(0.f, *ownMain
                        - (e.box.boxSizing == BoxSizing::BorderBox ? s.padMain + s.bordMain : 0.f));
                    s.mainKnown = true;
                } else if (c.horizontal.mode == ConstraintMode::Exact && s.axis.isRow) {
                    s.availableMain = std::max(0.f, c.horizontal.available - s.padMain - s.bordMain);
                    s.mainKnown = true;
                } else if (c.vertical.mode == ConstraintMode::Exact && !s.axis.isRow) {
                    s.availableMain = std::max(0.f, c.vertical.available - s.padMain - s.bordMain);
                    s.mainKnown = true;
                } else if (mainOuter.has_value()) {
                    s.availableMain = std::max(0.f, *mainOuter - s.padMain - s.bordMain);
                    s.mainKnown = false;  // AtMost - used as wrap budget but not for flex
                } else {
                    s.availableMain = INFINITY;
                    s.mainKnown = false;
                }

                if (ownCross.has_value()) {
                    s.availableCross = std::max(0.f, *ownCross
                        - (e.box.boxSizing == BoxSizing::BorderBox ? s.padCross + s.bordCross : 0.f));
                    s.crossKnown = true;
                } else if (crossOuter.has_value()) {
                    s.availableCross = std::max(0.f, *crossOuter - s.padCross - s.bordCross);
                    s.crossKnown = (s.axis.isRow ? c.vertical.mode    : c.horizontal.mode)
                                    == ConstraintMode::Exact;
                } else {
                    s.availableCross = INFINITY;
                    s.crossKnown = false;
                }

                // Resolve gaps. Per CSS: gap percentages resolve against the relevant
                // axis content extent.
                std::optional<float> mainBasis  = s.mainKnown  ? std::optional<float>{s.availableMain}  : std::nullopt;
                std::optional<float> crossBasis = s.crossKnown ? std::optional<float>{s.availableCross} : std::nullopt;
                std::optional<float> rowBasis   = s.axis.isRow ? crossBasis : mainBasis;
                std::optional<float> colBasis   = s.axis.isRow ? mainBasis  : crossBasis;
                float rowGap = resolveDimension(s.fl.gap.row,    rowBasis, ctx).value_or(0.f);
                float colGap = resolveDimension(s.fl.gap.column, colBasis, ctx).value_or(0.f);
                s.mainGap  = s.axis.isRow ? colGap : rowGap;
                s.crossGap = s.axis.isRow ? rowGap : colGap;

                // Gather in-flow items (skip NoDisplay, abs/fixed).
                std::vector<Element*> kids;
                for (auto& k : e.Children()) {
                    if (!k) continue;
                    if (k->layout.display == DisplayType::NoDisplay) continue;
                    auto p = k->layoutItem.positionType;
                    if (p != PositionType::Static && p != PositionType::Relative) continue;
                    kids.push_back(k.get());
                }

                // Sort by FlexItem::order (stable).
                std::vector<int> idx(kids.size());
                for (size_t i = 0; i < kids.size(); ++i) idx[i] = (int)i;
                std::stable_sort(idx.begin(), idx.end(), [&](int a, int b){
                    return itemPropsOf(*kids[a]).order < itemPropsOf(*kids[b]).order;
                });

                s.items.resize(kids.size());
                for (size_t i = 0; i < idx.size(); ++i) {
                    Item& it = s.items[i];
                    it.el = kids[idx[i]];
                    it.fi = itemPropsOf(*it.el);

                    resolveItemMargins(it, s.axis, inlineAvail.value_or(0.f), ctx);

                    // Resolve min/max main constraints (using container main as basis).
                    if (it.el->constraints.has_value()) {
                        const Dimension& minD = s.axis.isRow ? it.el->constraints->minWidth  : it.el->constraints->minHeight;
                        const Dimension& maxD = s.axis.isRow ? it.el->constraints->maxWidth  : it.el->constraints->maxHeight;
                        it.minMain = resolveMinMax(minD, mainBasis, ctx, 0.f);
                        it.maxMain = resolveMinMax(maxD, mainBasis, ctx, INFINITY);
                    }

                    // Flex base size & hypothetical main size.
                    it.baseSize = computeBaseSize(*it.el, it.fi, s.axis,
                                                  mainBasis, crossBasis, ctx);
                    it.hypoMain = std::clamp(it.baseSize, it.minMain, it.maxMain);
                }

                // Collect into lines.
                Line current;
                float cursor = 0.f;
                for (auto& it : s.items) {
                    float itMain = it.hypoMain + it.marginMainStart + it.marginMainEnd;
                    float gap    = current.items.empty() ? 0.f : s.mainGap;
                    bool fits = (cursor + gap + itMain) <= s.availableMain + 0.001f;
                    bool canWrap = (s.fl.wrap != FlexWrap::NoWrap) && s.mainKnown;
                    if (canWrap && !fits && !current.items.empty()) {
                        s.lines.push_back(std::move(current));
                        current = {};
                        cursor = 0.f;
                        gap = 0.f;
                    }
                    cursor += gap + itMain;
                    current.items.push_back(&it);
                }
                if (!current.items.empty()) s.lines.push_back(std::move(current));

                // For each line: resolve flexible lengths (if main known), then
                // measure each item with main=Exact to determine cross size.
                for (auto& line : s.lines) {
                    if (s.mainKnown) {
                        resolveFlexibleLengths(line.items, s.availableMain, s.mainGap);
                    } else {
                        for (auto* it : line.items) it->mainSize = it->hypoMain;
                    }

                    float lineCross = 0.f;
                    for (auto* it : line.items) {
                        // Measure with main=Exact, cross=Unbounded (or AtMost cross-available).
                        MeasureConstraints mc = s.axis.constraints(
                            ConstraintMode::Exact, std::max(0.f, it->mainSize),
                            s.crossKnown ? ConstraintMode::AtMost : ConstraintMode::Unbounded,
                            s.availableCross);
                        it->el->Measure(mc, ctx);
                        float meas = s.axis.isRow
                            ? it->el->measured.measuredHeight
                            : it->el->measured.measuredWidth;
                        it->crossSize = meas;
                        float withMargin = meas + it->marginCrossStart + it->marginCrossEnd;
                        lineCross = std::max(lineCross, withMargin);
                    }
                    line.crossSize = lineCross;
                }

                return s;
            }

            // ---- Cache slot for FlexState on the owning Element ---------------

            struct FlexComputed : LayoutComputed {
                FlexState state;
            };

            // Look up (or rebuild) the cached FlexState for `e` under
            // constraints `c`. Cache hit returns a reference to the existing
            // state; cache miss runs computeFlex and stores the result.
            FlexState& obtainFlexState(Element& e,
                                       const MeasureConstraints& c,
                                       const LayoutContext& ctx) {
                if (auto* fc = dynamic_cast<FlexComputed*>(e.layoutComputed.get());
                    fc && fc->valid && fc->key == c) {
                    return fc->state;
                }
                int prevCount = e.layoutComputed ? e.layoutComputed->recomputeCount : 0;
                auto fc = std::make_unique<FlexComputed>();
                fc->key   = c;
                fc->valid = true;
                fc->recomputeCount = prevCount + 1;
                fc->state = computeFlex(e, c, ctx);
                e.layoutComputed = std::move(fc);
                return static_cast<FlexComputed&>(*e.layoutComputed).state;
            }

        } // namespace

        // ---- Public entry points ------------------------------------------------

        void MeasureFlex(Element& e,
                         const MeasureConstraints& c,
                         const LayoutContext& ctx) {
            FlexState& s = obtainFlexState(e, c, ctx);

            // Container main extent: known (Exact / explicit) or sum of lines' content.
            float mainContent = s.mainKnown
                ? s.availableMain
                : [&]() {
                    float widest = 0.f;
                    for (const auto& ln : s.lines) {
                        float w = 0.f;
                        for (auto* it : ln.items) {
                            w += it->mainSize + it->marginMainStart + it->marginMainEnd;
                        }
                        if (!ln.items.empty()) w += s.mainGap * (float)(ln.items.size() - 1);
                        widest = std::max(widest, w);
                    }
                    return widest;
                }();

            float crossContent = 0.f;
            for (size_t i = 0; i < s.lines.size(); ++i) {
                crossContent += s.lines[i].crossSize;
                if (i + 1 < s.lines.size()) crossContent += s.crossGap;
            }
            if (s.crossKnown) crossContent = std::max(crossContent, s.availableCross);

            if (s.axis.isRow) {
                e.measured.measuredWidth  = mainContent  + s.padMain  + s.bordMain;
                e.measured.measuredHeight = crossContent + s.padCross + s.bordCross;
            } else {
                e.measured.measuredHeight = mainContent  + s.padMain  + s.bordMain;
                e.measured.measuredWidth  = crossContent + s.padCross + s.bordCross;
            }
        }

        void ArrangeFlex(Element& e,
                         const LayoutRect& finalRect,
                         const LayoutContext& ctx) {
            // The finalRect is now Exact in both axes. If Measure was called
            // with the same constraints the cache hits and we skip the entire
            // computeFlex pass.
            MeasureConstraints exact{
                { ConstraintMode::Exact, finalRect.width },
                { ConstraintMode::Exact, finalRect.height }
            };
            FlexState& s = obtainFlexState(e, exact, ctx);

            // Container content-area size (border-box in finalRect; strip border+padding).
            // Positions are computed in the local content-box frame further down.
            auto padIns  = resolveEdgeSizes(e.box.padding, finalRect.width, ctx);
            auto bordIns = resolveEdgeSizes(e.box.border,  finalRect.width, ctx);
            float contentW = std::max(0.f, finalRect.width  - bordIns.horizontal() - padIns.horizontal());
            float contentH = std::max(0.f, finalRect.height - bordIns.vertical()   - padIns.vertical());
            float availMain  = s.axis.isRow ? contentW : contentH;
            float availCross = s.axis.isRow ? contentH : contentW;

            // Determine each line's final cross size. align-content: stretch
            // distributes leftover cross space equally across lines.
            float totalLineCross = 0.f;
            for (size_t i = 0; i < s.lines.size(); ++i) {
                totalLineCross += s.lines[i].crossSize;
                if (i + 1 < s.lines.size()) totalLineCross += s.crossGap;
            }
            float crossFree = std::max(0.f, availCross - totalLineCross);

            if (s.fl.alignContent == AlignContent::Stretch && !s.lines.empty()) {
                float extra = crossFree / (float)s.lines.size();
                for (auto& ln : s.lines) ln.lineCrossSize = ln.crossSize + extra;
                crossFree = 0.f;
            } else {
                for (auto& ln : s.lines) ln.lineCrossSize = ln.crossSize;
            }

            // Distribute cross free space among lines per align-content.
            MainLayout lineDist = distributeCross(s.fl.alignContent, crossFree, (int)s.lines.size());
            float crossCursor = lineDist.leadingSpace;
            for (auto& ln : s.lines) {
                ln.crossPos = crossCursor;
                crossCursor += ln.lineCrossSize + s.crossGap + lineDist.between;
            }

            // Per-line: main-axis distribution per justify-content, cross-axis per align-self.
            for (auto& ln : s.lines) {
                float lineMain = 0.f;
                for (auto* it : ln.items) lineMain += it->mainSize + it->marginMainStart + it->marginMainEnd;
                if (!ln.items.empty()) lineMain += s.mainGap * (float)(ln.items.size() - 1);
                float free = std::max(0.f, availMain - lineMain);
                MainLayout md = distributeMain(s.fl.justifyContent, free, (int)ln.items.size());

                float mainCursor = md.leadingSpace;
                for (auto* it : ln.items) {
                    mainCursor += it->marginMainStart;
                    it->mainPos = mainCursor;
                    mainCursor += it->mainSize + it->marginMainEnd + s.mainGap + md.between;

                    AlignSelf as = effectiveAlignSelf(it->fi, s.fl.alignItems);
                    float itemCross = it->crossSize;
                    if (as == AlignSelf::Stretch) {
                        float marginsCross = it->marginCrossStart + it->marginCrossEnd;
                        itemCross = std::max(0.f, ln.lineCrossSize - marginsCross);
                        // Re-measure with cross=Exact so descendants lay out correctly.
                        MeasureConstraints mc = s.axis.constraints(
                            ConstraintMode::Exact, it->mainSize,
                            ConstraintMode::Exact, itemCross);
                        it->el->Measure(mc, ctx);
                        it->crossSize = itemCross;
                    }
                    it->crossPos = ln.crossPos + it->marginCrossStart
                                 + alignCross(ln.lineCrossSize - it->marginCrossStart - it->marginCrossEnd,
                                              it->crossSize, as);
                }

                // Reverse main axis if requested.
                if (s.axis.mainReverse) {
                    // Mirror each item's mainPos within the line's extent.
                    float lineExtent = availMain;
                    for (auto* it : ln.items) {
                        it->mainPos = lineExtent - it->mainPos - it->mainSize;
                    }
                }
            }

            // Reverse line stacking if wrapReverse.
            if (s.axis.wrapReverse && !s.lines.empty()) {
                for (auto& ln : s.lines) {
                    ln.crossPos = availCross - ln.crossPos - ln.lineCrossSize;
                }
            }

            // Finally: convert main/cross to x/y and Arrange each item.
            // Child positions are expressed in this container's content-box
            // frame (origin = top-left of content area), i.e. parent-relative,
            // matching the renderer's expectation that finalBounds is offset
            // from the parent's content-box. So we use border+padding as the
            // local base, NOT finalRect.x/y (which would inherit the parent's
            // own position and produce window-absolute coords).
            // position:relative is folded into the rect before Arrange so finalBounds
            // is correct on return; relative does not shift siblings.
            float localBaseX = bordIns.left + padIns.left;
            float localBaseY = bordIns.top  + padIns.top;
            for (auto& ln : s.lines) {
                for (auto* it : ln.items) {
                    float ix, iy, iw, ih;
                    if (s.axis.isRow) {
                        ix = localBaseX + it->mainPos;
                        iy = localBaseY + it->crossPos;
                        iw = it->mainSize;
                        ih = it->crossSize;
                    } else {
                        ix = localBaseX + it->crossPos;
                        iy = localBaseY + it->mainPos;
                        iw = it->crossSize;
                        ih = it->mainSize;
                    }
                    if (it->el->layoutItem.positionType == PositionType::Relative) {
                        auto [dx, dy] = computeRelativeOffset(*it->el, contentW, contentH, ctx);
                        ix += dx;
                        iy += dy;
                    }
                    it->el->Arrange(LayoutRect{ix, iy, iw, ih}, ctx);
                }
            }

            // Out-of-flow children (absolute/fixed) handled with the same CB as block flow.
            LayoutRect paddingBox{
                finalRect.x + bordIns.left,
                finalRect.y + bordIns.top,
                std::max(0.f, finalRect.width  - bordIns.horizontal()),
                std::max(0.f, finalRect.height - bordIns.vertical())
            };
            for (auto& kid : e.Children()) {
                if (!kid) continue;
                auto p = kid->layoutItem.positionType;
                if (p == PositionType::Absolute) {
                    ArrangePositionedChild(*kid, paddingBox, ctx);
                } else if (p == PositionType::Fixed) {
                    LayoutRect viewport{ 0, 0, ctx.viewportWidth, ctx.viewportHeight };
                    ArrangePositionedChild(*kid, viewport, ctx);
                }
            }
        }

    }
}
