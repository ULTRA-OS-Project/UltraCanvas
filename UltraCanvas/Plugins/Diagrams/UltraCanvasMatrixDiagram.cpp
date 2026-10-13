// Plugins/Diagrams/UltraCanvasMatrixDiagram.cpp
// Matrix diagram element: L, T, X and Y arrangements, ordinal cell marks, roll-ups
// Version: 1.1.1 - rotated column headers get room on the right instead of being clipped; SetDarkTheme(false) keeps non-colour settings
// Version: 1.1.0 - Phase 2: X (blank or captioned hub) and Y shapes on one cell lattice, form header band, cell tint, hover bands across panels
// Version: 1.0.1 - InvalidateLayout() overrides the layout engine's and calls it; the cache alone is DropLayoutCache()
// Version: 1.0.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMatrixDiagram.h"
#include "UltraCanvasTooltipManager.h"
#include <algorithm>
#include <cmath>
#include <cstdint>   // SIZE_MAX, uint8_t - transitive on libstdc++, not on MSVC
#include <cstdio>
#include <sstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace UltraCanvas {

// =============================================================================
// STYLE
// =============================================================================

    // Every colour field, and nothing else: what a theme switch replaces.
    static void CopyColours(const MatrixDiagramStyle& from, MatrixDiagramStyle& to) {
        to.gridLineColor           = from.gridLineColor;
        to.cellBackground          = from.cellBackground;
        to.cellAlternateBackground = from.cellAlternateBackground;
        to.markHighlightRing       = from.markHighlightRing;
        to.labelColor              = from.labelColor;
        to.axisTitleColor          = from.axisTitleColor;
        to.labelBandColor          = from.labelBandColor;
        to.labelBandTextColor      = from.labelBandTextColor;
        to.totalsBackground        = from.totalsBackground;
        to.totalsTextColor         = from.totalsTextColor;
        to.hoverBandColor          = from.hoverBandColor;
        to.selectedCellColor       = from.selectedCellColor;
        to.titleColor              = from.titleColor;
        to.subtitleColor           = from.subtitleColor;
        to.hubBackground           = from.hubBackground;
        to.hubCaptionTextColor     = from.hubCaptionTextColor;
        to.hubArrowColor           = from.hubArrowColor;
        to.headerFieldLabelColor   = from.headerFieldLabelColor;
        to.headerFieldValueColor   = from.headerFieldValueColor;
        to.headerBandBackground    = from.headerBandBackground;
        to.headerBandBorderColor   = from.headerBandBorderColor;
    }

    void MatrixDiagramStyle::ApplyLightTheme() {
        CopyColours(MatrixDiagramStyle(), *this);
    }

    void MatrixDiagramStyle::ApplyDarkTheme() {
        gridLineColor           = Color( 68,  74,  86, 255);
        cellBackground          = Color( 38,  42,  50, 255);
        cellAlternateBackground = Color( 43,  47,  56, 255);
        labelColor              = Color(220, 225, 233, 255);
        axisTitleColor          = Color(150, 157, 168, 255);
        labelBandColor          = Color( 44, 92, 100, 255);
        labelBandTextColor      = Color(238, 243, 248, 255);
        totalsBackground        = Color( 52,  57,  67, 255);
        totalsTextColor         = Color(222, 228, 236, 255);
        titleColor              = Color(238, 241, 246, 255);
        subtitleColor           = Color(155, 162, 172, 255);
        hoverBandColor          = Color(120, 170, 235, 46);
        selectedCellColor       = Color(120, 170, 235, 82);
        hubBackground           = Color( 45,  49,  58, 255);
        hubArrowColor           = Color(160, 167, 178, 255);
        headerFieldLabelColor   = Color(150, 157, 168, 255);
        headerFieldValueColor   = Color(232, 236, 242, 255);
        headerBandBackground    = Color( 38,  42,  50, 255);
        headerBandBorderColor   = Color( 74,  80,  92, 255);
    }

// =============================================================================
// LATTICE GEOMETRY
// =============================================================================

    namespace {

        Point2Dd VecAdd(const Point2Dd& a, const Point2Dd& b) {
            return Point2Dd(a.x + b.x, a.y + b.y);
        }

        Point2Dd VecScale(const Point2Dd& p, double k) {
            return Point2Dd(p.x * k, p.y * k);
        }

        // origin + a * u + b * v, the one formula every lattice point comes from.
        Point2Dd LatticeAt(const Point2Dd& origin, double a, const Point2Dd& u,
                    double b, const Point2Dd& v) {
            return Point2Dd(origin.x + a * u.x + b * v.x, origin.y + a * u.y + b * v.y);
        }

        double VecLength(const Point2Dd& p) {
            return std::sqrt(p.x * p.x + p.y * p.y);
        }

        double VecCross(const Point2Dd& a, const Point2Dd& b) {
            return a.x * b.y - a.y * b.x;
        }

        double VecDot(const Point2Dd& a, const Point2Dd& b) {
            return a.x * b.x + a.y * b.y;
        }

        // Solves p - origin = a * u + b * v. False when u and v are parallel.
        bool Unproject(const Point2Dd& p, const Point2Dd& origin, const Point2Dd& u,
                       const Point2Dd& v, double& a, double& b) {
            const double det = VecCross(u, v);
            if (std::fabs(det) < 1e-9) return false;
            const Point2Dd d(p.x - origin.x, p.y - origin.y);
            a = VecCross(d, v) / det;
            b = VecCross(u, d) / det;
            return true;
        }

        void DrawArrow(IRenderContext* ctx, const Point2Dd& from, const Point2Dd& to,
                       const Color& color) {
            const double len = VecLength(Point2Dd(to.x - from.x, to.y - from.y));
            if (len < 1.0) return;
            const Point2Dd dir((to.x - from.x) / len, (to.y - from.y) / len);
            const Point2Dd side(-dir.y, dir.x);
            const double head = std::min(6.0, len * 0.6);

            ctx->SetStrokePaint(color);
            ctx->SetStrokeWidth(1.4f);
            ctx->DrawLine(from, LatticeAt(to, -head * 0.6, dir, 0.0, side));
            std::vector<Point2Dd> tip = {
                    to,
                    LatticeAt(to, -head, dir, head * 0.55, side),
                    LatticeAt(to, -head, dir, -head * 0.55, side)};
            ctx->SetFillPaint(color);
            ctx->FillLinePath(tip);
        }

    } // namespace

// =============================================================================
// CONSTRUCTION
// =============================================================================

    UltraCanvasMatrixDiagram::UltraCanvasMatrixDiagram(const std::string& id,
                                                       int x, int y, int width, int height)
            : UltraCanvasChartElementBase(id, x, y, width, height) {
        enableSelection = true;
        enableTooltips = true;
        showGrid = false;
        showAxes = false;
        backgroundColor = Color(252, 252, 253, 255);

        legend = std::make_unique<ChartLegend>();
        legend->SetPosition(ChartLegendPosition::BottomCenter);
        legend->SetOrientation(LegendOrientation::Horizontal);
    }

