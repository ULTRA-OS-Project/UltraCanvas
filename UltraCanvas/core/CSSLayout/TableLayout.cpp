// core/CSSLayout/TableLayout.cpp
// display: table - HTML's automatic table layout (CSS 2.1 §17.5.2.2, as the
// browsers implement it). The container's in-flow children are the cells,
// placed with explicit grid lines (row / column / row-span / column-span).
//
//   1. Every cell reports a min-content width (its widest unbreakable run, or
//      its explicit px width) and a max-content width (its content on one line).
//   2. A column's min / max is the largest over its single-column cells; a cell
//      spanning several columns widens them only by what they lack, shared out
//      in proportion to their max-content widths.
//   3. A cell with a px width makes its column fixed, one with a % width makes
//      it a percentage column; the others are auto columns.
//   4. The table's width is its own (px / %) or, when auto, its preferred
//      width (the columns' max-content, grown so every % column gets its
//      share) within the available space - never below the columns' minimum.
//   5. That width is handed out: every column gets its minimum, then fixed
//      columns their width, % columns their share and auto columns their
//      max-content, each group in turn and proportionally when there is not
//      enough. What is left widens the auto columns (the % columns, then all,
//      when there are none).
//   6. A row is as tall as its tallest cell at the column width it got; every
//      cell is stretched to its row(s), so its background fills the slot.
//
// The GridLayout gaps are the border-spacing: between the cells and around the
// outer ones, as in CSS. Vertical alignment of a cell's content is the cell's
// own business (a flex-column cell with justify-content does it).
// Version: 1.3.0 - a table's extra height goes to rows without a set height; a
//                 cell's percentage height / min-height / max-height resolves
//                 against the table's set height (the cells' percentHeightBase)
// Version: 1.2.0 - a percentage height resolves against a block parent's set height
// Version: 1.1.0 - max-width caps the table's width
// Last Modified: 2026-10-03
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

            bool isInFlowItem(const Element& e) {
                if (e.layout.display == DisplayType::NoDisplay) return false;
                auto p = e.layoutItem.positionType;
                return p == PositionType::Static || p == PositionType::Relative;
            }

            struct Cell {
                Element* el = nullptr;
                int col = 0, row = 0, colSpan = 1, rowSpan = 1;
                float minW = 0, maxW = 0;
                std::optional<float> fixedW;   // px width (border-box)
                std::optional<float> pctW;     // % width
            };

            struct Column {
                float minW = 0, maxW = 0;
                std::optional<float> fixedW;
                std::optional<float> pctW;
                float width = 0;
                bool IsAuto() const { return !fixedW && !pctW; }
            };

            // Column widths and cell placement, independent of the constraints.
            struct TableIntrinsic {
                std::vector<Cell> cells;
                std::vector<Column> cols;
                int numRows = 0;
                float spacingH = 0, spacingV = 0;
                float minTotal = 0;    // columns' min-content + spacing
                float maxTotal = 0;    // preferred (auto) width + spacing
            };

            struct TableState {
                MeasureConstraints key;
                std::vector<float> colWidths;
                std::vector<float> rowHeights;
                float contentW = 0, contentH = 0;
                float padH = 0, padV = 0, bordH = 0, bordV = 0;
            };

            struct TableComputed : LayoutComputed {
                bool intrinsicReady = false;
                TableIntrinsic intrinsic;
                std::vector<TableState> states;   // a few recent constraint keys
            };

            float borderBoxPx(const Element& e, float contentOrBorderPx, const LayoutContext& ctx) {
                if (e.box.boxSizing == BoxSizing::BorderBox) return contentOrBorderPx;
                auto pad  = resolveEdgeSizes(e.box.padding, 0.f, ctx);
                auto bord = resolveEdgeSizes(e.box.border,  0.f, ctx);
                return contentOrBorderPx + pad.horizontal() + bord.horizontal();
            }

            float MinContentWidthOf(Element& e, const LayoutContext& ctx);

            // Min-content of a table: its columns' minimum plus spacing.
            TableIntrinsic& ObtainIntrinsic(Element& e, const LayoutContext& ctx);

            // The narrowest border-box width `e` can take without its content
            // overflowing: its explicit px width, a leaf's published
            // min-content, or what its children need side by side (a flex row)
            // or one above the other (everything else).
            float MinContentWidthOf(Element& e, const LayoutContext& ctx) {
                if (e.layout.display == DisplayType::NoDisplay) return 0.f;
                if (!e.intrinsic.valid) {
                    e.ComputeIntrinsicSizes(ctx);
                    e.intrinsic.valid = true;
                }
                if (e.size.width.unit == DimensionUnit::Pixels) {
                    return borderBoxPx(e, e.size.width.value, ctx);
                }
                auto pad  = resolveEdgeSizes(e.box.padding, 0.f, ctx);
                auto bord = resolveEdgeSizes(e.box.border,  0.f, ctx);
                const float frame = pad.horizontal() + bord.horizontal();

                float kids = 0.f;
                if (e.layout.display == DisplayType::Table) {
                    kids = ObtainIntrinsic(e, ctx).minTotal;
                } else {
                    bool sideBySide = false;
                    float gap = 0.f;
                    if (e.layout.display == DisplayType::Flex &&
                        std::holds_alternative<FlexLayout>(e.layout.data)) {
                        const auto& fl = std::get<FlexLayout>(e.layout.data);
                        const bool row = fl.direction == FlexDirection::Row ||
                                         fl.direction == FlexDirection::RowReverse;
                        sideBySide = row && fl.wrap == FlexWrap::NoWrap;
                        gap = resolveDimension(fl.gap.column, std::nullopt, ctx).value_or(0.f);
                    }
                    int count = 0;
                    for (auto& k : e.Children()) {
                        if (!k || !isInFlowItem(*k)) continue;
                        float w = MinContentWidthOf(*k, ctx);
                        w += resolveDimension(k->box.margin.left,  std::nullopt, ctx).value_or(0.f)
                           + resolveDimension(k->box.margin.right, std::nullopt, ctx).value_or(0.f);
                        if (sideBySide) kids += w + (count > 0 ? gap : 0.f);
                        else            kids = std::max(kids, w);
                        ++count;
                    }
                }
                // A leaf publishes its own min-content in border-box units.
                float own = e.Children().empty() ? e.intrinsic.minContentWidth : 0.f;
                float result = std::max(own, kids + frame);
                if (e.boxConstraints && e.boxConstraints->minWidth.unit == DimensionUnit::Pixels) {
                    result = std::max(result, borderBoxPx(e, e.boxConstraints->minWidth.value, ctx));
                }
                return result;
            }

            // Hand `amount` out over `cols[s, e)` in proportion to `weight`
            // (evenly when every weight is zero), raising `field`.
            template <typename Weight, typename Field>
            void Spread(std::vector<Column>& cols, int s, int e, float amount,
                        Weight weight, Field field) {
                if (amount <= 0.f || e <= s) return;
                float total = 0.f;
                for (int i = s; i < e; ++i) total += weight(cols[i]);
                for (int i = s; i < e; ++i) {
                    float share = total > 0.f ? amount * weight(cols[i]) / total
                                              : amount / float(e - s);
                    field(cols[i]) += share;
                }
            }

            TableIntrinsic& ObtainIntrinsic(Element& e, const LayoutContext& ctx) {
                auto* tc = dynamic_cast<TableComputed*>(e.layoutComputed.get());
                if (!tc || !tc->valid) {
                    int prevCount = e.layoutComputed ? e.layoutComputed->recomputeCount : 0;
                    auto fresh = std::make_unique<TableComputed>();
                    fresh->valid = true;
                    fresh->recomputeCount = prevCount + 1;
                    tc = fresh.get();
                    e.layoutComputed = std::move(fresh);
                }
                if (tc->intrinsicReady) return tc->intrinsic;

                TableIntrinsic& ti = tc->intrinsic;
                ti = TableIntrinsic{};
                if (std::holds_alternative<GridLayout>(e.layout.data)) {
                    const auto& gl = std::get<GridLayout>(e.layout.data);
                    ti.spacingH = resolveDimension(gl.columnGap, std::nullopt, ctx).value_or(0.f);
                    ti.spacingV = resolveDimension(gl.rowGap,    std::nullopt, ctx).value_or(0.f);
                }

                // Placement: explicit lines; a cell without them follows the
                // previous one on its row.
                int numCols = 0, cursorRow = 0, cursorCol = 0;
                for (auto& k : e.Children()) {
                    if (!k || !isInFlowItem(*k)) continue;
                    Cell cell;
                    cell.el = k.get();
                    GridItem gi;
                    if (std::holds_alternative<GridItem>(k->layoutItem.data))
                        gi = std::get<GridItem>(k->layoutItem.data);
                    cell.row = gi.rowStart.type == GridLineKind::Line
                        ? std::max(0, gi.rowStart.index - 1) : cursorRow;
                    cell.col = gi.columnStart.type == GridLineKind::Line
                        ? std::max(0, gi.columnStart.index - 1) : cursorCol;
                    if (gi.columnEnd.type == GridLineKind::Span) cell.colSpan = std::max(1, gi.columnEnd.index);
                    else if (gi.columnEnd.type == GridLineKind::Line)
                        cell.colSpan = std::max(1, gi.columnEnd.index - 1 - cell.col);
                    if (gi.rowEnd.type == GridLineKind::Span) cell.rowSpan = std::max(1, gi.rowEnd.index);
                    else if (gi.rowEnd.type == GridLineKind::Line)
                        cell.rowSpan = std::max(1, gi.rowEnd.index - 1 - cell.row);
                    cursorRow = cell.row;
                    cursorCol = cell.col + cell.colSpan;
                    numCols = std::max(numCols, cell.col + cell.colSpan);
                    ti.numRows = std::max(ti.numRows, cell.row + cell.rowSpan);

                    // max-content: the cell on one line; min-content: its
                    // narrowest unbreakable width.
                    MeasureConstraints unbounded{
                        { ConstraintMode::Unbounded, INFINITY },
                        { ConstraintMode::Unbounded, INFINITY }
                    };
                    k->Measure(unbounded, ctx);
                    cell.maxW = k->measured.measuredWidth;
                    cell.minW = MinContentWidthOf(*k, ctx);
                    if (k->size.width.unit == DimensionUnit::Pixels) {
                        cell.fixedW = borderBoxPx(*k, k->size.width.value, ctx);
                    } else if (k->size.width.unit == DimensionUnit::Percent) {
                        cell.pctW = k->size.width.value;
                    }
                    cell.maxW = std::max(cell.maxW, cell.minW);
                    ti.cells.push_back(cell);
                }

                ti.cols.assign(numCols, Column{});
                // Single-column cells first.
                for (const auto& c : ti.cells) {
                    if (c.colSpan != 1) continue;
                    Column& col = ti.cols[c.col];
                    col.minW = std::max(col.minW, c.minW);
                    col.maxW = std::max(col.maxW, c.maxW);
                    if (c.fixedW) col.fixedW = std::max(col.fixedW.value_or(0.f), *c.fixedW);
                    if (c.pctW)   col.pctW   = std::max(col.pctW.value_or(0.f),   *c.pctW);
                }
                for (auto& col : ti.cols) {
                    if (col.fixedW) {
                        col.minW = std::max(col.minW, *col.fixedW);
                        col.maxW = std::max(col.minW, *col.fixedW);
                    }
                    col.maxW = std::max(col.maxW, col.minW);
                }
                // Spanning cells, narrowest span first, widen what they lack.
                std::vector<const Cell*> spanning;
                for (const auto& c : ti.cells) if (c.colSpan > 1) spanning.push_back(&c);
                std::stable_sort(spanning.begin(), spanning.end(),
                                 [](const Cell* a, const Cell* b) { return a->colSpan < b->colSpan; });
                for (const Cell* c : spanning) {
                    const int s = c->col, en = c->col + c->colSpan;
                    const float inner = ti.spacingH * float(c->colSpan - 1);
                    float haveMin = inner, haveMax = inner;
                    for (int i = s; i < en; ++i) { haveMin += ti.cols[i].minW; haveMax += ti.cols[i].maxW; }
                    Spread(ti.cols, s, en, c->minW - haveMin,
                           [](const Column& col) { return col.maxW; },
                           [](Column& col) -> float& { return col.minW; });
                    Spread(ti.cols, s, en, c->maxW - haveMax,
                           [](const Column& col) { return col.maxW; },
                           [](Column& col) -> float& { return col.maxW; });
                    for (int i = s; i < en; ++i)
                        ti.cols[i].maxW = std::max(ti.cols[i].maxW, ti.cols[i].minW);
                }

                const float spacing = ti.spacingH * float(numCols + 1);
                float sumMin = 0.f, sumMax = 0.f, sumPct = 0.f, nonPctMax = 0.f;
                float pctDriven = 0.f;
                for (const auto& col : ti.cols) {
                    sumMin += col.minW;
                    sumMax += col.maxW;
                    if (col.pctW && *col.pctW > 0.f) {
                        sumPct += *col.pctW;
                        pctDriven = std::max(pctDriven, col.maxW * 100.f / *col.pctW);
                    } else {
                        nonPctMax += col.maxW;
                    }
                }
                if (sumPct > 0.f && sumPct < 100.f) {
                    pctDriven = std::max(pctDriven, nonPctMax * 100.f / (100.f - sumPct));
                }
                ti.minTotal = numCols > 0 ? sumMin + spacing : 0.f;
                ti.maxTotal = numCols > 0 ? std::max(sumMax, pctDriven) + spacing : 0.f;
                tc->intrinsicReady = true;
                return ti;
            }

            // Hand the content width `avail` (spacing excluded) out over the
            // columns - see step 5 at the top of the file.
            std::vector<float> DistributeWidths(const TableIntrinsic& ti, float avail) {
                const size_t n = ti.cols.size();
                std::vector<float> w(n, 0.f);
                float remaining = avail;
                for (size_t i = 0; i < n; ++i) { w[i] = ti.cols[i].minW; remaining -= w[i]; }
                if (remaining <= 0.f) return w;

                auto grant = [&](auto target) {
                    std::vector<float> want(n, 0.f);
                    float total = 0.f;
                    for (size_t i = 0; i < n; ++i) {
                        want[i] = std::max(0.f, target(i) - w[i]);
                        total += want[i];
                    }
                    if (total <= 0.f || remaining <= 0.f) return;
                    const float scale = std::min(1.f, remaining / total);
                    for (size_t i = 0; i < n; ++i) w[i] += want[i] * scale;
                    remaining -= total * scale;
                };
                grant([&](size_t i) { return ti.cols[i].fixedW.value_or(0.f); });
                grant([&](size_t i) {
                    return ti.cols[i].pctW && !ti.cols[i].fixedW ? *ti.cols[i].pctW * avail / 100.f : 0.f;
                });
                grant([&](size_t i) { return ti.cols[i].IsAuto() ? ti.cols[i].maxW : 0.f; });

                if (remaining <= 0.5f) return w;
                // Widen: the auto columns, else the % columns, else all.
                std::vector<float> weight(n, 0.f);
                float total = 0.f;
                bool anyAuto = false, anyPct = false;
                for (size_t i = 0; i < n; ++i) {
                    anyAuto = anyAuto || ti.cols[i].IsAuto();
                    anyPct  = anyPct  || (ti.cols[i].pctW && !ti.cols[i].fixedW);
                }
                for (size_t i = 0; i < n; ++i) {
                    const Column& c = ti.cols[i];
                    if (anyAuto)     weight[i] = c.IsAuto() ? std::max(c.maxW, 0.f) : 0.f;
                    else if (anyPct) weight[i] = (c.pctW && !c.fixedW) ? *c.pctW : 0.f;
                    else             weight[i] = w[i];
                    total += weight[i];
                }
                if (total <= 0.f) {
                    // Every candidate is empty: share evenly among them.
                    size_t count = 0;
                    for (size_t i = 0; i < n; ++i) {
                        const Column& c = ti.cols[i];
                        bool candidate = anyAuto ? c.IsAuto()
                                       : anyPct  ? (c.pctW && !c.fixedW) : true;
                        weight[i] = candidate ? 1.f : 0.f;
                        count += candidate ? 1 : 0;
                    }
                    total = float(count);
                }
                if (total <= 0.f) return w;
                for (size_t i = 0; i < n; ++i) w[i] += remaining * weight[i] / total;
                return w;
            }

            TableState Compute(Element& e, const MeasureConstraints& c, const LayoutContext& ctx) {
                TableIntrinsic& ti = ObtainIntrinsic(e, ctx);
                TableState s;
                s.key = c;

                std::optional<float> parentInline =
                    c.horizontal.mode == ConstraintMode::Unbounded
                        ? std::nullopt : std::optional<float>{c.horizontal.available};
                std::optional<float> parentBlock =
                    c.vertical.mode == ConstraintMode::Unbounded
                        ? std::nullopt : std::optional<float>{c.vertical.available};
                auto pad  = resolveEdgeSizes(e.box.padding, parentInline.value_or(0.f), ctx);
                auto bord = resolveEdgeSizes(e.box.border,  parentInline.value_or(0.f), ctx);
                s.padH = pad.horizontal();  s.padV = pad.vertical();
                s.bordH = bord.horizontal(); s.bordV = bord.vertical();
                const float frameH = s.padH + s.bordH;

                const bool authoritative = c.horizontal.mode == ConstraintMode::Exact &&
                                           c.vertical.mode   == ConstraintMode::Exact;
                auto specW = resolveDimension(e.size.width, parentInline, ctx);

                // The table's content width (spacing included).
                float tableW;
                if (authoritative) {
                    tableW = std::max(0.f, c.horizontal.available - frameH);
                } else if (specW) {
                    tableW = e.box.boxSizing == BoxSizing::BorderBox
                        ? std::max(0.f, *specW - frameH) : *specW;
                } else {
                    // Auto: shrink to fit the preferred width within what is available.
                    tableW = ti.maxTotal;
                    if (parentInline) tableW = std::min(tableW, std::max(0.f, *parentInline - frameH));
                }
                // max-width caps an explicit or auto width (the used width a
                // parent imposes is already capped).
                if (!authoritative && e.boxConstraints) {
                    if (auto mx = resolveDimension(e.boxConstraints->maxWidth, parentInline, ctx)) {
                        const float cap = e.box.boxSizing == BoxSizing::BorderBox ? *mx - frameH : *mx;
                        tableW = std::min(tableW, std::max(0.f, cap));
                    }
                }
                tableW = std::max(tableW, ti.minTotal);   // a table never crushes its columns
                s.contentW = tableW;

                const size_t n = ti.cols.size();
                const float spacingTotalH = ti.spacingH * float(n + 1);
                s.colWidths = DistributeWidths(ti, std::max(0.f, tableW - spacingTotalH));

                // Row heights at those column widths.
                std::vector<float> colOrigin(n + 1, 0.f);
                for (size_t i = 0; i < n; ++i) colOrigin[i + 1] = colOrigin[i] + s.colWidths[i] + ti.spacingH;
                s.rowHeights.assign(ti.numRows, 0.f);
                auto cellWidth = [&](const Cell& cell) {
                    return std::max(0.f, colOrigin[cell.col + cell.colSpan] - colOrigin[cell.col] - ti.spacingH);
                };
                // The table's own set height: what a cell's percentage height,
                // min-height or max-height is a share of (none: auto, as in CSS).
                std::optional<float> tableH;
                if (auto specH = resolveDimension(e.size.height,
                                                  parentBlock ? parentBlock : e.percentHeightBase, ctx)) {
                    tableH = e.box.boxSizing == BoxSizing::BorderBox
                        ? std::max(0.f, *specH - s.padV - s.bordV) : *specH;
                }
                std::vector<std::pair<const Cell*, float>> tall;
                for (const auto& cell : ti.cells) {
                    if (cell.el->percentHeightBase != tableH) {
                        cell.el->percentHeightBase = tableH;
                        cell.el->measured.valid = false;
                    }
                    MeasureConstraints mc{
                        { ConstraintMode::Exact, cellWidth(cell) },
                        { ConstraintMode::Unbounded, INFINITY }
                    };
                    cell.el->Measure(mc, ctx);
                    const float h = cell.el->measured.measuredHeight;
                    if (cell.rowSpan == 1) s.rowHeights[cell.row] = std::max(s.rowHeights[cell.row], h);
                    else tall.push_back({ &cell, h });
                }
                for (const auto& [cell, h] : tall) {
                    const int rs = cell->row, re = std::min(ti.numRows, cell->row + cell->rowSpan);
                    float have = ti.spacingV * float(re - rs - 1);
                    for (int r = rs; r < re; ++r) have += s.rowHeights[r];
                    if (h > have && re > rs) {
                        const float per = (h - have) / float(re - rs);
                        for (int r = rs; r < re; ++r) s.rowHeights[r] += per;
                    }
                }
                float contentH = ti.numRows > 0 ? ti.spacingV * float(ti.numRows + 1) : 0.f;
                for (float h : s.rowHeights) contentH += h;

                // An explicit (or imposed) height taller than the rows grows them.
                std::optional<float> wantH;
                if (authoritative) {
                    wantH = std::max(0.f, c.vertical.available - s.padV - s.bordV);
                } else if (auto specH = resolveDimension(e.size.height,
                               parentBlock ? parentBlock : e.percentHeightBase, ctx)) {
                    wantH = e.box.boxSizing == BoxSizing::BorderBox
                        ? std::max(0.f, *specH - s.padV - s.bordV) : *specH;
                }
                if (wantH && *wantH > contentH + 0.5f && ti.numRows > 0) {
                    // The extra goes to the rows whose height nothing set (no
                    // cell with a height or min-height of its own), as in
                    // browsers; when every row has one, to all.
                    std::vector<bool> setRow(ti.numRows, false);
                    for (const auto& cell : ti.cells) {
                        if (cell.rowSpan != 1) continue;
                        const bool minSet = cell.el->boxConstraints &&
                                            !cell.el->boxConstraints->minHeight.isAuto();
                        if (!cell.el->size.height.isAuto() || minSet) setRow[cell.row] = true;
                    }
                    int open = 0;
                    for (int r = 0; r < ti.numRows; ++r) if (!setRow[r]) ++open;
                    const float per = (*wantH - contentH) / float(open > 0 ? open : ti.numRows);
                    for (int r = 0; r < ti.numRows; ++r)
                        if (open == 0 || !setRow[r]) s.rowHeights[r] += per;
                    contentH = *wantH;
                }
                s.contentH = wantH ? std::max(contentH, *wantH) : contentH;
                return s;
            }

            TableState& ObtainState(Element& e, const MeasureConstraints& c, const LayoutContext& ctx) {
                ObtainIntrinsic(e, ctx);
                auto* tc = static_cast<TableComputed*>(e.layoutComputed.get());
                for (auto& st : tc->states) {
                    if (st.key == c) return st;
                }
                if (tc->states.size() >= 4) tc->states.erase(tc->states.begin());
                tc->states.push_back(Compute(e, c, ctx));
                tc->key = c;
                return tc->states.back();
            }

        } // namespace

        void MeasureTable(Element& e, const MeasureConstraints& c, const LayoutContext& ctx) {
            TableState& s = ObtainState(e, c, ctx);
            e.measured.measuredWidth  = s.contentW + s.padH + s.bordH;
            e.measured.measuredHeight = s.contentH + s.padV + s.bordV;
        }

        void ArrangeTable(Element& e, const Rect2Df& finalRect, const LayoutContext& ctx) {
            MeasureConstraints exact{
                { ConstraintMode::Exact, finalRect.width  },
                { ConstraintMode::Exact, finalRect.height }
            };
            TableState& s = ObtainState(e, exact, ctx);
            const TableIntrinsic& ti =
                static_cast<TableComputed*>(e.layoutComputed.get())->intrinsic;

            auto pad  = resolveEdgeSizes(e.box.padding, finalRect.width, ctx);
            auto bord = resolveEdgeSizes(e.box.border,  finalRect.width, ctx);
            const float baseX = bord.left + pad.left + ti.spacingH;
            const float baseY = bord.top  + pad.top  + ti.spacingV;

            std::vector<float> colOrigin(s.colWidths.size() + 1, 0.f);
            for (size_t i = 0; i < s.colWidths.size(); ++i)
                colOrigin[i + 1] = colOrigin[i] + s.colWidths[i] + ti.spacingH;
            std::vector<float> rowOrigin(s.rowHeights.size() + 1, 0.f);
            for (size_t i = 0; i < s.rowHeights.size(); ++i)
                rowOrigin[i + 1] = rowOrigin[i] + s.rowHeights[i] + ti.spacingV;

            for (const auto& cell : ti.cells) {
                const int ce = std::min<int>(cell.col + cell.colSpan, (int)s.colWidths.size());
                const int re = std::min<int>(cell.row + cell.rowSpan, (int)s.rowHeights.size());
                const float w = std::max(0.f, colOrigin[ce] - colOrigin[cell.col] - ti.spacingH);
                const float h = std::max(0.f, rowOrigin[re] - rowOrigin[cell.row] - ti.spacingV);
                MeasureConstraints mc{
                    { ConstraintMode::Exact, w },
                    { ConstraintMode::Exact, h }
                };
                cell.el->Measure(mc, ctx);
                Rect2Df r{ baseX + colOrigin[cell.col], baseY + rowOrigin[cell.row], w, h };
                if (cell.el->layoutItem.positionType == PositionType::Relative) {
                    auto [dx, dy] = computeRelativeOffset(*cell.el, w, h, ctx);
                    r.x += dx;
                    r.y += dy;
                }
                cell.el->Arrange(r, ctx);
            }

            Rect2Df paddingBox{
                bord.left, bord.top,
                std::max(0.f, finalRect.width  - bord.horizontal()),
                std::max(0.f, finalRect.height - bord.vertical())
            };
            for (auto& kid : e.Children()) {
                if (!kid) continue;
                auto p = kid->layoutItem.positionType;
                if (p == PositionType::Absolute || p == PositionType::AbsoluteUI) {
                    ArrangePositionedChild(*kid, paddingBox, ctx);
                } else if (p == PositionType::Fixed) {
                    ArrangeFixedChild(*kid, ctx);
                }
            }
        }

    }
}
