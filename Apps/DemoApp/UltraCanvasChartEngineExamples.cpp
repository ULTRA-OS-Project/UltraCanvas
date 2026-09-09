// Apps/DemoApp/UltraCanvasChartEngineExamples.cpp
// Chart engine showcase: ONE minimal bar-chart content class - DescribeAxes,
// RenderChartContent and a label collector - driven from an option panel that
// exercises the engine's whole surrounding API. Everything the options switch
// (projections, axis scales including logarithmic, tick and number formatting,
// grid source, limiters, legend swatches, the label plan, hit regions and
// tooltips, the animation driver, the dirty model, named properties) is a
// service of UltraCanvasChartEngineElement, not of this chart.
// Version: 2.1.0
// Last Modified: 2026-08-22
// V2.1.0: The option rows are grouped into an UltraCanvasTabbedContainer (Plot /
//   Axes / Data / Annotations / Legend / Theme), the legend tab covers the whole
//   shared ChartLegend surface (all 16 placements, orientation, title, entry
//   values, the max-entries overflow row, the box style, the continuous keys and
//   the custom key area), and every option button sizes itself to its own text
//   instead of a hand-guessed pixel width.
// V2.0.0: Replaced the three static charts (vertical / horizontal / polar) with
//   a single live chart plus album-style radio option rows covering the engine
//   API. The projection row still shows all three projections from the one
//   content implementation; the second and third chart are gone.
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/Engine/UltraCanvasChartSeries.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {

namespace {

// =============================================================================
// DATA
// =============================================================================

struct EngineDataset {
    std::string name;
    std::string caption;                        // what it is good for
    std::vector<std::string> categories;
    std::vector<std::string> seriesNames;
    std::vector<std::vector<double>> values;    // one vector per series
};

// Three shapes of data, because the axis scales answer different questions:
// an ordinary positive set, one spanning six decades (what a Log axis is for)
// and one with negatives (what SymLog and a diverging stack are for).
std::vector<EngineDataset> MakeDatasets() {
    std::vector<EngineDataset> sets;

    EngineDataset revenue;
    revenue.name = "Revenue";
    revenue.caption = "ordinary positive data - the linear-scale case";
    revenue.categories = {"Q1", "Q2", "Q3", "Q4", "Q5e"};
    revenue.seriesNames = {"Product", "Services", "Licences"};
    revenue.values = {{380000, 545000, 470000, 690000, 615000},
                      {120000, 160000, 205000, 240000, 275000},
                      { 60000,  75000,  90000, 130000, 150000}};
    sets.push_back(revenue);

    EngineDataset decades;
    decades.name = "Wide range";
    decades.caption = "six decades of traffic - unreadable on a linear axis, "
                      "one tick per decade on Log";
    decades.categories = {"Idle", "Web", "API", "Batch", "Peak"};
    decades.seriesNames = {"Requests", "Retries", "Errors"};
    decades.values = {{12, 340, 5600, 74000, 1250000},
                      { 5, 120, 1900, 21000,  380000},
                      { 2,  17,  260,  3100,   41000}};
    sets.push_back(decades);

    EngineDataset signed_;
    signed_.name = "Signed";
    signed_.caption = "positive and negative values - SymLog keeps the sign, "
                      "stacks diverge around zero";
    signed_.categories = {"Q1", "Q2", "Q3", "Q4", "Q5e"};
    signed_.seriesNames = {"Cashflow", "Adjustments", "Hedging"};
    signed_.values = {{ 120000, -80000, 260000, -45000, 310000},
                      { -40000,  55000, -95000, 130000, -60000},
                      {  25000,  30000, -15000,  40000,  35000}};
    sets.push_back(signed_);

    return sets;
}

Color Lighten(const Color& c, int amount) {
    return Color(static_cast<uint8_t>(std::min(255, c.r + amount)),
                 static_cast<uint8_t>(std::min(255, c.g + amount)),
                 static_cast<uint8_t>(std::min(255, c.b + amount)),
                 c.a);
}

// =============================================================================
// THE ENTIRE CHART. A native engine chart implements phase 2 and three small
// descriptors; everything the option panel below switches is engine-supplied.
// =============================================================================
class EngineFeatureChart : public UltraCanvasChartEngineElement {
public:
    enum class NumberFormat { CompactDollars, Plain, TwoDecimals, PercentSuffix, CustomFormatter };
    // X11's Xlib.h #defines None, so the "no grid" case is named NoGrid
    // (the framework's ChartDirty::Clean is spelled that way for the same reason).
    enum class GridSource { ValueAxis, CategoryAxis, NoGrid };
    enum class LimiterMode { NoLimiter, Average, AverageAndTarget, Thresholds };
    enum class HighlightMode { NoHighlight, Band, Ellipse, Blob };
    enum class ValueLabelMode { All, Declutter, Off };
    // How the bars are painted. The legend swatch follows this, which is what
    // LegendSwatch is for: a series drawn with a non-solid fill must not
    // be represented by a colour square that matches nothing.
    enum class SeriesPaint { Solid, Gradient, Outline, Hatched };
    // The legend's placement is two independent choices - which edge it takes
    // and where along that edge it sits - because ChartLegendPosition spells
    // out all twelve outside combinations. The four insets are a third choice
    // that overrides both.
    enum class LegendSide { Top, Bottom, Left, Right };
    enum class LegendAlign { Start, Center, End };

    EngineFeatureChart(const std::string& id, int x, int y, int w, int h)
        : UltraCanvasChartEngineElement(id, x, y, w, h) {
        SetEnableTooltips(true);
        SetAnimateOnDataChange(true, 0.8f);
    }

    // Which route the title takes to the engine - the same string either way,
    // so the two are directly comparable.
    enum class TitleMode { Direct, ViaProperty, NoTitle };

    // ---- data --------------------------------------------------------------
    void SetDataset(const EngineDataset& set) {
        categoryNames = set.categories;
        seriesNames = set.seriesNames;
        seriesValues = set.values;
        seriesDisabled.assign(seriesNames.size(), false);
        datasetName = set.name;
        ApplyTitle();                           // the title names the data
        RebuildLimiters();
        RebuildHighlights();
        RebuildLegend();
        MarkEngineDirty(ChartDirty::Data);      // rebuilds axes, layout and plan
    }

    void SetTitleMode(TitleMode mode) { titleMode = mode; ApplyTitle(); }

    // Re-jitters the current values (+/-30%) so the dirty model and the
    // animation driver can be watched without changing anything else.
    void ShuffleValues() {
        for (auto& series : seriesValues) {
            for (double& v : series) {
                shuffleState = shuffleState * 6364136223846793005ULL + 1442695040888963407ULL;
                const double f = 0.7 + 0.6 * static_cast<double>(
                        (shuffleState >> 33) % 1000u) / 1000.0;
                v *= f;
            }
        }
        RebuildLimiters();
        RebuildHighlights();
        MarkEngineDirty(ChartDirty::Data);
    }

    // ---- engine-facing options ---------------------------------------------
    void SetValueScale(ChartScale scale) { valueScale = scale; Restyle(); }
    void SetArrangement(ChartBarArrangement a) { arrangement = a; RebuildLimiters(); Restyle(); }
    void SetBarCornerRadius(double px) { barCornerRadius = px; RequestRedraw(); }
    void SetInvertValueAxis(bool on) { invertValueAxis = on; Restyle(); }
    void SetAxesAtFarSide(bool on) { axesAtFarSide = on; Restyle(); }
    void SetNumberFormat(NumberFormat f) { numberFormat = f; Restyle(); }
    void SetSlotFill(double fill) { slotFill = fill; Restyle(); }
    void SetCategoryPadding(double padding) { categoryPadding = padding; Restyle(); }
    void SetTargetTickCount(int count) { targetTickCount = count; Restyle(); }
    void SetShowChartBackground(bool on) { showBackground = on; RequestRedraw(); }

    void SetGridSource(GridSource source) {
        gridSource = source;
        // SetGridAxis is re-applied by DescribeAxes; do it now too so a plain
        // grid change repaints without waiting for the next axis rebuild.
        ApplyGridAxis();
        RequestRedraw();
    }

    void SetLimiterMode(LimiterMode mode) { limiterMode = mode; RebuildLimiters(); }

    void SetHighlightMode(HighlightMode mode) {
        highlightMode = mode;
        RebuildHighlights();
    }

    void SetSeriesPaint(SeriesPaint paint) {
        seriesPaint = paint;
        RebuildLegend();                     // the swatch mirrors the paint
        RequestRedraw();
    }

    // ---- legend: the whole shared ChartLegend surface ----------------------
    void SetShowLegendPanel(bool on) { showLegendPanel = on; RebuildLegend(); }

    void SetLegendSide(LegendSide side) {
        legendSide = side;
        legendInsetCorner = -1;                 // an edge choice leaves the inset
        ApplyLegendPlacement();
    }

    LegendSide GetLegendSide() const { return legendSide; }

    void SetLegendAlign(LegendAlign align) {
        legendAlign = align;
        legendInsetCorner = -1;
        ApplyLegendPlacement();
    }

    // 0..3 = top-left, top-right, bottom-left, bottom-right; -1 returns to the
    // outside placement the side/align rows describe.
    void SetLegendInsetCorner(int corner) {
        legendInsetCorner = corner;
        ApplyLegendPlacement();
    }

    // The legend's own heading, measured into its box like everything else.
    void SetLegendTitleShown(bool on) { SetLegendTitle(on ? "Series" : ""); }

    // Per-entry secondary text: the legend right-aligns it in its own column,
    // so the entry list doubles as a small value table.
    void SetLegendValuesShown(bool on) { legendValuesShown = on; RebuildLegend(); }

    // 0 = unlimited; anything smaller than the entry count collapses the
    // remainder into a final "...and N more" row.
    void SetLegendMaxEntries(size_t n) {
        Legend().SetMaxEntries(n);
        MarkEngineDirty(ChartDirty::Geometry);  // the box changes size
    }

    // The legend box: the engine's framed default, or a plain unboxed list.
    void SetLegendFramed(bool on) {
        ChartLegendStyle& legendStyle = Legend().GetStyle();
        legendStyle.drawBackground = on;
        legendStyle.drawBorder = on;
        MarkEngineDirty(ChartDirty::Geometry);
    }

    // The engine repaints on a theme change; the legend swatch colours are
    // cached in the legend entries, so they are re-derived here.
    void OnThemeChanged() override { RebuildLegend(); }

    // Clicking a legend entry toggles its series: the entry renders dimmed
    // (the legend's own state) and the series drops out of the bars, the
    // stacks and the value labels. Data dirty, so stacked arrangements
    // re-solve without the hidden series.
    void OnLegendEntryToggled(size_t entryIndex, bool enabled) override {
        if (entryIndex >= seriesDisabled.size()) return;
        seriesDisabled[entryIndex] = !enabled;
        MarkEngineDirty(ChartDirty::Data);
    }

    bool SeriesDisabled(size_t s) const {
        return s < seriesDisabled.size() && seriesDisabled[s];
    }

    // Legend key showcase: Items is the discrete entry list; ColorBar keys
    // the value range through a continuous Viridis ramp; SizeLegend keys it
    // as sample bubble sizes. Both use the chart's own number format.
    void ShowLegendKey(ChartLegendMode legendMode) {
        if (legendMode != ChartLegendMode::Discrete) {
            double lo = std::numeric_limits<double>::max();
            double hi = -std::numeric_limits<double>::max();
            for (double v : PlottedValues()) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            if (lo > hi) { lo = 0.0; hi = 1.0; }
            const ChartAxis datum = DatumFormatter();
            auto format = [datum](double v) { return datum.FormatValue(v); };
            if (legendMode == ChartLegendMode::ColorBar) {
                LegendColorBar bar;
                bar.colormap = HeatmapColormap::Viridis;
                bar.minValue = lo;
                bar.maxValue = hi;
                bar.formatter = format;
                Legend().SetColorBar(bar);
            } else {
                LegendSizeScale scale;
                scale.minValue = lo;
                scale.maxValue = hi;
                scale.formatter = format;
                Legend().SetSizeScale(scale);
            }
        }
        SetLegendMode(legendMode);
    }

    void SetValueLabelMode(ValueLabelMode mode) {
        valueLabelMode = mode;
        LabelPlacementOptions options;
        options.minLabelSeparation = (mode == ValueLabelMode::Declutter) ? 4.0 : 0.0;
        options.declutter = (mode == ValueLabelMode::Declutter)
                                ? LabelDeclutterPolicy::SuppressColliding
                                : LabelDeclutterPolicy::PlaceAll;
        SetLabelOptions(options);               // marks Style dirty for us
    }

    // =========================================================================
    // ENGINE CONTRACT
    // =========================================================================

    void DescribeAxes(ChartAxisSet& axes) override {
        const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;

        // ---- axis 0: the value axis ----
        ChartAxis value("value");
        value.scale = valueScale;
        value.inverted = invertValueAxis;
        value.side = horizontal
                ? (axesAtFarSide ? ChartAxisSide::Top : ChartAxisSide::Bottom)
                : (axesAtFarSide ? ChartAxisSide::Right : ChartAxisSide::Left);
        if (valueScale == ChartScale::Percentile) {
            // A percentile axis positions by rank, so its ticks ARE ranks on
            // [0,1] - the number format belongs on the data, not on them.
            value.formatter = [](double t) {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "P%.0f", t * 100.0);
                return std::string(buffer);
            };
        } else {
            ApplyNumberFormat(value);
        }
        if (valueScale == ChartScale::Log) {
            // A log axis has no zero to grow from and no use for the zero
            // ObserveBarSeries feeds it, so the range is pinned to the whole
            // decades around the plotted values instead.
            double lo = 1.0, hi = 10.0;
            LogDecadeRange(lo, hi);
            value.SetRange(lo, hi);
        } else {
            // One call replaces the hand-rolled Observe loop every bar chart
            // used to carry: it feeds zero, then the values, the signed stack
            // totals or the percent extremes, whichever the arrangement plots.
            ObserveBarSeries(value, seriesValues, BarOptions());
        }

        // ---- axis 1: the category axis ----
        // A real Category scale: one slot per category, labels from the slot
        // list, and categoryPadding deciding whether the outer bars keep their
        // full footprint - no hand-padded linear axis.
        ChartAxis category("category");
        category.scale = ChartScale::Category;
        category.categories = categoryNames;
        category.categoryPadding = categoryPadding;
        category.side = horizontal
                ? (axesAtFarSide ? ChartAxisSide::Right : ChartAxisSide::Left)
                : (axesAtFarSide ? ChartAxisSide::Top : ChartAxisSide::Bottom);

        if (GetProjectionKind() == ChartProjectionKind::Polar) {
            // Polar has no edges to hang furniture on: an edge axis is drawn
            // along the rectangular plot bounds, which would be a straight rule
            // beside a round chart. The value axis becomes an IN-PLOT axis
            // instead - the engine maps those through the projection, so it
            // comes out as a radial rule with its ticks along it - and the
            // category axis stands down, its slots being the sectors themselves
            // (named by the rim labels CollectChartLabels adds).
            value.inPlot = true;
            value.plotPosition = 0.0;
            value.showTickLabels = true;
            category.visible = false;
        }
        axes.Add(value);            // index 0 - the limiters and grid refer to it
        axes.Add(category);         // index 1

        ApplyGridAxis();
    }

    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override {
        if (seriesValues.empty() || !frame.axes || frame.axes->Count() < 2) return;
        const ChartAxis& value = frame.axes->At(0);
        const ChartAxis& category = frame.axes->At(1);
        const ChartAxis datum = DatumFormatter();
        const double progress = frame.animationProgress;

        for (const ChartBarSpan& span : Spans(value, category)) {
            if (SeriesDisabled(span.seriesIndex)) continue;
            double v0 = span.v0, v1 = span.v1;
            ClampToPlot(v0, v1);
            v1 = v0 + (v1 - v0) * progress;     // entrance: bars grow from the base

            // One call maps the span onto the screen under whichever projection
            // is active - a rectangle when orthogonal, a ring sector under
            // Polar, rounded corners following the (possibly curved) edges.
            const std::vector<Point2Dd> outline =
                BuildBarOutline(*frame.projection, span.u0, v0, span.u1, v1,
                                8, barCornerRadius);
            if (outline.size() < 3) continue;

            const int64_t id = RegionId(span.seriesIndex, span.categoryIndex);
            const bool hovered = (HoveredRegionId() == id);
            const Color base =
                Palette().ColorAt(span.seriesIndex, seriesNames.size());

            PaintBar(ctx, outline, hovered ? Lighten(base, 45) : base);
            // The edge is the bar's own colour darkened, not a fixed near-black:
            // soft palettes (Pastel) keep their softness and dark themes get
            // coloured edges instead of invisible ones.
            ctx->SetStrokePaint(seriesPaint == SeriesPaint::Outline
                                    ? base
                                    : base.Darken(0.35f).WithAlpha(hovered ? 255 : 170));
            ctx->SetStrokeWidth((hovered || seriesPaint == SeriesPaint::Outline)
                                    ? 2.0f : 1.0f);
            ctx->DrawLinePath(outline, true);

            // The engine owns hover from here: hit-testing, the Hover-only
            // repaint and the tooltip through the standard tooltip manager.
            AddHitRegion(outline, id, TooltipFor(span, datum));
        }
    }

    // Measure pass: room for the value labels that sit past the bar ends.
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override {
        if (valueLabelMode == ValueLabelMode::Off) return;
        ctx->SetFontSize(axisFontSize);
        const ChartAxis datum = DatumFormatter();
        double widest = 0.0;
        for (double v : PlottedValues()) {
            widest = std::max(widest, static_cast<double>(
                    ctx->GetTextLineWidth(datum.FormatValue(v))));
        }
        if (GetProjectionKind() == ChartProjectionKind::Horizontal) {
            request.Reserve(ChartAxisEdge::Right, widest + 12.0);
        } else if (GetProjectionKind() == ChartProjectionKind::Vertical) {
            request.Reserve(ChartAxisEdge::Top, ctx->GetTextLineHeight("Ag") + 8.0);
        }
    }

    // Value labels ride the engine's solved label plan.
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override {
        if (valueLabelMode == ValueLabelMode::Off ||
            !Frame().axes || Frame().axes->Count() < 2 || !Frame().projection) return;
        const ChartAxis& value = Frame().axes->At(0);
        const ChartAxis& category = Frame().axes->At(1);
        const ChartAxis datum = DatumFormatter();
        ctx->SetFontSize(axisFontSize);

        const std::vector<ChartBarSpan> spans = Spans(value, category);
        // The largest bar's label is the one that must survive any declutter.
        size_t champion = 0;
        double best = -std::numeric_limits<double>::max();
        for (size_t i = 0; i < spans.size(); ++i) {
            if (SeriesDisabled(spans[i].seriesIndex)) continue;
            if (std::abs(spans[i].plotted) > best) {
                best = std::abs(spans[i].plotted);
                champion = i;
            }
        }

        // Polar: the category axis is stood down, so the slot names ride the
        // label plan as annotations around the rim instead.
        if (GetProjectionKind() == ChartProjectionKind::Polar) {
            for (size_t c = 0; c < categoryNames.size(); ++c) {
                ChartLabelRequest r;
                r.text = categoryNames[c];
                r.klass = ChartLabelClass::Annotation;
                r.anchor = Frame().projection->ToScreen(ChartNormalizedPoint(
                        category.Normalize(static_cast<double>(c)), 1.0));
                const Size2Di size = ctx->GetTextLineDimensions(r.text);
                r.textSize = Size2Dd(size.width, size.height);
                r.priority = 8;
                broker.Add(r);
            }
        }

        for (size_t i = 0; i < spans.size(); ++i) {
            const ChartBarSpan& span = spans[i];
            if (SeriesDisabled(span.seriesIndex)) continue;
            double v0 = span.v0, v1 = span.v1;
            ClampToPlot(v0, v1);

            ChartLabelRequest r;
            r.text = datum.FormatValue(span.plotted);
            r.klass = ChartLabelClass::ValueLabel;
            // Grouped bars label their tip; a stack segment labels its middle,
            // where the segment actually is.
            const double at = (arrangement == ChartBarArrangement::Grouped)
                                  ? v1 : (v0 + v1) * 0.5;
            r.anchor = Frame().projection->ToScreen(
                    ChartNormalizedPoint((span.u0 + span.u1) * 0.5, at));
            const Size2Di size = ctx->GetTextLineDimensions(r.text);
            r.textSize = Size2Dd(size.width, size.height);
            r.preferredSide = PreferredLabelSide();
            r.allowSuppress = (valueLabelMode == ValueLabelMode::Declutter);
            if (i == champion) r.priority = 10;
            broker.Add(r);
        }
    }

private:
    // ---- option state ------------------------------------------------------
    std::vector<std::string> categoryNames;
    std::vector<std::string> seriesNames;
    std::vector<std::vector<double>> seriesValues;
    std::vector<bool> seriesDisabled;           // legend click-to-toggle state

    ChartScale valueScale = ChartScale::Linear;
    ChartBarArrangement arrangement = ChartBarArrangement::Grouped;
    NumberFormat numberFormat = NumberFormat::CompactDollars;
    GridSource gridSource = GridSource::ValueAxis;
    LimiterMode limiterMode = LimiterMode::AverageAndTarget;
    HighlightMode highlightMode = HighlightMode::NoHighlight;
    bool customKeyRequested = false;
    ValueLabelMode valueLabelMode = ValueLabelMode::Declutter;
    SeriesPaint seriesPaint = SeriesPaint::Solid;
    bool showLegendPanel = true;
    LegendSide legendSide = LegendSide::Right;
    LegendAlign legendAlign = LegendAlign::Start;
    int legendInsetCorner = -1;                 // -1 = an outside placement
    bool legendValuesShown = false;
    double barCornerRadius = 0.0;
    double slotFill = 0.7;
    double categoryPadding = 0.5;
    bool invertValueAxis = false;
    bool axesAtFarSide = false;
    std::string datasetName;
    TitleMode titleMode = TitleMode::Direct;
    uint64_t shuffleState = 0x2545F4914F6CDD1DULL;

    // Restyling re-describes the axes, re-solves the layout and rebuilds the
    // label plan - and nothing else. (Hover and animation never do.)
    void Restyle() { MarkEngineDirty(ChartDirty::Style); }

    void ApplyTitle() {
        switch (titleMode) {
            case TitleMode::Direct:
                SetChartTitle(datasetName);
                break;
            case TitleMode::ViaProperty:
                // The named-property surface a registry, template or module
                // host configures the chart through, across module boundaries.
                SetProperty("title", datasetName + " (set via SetProperty)");
                break;
            case TitleMode::NoTitle:
                SetChartTitle("");
                break;
        }
    }

    ChartBarLayoutOptions BarOptions() const {
        ChartBarLayoutOptions options;
        options.arrangement = arrangement;
        options.slotFill = slotFill;
        options.groupGap = 0.1;
        return options;
    }

    std::vector<ChartBarSpan> Spans(const ChartAxis& value, const ChartAxis& category) const {
        if (seriesDisabled.empty() ||
            std::none_of(seriesDisabled.begin(), seriesDisabled.end(),
                         [](bool d) { return d; })) {
            return BuildBarSpans(value, category, categoryNames.size(),
                                 seriesValues, BarOptions());
        }
        // A toggled-off series is zeroed, not removed: indices (and with
        // them the palette colours) stay stable, and stacks collapse over it.
        std::vector<std::vector<double>> values = seriesValues;
        for (size_t s = 0; s < values.size(); ++s) {
            if (SeriesDisabled(s)) std::fill(values[s].begin(), values[s].end(), 0.0);
        }
        return BuildBarSpans(value, category, categoryNames.size(),
                             values, BarOptions());
    }

    // Data values are formatted through an axis of their own, because the value
    // axis does not always speak in data units: a Percentile axis positions -
    // and labels - by rank. Tick labels come from the value axis, datum labels
    // and tooltips from this one, so both read correctly under every scale.
    ChartAxis DatumFormatter() const {
        ChartAxis axis;
        ApplyNumberFormat(axis);
        axis.Observe(PlottedValues());
        axis.Finalize();                     // settles the auto-decimal choice
        return axis;
    }

    // ChartLegendPosition spells out 12 outside placements (edge x alignment)
    // and 4 insets; the option rows pick an edge, an alignment and optionally a
    // corner, and this composes them into the one enumerator the engine takes.
    void ApplyLegendPlacement() {
        if (legendInsetCorner >= 0 && legendInsetCorner < 4) {
            static const ChartLegendPosition insets[4] = {
                ChartLegendPosition::InsetTopLeft,    ChartLegendPosition::InsetTopRight,
                ChartLegendPosition::InsetBottomLeft, ChartLegendPosition::InsetBottomRight};
            SetLegendPosition(insets[legendInsetCorner]);
            return;
        }
        static const ChartLegendPosition outside[4][3] = {
            {ChartLegendPosition::TopStart,    ChartLegendPosition::TopCenter,
             ChartLegendPosition::TopEnd},
            {ChartLegendPosition::BottomStart, ChartLegendPosition::BottomCenter,
             ChartLegendPosition::BottomEnd},
            {ChartLegendPosition::LeftStart,   ChartLegendPosition::LeftCenter,
             ChartLegendPosition::LeftEnd},
            {ChartLegendPosition::RightStart,  ChartLegendPosition::RightCenter,
             ChartLegendPosition::RightEnd}};
        SetLegendPosition(outside[static_cast<int>(legendSide)]
                                 [static_cast<int>(legendAlign)]);
    }

    void ApplyGridAxis() {
        switch (gridSource) {
            case GridSource::ValueAxis:    SetGridAxis(0); break;
            case GridSource::CategoryAxis: SetGridAxis(1); break;
            case GridSource::NoGrid:       SetGridAxis(static_cast<size_t>(-1)); break;
        }
    }

    void ApplyNumberFormat(ChartAxis& axis) const {
        switch (numberFormat) {
            case NumberFormat::CompactDollars:
                axis.unitPrefix = "$";
                axis.compactNumbers = true;      // 1'250'000 -> "$1.3M"
                break;
            case NumberFormat::Plain:
                axis.decimals = 0;
                break;
            case NumberFormat::TwoDecimals:
                axis.decimals = 2;
                break;
            case NumberFormat::PercentSuffix:
                axis.unitSuffix = "%";
                axis.decimals = 0;
                break;
            case NumberFormat::CustomFormatter:
                // A ValueFormatter wins over every built-in formatting rule.
                axis.formatter = [](double v) {
                    return "€" + FormatCompactNumber(v, 1);
                };
                break;
        }
    }

    LabelSide PreferredLabelSide() const {
        if (arrangement != ChartBarArrangement::Grouped) return LabelSide::Inside;
        return (GetProjectionKind() == ChartProjectionKind::Horizontal)
                   ? LabelSide::Right : LabelSide::Top;
    }

    // Every value the current arrangement actually plots: the raw values when
    // grouped, the running stack edges when stacked, the percent shares when
    // 100% stacked. Used for the limiters, the measure pass and the log range,
    // so all three agree with what is on screen.
    std::vector<double> PlottedValues() const {
        std::vector<double> plotted;
        const size_t categories = categoryNames.size();
        if (arrangement == ChartBarArrangement::Grouped) {
            for (const auto& series : seriesValues) {
                for (double v : series) {
                    if (std::isfinite(v)) plotted.push_back(v);
                }
            }
            return plotted;
        }
        const bool percent = (arrangement == ChartBarArrangement::PercentStacked);
        for (size_t c = 0; c < categories; ++c) {
            double positive = 0.0, negative = 0.0;
            for (const auto& series : seriesValues) {
                if (c >= series.size() || !std::isfinite(series[c])) continue;
                if (series[c] >= 0.0) positive += series[c]; else negative += series[c];
            }
            const double absTotal = positive - negative;
            const double scale = (percent && absTotal > 0.0) ? 100.0 / absTotal : 1.0;
            double up = 0.0, down = 0.0;
            for (const auto& series : seriesValues) {
                if (c >= series.size() || !std::isfinite(series[c])) continue;
                double& running = (series[c] >= 0.0) ? up : down;
                running += series[c] * scale;
                plotted.push_back(running);
            }
        }
        return plotted;
    }

    void LogDecadeRange(double& lo, double& hi) const {
        double minPositive = std::numeric_limits<double>::max();
        double maxPositive = 0.0;
        for (double v : PlottedValues()) {
            if (v <= 0.0) continue;              // a log axis has no room for <= 0
            minPositive = std::min(minPositive, v);
            maxPositive = std::max(maxPositive, v);
        }
        if (maxPositive <= 0.0) { lo = 1.0; hi = 10.0; return; }
        lo = std::pow(10.0, std::floor(std::log10(minPositive)));
        hi = std::pow(10.0, std::ceil(std::log10(maxPositive)));
        if (!(hi > lo)) hi = lo * 10.0;
    }

    // On a log axis the base edge of the first bar normalises far below the
    // plot (Normalize(0) is the log floor), so bars are grown from the axis
    // end instead. Every other scale has a real zero and needs no clamping.
    void ClampToPlot(double& v0, double& v1) const {
        if (valueScale != ChartScale::Log) return;
        v0 = std::clamp(v0, 0.0, 1.0);
        v1 = std::clamp(v1, 0.0, 1.0);
    }

    static Rect2Dd OutlineBounds(const std::vector<Point2Dd>& outline) {
        double minX = outline[0].x, maxX = outline[0].x;
        double minY = outline[0].y, maxY = outline[0].y;
        for (const Point2Dd& p : outline) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        return Rect2Dd(minX, minY, maxX - minX, maxY - minY);
    }

    static void ClipToOutline(IRenderContext* ctx, const std::vector<Point2Dd>& outline) {
        ctx->ClearPath();
        ctx->MoveTo(outline[0].x, outline[0].y);
        for (size_t i = 1; i < outline.size(); ++i) ctx->LineTo(outline[i].x, outline[i].y);
        ctx->ClosePath();
        ctx->ClipPath();
    }

    // The projected outline is an arbitrary polygon (a ring sector under Polar),
    // so every paint style works off the path rather than off a rectangle.
    void PaintBar(IRenderContext* ctx, const std::vector<Point2Dd>& outline,
                  const Color& color) const {
        const Rect2Dd box = OutlineBounds(outline);
        switch (seriesPaint) {
            case SeriesPaint::Solid:
                ctx->SetFillPaint(color);
                ctx->FillLinePath(outline);
                break;
            case SeriesPaint::Gradient: {
                auto pattern = ctx->CreateLinearGradientPattern(
                        box.x, box.Bottom(), box.x, box.y,
                        {GradientStop(0.0, Lighten(color, 80)), GradientStop(1.0, color)});
                if (pattern) ctx->SetFillPaint(pattern); else ctx->SetFillPaint(color);
                ctx->FillLinePath(outline);
                break;
            }
            case SeriesPaint::Outline: {
                Color ghost = color;
                ghost.a = 45;
                ctx->SetFillPaint(ghost);
                ctx->FillLinePath(outline);
                break;                       // the caller strokes the border
            }
            case SeriesPaint::Hatched: {
                ctx->SetFillPaint(Lighten(color, 95));
                ctx->FillLinePath(outline);
                ctx->PushState();
                ClipToOutline(ctx, outline);
                ctx->SetStrokePaint(color);
                ctx->SetStrokeWidth(2.0f);
                const double run = std::max(4.0, box.height);
                for (double x = box.x - run; x < box.x + box.width; x += 7.0) {
                    ctx->DrawLine(Point2Dd(x, box.Bottom()), Point2Dd(x + run, box.y));
                }
                ctx->PopState();
                break;
            }
        }
    }

    static int64_t RegionId(size_t series, size_t category) {
        return static_cast<int64_t>(series) * 1000 + static_cast<int64_t>(category);
    }

    std::string TooltipFor(const ChartBarSpan& span, const ChartAxis& datum) const {
        std::ostringstream out;
        if (span.seriesIndex < seriesNames.size()) out << seriesNames[span.seriesIndex];
        if (span.categoryIndex < categoryNames.size()) {
            out << " · " << categoryNames[span.categoryIndex];
        }
        out << ": " << datum.FormatValue(span.plotted);
        return out.str();
    }

    void RebuildLimiters() {
        ClearLimiters();
        if (limiterMode == LimiterMode::NoLimiter) return;

        const std::vector<double> plotted = PlottedValues();
        if (plotted.empty()) return;
        double sum = 0.0, maximum = -std::numeric_limits<double>::max();
        for (double v : plotted) { sum += v; maximum = std::max(maximum, v); }
        const double average = sum / static_cast<double>(plotted.size());

        auto add = [this](double v, ChartLimiterKind kind, const Color& color,
                          const std::string& caption) {
            ChartLimiter limiter;
            limiter.kind = kind;
            limiter.axisIndex = 0;              // the value axis
            limiter.value = v;
            limiter.color = color;
            limiter.caption = caption;
            limiter.captionColor = color;
            AddLimiter(limiter);
        };

        switch (limiterMode) {
            case LimiterMode::NoLimiter:
                break;
            case LimiterMode::Average:
                add(average, ChartLimiterKind::Average, Color(90, 90, 90, 255), "average");
                break;
            case LimiterMode::AverageAndTarget:
                add(average, ChartLimiterKind::Average, Color(90, 90, 90, 255), "average");
                add(maximum * 0.9, ChartLimiterKind::Target,
                    Color(200, 60, 60, 255), "target");
                break;
            case LimiterMode::Thresholds:
                add(maximum * 0.25, ChartLimiterKind::Threshold,
                    Color(120, 160, 90, 255), "low");
                add(maximum * 0.50, ChartLimiterKind::Threshold,
                    Color(210, 160, 60, 255), "watch");
                add(maximum * 0.75, ChartLimiterKind::Threshold,
                    Color(200, 60, 60, 255), "critical");
                break;
        }
    }

    // One owner for the legend's custom key. A key describes marks the chart
    // actually draws: the 50%/95% ellipse key travels with the ellipse
    // highlight style; otherwise the legend tab may request the limiter-lines
    // key; otherwise there is none.
public:
    void SetLegendCustomKey(bool on) {
        customKeyRequested = on;
        SyncLegendKey();
    }

private:
    void SyncLegendKey() {
        if (highlightMode == HighlightMode::Ellipse) {
            Legend().SetCustomArea(
                Size2Dd(150.0, 52.0),
                [](IRenderContext* ctx, const Rect2Dd& r) {
                    const Point2Dd c(r.x + 52.0, r.y + r.height / 2.0 + 4.0);
                    ctx->SetStrokePaint(Color(150, 150, 150, 255));
                    ctx->SetStrokeWidth(1.0f);
                    ctx->DrawEllipse(Rect2Dd(c.x - 36, c.y - 18, 72, 36));
                    ctx->DrawEllipse(Rect2Dd(c.x - 17, c.y - 8, 34, 16));
                    ctx->SetFillPaint(Color(60, 60, 60, 255));
                    ctx->FillCircle(c, 2.5);
                    ctx->SetFontSize(9.0f);
                    ctx->SetTextPaint(Color(120, 120, 120, 255));
                    ctx->DrawText("95%", Point2Dd(r.x + 2.0, r.y + 1.0));
                    ctx->DrawText("50%", Point2Dd(c.x - 10.0, c.y - 26.0));
                    ctx->DrawText("mean", Point2Dd(c.x + 40.0, c.y - 5.0));
                });
        } else if (customKeyRequested) {
            Legend().SetCustomArea(
                Size2Dd(150.0, 44.0),
                [](IRenderContext* ctx, const Rect2Dd& r) {
                    const struct {
                        Color color;
                        const char* label;
                    } rows[] = {{Color(90, 90, 90, 255), "average"},
                                {Color(200, 60, 60, 255), "target"}};
                    ctx->SetFontSize(9.0f);
                    double y = r.y + 8.0;
                    for (const auto& row : rows) {
                        ctx->SetStrokePaint(row.color);
                        ctx->SetStrokeWidth(1.5f);
                        ctx->SetLineDash(UCDashPattern({4.0, 3.0}));
                        ctx->DrawLine(Point2Dd(r.x + 2.0, y),
                                      Point2Dd(r.x + 40.0, y));
                        ctx->SetLineDash(UCDashPattern());
                        ctx->SetTextPaint(Color(120, 120, 120, 255));
                        ctx->DrawText(row.label, Point2Dd(r.x + 48.0, y - 6.0));
                        y += 18.0;
                    }
                });
        } else {
            Legend().ClearCustomArea();
        }
        MarkEngineDirty(ChartDirty::Geometry);
    }

    // The highlight layer, driven by the chart's own data.
    void RebuildHighlights() {
        ClearHighlights();
        SyncLegendKey();

        if (highlightMode == HighlightMode::Band) {
            double sum = 0.0;
            size_t count = 0;
            for (double v : PlottedValues()) { sum += v; ++count; }
            const double average = count ? sum / count : 0.0;
            ChartHighlight band;
            band.shape = ChartHighlightShape::ValueBand;
            band.axisIndex = 0;                       // the value axis
            band.bandLow = average * 0.8;
            band.bandHigh = average * 1.2;
            band.fill = Color(120, 140, 170, 28);
            band.stroke = Color(120, 140, 170, 120);
            band.dashed = true;
            band.label = "expected range";
            band.zSlot = 200;                         // wash under the grid
            AddHighlight(band);
        } else if (highlightMode == HighlightMode::Ellipse ||
                   highlightMode == HighlightMode::Blob) {
            // One computed group shape per series, over its bar-tip points
            // (x = category index through the category axis, y = value).
            for (size_t s = 0; s < seriesValues.size(); ++s) {
                if (SeriesDisabled(s)) continue;
                ChartHighlight group;
                group.shape = (highlightMode == HighlightMode::Ellipse)
                                  ? ChartHighlightShape::ConfidenceEllipse
                                  : ChartHighlightShape::Blob;
                group.xAxisIndex = 1;                 // category axis drives u
                group.yAxisIndex = 0;                 // value axis drives v
                for (size_t c = 0; c < seriesValues[s].size(); ++c) {
                    group.members.emplace_back(static_cast<double>(c),
                                               seriesValues[s][c]);
                }
                const Color base = Palette().ColorAt(s, seriesValues.size());
                group.fill = base.WithAlpha(26);
                group.stroke = base.WithAlpha(190);
                group.confidence = 0.95;
                group.padding = 16.0;
                group.zSlot = (highlightMode == HighlightMode::Ellipse) ? 700 : 200;
                AddHighlight(group);
            }
        }
    }

    void RebuildLegend() {
        if (!showLegendPanel) {
            SetShowLegend(false);
            return;
        }
        LegendSwatch swatch = LegendSwatch::Square;
        switch (seriesPaint) {
            case SeriesPaint::Solid:    swatch = LegendSwatch::Square; break;
            case SeriesPaint::Gradient: swatch = LegendSwatch::Gradient; break;
            case SeriesPaint::Outline:  swatch = LegendSwatch::Outline; break;
            case SeriesPaint::Hatched:  swatch = LegendSwatch::Hatched; break;
        }

        const ChartAxis datum = DatumFormatter();
        std::vector<ChartLegendEntry> entries;
        for (size_t i = 0; i < seriesNames.size(); ++i) {
            ChartLegendEntry entry;
            entry.label = seriesNames[i];
            entry.color = Palette().ColorAt(i, seriesNames.size());
            entry.swatch = swatch;
            entry.enabled = !SeriesDisabled(i);   // keep toggles across rebuilds
            if (legendValuesShown) {
                // valueText is the legend's own right-aligned column, formatted
                // through the same datum axis the tooltips and value labels use.
                double total = 0.0;
                for (double v : seriesValues[i]) {
                    if (std::isfinite(v)) total += v;
                }
                entry.valueText = datum.FormatValue(total);
            }
            entries.push_back(entry);
        }
        SetLegendEntries(entries);
        SetShowLegend(true);
    }
};

// =============================================================================
// OPTION-PANEL WIDGETRY (same radio rows as the Album example)
// =============================================================================

const int kBtnH = 24;
const int kRowGap = 4;
const int kTabBarH = 28;
const int kTabPagePadV = 16;

// The option panel is exactly as tall as the tab in front of it: its tab bar,
// that page's padding, and that page's rows. Tabs carry different row counts,
// so a fixed panel sized for the tallest one would leave dead space under
// every other tab - and that space belongs to the chart.
inline float OptionPanelHeight(int rows) {
    return static_cast<float>(kTabBarH + kTabPagePadV + rows * kBtnH +
                              (rows - 1) * kRowGap);
}

// A plain flex layout wrapper that must never scroll itself.
std::shared_ptr<UltraCanvasContainer> MakeLayoutBox(const std::string& id) {
    auto c = std::make_shared<UltraCanvasContainer>(id);
    ContainerStyle st;
    st.autoShowScrollbars           = false;
    st.forceShowVerticalScrollbar   = false;
    st.forceShowHorizontalScrollbar = false;
    c->SetContainerStyle(st);
    return c;
}

// A horizontal control line: shrink:0, stretched on the cross axis.
std::shared_ptr<UltraCanvasContainer> MakeRow(const std::string& id) {
    auto row = MakeLayoutBox(id);
    row->layout.SetFlexRow().SetFlexGap(6)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    row->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                   .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    return row;
}

// One tab's page: a column of control rows, padded off the tab frame.
std::shared_ptr<UltraCanvasContainer> MakeTabPage(const std::string& id) {
    auto page = MakeLayoutBox(id);
    page->layout.SetFlexColumn().SetFlexGap(kRowGap)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    page->SetPadding(kTabPagePadV / 2, 10, kTabPagePadV / 2, 10);
    return page;
}

// The active choice gets a gold border + warm fill, so the page always shows
// which engine option is currently applied.
inline void StyleOptionButton(UltraCanvasButton* b, bool selected) {
    const Color baseBg(255, 255, 255, 255);
    const Color selBg(246, 214, 158, 255);     // warm highlight
    const Color baseBorder(0, 0, 0, 77);
    const Color selBorder(201, 143, 46, 255);  // #C98F2E gold
    const Color text(40, 40, 40, 255);
    b->SetColors(selected ? selBg : baseBg, selBg);
    b->SetTextColors(text);
    b->SetBorder(selected ? 2.0f : 1.0f, selected ? selBorder : baseBorder);
}

// The row owns its buttons (they are its children); a group is a borrowed view
// of them, so an option row can restyle another one without the two holding
// each other alive through their click handlers.
using OptionButtons = std::vector<UltraCanvasButton*>;

// Restyle a radio group from the outside: index -1 clears every button.
// Used where two rows describe one setting (the legend's edge placement and
// its inset corner), so picking in one row visibly releases the other.
inline void SelectOnly(const OptionButtons& group, int index) {
    for (size_t i = 0; i < group.size(); ++i) {
        StyleOptionButton(group[i], static_cast<int>(i) == index);
    }
}

// Every option button is built at width 0 - CSS `auto` - so the flex row takes
// its width from the button's own max-content measurement (text + padding +
// border). Nothing here guesses a pixel width per label, so a longer caption
// ("Colorblind", "SetChartTitle") widens its button instead of being clipped.
// The minimum keeps a one- or two-character caption ("4", "0.5") clickable.
inline void SizeToText(const std::shared_ptr<UltraCanvasUIElement>& element,
                       float minimumWidth) {
    element->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    element->boxConstraints = CSSLayout::BoxConstraints();
    element->boxConstraints->minWidth = CSSLayout::Dimension::Px(minimumWidth);
}

// Append a bold label followed by a radio row of text-sized buttons into `row`.
// onPick(i) fires for button i; clicking a button highlights it and clears its
// siblings. `selectedIndex` marks the initially-active button (-1 = none).
template <typename Fn>
OptionButtons AppendLabeledButtons(const std::shared_ptr<UltraCanvasContainer>& row,
                                   const std::string& idPrefix, const char* labelText,
                                   const std::vector<const char*>& labels, Fn onPick,
                                   int selectedIndex = -1) {
    auto lbl = std::make_shared<UltraCanvasLabel>(idPrefix + "lbl", 0, 0, 0, kBtnH);
    lbl->SetText(labelText);
    lbl->SetFontSize(12);
    lbl->SetFontWeight(FontWeight::Bold);
    lbl->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
    SizeToText(lbl, 0.0f);                 // the caption sizes the label too
    row->AddChild(lbl);

    auto group = std::make_shared<OptionButtons>();
    OptionButtons buttons;
    for (size_t i = 0; i < labels.size(); ++i) {
        auto b = std::make_shared<UltraCanvasButton>(
                idPrefix + std::to_string(i), 0, 0, 0, kBtnH, labels[i]);
        b->SetFontSize(11);
        b->SetCornerRadius(4.0f);
        StyleOptionButton(b.get(), static_cast<int>(i) == selectedIndex);
        const int idx = static_cast<int>(i);
        auto* raw = b.get();
        b->SetOnClick([idx, onPick, group, raw]() {
            for (auto* gb : *group) StyleOptionButton(gb, gb == raw);
            onPick(idx);
        });
        SizeToText(b, 30.0f);
        row->AddChild(b);
        group->push_back(raw);
        buttons.push_back(raw);
    }
    return buttons;
}

// A momentary (non-radio) button: fires and springs back, for the actions that
// have no persistent state (Shuffle, Replay animation).
template <typename Fn>
std::shared_ptr<UltraCanvasButton> AppendActionButton(
                          const std::shared_ptr<UltraCanvasContainer>& row,
                          const std::string& id, const char* text, Fn onClick) {
    auto b = std::make_shared<UltraCanvasButton>(id, 0, 0, 0, kBtnH, text);
    b->SetFontSize(11);
    b->SetCornerRadius(4.0f);
    StyleOptionButton(b.get(), false);
    b->SetOnClick(onClick);
    SizeToText(b, 30.0f);
    row->AddChild(b);
    return b;
}

} // namespace

