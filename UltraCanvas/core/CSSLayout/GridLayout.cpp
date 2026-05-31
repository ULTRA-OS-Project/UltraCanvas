// core/CSSLayout/GridLayout.cpp
// CSS Grid layout (subset of CSS Grid Layout Level 1/2).
// Implemented: explicit grid placement, auto-placement (row/column flow,
// non-dense), implicit auto tracks, track sizing for Fixed/Percent/Fr/Auto/
// MinContent/MaxContent/FitContent, gaps, justify-self / align-self.
// Deferred (TODO): named lines, named areas, dense packing, subgrid, MinMax
// proper resolution (currently approximated as Min..Max bounds).
// Version: 1.3.1
// Last Modified: 2026-05-31
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

            struct Track {
                GridTrackSize spec;
                float base = 0;   // resolved size in px
                bool  hasFr = false;
                float frFactor = 0;
                float minPx = 0;  // for intrinsic resolution
                float maxPx = INFINITY;
                bool  intrinsic = false;  // grows with item contributions
            };

            struct Placement {
                Element* el = nullptr;
                GridItem gi;
                int colStart = 0, colEnd = 0;   // 0-based, [start, end)
                int rowStart = 0, rowEnd = 0;
            };

            GridItem itemPropsOf(const Element& e) {
                if (std::holds_alternative<GridItem>(e.layoutItem.data))
                    return std::get<GridItem>(e.layoutItem.data);
                return GridItem{};
            }

            // ---- Track init ---------------------------------------------------

            void initTracks(std::vector<Track>& tracks, const GridTrackList& spec,
                            float containerExtent, const LayoutContext& ctx) {
                tracks.clear();
                tracks.reserve(spec.tracks.size());
                for (const auto& ts : spec.tracks) {
                    Track t;
                    t.spec = ts;
                    switch (ts.kind) {
                        case GridTrackSizeKind::Fixed: {
                            auto px = resolveDimension(ts.value, containerExtent, ctx);
                            t.base  = px.value_or(0.f);
                            t.minPx = t.maxPx = t.base;
                            break;
                        }
                        case GridTrackSizeKind::Percent: {
                            auto px = resolveDimension(ts.value, containerExtent, ctx);
                            t.base  = px.value_or(0.f);
                            t.minPx = t.maxPx = t.base;
                            break;
                        }
                        case GridTrackSizeKind::Fr: {
                            t.hasFr    = true;
                            t.frFactor = std::max(0.f, ts.value.value);
                            t.minPx    = 0;
                            break;
                        }
                        case GridTrackSizeKind::Auto:
                        case GridTrackSizeKind::MinContent:
                        case GridTrackSizeKind::MaxContent:
                            t.intrinsic = true;
                            break;
                        case GridTrackSizeKind::FitContent: {
                            t.intrinsic = true;
                            auto px = resolveDimension(ts.value, containerExtent, ctx);
                            if (px.has_value()) t.maxPx = *px;
                            break;
                        }
                        case GridTrackSizeKind::MinMax: {
                            auto mn = resolveDimension(ts.minValue, containerExtent, ctx);
                            auto mx = resolveDimension(ts.maxValue, containerExtent, ctx);
                            if (mn.has_value()) t.minPx = *mn;
                            if (mx.has_value()) t.maxPx = *mx;
                            t.base = t.minPx;
                            t.intrinsic = !mn.has_value() || !mx.has_value();
                            // TODO: proper MinMax with Fr in either slot.
                            break;
                        }
                    }
                    tracks.push_back(t);
                }
            }

            int resolveLineIndex(const GridLine& gl, int defaultIndex) {
                // Convert 1-based CSS line numbers to 0-based track indices.
                // Lines: 1 = before first track, 2 = after first, ...
                // For start: trackIndex = line - 1. For end: same.
                if (gl.type == GridLineKind::Line)
                    return std::max(0, gl.index - 1);
                return defaultIndex;
            }

            // ---- Placement: explicit + auto-flow -----------------------------

            void placeItems(std::vector<Placement>& placements,
                            const std::vector<Element*>& kids,
                            int& numCols, int& numRows,
                            GridAutoFlow flow) {
                // 1. Explicitly placed first, then auto-flow.
                // Maintain a 2-D occupancy grid; grow rows/cols as needed.
                struct Occ {
                    std::vector<bool> grid;   // size = numRows * maxCols
                    int rows = 0, cols = 0;
                    void ensure(int r, int c) {
                        if (r > rows || c > cols) {
                            int newRows = std::max(rows, r);
                            int newCols = std::max(cols, c);
                            std::vector<bool> g(newRows * newCols, false);
                            for (int rr = 0; rr < rows; ++rr)
                                for (int cc = 0; cc < cols; ++cc)
                                    g[rr * newCols + cc] = grid[rr * cols + cc];
                            grid = std::move(g);
                            rows = newRows;
                            cols = newCols;
                        }
                    }
                    bool occupied(int r0, int c0, int r1, int c1) const {
                        for (int r = r0; r < r1; ++r)
                            for (int c = c0; c < c1; ++c)
                                if (r < rows && c < cols && grid[r * cols + c]) return true;
                        return false;
                    }
                    void mark(int r0, int c0, int r1, int c1) {
                        ensure(r1, c1);
                        for (int r = r0; r < r1; ++r)
                            for (int c = c0; c < c1; ++c)
                                grid[r * cols + c] = true;
                    }
                };
                Occ occ;
                occ.ensure(numRows, numCols);

                auto resolveSpan = [](const GridLine& start, const GridLine& end,
                                      int defaultStart) -> std::pair<int,int> {
                    // Returns [start, end) zero-based.
                    int s = (start.type == GridLineKind::Line)
                            ? std::max(0, start.index - 1)
                            : defaultStart;
                    int span = 1;
                    if (end.type == GridLineKind::Span) span = std::max(1, end.index);
                    else if (end.type == GridLineKind::Line) {
                        int e = std::max(s + 1, end.index - 1);
                        span = e - s;
                    } else if (start.type == GridLineKind::Span) {
                        // 'span N' with auto end → defer to caller's defaultStart.
                        span = std::max(1, start.index);
                    }
                    return {s, s + span};
                };

                placements.clear();
                placements.reserve(kids.size());
                std::vector<Placement> autoQueue;

                // First pass: explicit only.
                for (auto* el : kids) {
                    Placement p;
                    p.el = el;
                    p.gi = itemPropsOf(*el);
                    bool colExplicit = p.gi.columnStart.type == GridLineKind::Line;
                    bool rowExplicit = p.gi.rowStart.type    == GridLineKind::Line;
                    if (colExplicit && rowExplicit) {
                        auto [cs, ce] = resolveSpan(p.gi.columnStart, p.gi.columnEnd, 0);
                        auto [rs, re] = resolveSpan(p.gi.rowStart,    p.gi.rowEnd,    0);
                        p.colStart = cs; p.colEnd = ce;
                        p.rowStart = rs; p.rowEnd = re;
                        occ.mark(rs, cs, re, ce);
                        numCols = std::max(numCols, ce);
                        numRows = std::max(numRows, re);
                        placements.push_back(p);
                    } else {
                        autoQueue.push_back(p);
                    }
                }

                // Auto-placement.
                bool rowFlow = (flow == GridAutoFlow::Row || flow == GridAutoFlow::RowDense);
                int cursorR = 0, cursorC = 0;
                int firstAxisLen = rowFlow ? std::max(1, numCols) : std::max(1, numRows);

                for (auto& p : autoQueue) {
                    auto [csA, ceA] = resolveSpan(p.gi.columnStart, p.gi.columnEnd, 0);
                    auto [rsA, reA] = resolveSpan(p.gi.rowStart,    p.gi.rowEnd,    0);
                    int spanC = ceA - csA;
                    int spanR = reA - rsA;
                    if (spanC < 1) spanC = 1;
                    if (spanR < 1) spanR = 1;

                    // Re-scan first-axis length on each placement (it may have grown).
                    firstAxisLen = rowFlow ? std::max(1, numCols) : std::max(1, numRows);

                    bool placed = false;
                    while (!placed) {
                        if (rowFlow) {
                            if (cursorC + spanC > firstAxisLen) {
                                ++cursorR;
                                cursorC = 0;
                                continue;
                            }
                            if (!occ.occupied(cursorR, cursorC, cursorR + spanR, cursorC + spanC)) {
                                p.colStart = cursorC; p.colEnd = cursorC + spanC;
                                p.rowStart = cursorR; p.rowEnd = cursorR + spanR;
                                occ.mark(p.rowStart, p.colStart, p.rowEnd, p.colEnd);
                                numCols = std::max(numCols, p.colEnd);
                                numRows = std::max(numRows, p.rowEnd);
                                placements.push_back(p);
                                cursorC += spanC;
                                placed = true;
                            } else {
                                ++cursorC;
                                if (cursorC >= firstAxisLen) {
                                    cursorC = 0;
                                    ++cursorR;
                                }
                            }
                        } else {
                            if (cursorR + spanR > firstAxisLen) {
                                ++cursorC;
                                cursorR = 0;
                                continue;
                            }
                            if (!occ.occupied(cursorR, cursorC, cursorR + spanR, cursorC + spanC)) {
                                p.colStart = cursorC; p.colEnd = cursorC + spanC;
                                p.rowStart = cursorR; p.rowEnd = cursorR + spanR;
                                occ.mark(p.rowStart, p.colStart, p.rowEnd, p.colEnd);
                                numCols = std::max(numCols, p.colEnd);
                                numRows = std::max(numRows, p.rowEnd);
                                placements.push_back(p);
                                cursorR += spanR;
                                placed = true;
                            } else {
                                ++cursorR;
                                if (cursorR >= firstAxisLen) {
                                    cursorR = 0;
                                    ++cursorC;
                                }
                            }
                        }
                    }
                }
            }

            // Pad tracks to at least `count`, adding implicit Auto tracks.
            void padTracks(std::vector<Track>& tracks, int count) {
                while ((int)tracks.size() < count) {
                    Track t;
                    t.spec.kind = GridTrackSizeKind::Auto;
                    t.intrinsic = true;
                    tracks.push_back(t);
                }
            }

            // ---- Resolve intrinsic tracks from item contributions -------------
            //
            // For each single-span item that lands on an intrinsic track, grow
            // that track's base size up to the item's max-content contribution
            // (capped by maxPx).
            // Multi-span items distribute their unmet content size evenly over
            // their intrinsic tracks (simplified vs. the §12 distribution).

            void resolveIntrinsicTracks(std::vector<Track>& cols, std::vector<Track>& rows,
                                        const std::vector<Placement>& placements,
                                        float gapColTotal, float gapRowTotal,
                                        const LayoutContext& ctx) {
                (void)gapColTotal; (void)gapRowTotal;

                // Read the item's max-content dimensions. Prefers the cached
                // intrinsic (published by widgets like UltraCanvasLabel via
                // ComputeIntrinsicSizes) so we can skip the otherwise-redundant
                // Measure(Unbounded) pass. Falls back to Measure when no
                // intrinsic is available.
                auto itemMaxContent = [&](Element* el) -> std::pair<float, float> {
                    if (el->intrinsic.valid &&
                        (el->intrinsic.maxContentWidth > 0 || el->intrinsic.maxContentHeight > 0)) {
                        return { el->intrinsic.maxContentWidth, el->intrinsic.maxContentHeight };
                    }
                    MeasureConstraints mc{
                        { ConstraintMode::Unbounded, INFINITY },
                        { ConstraintMode::Unbounded, INFINITY }
                    };
                    el->Measure(mc, ctx);
                    return { el->measured.measuredWidth, el->measured.measuredHeight };
                };

                // Pass 1: single-span items grow their tracks to max-content.
                for (const auto& p : placements) {
                    int spanC = p.colEnd - p.colStart;
                    int spanR = p.rowEnd - p.rowStart;
                    auto [w, h] = itemMaxContent(p.el);

                    if (spanC == 1 && cols[p.colStart].intrinsic) {
                        cols[p.colStart].base = std::min(
                            cols[p.colStart].maxPx,
                            std::max(cols[p.colStart].base, w));
                    }
                    if (spanR == 1 && rows[p.rowStart].intrinsic) {
                        rows[p.rowStart].base = std::min(
                            rows[p.rowStart].maxPx,
                            std::max(rows[p.rowStart].base, h));
                    }
                }

                // Pass 2: multi-span items — distribute remaining over intrinsic tracks.
                auto distribute = [](std::vector<Track>& tracks, int s, int e, float need) {
                    if (need <= 0) return;
                    float have = 0.f;
                    int intrCount = 0;
                    for (int i = s; i < e; ++i) { have += tracks[i].base; if (tracks[i].intrinsic) ++intrCount; }
                    if (intrCount == 0) return;
                    float deficit = need - have;
                    if (deficit <= 0) return;
                    float per = deficit / (float)intrCount;
                    for (int i = s; i < e; ++i) {
                        if (!tracks[i].intrinsic) continue;
                        tracks[i].base = std::min(tracks[i].maxPx, tracks[i].base + per);
                    }
                };

                for (const auto& p : placements) {
                    int spanC = p.colEnd - p.colStart;
                    int spanR = p.rowEnd - p.rowStart;
                    if (spanC <= 1 && spanR <= 1) continue;
                    auto [w, h] = itemMaxContent(p.el);
                    if (spanC > 1) distribute(cols, p.colStart, p.colEnd, w);
                    if (spanR > 1) distribute(rows, p.rowStart, p.rowEnd, h);
                }
            }

            // ---- Resolve fr tracks against remaining space -------------------

            void resolveFrTracks(std::vector<Track>& tracks, float available, float gapTotal) {
                float usedNonFr = gapTotal;
                float totalFr   = 0.f;
                for (auto& t : tracks) {
                    if (t.hasFr) totalFr += t.frFactor;
                    else         usedNonFr += t.base;
                }
                if (totalFr <= 0.f) return;
                float remaining = std::max(0.f, available - usedNonFr);
                for (auto& t : tracks) {
                    if (!t.hasFr) continue;
                    t.base = remaining * (t.frFactor / totalFr);
                }
            }

            // ---- Item positioning helpers ------------------------------------

            float justifySelfOffset(JustifySelf js, float trackSize, float itemSize) {
                switch (js) {
                    case JustifySelf::End:    return trackSize - itemSize;
                    case JustifySelf::Center: return (trackSize - itemSize) * 0.5f;
                    case JustifySelf::Start:
                    case JustifySelf::Stretch:
                    case JustifySelf::Auto:
                    default: return 0.f;
                }
            }
            float alignSelfOffset(AlignSelf as, float trackSize, float itemSize) {
                switch (as) {
                    case AlignSelf::End:    return trackSize - itemSize;
                    case AlignSelf::Center: return (trackSize - itemSize) * 0.5f;
                    case AlignSelf::Start:
                    case AlignSelf::Stretch:
                    case AlignSelf::Baseline:
                    case AlignSelf::Auto:
                    default: return 0.f;
                }
            }

            // ---- Shared compute (Measure & Arrange share core) ---------------

            struct GridState {
                GridLayout gl;
                std::vector<Track> cols;
                std::vector<Track> rows;
                std::vector<Placement> placements;
                float colGap = 0, rowGap = 0;
                float padH = 0, padV = 0;
                float bordH = 0, bordV = 0;
                float availW = 0, availH = 0;     // content-area extents
                bool  widthKnown = false, heightKnown = false;
            };

            GridState compute(Element& e,
                              const MeasureConstraints& c,
                              const LayoutContext& ctx) {
                GridState s;
                s.gl = std::get<GridLayout>(e.layout.data);

                std::optional<float> parentInline =
                    (c.horizontal.mode == ConstraintMode::Unbounded)
                        ? std::nullopt
                        : std::optional<float>{c.horizontal.available};
                std::optional<float> parentBlock =
                    (c.vertical.mode == ConstraintMode::Unbounded)
                        ? std::nullopt
                        : std::optional<float>{c.vertical.available};

                auto padIns  = resolveEdgeSizes(e.box.padding, parentInline.value_or(0.f), ctx);
                auto bordIns = resolveEdgeSizes(e.box.border,  parentInline.value_or(0.f), ctx);
                s.padH = padIns.horizontal(); s.padV = padIns.vertical();
                s.bordH = bordIns.horizontal(); s.bordV = bordIns.vertical();

                // Container content extents (when known).
                auto ownW = resolveDimension(e.size.width,  parentInline, ctx);
                auto ownH = resolveDimension(e.size.height, parentBlock,  ctx);
                if (ownW.has_value()) {
                    s.availW = std::max(0.f, *ownW
                        - (e.box.boxSizing == BoxSizing::BorderBox ? s.padH + s.bordH : 0.f));
                    s.widthKnown = true;
                } else if (c.horizontal.mode == ConstraintMode::Exact) {
                    s.availW = std::max(0.f, c.horizontal.available - s.padH - s.bordH);
                    s.widthKnown = true;
                } else if (parentInline.has_value()) {
                    s.availW = std::max(0.f, *parentInline - s.padH - s.bordH);
                    s.widthKnown = false;
                } else {
                    s.availW = INFINITY;
                    s.widthKnown = false;
                }
                if (ownH.has_value()) {
                    s.availH = std::max(0.f, *ownH
                        - (e.box.boxSizing == BoxSizing::BorderBox ? s.padV + s.bordV : 0.f));
                    s.heightKnown = true;
                } else if (c.vertical.mode == ConstraintMode::Exact) {
                    s.availH = std::max(0.f, c.vertical.available - s.padV - s.bordV);
                    s.heightKnown = true;
                } else if (parentBlock.has_value()) {
                    s.availH = std::max(0.f, *parentBlock - s.padV - s.bordV);
                    s.heightKnown = false;
                } else {
                    s.availH = INFINITY;
                    s.heightKnown = false;
                }

                s.colGap = resolveDimension(s.gl.columnGap, s.widthKnown  ? std::optional<float>{s.availW} : std::nullopt, ctx).value_or(0.f);
                s.rowGap = resolveDimension(s.gl.rowGap,    s.heightKnown ? std::optional<float>{s.availH} : std::nullopt, ctx).value_or(0.f);

                // Build initial track lists from explicit grid.
                initTracks(s.cols, s.gl.columns, s.widthKnown  ? s.availW : 0.f, ctx);
                initTracks(s.rows, s.gl.rows,    s.heightKnown ? s.availH : 0.f, ctx);

                // Gather in-flow kids.
                std::vector<Element*> kids;
                for (auto& k : e.Children()) {
                    if (!k) continue;
                    if (k->layout.display == DisplayType::NoDisplay) continue;
                    auto p = k->layoutItem.positionType;
                    if (p != PositionType::Static && p != PositionType::Relative) continue;
                    kids.push_back(k.get());
                }

                int numCols = (int)s.cols.size();
                int numRows = (int)s.rows.size();
                placeItems(s.placements, kids, numCols, numRows, s.gl.autoFlow);

                padTracks(s.cols, numCols);
                padTracks(s.rows, numRows);

                // Intrinsic track sizing from item contributions.
                float gapColTotal = s.colGap * std::max(0.f, (float)s.cols.size() - 1.f);
                float gapRowTotal = s.rowGap * std::max(0.f, (float)s.rows.size() - 1.f);
                resolveIntrinsicTracks(s.cols, s.rows, s.placements,
                                       gapColTotal, gapRowTotal, ctx);

                // Fr resolution (only when container extent is known).
                if (s.widthKnown)  resolveFrTracks(s.cols, s.availW, gapColTotal);
                if (s.heightKnown) resolveFrTracks(s.rows, s.availH, gapRowTotal);

                return s;
            }

            float sumTracks(const std::vector<Track>& tracks) {
                float r = 0.f;
                for (const auto& t : tracks) r += t.base;
                return r;
            }

            // ---- Cache slot for GridState on the owning Element ---------------

            struct GridComputed : LayoutComputed {
                GridState state;
            };

            // Look up (or rebuild) the cached GridState for `e` under
            // constraints `c`. Cache hit returns a reference to the existing
            // state; cache miss runs compute() and stores the result.
            GridState& obtainGridState(Element& e,
                                       const MeasureConstraints& c,
                                       const LayoutContext& ctx) {
                if (auto* gc = dynamic_cast<GridComputed*>(e.layoutComputed.get());
                    gc && gc->valid && gc->key == c) {
                    return gc->state;
                }
                int prevCount = e.layoutComputed ? e.layoutComputed->recomputeCount : 0;
                auto gc = std::make_unique<GridComputed>();
                gc->key   = c;
                gc->valid = true;
                gc->recomputeCount = prevCount + 1;
                gc->state = compute(e, c, ctx);
                e.layoutComputed = std::move(gc);
                return static_cast<GridComputed&>(*e.layoutComputed).state;
            }

        } // namespace

        void MeasureGrid(Element& e,
                         const MeasureConstraints& c,
                         const LayoutContext& ctx) {
            GridState& s = obtainGridState(e, c, ctx);

            float gapColTotal = s.colGap * std::max(0.f, (float)s.cols.size() - 1.f);
            float gapRowTotal = s.rowGap * std::max(0.f, (float)s.rows.size() - 1.f);
            float contentW = sumTracks(s.cols) + gapColTotal;
            float contentH = sumTracks(s.rows) + gapRowTotal;
            if (s.widthKnown)  contentW = s.availW;
            if (s.heightKnown) contentH = s.availH;

            // AbsoluteUI children grow the container's *auto* dimensions
            // (see MeasureBlock for rationale). Explicit/known sizes win.
            if (!s.widthKnown || !s.heightKnown) {
                for (auto& kid : e.Children()) {
                    if (!kid) continue;
                    if (kid->layoutItem.positionType != PositionType::AbsoluteUI) continue;
                    Rect2Df b = MeasureAbsoluteUIBox(*kid, contentW, contentH, ctx);
                    if (!s.widthKnown)  contentW = std::max(contentW, b.x + b.width);
                    if (!s.heightKnown) contentH = std::max(contentH, b.y + b.height);
                }
            }

            e.measured.measuredWidth  = contentW + s.padH + s.bordH;
            e.measured.measuredHeight = contentH + s.padV + s.bordV;
        }

        void ArrangeGrid(Element& e,
                         const Rect2Df& finalRect,
                         const LayoutContext& ctx) {
            // The finalRect is now Exact in both axes. If Measure was called
            // with the same constraints the cache hits and we skip compute().
            MeasureConstraints exact{
                { ConstraintMode::Exact, finalRect.width  },
                { ConstraintMode::Exact, finalRect.height }
            };
            GridState& s = obtainGridState(e, exact, ctx);

            auto padIns  = resolveEdgeSizes(e.box.padding, finalRect.width, ctx);
            auto bordIns = resolveEdgeSizes(e.box.border,  finalRect.width, ctx);
            // Positions are computed in the local content-box frame further down
            // (using bordIns/padIns directly); finalRect.x/y must not leak in.

            // Precompute track origins.
            std::vector<float> colOrigin(s.cols.size() + 1, 0.f);
            for (size_t i = 0; i < s.cols.size(); ++i)
                colOrigin[i + 1] = colOrigin[i] + s.cols[i].base + (i + 1 < s.cols.size() ? s.colGap : 0.f);
            std::vector<float> rowOrigin(s.rows.size() + 1, 0.f);
            for (size_t i = 0; i < s.rows.size(); ++i)
                rowOrigin[i + 1] = rowOrigin[i] + s.rows[i].base + (i + 1 < s.rows.size() ? s.rowGap : 0.f);

            for (auto& p : s.placements) {
                float colX = colOrigin[p.colStart];
                float colW = colOrigin[p.colEnd] - colOrigin[p.colStart];
                if (p.colEnd - p.colStart > 0) {
                    // Subtract trailing gap (we added gap after every track but the spanned cell
                    // shouldn't include the gap after its last track).
                    colW -= (p.colEnd < (int)s.cols.size() ? s.colGap : 0.f);
                }
                float rowY = rowOrigin[p.rowStart];
                float rowH = rowOrigin[p.rowEnd] - rowOrigin[p.rowStart];
                if (p.rowEnd - p.rowStart > 0) {
                    rowH -= (p.rowEnd < (int)s.rows.size() ? s.rowGap : 0.f);
                }
                colW = std::max(0.f, colW);
                rowH = std::max(0.f, rowH);

                // Determine item size from justify/align self.
                JustifySelf js = p.gi.justifySelf;
                AlignSelf   as = p.gi.alignSelf;

                float itemW = colW, itemH = rowH;
                if (js != JustifySelf::Stretch && js != JustifySelf::Auto) {
                    // Need item's natural width.
                    MeasureConstraints mc{
                        { ConstraintMode::Unbounded, INFINITY },
                        { ConstraintMode::Exact, rowH }
                    };
                    p.el->Measure(mc, ctx);
                    itemW = std::min(colW, p.el->measured.measuredWidth);
                }
                if (as != AlignSelf::Stretch && as != AlignSelf::Auto) {
                    MeasureConstraints mc{
                        { ConstraintMode::Exact, itemW },
                        { ConstraintMode::Unbounded, INFINITY }
                    };
                    p.el->Measure(mc, ctx);
                    itemH = std::min(rowH, p.el->measured.measuredHeight);
                }

                // Final measure with Exact constraints so descendants lay out.
                MeasureConstraints mc{
                    { ConstraintMode::Exact, itemW },
                    { ConstraintMode::Exact, itemH }
                };
                p.el->Measure(mc, ctx);

                // Child positions are in this container's content-box frame
                // (parent-relative). Use border+padding as the local base;
                // do NOT add contentX/contentY, which include finalRect.x/y
                // (the parent's own position in its parent's frame).
                float x = bordIns.left + padIns.left + colX + justifySelfOffset(js, colW, itemW);
                float y = bordIns.top  + padIns.top  + rowY + alignSelfOffset (as, rowH, itemH);
                if (p.el->layoutItem.positionType == PositionType::Relative) {
                    // CB for a grid-item's relative offsets is its grid area.
                    auto [dx, dy] = computeRelativeOffset(*p.el, colW, rowH, ctx);
                    x += dx;
                    y += dy;
                }
                p.el->Arrange(Rect2Df{x, y, itemW, itemH}, ctx);
            }

            // Out-of-flow children. Padding-box in this element's own (border-box) frame:
            // origin = border, so ArrangePositionedChild yields border-box-relative
            // finalBounds. NOT finalRect.x/y.
            Rect2Df paddingBox{
                bordIns.left,
                bordIns.top,
                std::max(0.f, finalRect.width  - bordIns.horizontal()),
                std::max(0.f, finalRect.height - bordIns.vertical())
            };
            for (auto& kid : e.Children()) {
                if (!kid) continue;
                auto p = kid->layoutItem.positionType;
                if (p == PositionType::Absolute || p == PositionType::AbsoluteUI) {
                    ArrangePositionedChild(*kid, paddingBox, ctx);
                } else if (p == PositionType::Fixed) {
                    Rect2Df viewport{ 0, 0, ctx.viewportWidth, ctx.viewportHeight };
                    ArrangePositionedChild(*kid, viewport, ctx);
                }
            }
        }

    }
}