// =============================================================================
// MODEL
// =============================================================================

    void UltraCanvasMatrixDiagram::SetModel(const MatrixModel& m) {
        model = m;
        if (!model.title.empty()) chartTitle = model.title;
        RebuildLegend();
        DropLayoutCache();
        RequestRedraw();
    }

    MatrixItemSet& UltraCanvasMatrixDiagram::AddSet(const std::string& setId,
                                                    const std::string& title,
                                                    const std::vector<std::string>& itemLabels) {
        DropLayoutCache();
        RequestRedraw();
        return model.AddSet(setId, title, itemLabels);
    }

    MatrixPanel& UltraCanvasMatrixDiagram::AddPanel(const std::string& rowSetId,
                                                    const std::string& colSetId,
                                                    const MatrixScale& scale) {
        MatrixPanel& panel = model.AddPanel(rowSetId, colSetId, scale);
        RebuildLegend();
        DropLayoutCache();
        RequestRedraw();
        return panel;
    }

    bool UltraCanvasMatrixDiagram::SetCell(int panel, const std::string& rowItemId,
                                           const std::string& colItemId,
                                           const std::string& levelId) {
        const bool ok = model.SetCell(panel, rowItemId, colItemId, levelId);
        if (ok) RequestRedraw();
        return ok;
    }

    bool UltraCanvasMatrixDiagram::SetCellAt(int panel, int row, int col,
                                             const std::string& levelId,
                                             const std::string& note) {
        const bool ok = model.SetCellAt(panel, row, col, levelId, note);
        if (ok) RequestRedraw();
        return ok;
    }

    bool UltraCanvasMatrixDiagram::ClearCell(int panel, int row, int col) {
        const bool ok = model.ClearCell(panel, row, col);
        if (ok) RequestRedraw();
        return ok;
    }

    void UltraCanvasMatrixDiagram::SetShape(MatrixShape shape) {
        if (model.shape == shape) return;
        model.shape = shape;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetSubtitle(const std::string& text) {
        model.subtitle = text;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::Clear() {
        model.Clear();
        hovered = MatrixRef();
        selected = MatrixRef();
        levelFilter.clear();
        RebuildLegend();
        DropLayoutCache();
        RequestRedraw();
    }

// =============================================================================
// PRESENTATION
// =============================================================================

    void UltraCanvasMatrixDiagram::SetStyle(const MatrixDiagramStyle& s) {
        style = s;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetDarkTheme(bool dark) {
        if (darkTheme == dark) return;
        darkTheme = dark;
        // Colours only. Resetting the whole struct here used to throw away
        // every size and switch the caller had set (row bands, cell tint).
        if (dark) {
            style.ApplyDarkTheme();
            backgroundColor = Color(30, 33, 40, 255);
        } else {
            style.ApplyLightTheme();
            backgroundColor = Color(252, 252, 253, 255);
        }
        RebuildLegend();
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetHeaderFit(MatrixHeaderFit fit) {
        if (headerFit == fit) return;
        headerFit = fit;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetTotals(MatrixTotals which) {
        if (totals == which) return;
        totals = which;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetShowAxisTitles(bool on) {
        if (showAxisTitles == on) return;
        showAxisTitles = on;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetShowLegend(bool on) {
        if (showLegend == on) return;
        showLegend = on;
        if (legend) legend->SetVisible(on);
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetHubMode(MatrixHubMode mode) {
        if (hubMode == mode) return;
        hubMode = mode;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetShowHeaderBand(bool on) {
        if (showHeaderBand == on) return;
        showHeaderBand = on;
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetLegendPosition(ChartLegendPosition position) {
        if (legend) legend->SetPosition(position);
        DropLayoutCache();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetShowCellValues(bool on) {
        if (showCellValues == on) return;
        showCellValues = on;
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::ClearSelection() {
        if (!selected.IsValid()) return;
        selected = MatrixRef();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetLevelFilter(const std::string& levelId) {
        if (levelFilter == levelId) return;
        levelFilter = levelId;
        RequestRedraw();
    }

// =============================================================================
// LEGEND
// =============================================================================

    void UltraCanvasMatrixDiagram::RebuildLegend() {
        if (!legend) return;

        std::vector<ChartLegendEntry> entries;
        std::vector<std::string> seen;

        // A T's two wings usually carry different scales; show every distinct
        // level once, in panel order, so the key covers the whole diagram.
        for (const MatrixPanel& panel : model.panels) {
            for (const MatrixLevel& level : panel.scale.levels) {
                if (std::find(seen.begin(), seen.end(), level.id) != seen.end()) continue;
                seen.push_back(level.id);

                ChartLegendEntry entry;
                entry.id = level.id;
                entry.label = level.label.empty() ? level.id : level.label;
                entry.color = level.color;
                entry.glyph = level.glyph;
                switch (level.shape) {
                    case MatrixMarkShape::Ring:  entry.swatch = LegendSwatch::Ring;   break;
                    case MatrixMarkShape::Text:  entry.swatch = LegendSwatch::Glyph;  break;
                    case MatrixMarkShape::Square:
                    case MatrixMarkShape::Cross: entry.swatch = LegendSwatch::Square; break;
                    case MatrixMarkShape::Diamond:
                    case MatrixMarkShape::Triangle: entry.swatch = LegendSwatch::Marker; break;
                    case MatrixMarkShape::Disc:
                    default:                     entry.swatch = LegendSwatch::Circle; break;
                }
                entries.push_back(entry);
            }
        }

        legend->SetEntries(entries);
        legend->SetVisible(showLegend && !entries.empty());

        // One legend covers every panel, so it may only borrow a scale title
        // when all the panels agree on it - a T whose wings ask different
        // questions ("Applies" and "Amount") must not be labelled with just the
        // first one.
        std::string sharedTitle = model.panels.empty()
                ? std::string() : model.panels[0].scale.title;
        for (const MatrixPanel& panel : model.panels) {
            if (panel.scale.title != sharedTitle) {
                sharedTitle.clear();
                break;
            }
        }
        legend->SetTitle(sharedTitle);

        // Take the whole preset rather than patching textColor alone: the
        // title, the disabled-entry colour and the swatch border all need to
        // follow the theme too, and the Dark preset already carries them.
        ChartLegendStyle legendStyle = darkTheme ? ChartLegendStyle::Dark()
                                                 : ChartLegendStyle::Light();
        legendStyle.textColor = EffectiveLabelColor();
        legend->SetStyle(legendStyle);
    }

// =============================================================================
// LAYOUT
// =============================================================================

    void UltraCanvasMatrixDiagram::DropLayoutCache() {
        layout.valid = false;
        layout.panels.clear();
        layout.arms.clear();
        layout.hubCaptions.clear();
        layout.headerFields.clear();
        layout.hub = Rect2Dd();
        layout.headerBand = Rect2Dd();
    }

    // std::clamp is undefined when lo > hi, and every bound below comes from a
    // caller-settable style field. Clamping through min/max instead keeps an
    // inverted or nonsensical style harmless: the lower bound simply wins.
    static double ClampSafe(double value, double lo, double hi) {
        return std::max(lo, std::min(value, std::max(lo, hi)));
    }

    double UltraCanvasMatrixDiagram::FitCellSize(double available, int count) const {
        if (count <= 0) return style.minCellSize;
        const double raw = available / static_cast<double>(count);
        return ClampSafe(raw, style.minCellSize, style.maxCellSize);
    }

    std::vector<double> UltraCanvasMatrixDiagram::RotatedHeaderReach(IRenderContext* ctx,
                                                                     const MatrixItemSet* cols,
                                                                     MatrixHeaderFit fit,
                                                                     double bandHeight) const {
        std::vector<double> reach;
        if (!cols || (fit != MatrixHeaderFit::Rotated45 && fit != MatrixHeaderFit::Rotated90)) {
            return reach;
        }

        // DrawColumnLabel anchors each label at its column's centre, runs it
        // up and to the right at `angle`, and ellipsises it to the band. Its
        // right end is the run's horizontal part plus half the text height
        // turned by the same angle.
        const double angle = (fit == MatrixHeaderFit::Rotated45) ? M_PI / 4.0 : M_PI / 2.0;
        const double maxLength = std::max(0.0, bandHeight / std::sin(angle) - 6.0);
        const double textHeight = style.colLabelFontSize * 1.25;

        ctx->SetFontSize(style.colLabelFontSize);
        for (const MatrixItem& item : cols->items) {
            const double length = std::min(static_cast<double>(ctx->GetTextLineWidth(item.label)),
                                           maxLength);
            reach.push_back(length * std::cos(angle) + textHeight / 2.0 * std::sin(angle));
        }
        return reach;
    }

    // The widest cell at which every label in `reach` ends inside `room`,
    // measured from the left edge of the first of `cellsBefore` cells that
    // come before this set's columns: column c's label ends at
    // (cellsBefore + c + 0.5) * cellWidth + reach[c].
    static double CellWidthLimit(const std::vector<double>& reach, double room, int cellsBefore) {
        double limit = 1e300;
        for (size_t c = 0; c < reach.size(); ++c) {
            limit = std::min(limit, (room - reach[c]) / (cellsBefore + static_cast<double>(c) + 0.5));
        }
        return limit;
    }

    // How far the labels in `reach` stick out past the right edge of their
    // `cols` columns of `cellWidth`.
    static double RightOverhang(const std::vector<double>& reach, double cellWidth) {
        double overhang = 0.0;
        const double n = static_cast<double>(reach.size());
        for (size_t c = 0; c < reach.size(); ++c) {
            overhang = std::max(overhang, reach[c] - (n - static_cast<double>(c) - 0.5) * cellWidth);
        }
        return overhang;
    }

    double UltraCanvasMatrixDiagram::MeasureWidestLabel(IRenderContext* ctx,
                                                        const MatrixItemSet* set,
                                                        float fontSize) const {
        if (!set) return 0.0;
        ctx->SetFontSize(fontSize);
        double widest = 0.0;
        for (const MatrixItem& item : set->items) {
            widest = std::max(widest, static_cast<double>(ctx->GetTextLineWidth(item.label)));
        }
        return widest;
    }

    double UltraCanvasMatrixDiagram::MeasureHeaderBand(IRenderContext* ctx,
                                                       const MatrixItemSet* cols,
                                                       MatrixHeaderFit fit,
                                                       double cellWidth) const {
        if (!cols || cols->items.empty()) return 0.0;

        ctx->SetFontSize(style.colLabelFontSize);
        double widest = 0.0;
        for (const MatrixItem& item : cols->items) {
            widest = std::max(widest,
                              static_cast<double>(ctx->GetTextLineWidth(item.label)));
        }

        const double lineHeight = style.colLabelFontSize * 1.25;
        double needed = lineHeight + 10.0;

        switch (fit) {
            case MatrixHeaderFit::Rotated90:
                needed = widest + 12.0;
                break;
            case MatrixHeaderFit::Rotated45:
                // The rotated label's vertical extent is its length times
                // sin(45); add a line for the glyph height across the baseline.
                needed = widest * 0.7071 + lineHeight + 8.0;
                break;
            case MatrixHeaderFit::Wrapped: {
                int lines = 1;
                for (const MatrixItem& item : cols->items) {
                    lines = std::max(lines, static_cast<int>(
                            WrapToLines(ctx, item.label, cellWidth - 4.0, 3).size()));
                }
                needed = lines * lineHeight + 10.0;
                break;
            }
            case MatrixHeaderFit::Horizontal:
            case MatrixHeaderFit::Auto:
            default:
                break;
        }

        return ClampSafe(needed, lineHeight + 8.0, style.maxHeaderBandHeight);
    }

    MatrixHeaderFit UltraCanvasMatrixDiagram::ResolveHeaderFit(IRenderContext* ctx,
                                                               const MatrixItemSet* cols,
                                                               double cellWidth,
                                                               double bandAllowance) const {
        if (headerFit != MatrixHeaderFit::Auto) return headerFit;

        if (!cols || cols->items.empty()) return MatrixHeaderFit::Horizontal;

        ctx->SetFontSize(style.colLabelFontSize);

        double widest = 0.0;
        double widestWord = 0.0;
        for (const MatrixItem& item : cols->items) {
            widest = std::max(widest,
                              static_cast<double>(ctx->GetTextLineWidth(item.label)));
            std::istringstream words(item.label);
            std::string word;
            while (words >> word) {
                widestWord = std::max(widestWord,
                                      static_cast<double>(ctx->GetTextLineWidth(word)));
            }
        }

        // Cheapest form that fits, measured once. No fixed-point iteration:
        // the band height is fixed before the cell size is chosen, so this
        // decision cannot feed back into itself.
        const double usable = cellWidth - 4.0;
        if (widest <= usable) return MatrixHeaderFit::Horizontal;
        if (widestWord <= usable &&
            bandAllowance >= style.colLabelFontSize * 3.6) {
            return MatrixHeaderFit::Wrapped;
        }
        // Prefer the 45 degree form while it fits the allowance; it stays
        // readable where a fully vertical label does not.
        return bandAllowance >= widest * 0.7071 + style.colLabelFontSize * 1.25 + 8.0
                       ? MatrixHeaderFit::Rotated45
                       : MatrixHeaderFit::Rotated90;
    }

    UltraCanvasMatrixDiagram::PanelLayout
    UltraCanvasMatrixDiagram::MeasurePanel(IRenderContext* ctx, int panelIndex,
                                           bool rowLabelsOnRight) const {
        PanelLayout pl;
        pl.panelIndex = panelIndex;
        pl.rowLabelsOnRight = rowLabelsOnRight;

        const MatrixItemSet* rows = model.RowSet(panelIndex);
        const MatrixItemSet* cols = model.ColSet(panelIndex);
        if (!rows || !cols) return pl;

        pl.rows = rows->Count();
        pl.cols = cols->Count();

        ctx->SetFontSize(style.rowLabelFontSize);
        double rowLabelWidth = 0.0;
        for (const MatrixItem& item : rows->items) {
            rowLabelWidth = std::max(rowLabelWidth,
                                     static_cast<double>(ctx->GetTextLineWidth(item.label)));
        }
        rowLabelWidth = std::min(rowLabelWidth, style.maxRowLabelWidth);
        pl.rowLabels.width = rowLabelWidth + style.labelPadding * 2.0;

        return pl;
    }

    void UltraCanvasMatrixDiagram::LayoutL(IRenderContext* ctx, const Rect2Dd& area) {
        PanelLayout pl = MeasurePanel(ctx, 0, false);
        if (pl.rows == 0 || pl.cols == 0) return;

        const bool rowTotals = (totals == MatrixTotals::Rows || totals == MatrixTotals::Both);
        const bool colTotals = (totals == MatrixTotals::Columns || totals == MatrixTotals::Both);

        const double axisTitleSpace = showAxisTitles ? style.axisTitleFontSize + 8.0 : 0.0;

        // The cell WIDTH depends only on horizontal space, so it settles first.
        // The header fit then follows from the cell width, and the band height
        // from the fit - which is what keeps this a single pass instead of a
        // measure/place cycle (see the proposal, section 7.3).
        const double gridLeft = area.x + pl.rowLabels.width + axisTitleSpace;
        const double gridAvailW = area.x + area.width - gridLeft
                                  - (rowTotals ? style.totalsGutterSize : 0.0);
        pl.cellWidth = FitCellSize(gridAvailW, pl.cols);

        // The grid has first claim on the height: the header may take only what
        // is left once every row has its minimum and the totals gutter is
        // reserved. A fully legible header above a clipped grid is the wrong
        // trade - the marks are the data, the header is the caption.
        const double gridFloor = pl.rows * style.minCellSize
                                 + (colTotals ? style.totalsGutterSize : 0.0)
                                 + axisTitleSpace;
        const double bandAllowance = ClampSafe(area.height - gridFloor,
                                               28.0, style.maxHeaderBandHeight);
        pl.fit = ResolveHeaderFit(ctx, model.ColSet(0), pl.cellWidth, bandAllowance);
        pl.headerLines = (pl.fit == MatrixHeaderFit::Wrapped) ? 3 : 1;

        double bandHeight = std::min(bandAllowance,
                                     MeasureHeaderBand(ctx, model.ColSet(0), pl.fit, pl.cellWidth));

        // A rotated header leans right past the last column. Narrow the cells
        // until it ends inside the content area instead of under the clip.
        // The fit stays: a rotated label does not depend on the cell width.
        const std::vector<double> reach = RotatedHeaderReach(ctx, model.ColSet(0), pl.fit, bandHeight);
        if (!reach.empty()) {
            pl.cellWidth = std::max(style.minCellSize,
                    std::min(pl.cellWidth, CellWidthLimit(reach, area.x + area.width - gridLeft, 0)));
        }

        const double gridTop = area.y + bandHeight + axisTitleSpace;
        const double gridAvailH = area.y + area.height - gridTop
                                  - (colTotals ? style.totalsGutterSize : 0.0);
        pl.cellHeight = FitCellSize(gridAvailH, pl.rows);

        pl.grid = Rect2Dd(gridLeft, gridTop,
                          pl.cellWidth * pl.cols, pl.cellHeight * pl.rows);
        SetAxisLattice(pl, false);
        pl.rowLabels = Rect2Dd(area.x + axisTitleSpace, pl.grid.y,
                               pl.rowLabels.width, pl.grid.height);
        pl.colLabels = Rect2Dd(pl.grid.x, area.y + axisTitleSpace,
                               pl.grid.width, bandHeight);

        if (rowTotals) {
            pl.rowTotals = Rect2Dd(pl.grid.x + pl.grid.width, pl.grid.y,
                                   style.totalsGutterSize, pl.grid.height);
        }
        if (colTotals) {
            pl.colTotals = Rect2Dd(pl.grid.x, pl.grid.y + pl.grid.height,
                                   pl.grid.width, style.totalsGutterSize);
        }

        layout.panels.push_back(pl);
    }

    void UltraCanvasMatrixDiagram::LayoutT(IRenderContext* ctx, const Rect2Dd& area) {
        if (model.panels.size() < 2) {
            LayoutL(ctx, area);
            return;
        }

        // A T shares its row axis between two wings. Panel 0 is the left wing
        // (its row labels sit on the right, against the spine); panel 1 is the
        // right wing. The shared labels are drawn once, between them.
        PanelLayout left = MeasurePanel(ctx, 0, true);
        PanelLayout right = MeasurePanel(ctx, 1, false);
        if (left.rows == 0 || left.cols == 0 || right.cols == 0) return;

        // The spine width is the wider of the two measurements - both wings
        // list the same row set, so they should agree, but a caller may have
        // crossed different sets and the layout must not break.
        const double spineWidth = std::max(left.rowLabels.width, right.rowLabels.width);

        const double axisTitleSpace = showAxisTitles ? style.axisTitleFontSize + 8.0 : 0.0;

        // Split the horizontal space between the wings in proportion to their
        // column counts, so cells stay square-ish on both sides.
        const double totalWidth = area.width - spineWidth - style.panelGap * 2.0;
        const int totalCols = left.cols + right.cols;
        double cellWidth = FitCellSize(totalWidth, totalCols);

        // Same single-pass order as the L: width, then fit, then band height,
        // with the grid holding first claim on the vertical space.
        const double gridFloor = left.rows * style.minCellSize + axisTitleSpace;
        const double bandAllowance = ClampSafe(area.height - gridFloor,
                                               28.0, style.maxHeaderBandHeight);
        left.fit = ResolveHeaderFit(ctx, model.ColSet(0), cellWidth, bandAllowance);
        right.fit = ResolveHeaderFit(ctx, model.ColSet(1), cellWidth, bandAllowance);

        // Both wings share one band, so it must satisfy the taller of the two.
        const double bandHeight = std::min(bandAllowance,
                std::max(MeasureHeaderBand(ctx, model.ColSet(0), left.fit, cellWidth),
                         MeasureHeaderBand(ctx, model.ColSet(1), right.fit, cellWidth)));

        // The right wing's rotated headers lean out past the assembly's right
        // edge; narrow the cells of both wings until they fit (see LayoutL).
        const std::vector<double> reach = RotatedHeaderReach(ctx, model.ColSet(1), right.fit, bandHeight);
        if (!reach.empty()) {
            cellWidth = std::max(style.minCellSize,
                    std::min(cellWidth, CellWidthLimit(reach, totalWidth, left.cols)));
        }

        const double gridTop = area.y + bandHeight + axisTitleSpace;
        const double gridAvailH = area.y + area.height - gridTop;
        const double cellHeight = FitCellSize(gridAvailH, left.rows);

        const double leftGridW = cellWidth * left.cols;
        const double rightGridW = cellWidth * right.cols;

        // Centre the whole assembly horizontally, counting the headers that
        // overhang its right edge as part of it.
        const double assemblyW = leftGridW + spineWidth + rightGridW + style.panelGap * 2.0
                                 + RightOverhang(reach, cellWidth);
        const double startX = area.x + std::max(0.0, (area.width - assemblyW) / 2.0);

        left.cellWidth = right.cellWidth = cellWidth;
        left.cellHeight = right.cellHeight = cellHeight;

        left.grid = Rect2Dd(startX, gridTop, leftGridW, cellHeight * left.rows);
        left.rowLabels = Rect2Dd(left.grid.x + leftGridW + style.panelGap / 2.0, gridTop,
                                 spineWidth, left.grid.height);
        left.colLabels = Rect2Dd(left.grid.x, area.y + axisTitleSpace, leftGridW, bandHeight);
        left.headerLines = (left.fit == MatrixHeaderFit::Wrapped) ? 3 : 1;

        const double rightX = left.rowLabels.x + spineWidth + style.panelGap / 2.0;
        right.rows = left.rows;
        right.grid = Rect2Dd(rightX, gridTop, rightGridW, cellHeight * right.rows);
        right.rowLabels = Rect2Dd(0, 0, 0, 0);   // drawn once, by the left wing
        right.colLabels = Rect2Dd(right.grid.x, area.y + axisTitleSpace, rightGridW, bandHeight);
        right.headerLines = (right.fit == MatrixHeaderFit::Wrapped) ? 3 : 1;

        SetAxisLattice(left, false);
        SetAxisLattice(right, false);
        layout.panels.push_back(left);
        layout.panels.push_back(right);
    }

    void UltraCanvasMatrixDiagram::SetAxisLattice(PanelLayout& pl, bool transposed) {
        pl.origin = Point2Dd(pl.grid.x, pl.grid.y);
        pl.axisAligned = true;
        if (transposed) {
            pl.rowStep = Point2Dd(pl.cellWidth, 0.0);
            pl.colStep = Point2Dd(0.0, pl.cellHeight);
        } else {
            pl.rowStep = Point2Dd(0.0, pl.cellHeight);
            pl.colStep = Point2Dd(pl.cellWidth, 0.0);
        }
    }

    // The X shape (proposal section 7.2). The four sets sit on the four arms of
    // a cross - sets[0] left, [1] top, [2] right, [3] bottom - and each panel
    // fills the quadrant between its two arms. The top and bottom arms list
    // their items as rows, the left and right ones as columns, so the shared
    // axes couple the quadrants: one cell width serves both left quadrants'
    // columns and both right ones', one cell height every row. Solved in one
    // pass, as the L is: hub width, cell width, header fit, band height, cell
    // height - the hub taking what is left in the middle.
    bool UltraCanvasMatrixDiagram::LayoutX(IRenderContext* ctx, const Rect2Dd& area) {
        if (model.sets.size() < 4) return false;

        const MatrixItemSet& left = model.sets[0];
        const MatrixItemSet& top = model.sets[1];
        const MatrixItemSet& right = model.sets[2];
        const MatrixItemSet& bottom = model.sets[3];
        const int nL = left.Count(), nT = top.Count(), nR = right.Count(), nB = bottom.Count();
        if (nL + nR == 0 || nT + nB == 0) return false;

        const bool captions = (hubMode == MatrixHubMode::Captions);
        const double titleSpace = (showAxisTitles && !captions)
                ? style.axisTitleFontSize + 10.0 : 0.0;

        // The vertical arms hold row labels, so their width is the widest of
        // them; the hub is at least that wide, wider when captions need it.
        const double rowLabelWidth = std::min(
                std::max(MeasureWidestLabel(ctx, &top, style.rowLabelFontSize),
                         MeasureWidestLabel(ctx, &bottom, style.rowLabelFontSize)),
                style.maxRowLabelWidth);
        double hubW = rowLabelWidth + style.labelPadding * 2.0;
        double hubMinH = 0.0;
        if (captions) {
            double minW = 0.0;
            LayoutHubCaptions(ctx, minW, hubMinH);
            hubW = std::max(hubW, minW);
        }

        double cellW = FitCellSize(area.width - hubW - titleSpace * 2.0, nL + nR);

        const double gridFloor = (nT + nB) * style.minCellSize + titleSpace * 2.0;
        const double bandAllowance = ClampSafe(area.height - gridFloor,
                                               28.0, style.maxHeaderBandHeight);
        const MatrixHeaderFit fitL = ResolveHeaderFit(ctx, &left, cellW, bandAllowance);
        const MatrixHeaderFit fitR = ResolveHeaderFit(ctx, &right, cellW, bandAllowance);
        const double bandH = std::min(bandAllowance,
                std::max(MeasureHeaderBand(ctx, &left, fitL, cellW),
                         MeasureHeaderBand(ctx, &right, fitR, cellW)));
        const double hubH = std::max(bandH, hubMinH);

        // The right arm's rotated headers lean out past the right quadrants;
        // narrow the cells until they fit (see LayoutL). The left arm's lean
        // into the hub, which is in the drawing already.
        const std::vector<double> reach = RotatedHeaderReach(ctx, &right, fitR, hubH);
        if (!reach.empty()) {
            cellW = std::max(style.minCellSize,
                    std::min(cellW, CellWidthLimit(reach, area.width - titleSpace * 2.0 - hubW, nL)));
        }

        const double cellH = FitCellSize(area.height - hubH - titleSpace * 2.0, nT + nB);

        const double totalW = (nL + nR) * cellW + hubW + RightOverhang(reach, cellW);
        const double totalH = (nT + nB) * cellH + hubH;
        const double x0 = area.x + titleSpace
                          + std::max(0.0, (area.width - titleSpace * 2.0 - totalW) / 2.0);
        const double y0 = area.y + titleSpace
                          + std::max(0.0, (area.height - titleSpace * 2.0 - totalH) / 2.0);

        const double hubX = x0 + nL * cellW;
        const double hubY = y0 + nT * cellH;
        layout.hub = Rect2Dd(hubX, hubY, hubW, hubH);

        // ---- Arms ----
        auto makeArm = [&](int setIndex, double ox, double oy, bool columns, int count,
                           double itemSize, double bandWidth, MatrixHeaderFit fit) {
            ArmLayout arm;
            arm.setIndex = setIndex;
            arm.origin = Point2Dd(ox, oy);
            arm.dir = columns ? Point2Dd(1.0, 0.0) : Point2Dd(0.0, 1.0);
            arm.across = columns ? Point2Dd(0.0, 1.0) : Point2Dd(1.0, 0.0);
            arm.itemSize = itemSize;
            arm.bandWidth = bandWidth;
            arm.count = count;
            arm.itemsAreColumns = columns;
            arm.fit = fit;
            arm.headerLines = (fit == MatrixHeaderFit::Wrapped) ? 3 : 1;
            return arm;
        };

        const double gap = 6.0;
        ArmLayout armL = makeArm(0, x0, hubY, true, nL, cellW, hubH, fitL);
        armL.titleAnchor = Point2Dd(x0 - gap - style.axisTitleFontSize / 2.0, hubY + hubH / 2.0);
        armL.titleVertical = true;
        ArmLayout armT = makeArm(1, hubX, y0, false, nT, cellH, hubW, MatrixHeaderFit::Horizontal);
        armT.titleAnchor = Point2Dd(hubX + hubW / 2.0, y0 - gap - style.axisTitleFontSize / 2.0);
        ArmLayout armR = makeArm(2, hubX + hubW, hubY, true, nR, cellW, hubH, fitR);
        armR.titleAnchor = Point2Dd(hubX + hubW + nR * cellW + gap + style.axisTitleFontSize / 2.0,
                                    hubY + hubH / 2.0);
        armR.titleVertical = true;
        ArmLayout armB = makeArm(3, hubX, hubY + hubH, false, nB, cellH, hubW,
                                 MatrixHeaderFit::Horizontal);
        armB.titleAnchor = Point2Dd(hubX + hubW / 2.0,
                                    hubY + hubH + nB * cellH + gap + style.axisTitleFontSize / 2.0);
        layout.arms = {armL, armT, armR, armB};

        // ---- Quadrants ----
        // Keyed by (top/bottom set, left/right set): the grid each pair fills.
        auto quadrant = [&](int vertical, int horizontal) {
            const double gx = (horizontal == 0) ? x0 : hubX + hubW;
            const double gy = (vertical == 1) ? y0 : hubY + hubH;
            const int cols = (horizontal == 0) ? nL : nR;
            const int rows = (vertical == 1) ? nT : nB;
            return Rect2Dd(gx, gy, cols * cellW, rows * cellH);
        };

        for (size_t p = 0; p < model.panels.size(); ++p) {
            const int r = model.IndexOfSet(model.panels[p].rowSetId);
            const int c = model.IndexOfSet(model.panels[p].colSetId);
            if (r < 0 || c < 0 || r > 3 || c > 3 || (r % 2) == (c % 2)) continue;

            // Model rows on a top/bottom arm read down the quadrant as
            // usual; model rows on a left/right arm run across it.
            const bool transposed = (r % 2) == 0;
            const int vertical = transposed ? c : r;
            const int horizontal = transposed ? r : c;

            PanelLayout pl;
            pl.panelIndex = static_cast<int>(p);
            pl.rows = model.sets[static_cast<size_t>(r)].Count();
            pl.cols = model.sets[static_cast<size_t>(c)].Count();
            pl.cellWidth = cellW;
            pl.cellHeight = cellH;
            pl.grid = quadrant(vertical, horizontal);
            pl.rowLabels = Rect2Dd(0, 0, 0, 0);   // the arms carry the labels
            pl.colLabels = Rect2Dd(0, 0, 0, 0);
            SetAxisLattice(pl, transposed);
            layout.panels.push_back(pl);
        }

        if (captions) PlaceHubCaptions();
        return true;
    }

    // The four caption boxes of an X hub: one per set, each by the hub edge
    // that faces its arm, with an arrow out to it. Measured before the cell
    // sizes are chosen so the hub can be made big enough to hold them.
    void UltraCanvasMatrixDiagram::LayoutHubCaptions(IRenderContext* ctx, double& minWidth,
                                                     double& minHeight) {
        layout.hubCaptions.clear();
        ctx->SetFontSize(style.hubCaptionFontSize);
        ctx->SetFontWeight(FontWeight::Bold);

        const double padX = 8.0, padY = 4.0;
        double widths[4] = {0, 0, 0, 0};
        double boxH = style.hubCaptionFontSize * 1.3 + padY * 2.0;
        for (int i = 0; i < 4 && i < static_cast<int>(model.sets.size()); ++i) {
            const std::string& title = model.sets[static_cast<size_t>(i)].title.empty()
                    ? model.sets[static_cast<size_t>(i)].id
                    : model.sets[static_cast<size_t>(i)].title;
            widths[i] = std::min(static_cast<double>(ctx->GetTextLineWidth(title)),
                                 style.maxRowLabelWidth) + padX * 2.0;
            HubCaption caption;
            caption.setIndex = i;
            caption.box = Rect2Dd(0, 0, widths[i], boxH);
            layout.hubCaptions.push_back(caption);
        }
        ctx->SetFontWeight(FontWeight::Normal);

        // Left and right boxes share the middle row; top and bottom each
        // take a row of their own, with room for the arrows between.
        const double arrow = 14.0, gap = 6.0;
        minWidth = std::max({widths[1] + gap * 2.0, widths[3] + gap * 2.0,
                             widths[0] + widths[2] + arrow * 2.0 + gap * 3.0});
        minHeight = boxH * 3.0 + arrow * 2.0 + gap * 2.0;
    }

    void UltraCanvasMatrixDiagram::PlaceHubCaptions() {
        const Rect2Dd& hub = layout.hub;
        const double cx = hub.x + hub.width / 2.0;
        const double cy = hub.y + hub.height / 2.0;
        const double inset = 4.0;

        for (HubCaption& caption : layout.hubCaptions) {
            Rect2Dd& b = caption.box;
            switch (caption.setIndex) {
                case 0:   // left arm: box at the left of the middle row
                    b.x = hub.x + inset + 14.0;
                    b.y = cy - b.height / 2.0;
                    caption.arrowFrom = Point2Dd(b.x, cy);
                    caption.arrowTo = Point2Dd(hub.x + 1.0, cy);
                    break;
                case 1:   // top arm
                    b.x = cx - b.width / 2.0;
                    b.y = hub.y + inset + 14.0;
                    caption.arrowFrom = Point2Dd(cx, b.y);
                    caption.arrowTo = Point2Dd(cx, hub.y + 1.0);
                    break;
                case 2:   // right arm
                    b.x = hub.x + hub.width - inset - 14.0 - b.width;
                    b.y = cy - b.height / 2.0;
                    caption.arrowFrom = Point2Dd(b.x + b.width, cy);
                    caption.arrowTo = Point2Dd(hub.x + hub.width - 1.0, cy);
                    break;
                case 3:   // bottom arm
                default:
                    b.x = cx - b.width / 2.0;
                    b.y = hub.y + hub.height - inset - 14.0 - b.height;
                    caption.arrowFrom = Point2Dd(cx, b.y + b.height);
                    caption.arrowTo = Point2Dd(cx, hub.y + hub.height - 1.0);
                    break;
            }
        }
    }

    // The Y shape. Three seams leave a centre point 120 degrees apart -
    // sets[0] straight up, [1] down-right, [2] down-left - and each seam is a
    // band listing its set. Between two seams lies the panel crossing their
    // sets, a rhombus whose cells step along both seams, so a row of one panel
    // and the matching row of its neighbour start at the same point on the
    // seam they share. The panels stand back from the seams by half a band
    // width so the labels have somewhere to go; everything else is the same
    // lattice the L uses, which is why the marks, the highlight and the hit
    // test need no Y-specific code.
    bool UltraCanvasMatrixDiagram::LayoutY(IRenderContext* ctx, const Rect2Dd& area) {
        if (model.sets.size() < 3) return false;

        const double kSin60 = 0.8660254037844386;
        const Point2Dd seams[3] = {
                Point2Dd(0.0, -1.0),
                Point2Dd(kSin60, 0.5),
                Point2Dd(-kSin60, 0.5)};

        double widest = 0.0;
        for (int i = 0; i < 3; ++i) {
            widest = std::max(widest, MeasureWidestLabel(ctx, &model.sets[static_cast<size_t>(i)],
                                                         style.rowLabelFontSize));
        }
        const double bandW = std::min(widest, style.maxRowLabelWidth) + style.labelPadding * 2.0;

        // A rhombus spanned by two seams 120 degrees apart, pushed along
        // their bisector by d, clears each seam by d * sin(60): half a band.
        const double d = (bandW / 2.0) / kSin60;

        int counts[3];
        for (int i = 0; i < 3; ++i) counts[i] = model.sets[static_cast<size_t>(i)].Count();

        // Collect the panels the Y can place, in the seams they span.
        struct Placed { int panel; int r; int c; };
        std::vector<Placed> placed;
        for (size_t p = 0; p < model.panels.size(); ++p) {
            const int r = model.IndexOfSet(model.panels[p].rowSetId);
            const int c = model.IndexOfSet(model.panels[p].colSetId);
            if (r < 0 || c < 0 || r > 2 || c > 2 || r == c) continue;
            placed.push_back({static_cast<int>(p), r, c});
        }
        if (placed.empty()) return false;

        // Every extreme point of the drawing is fixed + s * moving for the
        // cell size s, so the bounding box grows monotonically with s and the
        // largest s that fits can be found by bisection.
        ctx->SetFontSize(style.axisTitleFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        double titleW[3], titleH = style.axisTitleFontSize * 1.3;
        for (int i = 0; i < 3; ++i) {
            titleW[i] = showAxisTitles
                    ? ctx->GetTextLineWidth(model.sets[static_cast<size_t>(i)].title) : 0.0;
        }
        ctx->SetFontWeight(FontWeight::Normal);

        struct Extent { Point2Dd fixed; Point2Dd moving; };
        std::vector<Extent> extents;
        for (int i = 0; i < 3; ++i) {
            const Point2Dd u = seams[i];
            const Point2Dd across(-u.y, u.x);
            for (int side = -1; side <= 1; side += 2) {
                extents.push_back({LatticeAt(Point2Dd(0, 0), d, u, side * bandW / 2.0, across), Point2Dd(0, 0)});
                extents.push_back({LatticeAt(Point2Dd(0, 0), d, u, side * bandW / 2.0, across),
                                   VecScale(u, counts[i])});
            }
            if (showAxisTitles && titleW[i] > 0.0) {
                // The title sits beyond the band's outer end; its box is
                // approximated by its four corners around that point.
                const double reach = titleH / 2.0 + 8.0
                        + std::fabs(u.x) * titleW[i] / 2.0;
                for (int sx = -1; sx <= 1; sx += 2) {
                    for (int sy = -1; sy <= 1; sy += 2) {
                        extents.push_back({Point2Dd(u.x * (d + reach) + sx * titleW[i] / 2.0,
                                                    u.y * (d + reach) + sy * titleH / 2.0),
                                           VecScale(u, counts[i])});
                    }
                }
            }
        }
        for (const Placed& pp : placed) {
            const Point2Dd o = VecScale(VecAdd(seams[pp.r], seams[pp.c]), d);
            const int nr = counts[pp.r], nc = counts[pp.c];
            extents.push_back({o, Point2Dd(0, 0)});
            extents.push_back({o, VecScale(seams[pp.r], nr)});
            extents.push_back({o, VecScale(seams[pp.c], nc)});
            extents.push_back({o, VecAdd(VecScale(seams[pp.r], nr), VecScale(seams[pp.c], nc))});
        }

        auto bounds = [&extents](double s) {
            double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
            for (const Extent& e : extents) {
                const double x = e.fixed.x + s * e.moving.x;
                const double y = e.fixed.y + s * e.moving.y;
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
            return Rect2Dd(minX, minY, maxX - minX, maxY - minY);
        };

        double lo = style.minCellSize, hi = std::max(style.minCellSize, style.maxCellSize);
        auto fits = [&](double s) {
            Rect2Dd b = bounds(s);
            return b.width <= area.width && b.height <= area.height;
        };
        double s = lo;
        if (fits(hi)) {
            s = hi;
        } else if (fits(lo)) {
            for (int i = 0; i < 40; ++i) {
                const double mid = (lo + hi) / 2.0;
                if (fits(mid)) lo = mid; else hi = mid;
            }
            s = lo;
        }

        // Centre the drawing: the hub point goes wherever centres the box.
        const Rect2Dd box = bounds(s);
        const Point2Dd hub(area.x + (area.width - box.width) / 2.0 - box.x,
                           area.y + (area.height - box.height) / 2.0 - box.y);
        layout.hub = Rect2Dd(hub.x, hub.y, 0.0, 0.0);

        for (int i = 0; i < 3; ++i) {
            const Point2Dd u = seams[i];
            ArmLayout arm;
            arm.setIndex = i;
            arm.dir = u;
            // Turn the across vector so it never points left; the labels
            // run along it, so that keeps them reading left to right.
            arm.across = Point2Dd(-u.y, u.x);
            if (arm.across.x < 0.0 || (arm.across.x == 0.0 && arm.across.y < 0.0)) {
                arm.across = VecScale(arm.across, -1.0);
            }
            arm.origin = LatticeAt(hub, d, u, -bandW / 2.0, arm.across);
            arm.itemSize = s;
            arm.bandWidth = bandW;
            arm.count = counts[i];
            const double reach = titleH / 2.0 + 8.0 + std::fabs(u.x) * titleW[i] / 2.0;
            arm.titleAnchor = LatticeAt(hub, d + counts[i] * s + reach, u, 0.0, u);
            layout.arms.push_back(arm);
        }

        for (const Placed& pp : placed) {
            PanelLayout pl;
            pl.panelIndex = pp.panel;
            pl.rows = counts[pp.r];
            pl.cols = counts[pp.c];
            pl.cellWidth = pl.cellHeight = s;
            pl.axisAligned = false;
            pl.rowStep = VecScale(seams[pp.r], s);
            pl.colStep = VecScale(seams[pp.c], s);
            pl.origin = LatticeAt(hub, d, seams[pp.r], d, seams[pp.c]);

            const std::vector<Point2Dd> quad = CellQuad(pl, 0, 0, pl.rows, pl.cols);
            double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
            for (const Point2Dd& q : quad) {
                minX = std::min(minX, q.x); maxX = std::max(maxX, q.x);
                minY = std::min(minY, q.y); maxY = std::max(maxY, q.y);
            }
            pl.grid = Rect2Dd(minX, minY, maxX - minX, maxY - minY);
            pl.rowLabels = Rect2Dd(0, 0, 0, 0);
            pl.colLabels = Rect2Dd(0, 0, 0, 0);
            layout.panels.push_back(pl);
        }
        return true;
    }

    // The form header band: one box per field, widths in proportion to what
    // each needs, stretched to the full width.
    void UltraCanvasMatrixDiagram::LayoutHeaderBand(IRenderContext* ctx, Rect2Dd& area) {
        if (!showHeaderBand || model.headerFields.empty()) return;

        const double padX = 8.0;
        std::vector<double> want;
        double total = 0.0;
        for (const MatrixHeaderField& field : model.headerFields) {
            ctx->SetFontSize(style.headerFieldLabelFontSize);
            double w = ctx->GetTextLineWidth(field.label);
            ctx->SetFontSize(style.headerFieldValueFontSize);
            w = std::max(w, static_cast<double>(ctx->GetTextLineWidth(field.value)));
            w += padX * 2.0;
            want.push_back(w);
            total += w;
        }

        const double height = style.headerFieldLabelFontSize * 1.3
                              + style.headerFieldValueFontSize * 1.35 + 12.0;
        layout.headerBand = Rect2Dd(area.x, area.y, area.width, height);

        // Every box grows by the same share of the spare width; when the
        // fields need more than there is, they shrink in proportion and the
        // text is ellipsised.
        double x = area.x;
        const double spare = area.width - total;
        for (size_t i = 0; i < want.size(); ++i) {
            double w = (spare >= 0.0)
                    ? want[i] + spare / static_cast<double>(want.size())
                    : want[i] * (total > 0.0 ? area.width / total : 0.0);
            HeaderField hf;
            hf.box = Rect2Dd(x, area.y, w, height);
            hf.field = static_cast<int>(i);
            layout.headerFields.push_back(hf);
            x += w;
        }

        const double used = height + 10.0;
        area = Rect2Dd(area.x, area.y + used, area.width, std::max(0.0, area.height - used));
    }

    void UltraCanvasMatrixDiagram::UpdateLayout(IRenderContext* ctx) {
        if (layout.valid || !ctx) return;

        layout.panels.clear();

        Rect2Df local = GetLocalBounds();
        Rect2Dd area(local.x, local.y, local.width, local.height);

        const double margin = 12.0;
        area = Rect2Dd(area.x + margin, area.y + margin,
                       std::max(0.0, area.width - margin * 2.0),
                       std::max(0.0, area.height - margin * 2.0));

        // Title band
        double titleSpace = 0.0;
        if (!chartTitle.empty()) titleSpace += style.titleFontSize + 12.0;
        if (!model.subtitle.empty()) titleSpace += style.subtitleFontSize + 6.0;
        area = Rect2Dd(area.x, area.y + titleSpace,
                       area.width, std::max(0.0, area.height - titleSpace));

        LayoutHeaderBand(ctx, area);

        // The legend takes its bite before the grid is laid out. Render() must
        // draw it against this same pre-bite rectangle - handing it the
        // remainder instead would place it back over the grid it just made
        // room for.
        layout.legendArea = area;
        if (legend && legend->IsVisible()) {
            legend->Measure(ctx, area);
            area = legend->RemainingArea(area);
        }

        layout.content = area;

        if (!model.panels.empty() && area.width > 0.0 && area.height > 0.0) {
            switch (model.shape) {
                case MatrixShape::T:
                    LayoutT(ctx, area);
                    break;
                case MatrixShape::X:
                    // A model the X cannot place (too few sets, no panel
                    // pairing a side arm with an end arm) still shows its
                    // first panel rather than nothing.
                    if (!LayoutX(ctx, area)) LayoutL(ctx, area);
                    break;
                case MatrixShape::Y:
                    if (!LayoutY(ctx, area)) LayoutL(ctx, area);
                    break;
                case MatrixShape::L:
                case MatrixShape::House:
                default:
                    // House is not implemented yet; it falls back to the L
                    // layout of panel 0 rather than drawing nothing, so a
                    // caller who sets it early still sees their data.
                    LayoutL(ctx, area);
                    break;
            }
        }

        layout.valid = true;
    }

// =============================================================================
// RENDERING
// =============================================================================

    void UltraCanvasMatrixDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
        if (!ctx) return;

        UpdateLayout(ctx);

        if (showBackground) {
            ctx->DrawFilledRectangle(GetLocalBounds(), backgroundColor);
        }

        RenderTitles(ctx);
        RenderHeaderBand(ctx);
        RenderChart(ctx);

        if (legend && legend->IsVisible()) {
            legend->Render(ctx, layout.legendArea);
        }
    }

    void UltraCanvasMatrixDiagram::RenderChart(IRenderContext* ctx) {
        if (!ctx) return;
        if (!layout.valid) UpdateLayout(ctx);
        if (layout.panels.empty()) return;

        // When there are more rows than fit at minCellSize the grid overflows
        // its area. Clip it so the overflow is cut off at the content edge
        // instead of painting over the legend below.
        ctx->PushState();
        ctx->ClipRect(layout.content);
        if (model.shape == MatrixShape::X && layout.hub.width > 0.0) RenderHub(ctx);
        for (const PanelLayout& pl : layout.panels) {
            RenderPanel(ctx, pl);
        }
        for (const ArmLayout& arm : layout.arms) {
            RenderArm(ctx, arm);
            if (showAxisTitles && !(model.shape == MatrixShape::X &&
                                    hubMode == MatrixHubMode::Captions)) {
                RenderArmTitle(ctx, arm);
            }
        }
        ctx->PopState();
    }

    void UltraCanvasMatrixDiagram::RenderTitles(IRenderContext* ctx) {
        double y = 12.0;

        if (!chartTitle.empty()) {
            ctx->SetFontSize(style.titleFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(style.titleColor);
            ctx->DrawText(chartTitle, Point2Dd(14.0, y));
            ctx->SetFontWeight(FontWeight::Normal);
            y += style.titleFontSize + 8.0;
        }

        if (!model.subtitle.empty()) {
            ctx->SetFontSize(style.subtitleFontSize);
            ctx->SetTextPaint(style.subtitleColor);
            ctx->DrawText(model.subtitle, Point2Dd(14.0, y));
        }
    }

    void UltraCanvasMatrixDiagram::RenderPanel(IRenderContext* ctx, const PanelLayout& pl) {
        if (pl.rows == 0 || pl.cols == 0) return;

        RenderGrid(ctx, pl);
        RenderHighlight(ctx, pl);
        RenderMarks(ctx, pl);
        RenderRowLabels(ctx, pl);
        RenderColumnLabels(ctx, pl);
        RenderTotals(ctx, pl);
        if (showAxisTitles) RenderAxisTitles(ctx, pl);
    }

    void UltraCanvasMatrixDiagram::RenderGrid(IRenderContext* ctx, const PanelLayout& pl) {
        ctx->SetFillPaint(style.cellBackground);
        FillCells(ctx, pl, 0, 0, pl.rows, pl.cols);

        if (style.alternateRowBands) {
            ctx->SetFillPaint(style.cellAlternateBackground);
            for (int r = 1; r < pl.rows; r += 2) FillCells(ctx, pl, r, 0, 1, pl.cols);
        }

        // The tint is the cell's own level colour, faint, under the mark. A
        // filtered-out level fades with its mark.
        if (style.tintCellsByLevel) {
            const MatrixPanel& panel = model.panels[static_cast<size_t>(pl.panelIndex)];
            for (const MatrixCell& cell : model.cells) {
                if (cell.panel != pl.panelIndex) continue;
                if (cell.row < 0 || cell.row >= pl.rows || cell.col < 0 || cell.col >= pl.cols) continue;
                const MatrixLevel* level = panel.scale.Find(cell.levelId);
                if (!level) continue;
                Color tint = level->color;
                tint.a = IsDimmed(cell.levelId) ? static_cast<uint8_t>(style.cellTintAlpha / 4)
                                                : style.cellTintAlpha;
                ctx->SetFillPaint(tint);
                FillCells(ctx, pl, cell.row, cell.col, 1, 1);
            }
        }

        ctx->SetStrokePaint(style.gridLineColor);
        ctx->SetStrokeWidth(style.gridLineWidth);
        for (int c = 0; c <= pl.cols; ++c) {
            ctx->DrawLine(LatticeAt(pl.origin, 0, pl.rowStep, c, pl.colStep),
                          LatticeAt(pl.origin, pl.rows, pl.rowStep, c, pl.colStep));
        }
        for (int r = 0; r <= pl.rows; ++r) {
            ctx->DrawLine(LatticeAt(pl.origin, r, pl.rowStep, 0, pl.colStep),
                          LatticeAt(pl.origin, r, pl.rowStep, pl.cols, pl.colStep));
        }
    }

    // The hover bands follow the ITEMS, not the panel: hovering a cell lights
    // its row item and its column item wherever either appears, so the
    // cross-hair runs on through a T's other wing or an X's neighbouring
    // quadrant that lists the same item.
    void UltraCanvasMatrixDiagram::RenderHighlight(IRenderContext* ctx, const PanelLayout& pl) {
        if (hovered.IsValid() && hovered.panel >= 0 &&
            hovered.panel < static_cast<int>(model.panels.size())) {
            const MatrixPanel& source = model.panels[static_cast<size_t>(hovered.panel)];
            const MatrixPanel& here = model.panels[static_cast<size_t>(pl.panelIndex)];

            struct Lit { const std::string* setId; int index; };
            std::vector<Lit> lit;
            if (hovered.row >= 0) lit.push_back({&source.rowSetId, hovered.row});
            if (hovered.col >= 0) lit.push_back({&source.colSetId, hovered.col});

            ctx->SetFillPaint(style.hoverBandColor);
            for (const Lit& item : lit) {
                if (here.rowSetId == *item.setId && item.index < pl.rows) {
                    FillCells(ctx, pl, item.index, 0, 1, pl.cols);
                }
                if (here.colSetId == *item.setId && item.index < pl.cols) {
                    FillCells(ctx, pl, 0, item.index, pl.rows, 1);
                }
            }
        }

        if (selected.IsCell() && selected.panel == pl.panelIndex &&
            selected.row < pl.rows && selected.col < pl.cols) {
            ctx->SetFillPaint(style.selectedCellColor);
            FillCells(ctx, pl, selected.row, selected.col, 1, 1);
        }
    }

    void UltraCanvasMatrixDiagram::RenderMarks(IRenderContext* ctx, const PanelLayout& pl) {
        const MatrixPanel& panel = model.panels[static_cast<size_t>(pl.panelIndex)];

        for (const MatrixCell& cell : model.cells) {
            if (cell.panel != pl.panelIndex) continue;
            if (cell.row < 0 || cell.row >= pl.rows) continue;
            if (cell.col < 0 || cell.col >= pl.cols) continue;

            const MatrixLevel* level = panel.scale.Find(cell.levelId);
            if (!level) continue;

            const Rect2Dd rect = MarkBox(pl, cell.row, cell.col);
            DrawMark(ctx, *level, rect, IsDimmed(cell.levelId));

            if (showCellValues) {
                ctx->SetFontSize(std::max(7.0, style.colLabelFontSize - 2.0));
                ctx->SetTextPaint(EffectiveLabelColor());
                const std::string text = FormatTotal(level->weight);
                Size2Di s = ctx->GetTextLineDimensions(text);
                ctx->DrawText(text, Point2Dd(rect.x + (rect.width - s.width) / 2.0,
                                             rect.y + rect.height - s.height - 1.0));
            }
        }
    }

    void UltraCanvasMatrixDiagram::DrawMark(IRenderContext* ctx, const MatrixLevel& level,
                                            const Rect2Dd& cell, bool dimmed) const {
        const double r = std::min(cell.width, cell.height) * style.markSize
                         * 0.5 * std::max(0.1, level.sizeScale);
        if (r <= 0.0) return;

        const Point2Dd c(cell.x + cell.width / 2.0, cell.y + cell.height / 2.0);

        Color color = level.color;
        if (dimmed) color.a = static_cast<uint8_t>(color.a / 5);

        switch (level.shape) {
            case MatrixMarkShape::Ring:
                ctx->SetStrokePaint(color);
                ctx->SetStrokeWidth(style.markStrokeWidth);
                ctx->DrawCircle(c, r);
                break;

            case MatrixMarkShape::Square:
                ctx->SetFillPaint(color);
                ctx->FillRectangle(Rect2Dd(c.x - r, c.y - r, r * 2.0, r * 2.0));
                break;

            case MatrixMarkShape::Triangle: {
                std::vector<Point2Dd> tri = {
                        Point2Dd(c.x, c.y - r),
                        Point2Dd(c.x + r, c.y + r * 0.82),
                        Point2Dd(c.x - r, c.y + r * 0.82)};
                ctx->SetFillPaint(color);
                ctx->FillLinePath(tri);
                break;
            }

            case MatrixMarkShape::Diamond: {
                std::vector<Point2Dd> diamond = {
                        Point2Dd(c.x, c.y - r), Point2Dd(c.x + r, c.y),
                        Point2Dd(c.x, c.y + r), Point2Dd(c.x - r, c.y)};
                ctx->SetFillPaint(color);
                ctx->FillLinePath(diamond);
                break;
            }

            case MatrixMarkShape::Cross:
                ctx->SetStrokePaint(color);
                ctx->SetStrokeWidth(style.markStrokeWidth);
                ctx->DrawLine(Point2Dd(c.x - r, c.y - r), Point2Dd(c.x + r, c.y + r));
                ctx->DrawLine(Point2Dd(c.x - r, c.y + r), Point2Dd(c.x + r, c.y - r));
                break;

            case MatrixMarkShape::Text: {
                if (level.glyph.empty()) break;
                ctx->SetFontSize(r * 2.0);
                ctx->SetTextPaint(color);
                Size2Di s = ctx->GetTextLineDimensions(level.glyph);
                ctx->DrawText(level.glyph, Point2Dd(c.x - s.width / 2.0, c.y - s.height / 2.0));
                break;
            }

            case MatrixMarkShape::Disc:
            default:
                ctx->SetFillPaint(color);
                ctx->FillCircle(c, r);
                break;
        }
    }

    void UltraCanvasMatrixDiagram::RenderRowLabels(IRenderContext* ctx, const PanelLayout& pl) {
        if (pl.rowLabels.width <= 0.0) return;

        const MatrixItemSet* rows = model.RowSet(pl.panelIndex);
        if (!rows) return;

        if (style.fillLabelBands) {
            ctx->SetFillPaint(style.labelBandColor);
            ctx->FillRectangle(pl.rowLabels);
        }

        ctx->SetFontSize(style.rowLabelFontSize);
        const Color textColor = style.fillLabelBands ? style.labelBandTextColor
                                                     : EffectiveLabelColor();

        for (int r = 0; r < pl.rows && r < rows->Count(); ++r) {
            const std::string label = EllipsizeToWidth(
                    ctx, rows->items[static_cast<size_t>(r)].label,
                    pl.rowLabels.width - style.labelPadding * 2.0);

            Size2Di s = ctx->GetTextLineDimensions(label);
            const double y = pl.grid.y + r * pl.cellHeight + (pl.cellHeight - s.height) / 2.0;

            // Right-aligned against the grid on the left wing of a T, which is
            // how the printed form reads: labels lean towards their cells.
            const double x = pl.rowLabelsOnRight
                    ? pl.rowLabels.x + style.labelPadding
                    : pl.rowLabels.x + pl.rowLabels.width - style.labelPadding - s.width;

            ctx->SetTextPaint(textColor);
            ctx->DrawText(label, Point2Dd(x, y));
        }
    }

    void UltraCanvasMatrixDiagram::RenderColumnLabels(IRenderContext* ctx, const PanelLayout& pl) {
        const MatrixItemSet* cols = model.ColSet(pl.panelIndex);
        if (!cols || pl.colLabels.height <= 0.0) return;

        if (style.fillLabelBands && pl.fit == MatrixHeaderFit::Horizontal) {
            ctx->SetFillPaint(style.labelBandColor);
            ctx->FillRectangle(Rect2Dd(pl.colLabels.x,
                                       pl.colLabels.y + pl.colLabels.height
                                           - (style.colLabelFontSize + 12.0),
                                       pl.colLabels.width,
                                       style.colLabelFontSize + 12.0));
        }

        ctx->SetFontSize(style.colLabelFontSize);
        const bool banded = style.fillLabelBands && pl.fit == MatrixHeaderFit::Horizontal;
        ctx->SetTextPaint(banded ? style.labelBandTextColor : EffectiveLabelColor());

        const double bandBottom = pl.colLabels.y + pl.colLabels.height;

        for (int c = 0; c < pl.cols && c < cols->Count(); ++c) {
            const std::string& label = cols->items[static_cast<size_t>(c)].label;
            const double cellCenterX = pl.grid.x + c * pl.cellWidth + pl.cellWidth / 2.0;
            DrawColumnLabel(ctx, label, cellCenterX, bandBottom, pl.colLabels.height,
                            pl.fit, pl.cellWidth, pl.headerLines);
        }
    }

    void UltraCanvasMatrixDiagram::DrawColumnLabel(IRenderContext* ctx, const std::string& label,
                                                   double cellCenterX, double bandBottom,
                                                   double bandHeight, MatrixHeaderFit fit,
                                                   double cellWidth, int lines) {
        switch (fit) {
            case MatrixHeaderFit::Wrapped: {
                std::vector<std::string> wrapped = WrapToLines(
                        ctx, label, cellWidth - 4.0, lines);
                const double lineHeight = style.colLabelFontSize * 1.25;
                double y = bandBottom - wrapped.size() * lineHeight - 4.0;
                for (const std::string& line : wrapped) {
                    Size2Di s = ctx->GetTextLineDimensions(line);
                    ctx->DrawText(line, Point2Dd(cellCenterX - s.width / 2.0, y));
                    y += lineHeight;
                }
                break;
            }

            case MatrixHeaderFit::Rotated45:
            case MatrixHeaderFit::Rotated90: {
                const double angle = (fit == MatrixHeaderFit::Rotated45)
                        ? -M_PI / 4.0 : -M_PI / 2.0;
                const std::string text = EllipsizeToWidth(
                        ctx, label, bandHeight / std::sin(-angle) - 6.0);
                Size2Di s = ctx->GetTextLineDimensions(text);
                ctx->PushState();
                ctx->Translate(cellCenterX, bandBottom - 4.0);
                ctx->Rotate(angle);
                // Draw leftwards from the anchor so the label's tail sits
                // over its own column rather than the next one.
                ctx->DrawText(text, Point2Dd(0.0, -s.height / 2.0));
                ctx->PopState();
                break;
            }

            case MatrixHeaderFit::Horizontal:
            case MatrixHeaderFit::Auto:
            default: {
                const std::string text = EllipsizeToWidth(ctx, label, cellWidth - 4.0);
                Size2Di s = ctx->GetTextLineDimensions(text);
                ctx->DrawText(text, Point2Dd(cellCenterX - s.width / 2.0,
                                             bandBottom - s.height - 6.0));
                break;
            }
        }
    }

    void UltraCanvasMatrixDiagram::RenderArm(IRenderContext* ctx, const ArmLayout& arm) {
        if (arm.setIndex < 0 || arm.setIndex >= static_cast<int>(model.sets.size())) return;
        const MatrixItemSet& set = model.sets[static_cast<size_t>(arm.setIndex)];
        if (arm.count <= 0) return;

        const std::vector<Point2Dd> band = {
                arm.origin,
                LatticeAt(arm.origin, arm.count * arm.itemSize, arm.dir, 0.0, arm.across),
                LatticeAt(arm.origin, arm.count * arm.itemSize, arm.dir, arm.bandWidth, arm.across),
                LatticeAt(arm.origin, 0.0, arm.dir, arm.bandWidth, arm.across)};
        if (style.fillLabelBands) {
            ctx->SetFillPaint(style.labelBandColor);
            ctx->FillLinePath(band);
        }
        // Rule the item boundaries across the band so each label reads as
        // belonging to its own row or column of cells.
        ctx->SetStrokePaint(style.gridLineColor);
        ctx->SetStrokeWidth(style.gridLineWidth);
        for (int i = 0; i <= arm.count; ++i) {
            ctx->DrawLine(LatticeAt(arm.origin, i * arm.itemSize, arm.dir, 0.0, arm.across),
                          LatticeAt(arm.origin, i * arm.itemSize, arm.dir, arm.bandWidth, arm.across));
        }

        const Color textColor = style.fillLabelBands ? style.labelBandTextColor
                                                     : EffectiveLabelColor();
        const bool axisAligned = (arm.dir.x == 0.0 || arm.dir.y == 0.0);

        for (int i = 0; i < arm.count && i < set.Count(); ++i) {
            const std::string& label = set.items[static_cast<size_t>(i)].label;
            const Point2Dd mid = LatticeAt(arm.origin, (i + 0.5) * arm.itemSize, arm.dir,
                                    arm.bandWidth / 2.0, arm.across);

            if (arm.itemsAreColumns) {
                // An X's left or right arm: a column header band between
                // the quadrant above and the one below.
                ctx->SetFontSize(style.colLabelFontSize);
                ctx->SetTextPaint(textColor);
                DrawColumnLabel(ctx, label, mid.x, arm.origin.y + arm.bandWidth,
                                arm.bandWidth, arm.fit, arm.itemSize, arm.headerLines);
                continue;
            }

            ctx->SetFontSize(style.rowLabelFontSize);
            ctx->SetTextPaint(textColor);
            const std::string text = EllipsizeToWidth(ctx, label,
                                                      arm.bandWidth - style.labelPadding * 2.0);
            Size2Di s = ctx->GetTextLineDimensions(text);

            if (axisAligned) {
                ctx->DrawText(text, Point2Dd(mid.x - s.width / 2.0, mid.y - s.height / 2.0));
            } else {
                // A Y seam: the label runs across the band, turned to its
                // direction; `across` never points up, so it stays upright.
                ctx->PushState();
                ctx->Translate(mid.x, mid.y);
                ctx->Rotate(std::atan2(arm.across.y, arm.across.x));
                ctx->DrawText(text, Point2Dd(-s.width / 2.0, -s.height / 2.0));
                ctx->PopState();
            }
        }
    }

    void UltraCanvasMatrixDiagram::RenderArmTitle(IRenderContext* ctx, const ArmLayout& arm) {
        if (arm.setIndex < 0 || arm.setIndex >= static_cast<int>(model.sets.size())) return;
        const std::string& title = model.sets[static_cast<size_t>(arm.setIndex)].title;
        if (title.empty()) return;

        ctx->SetFontSize(style.axisTitleFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(style.axisTitleColor);
        Size2Di s = ctx->GetTextLineDimensions(title);
        if (arm.titleVertical) {
            ctx->PushState();
            ctx->Translate(arm.titleAnchor.x, arm.titleAnchor.y);
            ctx->Rotate(-M_PI / 2.0);
            ctx->DrawText(title, Point2Dd(-s.width / 2.0, -s.height / 2.0));
            ctx->PopState();
        } else {
            ctx->DrawText(title, Point2Dd(arm.titleAnchor.x - s.width / 2.0,
                                          arm.titleAnchor.y - s.height / 2.0));
        }
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasMatrixDiagram::RenderHub(IRenderContext* ctx) {
        const Rect2Dd& hub = layout.hub;
        ctx->SetFillPaint(style.hubBackground);
        ctx->FillRectangle(hub);
        ctx->SetStrokePaint(style.gridLineColor);
        ctx->SetStrokeWidth(style.gridLineWidth);
        ctx->DrawRectangle(hub);

        if (hubMode != MatrixHubMode::Captions) return;

        ctx->SetFontSize(style.hubCaptionFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        for (const HubCaption& caption : layout.hubCaptions) {
            if (caption.setIndex < 0 || caption.setIndex >= static_cast<int>(model.sets.size())) continue;
            const MatrixItemSet& set = model.sets[static_cast<size_t>(caption.setIndex)];
            const std::string title = EllipsizeToWidth(
                    ctx, set.title.empty() ? set.id : set.title, caption.box.width - 8.0);

            ctx->SetFillPaint(set.accent);
            ctx->FillRoundedRectangle(caption.box, 4.0f);
            ctx->SetTextPaint(style.hubCaptionTextColor);
            Size2Di s = ctx->GetTextLineDimensions(title);
            ctx->DrawText(title, Point2Dd(caption.box.x + (caption.box.width - s.width) / 2.0,
                                          caption.box.y + (caption.box.height - s.height) / 2.0));
            DrawArrow(ctx, caption.arrowFrom, caption.arrowTo, style.hubArrowColor);
        }
        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasMatrixDiagram::RenderHeaderBand(IRenderContext* ctx) {
        if (layout.headerFields.empty()) return;

        ctx->SetFillPaint(style.headerBandBackground);
        ctx->FillRectangle(layout.headerBand);
        ctx->SetStrokePaint(style.headerBandBorderColor);
        ctx->SetStrokeWidth(1.0f);

        const double padX = 8.0;
        for (const HeaderField& hf : layout.headerFields) {
            if (hf.field < 0 || hf.field >= static_cast<int>(model.headerFields.size())) continue;
            const MatrixHeaderField& field = model.headerFields[static_cast<size_t>(hf.field)];
            ctx->DrawRectangle(hf.box);

            const double maxW = hf.box.width - padX * 2.0;
            ctx->SetFontSize(style.headerFieldLabelFontSize);
            ctx->SetFontWeight(FontWeight::Bold);
            ctx->SetTextPaint(style.headerFieldLabelColor);
            ctx->DrawText(EllipsizeToWidth(ctx, field.label, maxW),
                          Point2Dd(hf.box.x + padX, hf.box.y + 5.0));
            ctx->SetFontWeight(FontWeight::Normal);

            ctx->SetFontSize(style.headerFieldValueFontSize);
            ctx->SetTextPaint(style.headerFieldValueColor);
            ctx->DrawText(EllipsizeToWidth(ctx, field.value, maxW),
                          Point2Dd(hf.box.x + padX,
                                   hf.box.y + 7.0 + style.headerFieldLabelFontSize * 1.3));
        }
    }

    void UltraCanvasMatrixDiagram::RenderAxisTitles(IRenderContext* ctx, const PanelLayout& pl) {
        const MatrixItemSet* rows = model.RowSet(pl.panelIndex);
        const MatrixItemSet* cols = model.ColSet(pl.panelIndex);

        ctx->SetFontSize(style.axisTitleFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(style.axisTitleColor);

        // X and Y panels have no bands of their own - their arms carry the
        // labels and the set titles - so they draw no axis titles here.
        if (cols && !cols->title.empty() && pl.colLabels.width > 0.0) {
            Size2Di s = ctx->GetTextLineDimensions(cols->title);
            ctx->DrawText(cols->title,
                          Point2Dd(pl.grid.x + (pl.grid.width - s.width) / 2.0,
                                   pl.colLabels.y - s.height - 2.0));
        }

        // The row axis title is only drawn by the panel that owns the labels;
        // a T's right wing shares the left wing's spine.
        if (rows && !rows->title.empty() && pl.rowLabels.width > 0.0) {
            Size2Di s = ctx->GetTextLineDimensions(rows->title);
            ctx->PushState();
            ctx->Translate(pl.rowLabels.x - 6.0, pl.grid.y + pl.grid.height / 2.0);
            ctx->Rotate(-M_PI / 2.0);
            ctx->DrawText(rows->title, Point2Dd(-s.width / 2.0, -s.height));
            ctx->PopState();
        }

        ctx->SetFontWeight(FontWeight::Normal);
    }

    void UltraCanvasMatrixDiagram::RenderTotals(IRenderContext* ctx, const PanelLayout& pl) {
        ctx->SetFontSize(style.totalsFontSize);

        if (pl.rowTotals.width > 0.0) {
            ctx->SetFillPaint(style.totalsBackground);
            ctx->FillRectangle(pl.rowTotals);
            ctx->SetStrokePaint(style.gridLineColor);
            ctx->SetStrokeWidth(style.gridLineWidth);
            ctx->DrawRectangle(pl.rowTotals);

            ctx->SetTextPaint(style.totalsTextColor);
            for (int r = 0; r < pl.rows; ++r) {
                const std::string text = FormatTotal(model.RowScore(pl.panelIndex, r));
                Size2Di s = ctx->GetTextLineDimensions(text);
                ctx->DrawText(text,
                              Point2Dd(pl.rowTotals.x + (pl.rowTotals.width - s.width) / 2.0,
                                       pl.grid.y + r * pl.cellHeight
                                           + (pl.cellHeight - s.height) / 2.0));
            }
        }

        if (pl.colTotals.height > 0.0) {
            ctx->SetFillPaint(style.totalsBackground);
            ctx->FillRectangle(pl.colTotals);
            ctx->SetStrokePaint(style.gridLineColor);
            ctx->SetStrokeWidth(style.gridLineWidth);
            ctx->DrawRectangle(pl.colTotals);

            ctx->SetTextPaint(style.totalsTextColor);
            for (int c = 0; c < pl.cols; ++c) {
                const std::string text = FormatTotal(model.ColumnScore(pl.panelIndex, c));
                Size2Di s = ctx->GetTextLineDimensions(text);
                ctx->DrawText(text,
                              Point2Dd(pl.grid.x + c * pl.cellWidth
                                           + (pl.cellWidth - s.width) / 2.0,
                                       pl.colTotals.y
                                           + (pl.colTotals.height - s.height) / 2.0));
            }
        }
    }

// =============================================================================
// HIT TESTING
// =============================================================================

    std::vector<Point2Dd> UltraCanvasMatrixDiagram::CellQuad(const PanelLayout& pl, int row,
                                                             int col, int rows, int cols) const {
        return {LatticeAt(pl.origin, row, pl.rowStep, col, pl.colStep),
                LatticeAt(pl.origin, row, pl.rowStep, col + cols, pl.colStep),
                LatticeAt(pl.origin, row + rows, pl.rowStep, col + cols, pl.colStep),
                LatticeAt(pl.origin, row + rows, pl.rowStep, col, pl.colStep)};
    }

    void UltraCanvasMatrixDiagram::FillCells(IRenderContext* ctx, const PanelLayout& pl,
                                             int row, int col, int rows, int cols) const {
        if (rows <= 0 || cols <= 0) return;
        const std::vector<Point2Dd> quad = CellQuad(pl, row, col, rows, cols);
        if (pl.axisAligned) {
            // A rectangle keeps pixel-aligned edges crisp where a path would
            // be antialiased.
            const double x0 = std::min(quad[0].x, quad[2].x), x1 = std::max(quad[0].x, quad[2].x);
            const double y0 = std::min(quad[0].y, quad[2].y), y1 = std::max(quad[0].y, quad[2].y);
            ctx->FillRectangle(Rect2Dd(x0, y0, x1 - x0, y1 - y0));
        } else {
            ctx->FillLinePath(quad);
        }
    }

    Rect2Dd UltraCanvasMatrixDiagram::MarkBox(const PanelLayout& pl, int row, int col) const {
        const std::vector<Point2Dd> quad = CellQuad(pl, row, col, 1, 1);
        if (pl.axisAligned) {
            const double x0 = std::min(quad[0].x, quad[2].x), x1 = std::max(quad[0].x, quad[2].x);
            const double y0 = std::min(quad[0].y, quad[2].y), y1 = std::max(quad[0].y, quad[2].y);
            return Rect2Dd(x0, y0, x1 - x0, y1 - y0);
        }
        // The inscribed circle of a parallelogram has the diameter of its
        // shorter height: |u x v| / longer side.
        const double area = std::fabs(VecCross(pl.rowStep, pl.colStep));
        const double side = std::max(VecLength(pl.rowStep), VecLength(pl.colStep));
        const double size = side > 0.0 ? area / side : 0.0;
        const Point2Dd c = LatticeAt(pl.origin, row + 0.5, pl.rowStep, col + 0.5, pl.colStep);
        return Rect2Dd(c.x - size / 2.0, c.y - size / 2.0, size, size);
    }

    MatrixRef UltraCanvasMatrixDiagram::ArmRef(int setIndex, int item) const {
        if (setIndex < 0 || setIndex >= static_cast<int>(model.sets.size())) return MatrixRef();
        const std::string& setId = model.sets[static_cast<size_t>(setIndex)].id;
        for (const PanelLayout& pl : layout.panels) {
            const MatrixPanel& panel = model.panels[static_cast<size_t>(pl.panelIndex)];
            MatrixRef ref;
            ref.panel = pl.panelIndex;
            if (panel.rowSetId == setId && item < pl.rows) { ref.row = item; return ref; }
            if (panel.colSetId == setId && item < pl.cols) { ref.col = item; return ref; }
        }
        return MatrixRef();
    }

    const UltraCanvasMatrixDiagram::PanelLayout*
    UltraCanvasMatrixDiagram::PanelAt(int panelIndex) const {
        for (const PanelLayout& pl : layout.panels) {
            if (pl.panelIndex == panelIndex) return &pl;
        }
        return nullptr;
    }

    MatrixRef UltraCanvasMatrixDiagram::HitTest(const Point2Di& point) const {
        const double px = static_cast<double>(point.x);
        const double py = static_cast<double>(point.y);

        for (const PanelLayout& pl : layout.panels) {
            if (pl.cellWidth <= 0.0 || pl.cellHeight <= 0.0) continue;

            // The cells, through the inverse of the lattice - the one test
            // that serves an L, a turned X quadrant and a Y rhombus alike.
            double a = 0.0, b = 0.0;
            if (Unproject(Point2Dd(px, py), pl.origin, pl.rowStep, pl.colStep, a, b) &&
                a >= 0.0 && a < pl.rows && b >= 0.0 && b < pl.cols) {
                MatrixRef ref;
                ref.panel = pl.panelIndex;
                ref.row = std::clamp(static_cast<int>(a), 0, pl.rows - 1);
                ref.col = std::clamp(static_cast<int>(b), 0, pl.cols - 1);
                return ref;
            }
            if (!pl.axisAligned) continue;

            const bool inGridX = px >= pl.grid.x && px < pl.grid.x + pl.grid.width;
            const bool inGridY = py >= pl.grid.y && py < pl.grid.y + pl.grid.height;


            // Row label band -> the whole row.
            if (pl.rowLabels.width > 0.0 && inGridY &&
                px >= pl.rowLabels.x && px < pl.rowLabels.x + pl.rowLabels.width) {
                MatrixRef ref;
                ref.panel = pl.panelIndex;
                ref.row = std::clamp(static_cast<int>((py - pl.grid.y) / pl.cellHeight),
                                     0, pl.rows - 1);
                return ref;
            }

            // Column header band -> the whole column.
            if (inGridX && pl.colLabels.height > 0.0 &&
                py >= pl.colLabels.y && py < pl.colLabels.y + pl.colLabels.height) {
                MatrixRef ref;
                ref.panel = pl.panelIndex;
                ref.col = std::clamp(static_cast<int>((px - pl.grid.x) / pl.cellWidth),
                                     0, pl.cols - 1);
                return ref;
            }
        }

        // The X and Y label bands: an item there is a whole row or column of
        // the first panel that lists it.
        for (const ArmLayout& arm : layout.arms) {
            if (arm.itemSize <= 0.0) continue;
            const Point2Dd d(px - arm.origin.x, py - arm.origin.y);
            const double along = VecDot(d, arm.dir);
            const double across = VecDot(d, arm.across);
            if (across < 0.0 || across >= arm.bandWidth) continue;
            if (along < 0.0 || along >= arm.count * arm.itemSize) continue;
            return ArmRef(arm.setIndex, static_cast<int>(along / arm.itemSize));
        }
        return MatrixRef();
    }

    bool UltraCanvasMatrixDiagram::HandleChartMouseMove(const Point2Di& mousePos) {
        if (!layout.valid) return false;

        MatrixRef found = HitTest(mousePos);
        if (found != hovered) {
            hovered = found;
            if (hovered.IsValid() && onCellHover) onCellHover(hovered);
            RequestRedraw();
        }

        if (enableTooltips) {
            if (hovered.IsValid()) {
                Point2Di tipPos(mousePos.x + 15, mousePos.y - 20);
                auto windowPos = MapFromLocal(tipPos, nullptr);
                UltraCanvasTooltipManager::UpdateAndShowTooltip(
                        GetWindow(), BuildTooltip(hovered), windowPos);
            } else {
                UltraCanvasTooltipManager::HideTooltip();
            }
        }

        return hovered.IsValid();
    }

    bool UltraCanvasMatrixDiagram::OnEvent(const UCEvent& event) {
        if (IsDisabled() || !IsVisible()) return false;

        switch (event.type) {
            case UCEventType::MouseDown: {
                if (event.button != UCMouseButton::Left) break;

                const Point2Di point(event.pointer.x, event.pointer.y);

                // A legend click filters the marks by level.
                if (legend && legend->IsVisible()) {
                    const size_t entry = legend->HitTest(
                            Point2Dd(static_cast<double>(point.x),
                                     static_cast<double>(point.y)));
                    if (entry != SIZE_MAX && entry < legend->GetEntryCount()) {
                        const std::string& id = legend->GetEntry(entry).id;
                        SetLevelFilter(levelFilter == id ? std::string() : id);
                        legend->SetHighlightedEntry(levelFilter.empty() ? SIZE_MAX : entry);
                        return true;
                    }
                }

                MatrixRef clicked = HitTest(point);
                if (!clicked.IsValid()) break;

                if (clicked.IsCell()) {
                    selected = (clicked == selected) ? MatrixRef() : clicked;
                    if (onCellClick) onCellClick(clicked);
                } else if (clicked.row >= 0 && onRowClick) {
                    const MatrixItemSet* rows = model.RowSet(clicked.panel);
                    if (rows && clicked.row < rows->Count()) {
                        onRowClick(clicked.panel, clicked.row,
                                   rows->items[static_cast<size_t>(clicked.row)]);
                    }
                } else if (clicked.col >= 0 && onColumnClick) {
                    const MatrixItemSet* cols = model.ColSet(clicked.panel);
                    if (cols && clicked.col < cols->Count()) {
                        onColumnClick(clicked.panel, clicked.col,
                                      cols->items[static_cast<size_t>(clicked.col)]);
                    }
                }
                RequestRedraw();
                return true;
            }

            case UCEventType::MouseLeave:
                if (hovered.IsValid()) {
                    hovered = MatrixRef();
                    if (enableTooltips) UltraCanvasTooltipManager::HideTooltip();
                    RequestRedraw();
                }
                break;

            default:
                break;
        }

        return UltraCanvasChartElementBase::OnEvent(event);
    }

// =============================================================================
// HELPERS
// =============================================================================

    bool UltraCanvasMatrixDiagram::IsDimmed(const std::string& levelId) const {
        return !levelFilter.empty() && levelFilter != levelId;
    }

    Color UltraCanvasMatrixDiagram::EffectiveLabelColor() const {
        return style.labelColor;
    }

    std::string UltraCanvasMatrixDiagram::FormatTotal(double value) const {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.*f",
                      std::max(0, style.totalsDecimals), value);
        return std::string(buffer);
    }

    std::string UltraCanvasMatrixDiagram::EllipsizeToWidth(IRenderContext* ctx,
                                                           const std::string& text,
                                                           double maxWidth) const {
        if (maxWidth <= 0.0 || text.empty()) return text;
        if (ctx->GetTextLineWidth(text) <= maxWidth) return text;

        // Trim on byte boundaries but never split a UTF-8 sequence.
        std::string trimmed = text;
        while (!trimmed.empty()) {
            do {
                trimmed.pop_back();
            } while (!trimmed.empty() &&
                     (static_cast<unsigned char>(trimmed.back()) & 0xC0) == 0x80);

            if (ctx->GetTextLineWidth(trimmed + "...") <= maxWidth) break;
        }
        return trimmed.empty() ? std::string("...") : trimmed + "...";
    }

    std::vector<std::string> UltraCanvasMatrixDiagram::WrapToLines(IRenderContext* ctx,
                                                                   const std::string& text,
                                                                   double maxWidth,
                                                                   int maxLines) const {
        std::vector<std::string> lines;
        if (maxLines <= 0) return lines;

        std::istringstream words(text);
        std::string word;
        std::string current;

        while (words >> word) {
            const std::string candidate = current.empty() ? word : current + " " + word;
            if (current.empty() || ctx->GetTextLineWidth(candidate) <= maxWidth) {
                current = candidate;
                continue;
            }
            lines.push_back(current);
            current = word;
            if (static_cast<int>(lines.size()) == maxLines) break;
        }

        if (!current.empty() && static_cast<int>(lines.size()) < maxLines) {
            lines.push_back(current);
        }

        // Anything that did not fit is ellipsised into the last line rather
        // than silently dropped.
        if (!lines.empty()) {
            lines.back() = EllipsizeToWidth(ctx, lines.back(), maxWidth);
        }
        return lines;
    }

    std::string UltraCanvasMatrixDiagram::BuildTooltip(const MatrixRef& ref) const {
        if (!ref.IsValid()) return std::string();

        const MatrixItemSet* rows = model.RowSet(ref.panel);
        const MatrixItemSet* cols = model.ColSet(ref.panel);
        if (!rows || !cols) return std::string();

        std::ostringstream out;

        if (ref.IsCell()) {
            if (ref.row >= rows->Count() || ref.col >= cols->Count()) return std::string();
            out << rows->items[static_cast<size_t>(ref.row)].label
                << "  x  " << cols->items[static_cast<size_t>(ref.col)].label;

            const MatrixCell* cell = model.FindCell(ref.panel, ref.row, ref.col);
            if (cell) {
                const MatrixLevel* level =
                        model.panels[static_cast<size_t>(ref.panel)].scale.Find(cell->levelId);
                out << "\n" << (level ? level->label : cell->levelId);
                if (level) out << " (" << FormatTotal(level->weight) << ")";
                if (!cell->note.empty()) out << "\n" << cell->note;
            } else {
                out << "\nno relationship";
            }
        } else if (ref.row >= 0 && ref.row < rows->Count()) {
            const MatrixItem& item = rows->items[static_cast<size_t>(ref.row)];
            out << item.label << "\ntotal " << FormatTotal(model.RowScore(ref.panel, ref.row));
            if (!item.note.empty()) out << "\n" << item.note;
        } else if (ref.col >= 0 && ref.col < cols->Count()) {
            const MatrixItem& item = cols->items[static_cast<size_t>(ref.col)];
            out << item.label << "\ntotal "
                << FormatTotal(model.ColumnScore(ref.panel, ref.col));
            if (!item.note.empty()) out << "\n" << item.note;
        }

        return out.str();
    }

// =============================================================================
// FACTORY
// =============================================================================

    std::shared_ptr<UltraCanvasMatrixDiagram> CreateMatrixDiagramElement(
            const std::string& id, int x, int y, int width, int height) {
        return std::make_shared<UltraCanvasMatrixDiagram>(id, x, y, width, height);
    }

    std::shared_ptr<UltraCanvasMatrixDiagram> CreateMatrixDiagramElement(
            const std::string& id, int x, int y, int width, int height,
            const MatrixModel& model) {
        auto element = std::make_shared<UltraCanvasMatrixDiagram>(id, x, y, width, height);
        element->SetModel(model);
        return element;
    }

} // namespace UltraCanvas