// =============================================================================
// THE PAGE
// =============================================================================

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateChartEngineExamples() {
    const std::vector<EngineDataset> datasets = MakeDatasets();

    auto root = MakeLayoutBox("ChartEngineExamples");
    root->layout.SetFlexColumn().SetFlexGap(8)
                .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    root->SetPadding(10, 12, 10, 12);

    // ===== Header (pinned) =====
    auto header = MakeLayoutBox("EngineHeader");
    header->layout.SetFlexColumn().SetFlexGap(4)
                  .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    header->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto title = std::make_shared<UltraCanvasLabel>("EngineTitle", 0, 0, 0, 28);
    title->SetText("Chart Engine — one chart definition, every engine service, "
                   "switchable live");
    title->SetFontSize(16);
    title->SetFontWeight(FontWeight::Bold);
    title->SetTextColor(Color(50, 50, 150, 255));
    title->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    header->AddChild(title);

    // Two single-line labels rather than one wrapped block: the header is
    // pinned at a fixed height, so a re-wrap must never hide the second line.
    const char* subtitleLines[] = {
        "The chart below is ONE bar-chart class implementing only the engine's content contract: "
        "DescribeAxes, RenderChartContent and a label collector.",
        "Every option switches an engine service, not a chart-type feature; the tabs group them "
        "by service. Hover a bar for the engine's hit-region tooltip."
    };
    for (int i = 0; i < 2; ++i) {
        auto line = std::make_shared<UltraCanvasLabel>(
                "EngineSubtitle" + std::to_string(i), 0, 0, 0, 16);
        line->SetText(subtitleLines[i]);
        line->SetFontSize(11);
        line->SetTextColor(Color(110, 110, 110, 255));
        line->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
        header->AddChild(line);
    }
    root->AddChild(header);

    // ===== The chart (grows to fill) =====
    auto chart = std::make_shared<EngineFeatureChart>("engineChart", 0, 0, 0, 0);
    chart->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
                     .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    chart->SetDataset(datasets[0]);          // also titles the chart
    chart->SetValueLabelMode(EngineFeatureChart::ValueLabelMode::Declutter);
    chart->SetTheme("Pastel");               // the showcase's default look
    auto* chartPtr = chart.get();
    root->AddChild(chart);

    // ===== Status (pinned) =====
    auto status = std::make_shared<UltraCanvasLabel>("EngineStatus", 0, 0, 0, 22);
    status->SetText("Vertical projection · linear axis · grouped bars · grid from the "
                    "value ticks · average + target limiters");
    status->SetFontSize(11);
    status->SetTextColor(Color(120, 120, 120, 255));
    status->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                      .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    root->AddChild(status);
    auto* statusPtr = status.get();

    auto say = [statusPtr](const std::string& text) { statusPtr->SetText(text); };

    // =========================================================================
    // Controls (pinned): one tab per engine service family, so a row only ever
    // sits beside the rows it belongs with, and the chart gets back most of the
    // height the nine stacked rows used to take.
    // =========================================================================
    // Rows per tab, in the order they are added below: the panel resizes to the
    // page in front so the chart gets every row the current tab does not need.
    static const int kRowsPerTab[] = {2, 2, 2, 2, 4, 2};

    auto optionTabs = std::make_shared<UltraCanvasTabbedContainer>(
            "EngineOptionTabs", 0, 0, 0, OptionPanelHeight(kRowsPerTab[0]));
    optionTabs->SetTabStyle(TabStyle::Rounded);
    optionTabs->SetTabHeight(kTabBarH);
    optionTabs->SetTabMinWidth(78);
    optionTabs->SetCloseMode(TabCloseMode::NoClose);
    optionTabs->layoutItem.SetFlexGrow(0).SetFlexShrink(0)
                          .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    // -------------------------------------------------------------------------
    // TAB: Plot — how the content is shaped and painted
    // -------------------------------------------------------------------------
    auto plotPage = MakeTabPage("engine_tab_plot");

    auto rowProjection = MakeRow("engine_row_projection");
    const ChartProjectionKind projections[] = {
        ChartProjectionKind::Vertical, ChartProjectionKind::Horizontal,
        ChartProjectionKind::Polar
    };
    AppendLabeledButtons(rowProjection, "engine_proj_", "Projection",
                  {"Vertical", "Horizontal", "Polar"},
                  [chartPtr, say, projections](int i) {
                      chartPtr->SetProjectionKind(projections[i]);
                      const char* what[] = {
                          "Vertical projection — domain right, value up",
                          "Horizontal projection — the same content transposed by the engine",
                          "Polar projection — the same bars as ring sectors, no chart-side change"};
                      say(what[i]);
                  }, 0);
    rowProjection->AddSpacer(20);
    const ChartBarArrangement arrangements[] = {
        ChartBarArrangement::Grouped, ChartBarArrangement::Stacked,
        ChartBarArrangement::PercentStacked
    };
    AppendLabeledButtons(rowProjection, "engine_arr_", "Bars",
                  {"Grouped", "Stacked", "100%"},
                  [chartPtr, say, arrangements](int i) {
                      chartPtr->SetArrangement(arrangements[i]);
                      const char* what[] = {
                          "Grouped bars — ChartBarArrangement::Grouped (negatives grow downward)",
                          "Stacked bars — positives accumulate up, negatives down: a diverging stack",
                          "100% stacked — every category shares out its absolute total"};
                      say(what[i]);
                  }, 0);
    rowProjection->AddSpacer(20);
    AppendLabeledButtons(rowProjection, "engine_corner_", "Corners",
                  {"Square", "Rounded"},
                  [chartPtr, say](int i) {
                      chartPtr->SetBarCornerRadius(i == 0 ? 0.0 : 6.0);
                      say(i == 0 ? "Square bar outlines"
                                 : "Rounded bar outlines — BuildBarOutline rounds in screen "
                                   "space, so ring sectors round too");
                  }, 0);
    plotPage->AddChild(rowProjection);

    auto rowPaint = MakeRow("engine_row_paint");
    AppendLabeledButtons(rowPaint, "engine_paint_", "Bar paint",
                  {"Solid", "Gradient", "Outline", "Hatched"},
                  [chartPtr, say](int i) {
                      const EngineFeatureChart::SeriesPaint paints[] = {
                          EngineFeatureChart::SeriesPaint::Solid,
                          EngineFeatureChart::SeriesPaint::Gradient,
                          EngineFeatureChart::SeriesPaint::Outline,
                          EngineFeatureChart::SeriesPaint::Hatched};
                      chartPtr->SetSeriesPaint(paints[i]);
                      say("Series paint changed — the legend swatch follows it "
                          "(LegendSwatch), so a non-solid series is never represented by a "
                          "colour square that matches nothing");
                  }, 0);
    rowPaint->AddSpacer(20);
    AppendLabeledButtons(rowPaint, "engine_fill_", "Slot fill",
                  {"Narrow", "Default", "Wide"},
                  [chartPtr, say](int i) {
                      const double fills[] = {0.4, 0.7, 0.95};
                      chartPtr->SetSlotFill(fills[i]);
                      std::ostringstream o;
                      o << "ChartBarLayoutOptions::slotFill = " << fills[i]
                        << " — the fraction of a category slot the bars occupy";
                      say(o.str());
                  }, 1);
    rowPaint->AddSpacer(20);
    AppendLabeledButtons(rowPaint, "engine_pad_", "Slot pad",
                  {"0", "0.5"},
                  [chartPtr, say](int i) {
                      chartPtr->SetCategoryPadding(i == 0 ? 0.0 : 0.5);
                      say(i == 0 ? "categoryPadding 0 — the range spans exactly 0..n-1, so the "
                                   "outer bars are clipped in half"
                                 : "categoryPadding 0.5 — every slot gets the same width and the "
                                   "outer bars keep their full footprint");
                  }, 1);
    plotPage->AddChild(rowPaint);
    optionTabs->AddTab("Plot", plotPage);

    // -------------------------------------------------------------------------
    // TAB: Axes — the axis scales, the grid derived from them, the tick numbers
    // -------------------------------------------------------------------------
    auto axesPage = MakeTabPage("engine_tab_axes");

    auto rowScale = MakeRow("engine_row_scale");
    const ChartScale scales[] = {
        ChartScale::Linear, ChartScale::Log, ChartScale::SymLog, ChartScale::Percentile
    };
    AppendLabeledButtons(rowScale, "engine_scale_", "Value axis",
                  {"Linear", "Log", "SymLog", "Percentile"},
                  [chartPtr, say, scales](int i) {
                      chartPtr->SetValueScale(scales[i]);
                      const char* what[] = {
                          "Linear scale — the auto range is rounded outward to nice numbers",
                          "Logarithmic scale — one tick per decade; the range is pinned to whole "
                          "decades and the bars grow from the axis floor (try the Wide range data)",
                          "Symmetric log — linear near zero, logarithmic beyond, so negative values "
                          "survive (try the Signed data)",
                          "Percentile scale — position by rank, which uniformises a skewed column; "
                          "ChartAxis also carries Z-score and robust z-score"};
                      say(what[i]);
                  }, 0);
    rowScale->AddSpacer(20);
    AppendLabeledButtons(rowScale, "engine_invert_", "Direction",
                  {"Normal", "Inverted"},
                  [chartPtr, say](int i) {
                      chartPtr->SetInvertValueAxis(i == 1);
                      say(i == 1 ? "Value axis inverted — ChartAxis::inverted flips which end is high"
                                 : "Value axis in its natural direction");
                  }, 0);
    rowScale->AddSpacer(20);
    AppendLabeledButtons(rowScale, "engine_side_", "Axis side",
                  {"Near", "Far"},
                  [chartPtr, say](int i) {
                      chartPtr->SetAxesAtFarSide(i == 1);
                      say(i == 1 ? "Axes moved to the far edges (Right / Top) — the measured layout "
                                   "moves the margins with them"
                                 : "Axes on the near edges (Left / Bottom)");
                  }, 0);
    axesPage->AddChild(rowScale);

    auto rowGrid = MakeRow("engine_row_grid");
    AppendLabeledButtons(rowGrid, "engine_grid_", "Grid",
                  {"Value ticks", "Category", "Off"},
                  [chartPtr, say](int i) {
                      const EngineFeatureChart::GridSource sources[] = {
                          EngineFeatureChart::GridSource::ValueAxis,
                          EngineFeatureChart::GridSource::CategoryAxis,
                          EngineFeatureChart::GridSource::NoGrid};
                      chartPtr->SetGridSource(sources[i]);
                      const char* what[] = {
                          "Gridlines derived from the value axis ticks — grid and labels can never disagree",
                          "Gridlines derived from the category slots",
                          "No gridlines — SetGridAxis(SIZE_MAX)"};
                      say(what[i]);
                  }, 0);
    rowGrid->AddSpacer(20);
    AppendLabeledButtons(rowGrid, "engine_ticks_", "Ticks",
                  {"4", "6", "10"},
                  [chartPtr, say](int i) {
                      const int counts[] = {4, 6, 10};
                      chartPtr->SetTargetTickCount(counts[i]);
                      std::ostringstream o;
                      o << "Target tick count " << counts[i]
                        << " — the 1-D declutter keeps the ends and zero longest";
                      say(o.str());
                  }, 1);
    rowGrid->AddSpacer(20);
    AppendLabeledButtons(rowGrid, "engine_fmt_", "Numbers",
                  {"Compact $", "Plain", "2 decimals", "Suffix %", "Custom fn"},
                  [chartPtr, say](int i) {
                      const EngineFeatureChart::NumberFormat formats[] = {
                          EngineFeatureChart::NumberFormat::CompactDollars,
                          EngineFeatureChart::NumberFormat::Plain,
                          EngineFeatureChart::NumberFormat::TwoDecimals,
                          EngineFeatureChart::NumberFormat::PercentSuffix,
                          EngineFeatureChart::NumberFormat::CustomFormatter};
                      chartPtr->SetNumberFormat(formats[i]);
                      const char* what[] = {
                          "unitPrefix \"$\" + compactNumbers — 1'250'000 reads \"$1.3M\"",
                          "decimals = 0 — plain integers",
                          "decimals = 2",
                          "unitSuffix \"%\" — pairs with the 100% stacked arrangement",
                          "A custom ValueFormatter, which wins over every built-in rule"};
                      say(what[i]);
                  }, 0);
    axesPage->AddChild(rowGrid);
    optionTabs->AddTab("Axes", axesPage);

    // -------------------------------------------------------------------------
    // TAB: Data — the dataset, the dirty model and the animation driver it feeds
    // -------------------------------------------------------------------------
    auto dataPage = MakeTabPage("engine_tab_data");

    auto rowData = MakeRow("engine_row_data");
    AppendLabeledButtons(rowData, "engine_data_", "Data",
                  {"Revenue", "Wide range", "Signed"},
                  [chartPtr, say, datasets](int i) {
                      chartPtr->SetDataset(datasets[i]);
                      say("Data: " + datasets[i].name + " — " + datasets[i].caption);
                  }, 0);
    AppendActionButton(rowData, "engine_shuffle", "Shuffle",
                  [chartPtr, say]() {
                      chartPtr->ShuffleValues();
                      say("Values re-jittered — ChartDirty::Data rebuilt the axes, the layout and "
                          "the label plan, and restarted the entrance animation");
                  });
    dataPage->AddChild(rowData);

    auto rowAnimation = MakeRow("engine_row_animation");
    AppendLabeledButtons(rowAnimation, "engine_anim_", "Animation",
                  {"On data", "Off"},
                  [chartPtr, say](int i) {
                      chartPtr->SetAnimateOnDataChange(i == 0, 0.8f);
                      say(i == 0 ? "The engine animation driver restarts on every ChartDirty::Data"
                                 : "Animation off — data changes land finished");
                  }, 0);
    AppendActionButton(rowAnimation, "engine_replay", "Replay",
                  [chartPtr, say]() {
                      chartPtr->StartEngineAnimation(0.8f);
                      say("StartEngineAnimation — frame.animationProgress runs 0..1 (ease-out cubic) "
                          "off a 60fps engine timer, with no chart-side timer of its own");
                  });
    rowAnimation->AddSpacer(20);
    AppendLabeledButtons(rowAnimation, "engine_tips_", "Tooltips",
                  {"On", "Off"},
                  [chartPtr, say](int i) {
                      chartPtr->SetEnableTooltips(i == 0);
                      say(i == 0 ? "Hit-region tooltips on — the engine hit-tests the polygons the "
                                   "content registered and shows them through the tooltip manager"
                                 : "Tooltips off — hovering still repaints via ChartDirty::Hover only");
                  }, 0);
    dataPage->AddChild(rowAnimation);
    optionTabs->AddTab("Data", dataPage);

    // -------------------------------------------------------------------------
    // TAB: Annotations — everything the engine writes or draws over the plot
    // -------------------------------------------------------------------------
    auto annotationPage = MakeTabPage("engine_tab_annotations");

    auto rowLabels = MakeRow("engine_row_labels");
    AppendLabeledButtons(rowLabels, "engine_vlabels_", "Labels",
                  {"All", "Declutter", "Off"},
                  [chartPtr, say](int i) {
                      const EngineFeatureChart::ValueLabelMode modes[] = {
                          EngineFeatureChart::ValueLabelMode::All,
                          EngineFeatureChart::ValueLabelMode::Declutter,
                          EngineFeatureChart::ValueLabelMode::Off};
                      chartPtr->SetValueLabelMode(modes[i]);
                      const char* what[] = {
                          "Every value label placed (LabelDeclutterPolicy::PlaceAll) — a crowded "
                          "chart overprints",
                          "allowSuppress + SuppressColliding — crowded labels are dropped instead, "
                          "and the largest bar's label has priority",
                          "No value labels collected"};
                      say(what[i]);
                  }, 1);
    rowLabels->AddSpacer(20);
    AppendLabeledButtons(rowLabels, "engine_title_", "Title",
                  {"SetChartTitle", "SetProperty", "None"},
                  [chartPtr, say](int i) {
                      const EngineFeatureChart::TitleMode modes[] = {
                          EngineFeatureChart::TitleMode::Direct,
                          EngineFeatureChart::TitleMode::ViaProperty,
                          EngineFeatureChart::TitleMode::NoTitle};
                      chartPtr->SetTitleMode(modes[i]);
                      const char* what[] = {
                          "Title set directly — it owns an exclusive band above every other top "
                          "reservation, so axis labels can never overprint it",
                          "The same title set with SetProperty(\"title\", ...) — the "
                          "IConfigurableElement surface a registry, template or module host uses "
                          "across a module boundary",
                          "No title — the reserved band returns to the plot area"};
                      say(what[i]);
                  }, 0);
    rowLabels->AddSpacer(20);
    AppendLabeledButtons(rowLabels, "engine_bg_", "Chart bg",
                  {"On", "Off"},
                  [chartPtr, say](int i) {
                      chartPtr->SetShowChartBackground(i == 0);
                      say(i == 0 ? "Engine background layer on — element fill plus the plot-area fill"
                                 : "Background layer off (slot 100 skipped)");
                  }, 0);
    annotationPage->AddChild(rowLabels);

    auto rowOverlays = MakeRow("engine_row_overlays");
    AppendLabeledButtons(rowOverlays, "engine_lim_", "Limiters",
                  {"None", "Average", "Avg+target", "Thresholds"},
                  [chartPtr, say](int i) {
                      const EngineFeatureChart::LimiterMode modes[] = {
                          EngineFeatureChart::LimiterMode::NoLimiter,
                          EngineFeatureChart::LimiterMode::Average,
                          EngineFeatureChart::LimiterMode::AverageAndTarget,
                          EngineFeatureChart::LimiterMode::Thresholds};
                      chartPtr->SetLimiterMode(modes[i]);
                      const char* what[] = {
                          "No limiter lines",
                          "An average line computed from the plotted values (phase 1, slot 400)",
                          "Average + target — both captions are solved into the label plan, so they "
                          "cannot overprint the value labels",
                          "Three threshold lines — low / watch / critical"};
                      say(what[i]);
                  }, 2);
    rowOverlays->AddSpacer(20);
    AppendLabeledButtons(rowOverlays, "engine_hl_", "Highlight",
                  {"Off", "Band", "Ellipse", "Blob"},
                  [chartPtr, say](int i) {
                      const EngineFeatureChart::HighlightMode modes[] = {
                          EngineFeatureChart::HighlightMode::NoHighlight,
                          EngineFeatureChart::HighlightMode::Band,
                          EngineFeatureChart::HighlightMode::Ellipse,
                          EngineFeatureChart::HighlightMode::Blob};
                      chartPtr->SetHighlightMode(modes[i]);
                      const char* what[] = {
                          "Highlights off (AddHighlight/ClearHighlights)",
                          "ValueBand at zSlot 200 — a dashed expected-range wash "
                          "under the grid, its caption riding the label plan as a "
                          "HighlightLabel",
                          "ConfidenceEllipse per series at zSlot 700 — covariance "
                          "ellipses (95%) with mean markers over each series' bar "
                          "tips, in the series colours; the legend gains the "
                          "50%/95% ellipse key, because a key describes marks the "
                          "chart actually draws",
                          "Blob per series at zSlot 200 — Chaikin-smoothed padded "
                          "hulls hugging each series' points, washed under the grid"};
                      say(what[i]);
                  }, 0);
    annotationPage->AddChild(rowOverlays);
    optionTabs->AddTab("Annotations", annotationPage);

    // -------------------------------------------------------------------------
    // TAB: Legend — the shared ChartLegend component's whole surface. Placement
    // is two rows because ChartLegendPosition spells out 12 outside placements
    // (edge x alignment) and 4 insets; the rest of the rows are the component's
    // long tail, all of it reachable from the engine element.
    // -------------------------------------------------------------------------
    auto legendPage = MakeTabPage("engine_tab_legend");

    // Both rows describe one placement, so each releases the other's highlight.
    auto sideButtons = std::make_shared<OptionButtons>();
    auto insetButtons = std::make_shared<OptionButtons>();

    auto rowLegendPlace = MakeRow("engine_row_legend_place");
    *sideButtons = AppendLabeledButtons(rowLegendPlace, "engine_legend_side_", "Legend",
                  {"Off", "Top", "Bottom", "Left", "Right"},
                  [chartPtr, say, insetButtons](int i) {
                      SelectOnly(*insetButtons, 0);      // an edge leaves the inset
                      if (i == 0) {
                          chartPtr->SetShowLegendPanel(false);
                          say("Legend off — its reserved margin returns to the plot area");
                          return;
                      }
                      const EngineFeatureChart::LegendSide sides[] = {
                          EngineFeatureChart::LegendSide::Top,
                          EngineFeatureChart::LegendSide::Bottom,
                          EngineFeatureChart::LegendSide::Left,
                          EngineFeatureChart::LegendSide::Right};
                      chartPtr->SetShowLegendPanel(true);
                      chartPtr->SetLegendSide(sides[i - 1]);
                      const char* what[] = {
                          "Legend on the top edge — an outside placement reserves its edge in the "
                          "layout negotiation, so the plot area shrinks to make room",
                          "Legend on the bottom edge — Auto orientation flows top/bottom placements "
                          "horizontally and wraps the rows to the width",
                          "Legend on the left edge — Auto orientation makes side placements a "
                          "vertical list, wrapped into further columns when it runs out of height",
                          "Legend on the right edge — the engine's default placement"};
                      say(what[i - 1]);
                  }, 4);                                  // Right - the default
    rowLegendPlace->AddSpacer(20);
    AppendLabeledButtons(rowLegendPlace, "engine_legend_align_", "Align",
                  {"Start", "Center", "End"},
                  [chartPtr, say, sideButtons, insetButtons](int i) {
                      SelectOnly(*insetButtons, 0);
                      // Alignment applies to whichever edge is in force, so show
                      // that edge selected again (it may have been an inset, or
                      // the legend may have been switched off).
                      SelectOnly(*sideButtons,
                                 1 + static_cast<int>(chartPtr->GetLegendSide()));
                      const EngineFeatureChart::LegendAlign aligns[] = {
                          EngineFeatureChart::LegendAlign::Start,
                          EngineFeatureChart::LegendAlign::Center,
                          EngineFeatureChart::LegendAlign::End};
                      chartPtr->SetShowLegendPanel(true);
                      chartPtr->SetLegendAlign(aligns[i]);
                      const char* what[] = {
                          "Alignment Start — the legend hugs the beginning of its edge "
                          "(TopStart / LeftStart / …)",
                          "Alignment Center — centred along its edge",
                          "Alignment End — pushed to the end of its edge"};
                      say(what[i]);
                  }, 0);
    legendPage->AddChild(rowLegendPlace);

    auto rowLegendInset = MakeRow("engine_row_legend_inset");
    *insetButtons = AppendLabeledButtons(rowLegendInset, "engine_legend_inset_", "Inset",
                  {"Off", "Top left", "Top right", "Bottom left", "Bottom right"},
                  [chartPtr, say, sideButtons](int i) {
                      chartPtr->SetShowLegendPanel(true);
                      if (i == 0) {
                          chartPtr->SetLegendInsetCorner(-1);
                          SelectOnly(*sideButtons,
                                     1 + static_cast<int>(chartPtr->GetLegendSide()));
                          say("Back to an outside placement — the legend reserves its edge again");
                          return;
                      }
                      SelectOnly(*sideButtons, -1);      // the edge row no longer applies
                      chartPtr->SetLegendInsetCorner(i - 1);
                      say("Inset placement — the legend floats over the plot and reserves "
                          "nothing, so the plot keeps the full area; it still rides the label "
                          "plan as an obstacle the solved value labels steer around");
                  }, 0);
    rowLegendInset->AddSpacer(20);
    AppendLabeledButtons(rowLegendInset, "engine_legend_orient_", "Flow",
                  {"Auto", "Rows", "Column"},
                  [chartPtr, say](int i) {
                      const LegendOrientation orientations[] = {
                          LegendOrientation::Auto, LegendOrientation::Horizontal,
                          LegendOrientation::Vertical};
                      chartPtr->SetLegendOrientation(orientations[i]);
                      const char* what[] = {
                          "LegendOrientation::Auto — horizontal on the top/bottom edges, "
                          "vertical on the sides",
                          "Forced Horizontal — entries flow in rows and wrap to the width, "
                          "wherever the legend sits",
                          "Forced Vertical — one entry per line, wrapped into further columns "
                          "instead of being clipped"};
                      say(what[i]);
                  }, 0);
    legendPage->AddChild(rowLegendInset);

    auto rowLegendContent = MakeRow("engine_row_legend_content");
    AppendLabeledButtons(rowLegendContent, "engine_legend_key_", "Key",
                  {"Items", "Colour bar", "Sizes"},
                  [chartPtr, say](int i) {
                      const ChartLegendMode modes[] = {ChartLegendMode::Discrete,
                                                       ChartLegendMode::ColorBar,
                                                       ChartLegendMode::SizeLegend};
                      chartPtr->ShowLegendKey(modes[i]);
                      const char* what[] = {
                          "SetLegendMode(Discrete) — the classic swatch + label list",
                          "SetLegendMode(ColorBar) — a continuous Viridis ramp over the "
                          "value range with tick labels, the key a heatmap or contour "
                          "surface uses (Legend().SetColorBar)",
                          "SetLegendMode(SizeLegend) — sample circles keying a bubble-"
                          "size scale, largest first (Legend().SetSizeScale)"};
                      say(what[i]);
                  }, 0);
    rowLegendContent->AddSpacer(20);
    AppendLabeledButtons(rowLegendContent, "engine_legend_title_", "Heading",
                  {"Off", "On"},
                  [chartPtr, say](int i) {
                      chartPtr->SetLegendTitleShown(i == 1);
                      say(i == 1 ? "SetLegendTitle(\"Series\") — a heading above the entries, "
                                   "measured into the legend box like everything else"
                                 : "No legend heading");
                  }, 0);
    rowLegendContent->AddSpacer(20);
    AppendLabeledButtons(rowLegendContent, "engine_legend_values_", "Values",
                  {"Off", "On"},
                  [chartPtr, say](int i) {
                      chartPtr->SetLegendValuesShown(i == 1);
                      say(i == 1 ? "ChartLegendEntry::valueText — a right-aligned second column "
                                   "(here each series' total, through the chart's own number "
                                   "format), turning the key into a small value table"
                                 : "Entries show their label only");
                  }, 0);
    legendPage->AddChild(rowLegendContent);

    auto rowLegendStyle = MakeRow("engine_row_legend_style");
    AppendLabeledButtons(rowLegendStyle, "engine_legend_box_", "Box",
                  {"Framed", "Plain"},
                  [chartPtr, say](int i) {
                      chartPtr->SetLegendFramed(i == 0);
                      say(i == 0 ? "ChartLegendStyle drawBackground + drawBorder — the engine's "
                                   "framed default, which keeps an inset legend readable over "
                                   "the plot"
                                 : "An unboxed list — the component's plainer default, for a "
                                   "legend that sits outside the plot anyway");
                  }, 0);
    rowLegendStyle->AddSpacer(20);
    AppendLabeledButtons(rowLegendStyle, "engine_legend_max_", "Max rows",
                  {"All", "2"},
                  [chartPtr, say](int i) {
                      chartPtr->SetLegendMaxEntries(i == 0 ? 0u : 2u);
                      say(i == 0 ? "SetMaxEntries(0) — every entry is listed"
                                 : "SetMaxEntries(2) — the remaining entries collapse into a "
                                   "final \"...and N more\" row instead of growing the box");
                  }, 0);
    rowLegendStyle->AddSpacer(20);
    AppendLabeledButtons(rowLegendStyle, "engine_legend_custom_", "Custom key",
                  {"Off", "Limiters"},
                  [chartPtr, say](int i) {
                      chartPtr->SetLegendCustomKey(i == 1);
                      say(i == 1 ? "Legend().SetCustomArea — a host-drawn panel below the entries, "
                                   "richer than any swatch: here the average / target limiter "
                                   "lines. A key must describe marks the chart actually draws, so "
                                   "the ellipse highlight's own 50%/95% key takes precedence "
                                   "while that highlight is on"
                                 : "No custom key panel (Legend().ClearCustomArea)");
                  }, 0);
    legendPage->AddChild(rowLegendStyle);
    optionTabs->AddTab("Legend", legendPage);

    // -------------------------------------------------------------------------
    // TAB: Theme — colours only; repaint, no layout and no label re-solve
    // -------------------------------------------------------------------------
    auto themePage = MakeTabPage("engine_tab_theme");

    auto rowTheme = MakeRow("engine_row_theme");
    AppendLabeledButtons(rowTheme, "engine_theme_", "Theme",
                  {"Light", "Dark", "Vibrant", "Pastel", "Colorblind", "Ocean"},
                  [chartPtr, say](int i) {
                      const char* names[] = {"Light", "Dark", "Vibrant",
                                             "Pastel", "Colorblind", "Ocean"};
                      chartPtr->SetTheme(names[i]);
                      say(std::string("SetTheme(\"") + names[i] + "\") — one of the "
                          "14 built-in ChartThemes; furniture and palette change "
                          "together, and it is repaint-only: no layout, no label "
                          "re-solve. Also reachable as SetProperty(\"theme\", name)");
                  }, 3);          // Pastel - the showcase's default look
    themePage->AddChild(rowTheme);

    auto rowPalette = MakeRow("engine_row_palette");
    AppendLabeledButtons(rowPalette, "engine_palette_", "Palette",
                  {"Theme's", "Viridis N"},
                  [chartPtr, say](int i) {
                      if (i == 0) {
                          chartPtr->SetPalette(
                              chartPtr->GetTheme().name.empty()
                                  ? ChartThemes::Light().palette
                                  : ChartThemes::Get(chartPtr->GetTheme().name).palette);
                          say("Palette restored from the active theme");
                      } else {
                          chartPtr->SetPalette(ChartPalette::FromColormap(
                              HeatmapColormap::Viridis, 12));
                          say("SetPalette(ChartPalette::FromColormap(Viridis, 12)) — a "
                              "categorical palette of any requested size sampled from a "
                              "continuous colormap; ColorAt(i, count) spreads the three "
                              "series across the whole ramp");
                      }
                  }, 0);
    themePage->AddChild(rowPalette);
    optionTabs->AddTab("Theme", themePage);

    auto* optionTabsPtr = optionTabs.get();
    optionTabs->onTabChange = [optionTabsPtr](int /*oldIndex*/, int newIndex) {
        const int tabCount = static_cast<int>(std::size(kRowsPerTab));
        if (newIndex < 0 || newIndex >= tabCount) return;
        // Width stays auto so the panel keeps stretching to the page.
        optionTabsPtr->SetElementSize(
                CSSLayout::Dimension::Auto(),
                CSSLayout::Dimension::Px(OptionPanelHeight(kRowsPerTab[newIndex])));
    };

    optionTabs->SetActiveTab(0);
    root->AddChild(optionTabs);
    return root;
}

} // namespace UltraCanvas
