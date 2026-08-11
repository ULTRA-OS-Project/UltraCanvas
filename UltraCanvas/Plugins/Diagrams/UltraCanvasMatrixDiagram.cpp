// Plugins/Diagrams/UltraCanvasMatrixDiagram.cpp
// Matrix diagram element: L and T arrangements, ordinal cell marks, roll-ups
// Version: 1.0.0
// Last Modified: 2026-08-11
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
    }

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
        InvalidateLayout();
        RequestRedraw();
    }

    MatrixItemSet& UltraCanvasMatrixDiagram::AddSet(const std::string& setId,
                                                    const std::string& title,
                                                    const std::vector<std::string>& itemLabels) {
        InvalidateLayout();
        RequestRedraw();
        return model.AddSet(setId, title, itemLabels);
    }

    MatrixPanel& UltraCanvasMatrixDiagram::AddPanel(const std::string& rowSetId,
                                                    const std::string& colSetId,
                                                    const MatrixScale& scale) {
        MatrixPanel& panel = model.AddPanel(rowSetId, colSetId, scale);
        RebuildLegend();
        InvalidateLayout();
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
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetSubtitle(const std::string& text) {
        model.subtitle = text;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::Clear() {
        model.Clear();
        hovered = MatrixRef();
        selected = MatrixRef();
        levelFilter.clear();
        RebuildLegend();
        InvalidateLayout();
        RequestRedraw();
    }

// =============================================================================
// PRESENTATION
// =============================================================================

    void UltraCanvasMatrixDiagram::SetStyle(const MatrixDiagramStyle& s) {
        style = s;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetDarkTheme(bool dark) {
        if (darkTheme == dark) return;
        darkTheme = dark;
        if (dark) {
            style.ApplyDarkTheme();
            backgroundColor = Color(30, 33, 40, 255);
        } else {
            style = MatrixDiagramStyle();
            backgroundColor = Color(252, 252, 253, 255);
        }
        RebuildLegend();
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetHeaderFit(MatrixHeaderFit fit) {
        if (headerFit == fit) return;
        headerFit = fit;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetTotals(MatrixTotals which) {
        if (totals == which) return;
        totals = which;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetShowAxisTitles(bool on) {
        if (showAxisTitles == on) return;
        showAxisTitles = on;
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetShowLegend(bool on) {
        if (showLegend == on) return;
        showLegend = on;
        if (legend) legend->SetVisible(on);
        InvalidateLayout();
        RequestRedraw();
    }

    void UltraCanvasMatrixDiagram::SetLegendPosition(ChartLegendPosition position) {
        if (legend) legend->SetPosition(position);
        InvalidateLayout();
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

    void UltraCanvasMatrixDiagram::InvalidateLayout() {
        layout.valid = false;
        layout.panels.clear();
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

    double UltraCanvasMatrixDiagram::MeasureHeaderBand(IRenderContext* ctx, int panelIndex,
                                                       MatrixHeaderFit fit,
                                                       double cellWidth) const {
        const MatrixItemSet* cols = model.ColSet(panelIndex);
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
                                                               int panelIndex,
                                                               double cellWidth,
                                                               double bandAllowance) const {
        if (headerFit != MatrixHeaderFit::Auto) return headerFit;

        const MatrixItemSet* cols = model.ColSet(panelIndex);
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
        pl.fit = ResolveHeaderFit(ctx, 0, pl.cellWidth, bandAllowance);
        pl.headerLines = (pl.fit == MatrixHeaderFit::Wrapped) ? 3 : 1;

        double bandHeight = std::min(bandAllowance,
                                     MeasureHeaderBand(ctx, 0, pl.fit, pl.cellWidth));

        const double gridTop = area.y + bandHeight + axisTitleSpace;
        const double gridAvailH = area.y + area.height - gridTop
                                  - (colTotals ? style.totalsGutterSize : 0.0);
        pl.cellHeight = FitCellSize(gridAvailH, pl.rows);

        pl.grid = Rect2Dd(gridLeft, gridTop,
                          pl.cellWidth * pl.cols, pl.cellHeight * pl.rows);
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
        const double cellWidth = FitCellSize(totalWidth, totalCols);

        // Same single-pass order as the L: width, then fit, then band height,
        // with the grid holding first claim on the vertical space.
        const double gridFloor = left.rows * style.minCellSize + axisTitleSpace;
        const double bandAllowance = ClampSafe(area.height - gridFloor,
                                               28.0, style.maxHeaderBandHeight);
        left.fit = ResolveHeaderFit(ctx, 0, cellWidth, bandAllowance);
        right.fit = ResolveHeaderFit(ctx, 1, cellWidth, bandAllowance);

        // Both wings share one band, so it must satisfy the taller of the two.
        const double bandHeight = std::min(bandAllowance,
                std::max(MeasureHeaderBand(ctx, 0, left.fit, cellWidth),
                         MeasureHeaderBand(ctx, 1, right.fit, cellWidth)));

        const double gridTop = area.y + bandHeight + axisTitleSpace;
        const double gridAvailH = area.y + area.height - gridTop;
        const double cellHeight = FitCellSize(gridAvailH, left.rows);

        const double leftGridW = cellWidth * left.cols;
        const double rightGridW = cellWidth * right.cols;

        // Centre the whole assembly horizontally.
        const double assemblyW = leftGridW + spineWidth + rightGridW + style.panelGap * 2.0;
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

        layout.panels.push_back(left);
        layout.panels.push_back(right);
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
                case MatrixShape::L:
                case MatrixShape::Y:
                case MatrixShape::X:
                case MatrixShape::House:
                default:
                    // Y, X and House are not implemented yet; they fall back to
                    // the L layout of panel 0 rather than drawing nothing, so a
                    // caller who sets them early still sees their data.
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
        for (const PanelLayout& pl : layout.panels) {
            RenderPanel(ctx, pl);
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
        ctx->FillRectangle(pl.grid);

        if (style.alternateRowBands) {
            ctx->SetFillPaint(style.cellAlternateBackground);
            for (int r = 1; r < pl.rows; r += 2) {
                ctx->FillRectangle(Rect2Dd(pl.grid.x, pl.grid.y + r * pl.cellHeight,
                                           pl.grid.width, pl.cellHeight));
            }
        }

        ctx->SetStrokePaint(style.gridLineColor);
        ctx->SetStrokeWidth(style.gridLineWidth);
        for (int c = 0; c <= pl.cols; ++c) {
            const double x = pl.grid.x + c * pl.cellWidth;
            ctx->DrawLine(Point2Dd(x, pl.grid.y), Point2Dd(x, pl.grid.y + pl.grid.height));
        }
        for (int r = 0; r <= pl.rows; ++r) {
            const double y = pl.grid.y + r * pl.cellHeight;
            ctx->DrawLine(Point2Dd(pl.grid.x, y), Point2Dd(pl.grid.x + pl.grid.width, y));
        }
    }

    void UltraCanvasMatrixDiagram::RenderHighlight(IRenderContext* ctx, const PanelLayout& pl) {
        if (hovered.IsValid() && hovered.panel == pl.panelIndex) {
            ctx->SetFillPaint(style.hoverBandColor);
            if (hovered.row >= 0 && hovered.row < pl.rows) {
                ctx->FillRectangle(Rect2Dd(pl.grid.x, pl.grid.y + hovered.row * pl.cellHeight,
                                           pl.grid.width, pl.cellHeight));
            }
            if (hovered.col >= 0 && hovered.col < pl.cols) {
                ctx->FillRectangle(Rect2Dd(pl.grid.x + hovered.col * pl.cellWidth, pl.grid.y,
                                           pl.cellWidth, pl.grid.height));
            }
        }

        if (selected.IsCell() && selected.panel == pl.panelIndex &&
            selected.row < pl.rows && selected.col < pl.cols) {
            ctx->SetFillPaint(style.selectedCellColor);
            ctx->FillRectangle(CellRect(pl, selected.row, selected.col));
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

            const Rect2Dd rect = CellRect(pl, cell.row, cell.col);
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

            switch (pl.fit) {
                case MatrixHeaderFit::Wrapped: {
                    std::vector<std::string> lines = WrapToLines(
                            ctx, label, pl.cellWidth - 4.0, pl.headerLines);
                    const double lineHeight = style.colLabelFontSize * 1.25;
                    double y = bandBottom - lines.size() * lineHeight - 4.0;
                    for (const std::string& line : lines) {
                        Size2Di s = ctx->GetTextLineDimensions(line);
                        ctx->DrawText(line, Point2Dd(cellCenterX - s.width / 2.0, y));
                        y += lineHeight;
                    }
                    break;
                }

                case MatrixHeaderFit::Rotated45:
                case MatrixHeaderFit::Rotated90: {
                    const double angle = (pl.fit == MatrixHeaderFit::Rotated45)
                            ? -M_PI / 4.0 : -M_PI / 2.0;
                    const std::string text = EllipsizeToWidth(
                            ctx, label, pl.colLabels.height / std::sin(-angle) - 6.0);
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
                    const std::string text = EllipsizeToWidth(ctx, label, pl.cellWidth - 4.0);
                    Size2Di s = ctx->GetTextLineDimensions(text);
                    ctx->DrawText(text, Point2Dd(cellCenterX - s.width / 2.0,
                                                 bandBottom - s.height - 6.0));
                    break;
                }
            }
        }
    }

    void UltraCanvasMatrixDiagram::RenderAxisTitles(IRenderContext* ctx, const PanelLayout& pl) {
        const MatrixItemSet* rows = model.RowSet(pl.panelIndex);
        const MatrixItemSet* cols = model.ColSet(pl.panelIndex);

        ctx->SetFontSize(style.axisTitleFontSize);
        ctx->SetFontWeight(FontWeight::Bold);
        ctx->SetTextPaint(style.axisTitleColor);

        if (cols && !cols->title.empty()) {
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

    Rect2Dd UltraCanvasMatrixDiagram::CellRect(const PanelLayout& pl, int row, int col) const {
        return Rect2Dd(pl.grid.x + col * pl.cellWidth,
                       pl.grid.y + row * pl.cellHeight,
                       pl.cellWidth, pl.cellHeight);
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

            const bool inGridX = px >= pl.grid.x && px < pl.grid.x + pl.grid.width;
            const bool inGridY = py >= pl.grid.y && py < pl.grid.y + pl.grid.height;

            if (inGridX && inGridY) {
                MatrixRef ref;
                ref.panel = pl.panelIndex;
                ref.row = static_cast<int>((py - pl.grid.y) / pl.cellHeight);
                ref.col = static_cast<int>((px - pl.grid.x) / pl.cellWidth);
                ref.row = std::clamp(ref.row, 0, pl.rows - 1);
                ref.col = std::clamp(ref.col, 0, pl.cols - 1);
                return ref;
            }

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
