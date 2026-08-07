// Apps/DemoApp/UltraCanvasBarChartExamples.cpp
// Bar chart showcase on the chart engine: one bar-chart content class covering
// every bar arrangement and fill treatment. Each tab isolates one aspect -
// vertical vs horizontal projection, clustered series, stacked and 100%
// stacked, the fill styles, and bars filled with graphics (texture and
// pictogram). The engine supplies axes, grid, layout, legend, the solved
// value-label plan, hover hit-testing with tooltips, and the entrance
// animation; this file adds only the bar content contract.
// Version: 1.1.0
// Last Modified: 2026-08-07
// Author: UltraCanvas Framework
//
// Changelog:
//   v1.1.0 (2026-08-07):
//     - Moved onto the engine services added for the bar family: Category
//       scale slot padding, ObserveBarSeries/BuildBarSpans geometry, engine
//       hit regions with tooltips, the animation driver, legend swatch styles
//       and image paint patterns (texture fills now survive the Polar
//       projection, which the orientation dropdowns now offer).

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "Plugins/Charts/Engine/UltraCanvasChartSeries.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasButton.h"
#include "CSSLayout/CSSLayout.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace UltraCanvas {

namespace {

// =============================================================================
// THE BAR CHART. One engine content class covers single-series, clustered and
// (100%) stacked bars under every projection; the fill style only changes how
// a bar's quad is painted, never where it is. Geometry comes from the engine's
// BuildBarSpans, hover and tooltips from its hit regions, growth from its
// animation driver.
// =============================================================================

struct DemoBarSeries {
    std::string name;
    Color color;
    std::vector<double> values;      // one value per category
};

enum class BarFillStyle { Solid, Gradient, Rounded, Outline, Hatched, Texture, Pictogram };

class EngineBarChart : public UltraCanvasChartEngineElement {
public:
    EngineBarChart(const std::string& id, int x, int y, int w, int h)
        : UltraCanvasChartEngineElement(id, x, y, w, h) {
        SetAnimateOnDataChange(true, 0.7f);   // bars grow in on every data change
    }

    void SetData(std::vector<std::string> categories, std::vector<DemoBarSeries> seriesList) {
        categoryNames = std::move(categories);
        series = std::move(seriesList);
        SetShowLegend(series.size() > 1);
        RefreshLegend();
        MarkEngineDirty(ChartDirty::Data);
    }

    void SetArrangement(ChartBarArrangement a) {
        arrangement = a;                       // the value axis range changes
        MarkEngineDirty(ChartDirty::Data);
    }
    ChartBarArrangement GetArrangement() const { return arrangement; }

    void SetFillStyle(BarFillStyle style) {
        fillStyle = style;
        RefreshLegend();                       // the swatch mirrors the fill
        MarkEngineDirty(ChartDirty::Style);
    }

    // Image used by the Texture and Pictogram fill styles.
    void SetFillImage(const std::string& path) {
        fillImagePath = path;
        RefreshLegend();
        MarkEngineDirty(ChartDirty::Style);
    }

    void SetValueFormat(const std::string& prefix, const std::string& suffix, bool compact) {
        valuePrefix = prefix;
        valueSuffix = suffix;
        compactValues = compact;
        MarkEngineDirty(ChartDirty::Data);
    }

    void SetShowValueLabels(bool show) {
        showValueLabels = show;
        MarkEngineDirty(ChartDirty::Style);
    }

    // ---- engine contract ---------------------------------------------------

    void DescribeAxes(ChartAxisSet& axes) override {
        const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;

        ChartAxis value("value");
        value.side = horizontal ? ChartAxisSide::Bottom : ChartAxisSide::Left;
        if (arrangement == ChartBarArrangement::PercentStacked) {
            value.unitSuffix = "%";
        } else {
            value.unitPrefix = valuePrefix;
            value.unitSuffix = valueSuffix;
            value.compactNumbers = compactValues;
        }
        // The engine derives the range from the arrangement: every value for
        // grouped bars, the signed stack totals for stacked, percent extremes
        // for 100% stacked.
        ObserveBarSeries(value, SeriesValues(), LayoutOptions());
        axes.Add(value);

        // A padded Category axis: one slot per category, the outer slots as
        // wide as the inner ones, one tick per slot - no hand-rolled ranges.
        ChartAxis category("category");
        category.scale = ChartScale::Category;
        category.side = horizontal ? ChartAxisSide::Left : ChartAxisSide::Bottom;
        category.categories = categoryNames;
        category.categoryPadding = 0.5;
        axes.Add(category);

        SetGridAxis(0);
    }

    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override {
        if (series.empty() || categoryNames.empty() || frame.axes->Count() < 2) return;
        const ChartAxis& value = frame.axes->At(0);
        const ChartAxis& category = frame.axes->At(1);
        const bool orthogonal = GetProjectionKind() != ChartProjectionKind::Polar;
        const double zero = value.Normalize(0.0);
        const double progress = frame.animationProgress;

        for (const ChartBarSpan& span : BuildBarSpans(value, category,
                                                      categoryNames.size(),
                                                      SeriesValues(), LayoutOptions())) {
            // The whole stack grows out of the zero line together, so animated
            // segments never separate.
            const double v0 = zero + (span.v0 - zero) * progress;
            const double v1 = zero + (span.v1 - zero) * progress;
            const int64_t regionId = RegionId(span.seriesIndex, span.categoryIndex);
            const bool hovered = (HoveredRegionId() == regionId);
            const Color& color = series[span.seriesIndex].color;

            const std::vector<Point2Dd> quad =
                ProjectedQuad(*frame.projection, span.u0, v0, span.u1, v1);

            if (orthogonal) {
                const Point2Dd a = frame.projection->ToScreen(ChartNormalizedPoint(span.u0, v0));
                const Point2Dd b = frame.projection->ToScreen(ChartNormalizedPoint(span.u1, v1));
                const Rect2Dd rect(std::min(a.x, b.x), std::min(a.y, b.y),
                                   std::abs(b.x - a.x), std::abs(b.y - a.y));
                RenderBar(ctx, quad, rect, color, hovered, true);
                AddHitRegion(rect, regionId, TooltipFor(span, value));
            } else {
                Rect2Dd bbox = QuadBounds(quad);
                RenderBar(ctx, quad, bbox, color, hovered, false);
                AddHitRegion(quad, regionId, TooltipFor(span, value));
            }
        }
    }

    // Room for the value labels past the bar ends.
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override {
        if (!showValueLabels || series.empty()) return;
        ctx->SetFontSize(axisFontSize);
        double widest = 0.0;
        ChartAxis probe;
        probe.unitPrefix = valuePrefix;
        probe.unitSuffix = valueSuffix;
        probe.compactNumbers = compactValues;
        for (const auto& s : series) {
            for (double v : s.values) {
                widest = std::max(widest, static_cast<double>(
                    ctx->GetTextLineWidth(probe.FormatValue(v))));
            }
        }
        if (GetProjectionKind() == ChartProjectionKind::Horizontal) {
            request.Reserve(ChartAxisEdge::Right, widest + 12.0);
        } else {
            request.Reserve(ChartAxisEdge::Top, ctx->GetTextLineHeight("Ag") + 8.0);
        }
    }

    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override {
        if (!showValueLabels || Frame().axes->Count() < 2) return;
        // 100% stacked: every column is 100, so per-bar totals say nothing.
        if (arrangement == ChartBarArrangement::PercentStacked) return;
        const ChartAxis& value = Frame().axes->At(0);
        const ChartAxis& category = Frame().axes->At(1);
        ctx->SetFontSize(axisFontSize);
        const LabelSide side = (GetProjectionKind() == ChartProjectionKind::Horizontal)
                                   ? LabelSide::Right : LabelSide::Top;

        auto submit = [&](const std::string& text, double u, double v, int priority) {
            ChartLabelRequest r;
            r.text = text;
            r.klass = ChartLabelClass::ValueLabel;
            r.anchor = Frame().projection->ToScreen(ChartNormalizedPoint(u, v));
            const Size2Di size = ctx->GetTextLineDimensions(r.text);
            r.textSize = Size2Dd(size.width, size.height);
            r.preferredSide = side;
            r.allowSuppress = true;
            r.priority = priority;
            broker.Add(r);
        };

        const auto spans = BuildBarSpans(value, category, categoryNames.size(),
                                         SeriesValues(), LayoutOptions());
        if (arrangement == ChartBarArrangement::Stacked) {
            // One total on top of each stack: the furthest positive edge.
            for (size_t c = 0; c < categoryNames.size(); ++c) {
                double total = 0.0, top = value.Normalize(0.0);
                bool any = false;
                for (const ChartBarSpan& span : spans) {
                    if (span.categoryIndex != c) continue;
                    total += span.value;
                    top = std::max(top, std::max(span.v0, span.v1));
                    any = true;
                }
                if (!any) continue;
                submit(value.FormatValue(total),
                       category.Normalize(static_cast<double>(c)), top, 0);
            }
        } else {
            double maxValue = 0.0;
            for (const auto& s : series)
                for (double v : s.values) maxValue = std::max(maxValue, v);
            for (const ChartBarSpan& span : spans) {
                submit(value.FormatValue(span.value),
                       (span.u0 + span.u1) * 0.5, span.v1,
                       span.value >= maxValue ? 10 : 0);
            }
        }
    }

private:
    static int64_t RegionId(size_t s, size_t c) {
        return static_cast<int64_t>(s) * 1000 + static_cast<int64_t>(c);
    }

    std::string TooltipFor(const ChartBarSpan& span, const ChartAxis& value) const {
        std::string text = categoryNames[span.categoryIndex];
        if (series.size() > 1) text += " - " + series[span.seriesIndex].name;
        text += ": " + value.FormatValue(
            arrangement == ChartBarArrangement::PercentStacked ? span.plotted : span.value);
        return text;
    }

    std::vector<std::vector<double>> SeriesValues() const {
        std::vector<std::vector<double>> values;
        values.reserve(series.size());
        for (const auto& s : series) values.push_back(s.values);
        return values;
    }

    ChartBarLayoutOptions LayoutOptions() const {
        ChartBarLayoutOptions options;
        options.arrangement = arrangement;
        options.slotFill = 0.7;
        return options;
    }

    // The legend swatch mirrors how the bars are actually painted.
    void RefreshLegend() {
        std::vector<ChartLegendEntry> legend;
        for (const auto& s : series) {
            ChartLegendEntry entry;
            entry.label = s.name;
            entry.color = s.color;
            switch (fillStyle) {
                case BarFillStyle::Gradient: entry.swatch = ChartLegendSwatch::Gradient; break;
                case BarFillStyle::Outline:  entry.swatch = ChartLegendSwatch::Outline;  break;
                case BarFillStyle::Hatched:  entry.swatch = ChartLegendSwatch::Hatched;  break;
                case BarFillStyle::Texture:
                case BarFillStyle::Pictogram:
                    entry.swatch = ChartLegendSwatch::Image;
                    entry.imagePath = fillImagePath;
                    break;
                default: break;
            }
            legend.push_back(entry);
        }
        SetLegendEntries(legend);
    }

    static std::vector<Point2Dd> ProjectedQuad(const IChartProjection& projection,
                                               double u0, double v0, double u1, double v1) {
        std::vector<Point2Dd> quad;
        auto edge = [&](double ua, double va, double ub, double vb) {
            for (int s = 0; s <= 8; ++s) {
                const double t = s / 8.0;
                quad.push_back(projection.ToScreen(
                    ChartNormalizedPoint(ua + (ub - ua) * t, va + (vb - va) * t)));
            }
        };
        edge(u0, v0, u1, v0);
        edge(u1, v0, u1, v1);
        edge(u1, v1, u0, v1);
        edge(u0, v1, u0, v0);
        return quad;
    }

    static Rect2Dd QuadBounds(const std::vector<Point2Dd>& quad) {
        double minX = quad[0].x, maxX = quad[0].x, minY = quad[0].y, maxY = quad[0].y;
        for (const Point2Dd& p : quad) {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        }
        return Rect2Dd(minX, minY, maxX - minX, maxY - minY);
    }

    static Color Lighten(const Color& c, int amount) {
        return Color(static_cast<uint8_t>(std::min(255, c.r + amount)),
                     static_cast<uint8_t>(std::min(255, c.g + amount)),
                     static_cast<uint8_t>(std::min(255, c.b + amount)), c.a);
    }

    void ClipQuad(IRenderContext* ctx, const std::vector<Point2Dd>& quad) {
        ctx->ClearPath();
        ctx->MoveTo(quad[0].x, quad[0].y);
        for (size_t i = 1; i < quad.size(); ++i) ctx->LineTo(quad[i].x, quad[i].y);
        ctx->ClosePath();
        ctx->ClipPath();
    }

    void StrokeQuad(IRenderContext* ctx, const std::vector<Point2Dd>& quad,
                    const Color& color, float width) {
        ctx->SetStrokePaint(color);
        ctx->SetStrokeWidth(width);
        ctx->DrawLinePath(quad, true);
    }

    // One bar under any projection: `quad` is the projected outline (the exact
    // rectangle when orthogonal), `bbox` its screen bounds. The value-direction
    // styles orient by the projection; the shape-dependent ones (Rounded,
    // Pictogram) keep their rectangle-only fast path and fall back to Solid
    // under Polar.
    void RenderBar(IRenderContext* ctx, const std::vector<Point2Dd>& quad,
                   const Rect2Dd& bbox, const Color& seriesColor,
                   bool hovered, bool orthogonal) {
        const Color fill = hovered ? Lighten(seriesColor, 40) : seriesColor;
        const Color border(40, 40, 40, hovered ? 255 : 120);
        const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;
        const float borderWidth = hovered ? 2.0f : 1.0f;

        switch (fillStyle) {
        case BarFillStyle::Gradient: {
            // Light at the value end, saturated at the base.
            std::vector<GradientStop> stops = {
                GradientStop(0.0, Lighten(fill, 70)),
                GradientStop(1.0, fill)
            };
            auto gradient = horizontal
                ? ctx->CreateLinearGradientPattern(bbox.x + bbox.width, bbox.y, bbox.x, bbox.y, stops)
                : ctx->CreateLinearGradientPattern(bbox.x, bbox.y, bbox.x, bbox.y + bbox.height, stops);
            if (gradient) ctx->SetFillPaint(gradient); else ctx->SetFillPaint(fill);
            ctx->FillLinePath(quad);
            StrokeQuad(ctx, quad, border, borderWidth);
            break;
        }
        case BarFillStyle::Rounded: {
            if (orthogonal) {
                const float radius = static_cast<float>(
                    std::min({8.0, bbox.width * 0.5, bbox.height * 0.5}));
                ctx->DrawFilledRectangle(bbox, fill, borderWidth, border, radius);
            } else {
                ctx->SetFillPaint(fill);
                ctx->FillLinePath(quad);
                StrokeQuad(ctx, quad, border, borderWidth);
            }
            break;
        }
        case BarFillStyle::Outline: {
            Color ghost = seriesColor;
            ghost.a = hovered ? 80 : 36;
            ctx->SetFillPaint(ghost);
            ctx->FillLinePath(quad);
            StrokeQuad(ctx, quad, seriesColor, hovered ? 3.0f : 2.0f);
            break;
        }
        case BarFillStyle::Hatched: {
            Color background = Lighten(seriesColor, 90);
            background.a = hovered ? 255 : 220;
            ctx->SetFillPaint(background);
            ctx->FillLinePath(quad);
            ctx->PushState();
            ClipQuad(ctx, quad);                  // hatch stays inside the bar,
            ctx->SetStrokePaint(seriesColor);     // ring sectors included
            ctx->SetStrokeWidth(1.5f);
            const double span = bbox.width + bbox.height;
            for (double offset = 0.0; offset < span; offset += 7.0) {
                ctx->DrawLine(Point2Dd(bbox.x + offset, bbox.y),
                              Point2Dd(bbox.x + offset - bbox.height, bbox.y + bbox.height),
                              seriesColor);
            }
            ctx->PopState();
            StrokeQuad(ctx, quad, border, borderWidth);
            break;
        }
        case BarFillStyle::Texture: {
            std::shared_ptr<IPaintPattern> pattern;
            if (!fillImagePath.empty()) {
                pattern = ctx->CreateImagePattern(fillImagePath, bbox, ImageFitMode::Cover, false);
            }
            if (pattern) {
                ctx->SetFillPaint(pattern);
                ctx->FillLinePath(quad);
                if (hovered) {
                    ctx->SetFillPaint(Color(255, 255, 255, 60));
                    ctx->FillLinePath(quad);
                }
            } else {
                ctx->SetFillPaint(fill);          // backend without image patterns
                ctx->FillLinePath(quad);
            }
            StrokeQuad(ctx, quad, border, hovered ? 2.5f : 1.5f);
            break;
        }
        case BarFillStyle::Pictogram: {
            if (!fillImagePath.empty() && orthogonal) {
                ctx->PushState();
                ctx->ClipRect(bbox);
                if (horizontal) {
                    // Icons march from the base (left) toward the value end;
                    // the clip cuts the last icon to the exact fraction.
                    const double cell = std::min(bbox.height, 34.0);
                    for (double x = bbox.x; x < bbox.x + bbox.width; x += cell) {
                        ctx->DrawImage(fillImagePath,
                                       Rect2Dd(x, bbox.y + (bbox.height - cell) * 0.5, cell, cell),
                                       ImageFitMode::Contain);
                    }
                } else {
                    const double cell = std::min(bbox.width, 34.0);
                    for (double y = bbox.y + bbox.height - cell;
                         y > bbox.y - cell; y -= cell) {
                        ctx->DrawImage(fillImagePath,
                                       Rect2Dd(bbox.x + (bbox.width - cell) * 0.5, y, cell, cell),
                                       ImageFitMode::Contain);
                    }
                }
                ctx->PopState();
                if (hovered) StrokeQuad(ctx, quad, border, 2.0f);
            } else {
                ctx->SetFillPaint(fill);
                ctx->FillLinePath(quad);
                if (!orthogonal) StrokeQuad(ctx, quad, border, borderWidth);
            }
            break;
        }
        case BarFillStyle::Solid:
        default:
            ctx->SetFillPaint(fill);
            ctx->FillLinePath(quad);
            StrokeQuad(ctx, quad, border, borderWidth);
            break;
        }
    }

    std::vector<std::string> categoryNames;
    std::vector<DemoBarSeries> series;
    ChartBarArrangement arrangement = ChartBarArrangement::Grouped;
    BarFillStyle fillStyle = BarFillStyle::Solid;
    std::string fillImagePath;
    std::string valuePrefix, valueSuffix;
    bool compactValues = false;
    bool showValueLabels = true;
};

// =============================================================================
// SAMPLE DATA
// =============================================================================

std::shared_ptr<EngineBarChart> MakeFruitChart(const std::string& id) {
    auto chart = std::make_shared<EngineBarChart>(id, 0, 0, 0, 0);
    chart->SetData({"Shop 1", "Shop 2", "Shop 3", "Shop 4"},
                   {{"Apples",     Color( 38,  70,  83, 255), {33, 38, 31, 44}},
                    {"Bananas",    Color( 42, 157, 143, 255), {30, 25, 20, 25}},
                    {"Pears",      Color(142,  32,  85, 255), {21, 18, 18, 19}},
                    {"Pineapples", Color(244, 162,  97, 255), {10, 10, 10, 10}}});
    return chart;
}

std::shared_ptr<EngineBarChart> MakeRevenueChart(const std::string& id) {
    auto chart = std::make_shared<EngineBarChart>(id, 0, 0, 0, 0);
    chart->SetData({"Q1", "Q2", "Q3", "Q4"},
                   {{"Europe",   Color( 68, 119, 170, 255), {420000, 480000, 510000, 640000}},
                    {"Americas", Color(238, 102, 119, 255), {350000, 330000, 420000, 500000}},
                    {"Asia",     Color( 34, 136,  51, 255), {260000, 310000, 380000, 460000}}});
    chart->SetValueFormat("$", "", true);
    return chart;
}

std::shared_ptr<EngineBarChart> MakeCityChart(const std::string& id) {
    auto chart = std::make_shared<EngineBarChart>(id, 0, 0, 0, 0);
    chart->SetData({"Berlin", "Madrid", "Rome", "Vienna", "Prague"},
                   {{"Visitors", Color( 68, 119, 170, 255), {14.1, 10.4, 9.6, 7.9, 6.8}}});
    chart->SetValueFormat("", "M", false);
    return chart;
}

// =============================================================================
// PAGE ASSEMBLY
// =============================================================================

// Captions carry explicit line breaks and a fixed two-line height: a wrapped
// label re-measures against an unconstrained width on re-layout (see the chord
// chart page), so wrapping is not used here.
std::shared_ptr<UltraCanvasLabel> MakeCaption(const std::string& id, const std::string& text) {
    auto caption = std::make_shared<UltraCanvasLabel>(id, 0, 34, text);
    caption->SetFontSize(12);
    caption->SetTextColor(Color(90, 90, 100, 255));
    caption->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    return caption;
}

// A tab: caption on top, charts filling the rest in one flex row.
std::shared_ptr<UltraCanvasContainer> MakeChartRowTab(
    const std::string& idPrefix, const std::string& caption,
    const std::vector<std::shared_ptr<UltraCanvasUIElement>>& elements)
{
    auto tab = std::make_shared<UltraCanvasContainer>(idPrefix + "Tab");
    tab->SetPadding(6);
    tab->layout.SetFlexColumn().SetFlexGap(4)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

    tab->AddChild(MakeCaption(idPrefix + "Caption", caption));

    auto row = std::make_shared<UltraCanvasContainer>(idPrefix + "Row");
    row->layout.SetFlexRow().SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    row->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                   .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
    for (auto& element : elements) {
        element->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                           .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        row->AddChild(element);
    }
    tab->AddChild(row);
    return tab;
}

// Small control strip: projection dropdown plus optional extras.
std::shared_ptr<UltraCanvasContainer> MakeControlStrip(
    const std::string& idPrefix,
    const std::vector<std::shared_ptr<EngineBarChart>>& charts,
    std::function<void(UltraCanvasContainer&)> addExtras = nullptr)
{
    auto strip = std::make_shared<UltraCanvasContainer>(idPrefix + "Controls", 0, 30);
    strip->layout.SetFlexRow().SetFlexGap(10)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    strip->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    auto label = std::make_shared<UltraCanvasLabel>(idPrefix + "OrientLabel", 70, 20, "Projection:");
    label->SetFontSize(11);
    label->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    strip->AddChild(label);

    auto orientation = std::make_shared<UltraCanvasDropdown>(idPrefix + "Orientation", 130, 24);
    orientation->AddItem("Vertical");
    orientation->AddItem("Horizontal");
    orientation->AddItem("Polar");
    orientation->SetSelectedIndex(0);
    orientation->onSelectionChanged = [charts](int index, const DropdownItem&) {
        const ChartProjectionKind kind =
            index == 1 ? ChartProjectionKind::Horizontal
                       : index == 2 ? ChartProjectionKind::Polar
                                    : ChartProjectionKind::Vertical;
        for (const auto& chart : charts) chart->SetProjectionKind(kind);
    };
    orientation->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    strip->AddChild(orientation);

    auto labelsCheckbox = std::make_shared<UltraCanvasCheckbox>(idPrefix + "Labels", 130, 24);
    labelsCheckbox->SetText("Value labels");
    labelsCheckbox->SetChecked(true);
    labelsCheckbox->onStateChanged = [charts](CheckedState, CheckedState newState) {
        for (const auto& chart : charts) {
            chart->SetShowValueLabels(newState == CheckedState::Checked);
        }
    };
    labelsCheckbox->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    strip->AddChild(labelsCheckbox);

    auto replayButton = std::make_shared<UltraCanvasButton>(idPrefix + "Animate", 0, 0, 120, 24);
    replayButton->SetText("Replay grow-in");
    replayButton->onClick = [charts]() {
        for (const auto& chart : charts) chart->StartEngineAnimation(0.7f);
    };
    replayButton->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    strip->AddChild(replayButton);

    if (addExtras) addExtras(*strip);
    return strip;
}

// ----- TAB 1: NORMAL - VERTICAL AND HORIZONTAL -----
std::shared_ptr<UltraCanvasContainer> MakeOrientationTab() {
    auto vertical = MakeCityChart("BarOrientV");
    vertical->SetChartTitle("Vertical bars");

    auto horizontal = MakeCityChart("BarOrientH");
    horizontal->SetProjectionKind(ChartProjectionKind::Horizontal);
    horizontal->SetChartTitle("Horizontal bars - the same chart transposed");

    return MakeChartRowTab(
        "BarOrient",
        "A normal single-series bar chart. Both charts are the same content class and the same data;\n"
        "the engine's Horizontal projection transposes the right-hand one - no second implementation.",
        {vertical, horizontal});
}

// ----- TAB 2: CLUSTERED -----
std::shared_ptr<UltraCanvasContainer> MakeClusteredTab() {
    auto chart = MakeFruitChart("BarClustered");
    chart->SetChartTitle("Fruit sales per shop - four series, clustered");

    auto tab = MakeChartRowTab(
        "BarClustered",
        "Clustered (grouped) bars: each category slot holds one bar per series, side by side. The legend,\n"
        "grid, value labels, hover highlight and tooltips are engine services - hover a bar for its tooltip.",
        {chart});
    tab->AddChild(MakeControlStrip("BarClustered", {chart}));
    return tab;
}

// ----- TAB 3: STACKED AND 100% STACKED -----
std::shared_ptr<UltraCanvasContainer> MakeStackedTab() {
    auto chart = MakeRevenueChart("BarStacked");
    chart->SetArrangement(ChartBarArrangement::Stacked);
    chart->SetChartTitle("Quarterly revenue by region - stacked");

    auto tab = MakeChartRowTab(
        "BarStacked",
        "Stacked bars: series segments pile up per category and the label plan carries the stack totals.\n"
        "100% stacking rescales every column to its share of the total, with a fixed percent axis.",
        {chart});
    tab->AddChild(MakeControlStrip("BarStacked", {chart},
        [chart](UltraCanvasContainer& strip) {
            auto percent = std::make_shared<UltraCanvasCheckbox>("BarStackedPercent", 140, 24);
            percent->SetText("100% stacked");
            percent->onStateChanged = [chart](CheckedState, CheckedState newState) {
                chart->SetArrangement(newState == CheckedState::Checked
                                          ? ChartBarArrangement::PercentStacked
                                          : ChartBarArrangement::Stacked);
                chart->SetChartTitle(newState == CheckedState::Checked
                                         ? "Regional share of revenue - 100% stacked"
                                         : "Quarterly revenue by region - stacked");
            };
            percent->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
            strip.AddChild(percent);
        }));
    return tab;
}

// ----- TAB 4: RENDERING STYLES -----
std::shared_ptr<UltraCanvasContainer> MakeStylesTab() {
    auto makeStyled = [](const std::string& id, BarFillStyle style, const std::string& title) {
        auto chart = MakeCityChart(id);
        chart->SetFillStyle(style);
        chart->SetChartTitle(title);
        chart->SetShowValueLabels(false);       // keep the small multiples clean
        chart->SetShowLegend(true);             // the swatch mirrors the fill style
        return chart;
    };

    auto tab = std::make_shared<UltraCanvasContainer>("BarStylesTab");
    tab->SetPadding(6);
    tab->layout.SetFlexColumn().SetFlexGap(4)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    tab->AddChild(MakeCaption(
        "BarStylesCaption",
        "One dataset, four fill treatments of the same bar geometry: gradient, rounded, outline and hatched.\n"
        "Each chart's legend swatch is painted in its fill style - the engine's legend understands non-solid series."));

    auto addRow = [&tab](const std::string& id,
                         std::shared_ptr<UltraCanvasUIElement> left,
                         std::shared_ptr<UltraCanvasUIElement> right) {
        auto row = std::make_shared<UltraCanvasContainer>(id);
        row->layout.SetFlexRow().SetFlexGap(8)
                   .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
        row->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                       .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        for (auto& chart : {left, right}) {
            chart->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                             .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
            row->AddChild(chart);
        }
        tab->AddChild(row);
    };

    addRow("BarStylesRow1",
           makeStyled("BarStyleGradient", BarFillStyle::Gradient, "Gradient fill"),
           makeStyled("BarStyleRounded", BarFillStyle::Rounded, "Rounded bars"));
    addRow("BarStylesRow2",
           makeStyled("BarStyleOutline", BarFillStyle::Outline, "Outline style"),
           makeStyled("BarStyleHatched", BarFillStyle::Hatched, "Hatched fill"));
    return tab;
}

// ----- TAB 5: FILLED WITH GRAPHICS -----
std::shared_ptr<UltraCanvasContainer> MakeGraphicFillTab() {
    auto texture = std::make_shared<EngineBarChart>("BarTexture", 0, 0, 0, 0);
    texture->SetData({"2021", "2022", "2023", "2024", "2025"},
                     {{"Visitors", Color(68, 119, 170, 255), {6.2, 8.9, 11.4, 13.0, 14.6}}});
    texture->SetValueFormat("", "M", false);
    texture->SetFillStyle(BarFillStyle::Texture);
    texture->SetFillImage(NormalizePath(GetResourcesDir() + "media/images/landscape.jpg"));
    texture->SetChartTitle("Park visitors - bars filled with a photo texture");

    auto pictogram = std::make_shared<EngineBarChart>("BarPictogram", 0, 0, 0, 0);
    pictogram->SetData({"Mon", "Tue", "Wed", "Thu", "Fri"},
                       {{"Games", Color(238, 102, 119, 255), {3.5, 5.0, 4.2, 6.8, 8.4}}});
    pictogram->SetFillStyle(BarFillStyle::Pictogram);
    pictogram->SetFillImage(NormalizePath(GetResourcesDir() + "media/images/dice.png"));
    pictogram->SetChartTitle("Games played - pictogram bars from a repeated icon");

    auto tab = MakeChartRowTab(
        "BarGraphic",
        "Bars filled with graphics: the left chart floods each bar with an image paint pattern (texture -\n"
        "try Polar: the pattern survives ring sectors); the right stacks a repeated icon cut to the fraction.",
        {texture, pictogram});
    tab->AddChild(MakeControlStrip("BarGraphic", {texture, pictogram}));
    return tab;
}

} // namespace

// =============================================================================
// MAIN BAR CHART EXAMPLES CREATOR
// =============================================================================

std::shared_ptr<UltraCanvasUIElement> UltraCanvasDemoApplication::CreateBarChartsExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("BarChartContainer");
    container->SetBackgroundColor(Color(255, 255, 255, 255));
    container->SetPadding(8, 10);
    container->layout.SetFlexColumn().SetFlexGap(6)
                     .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    container->layoutItem.SetFlexGrow(1).SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    auto titleLabel = std::make_shared<UltraCanvasLabel>("BarTitleLabel", 0, 32);
    titleLabel->SetText("Bar Charts - Every Arrangement and Fill on the Chart Engine");
    titleLabel->SetFontSize(18);
    titleLabel->SetFontWeight(FontWeight::Bold);
    titleLabel->SetAlignment(TextAlignment::Center);
    titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
    titleLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    container->AddChild(titleLabel);

    auto descLabel = std::make_shared<UltraCanvasLabel>("BarDescLabel", 0, 42);
    descLabel->SetText(
        "One bar-chart content class on UltraCanvasChartEngineElement: axes, grid, layout, legend, value labels, bar geometry,\n"
        "hover tooltips and the grow-in animation are engine services. Tabs cover vertical/horizontal/polar, clustered, stacked, styles and graphic fills.");
    descLabel->SetFontSize(11);
    descLabel->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    container->AddChild(descLabel);

    auto tabs = std::make_shared<UltraCanvasTabbedContainer>("BarChartTabs", 0, 0, 0, 0);
    tabs->SetTabPosition(TabPosition::Top);
    tabs->SetTabStyle(TabStyle::Modern);
    tabs->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(0))
                    .SetAlignSelf(CSSLayout::AlignSelf::Stretch);

    tabs->AddTab("Vertical & Horizontal", MakeOrientationTab());
    tabs->AddTab("Clustered",             MakeClusteredTab());
    tabs->AddTab("Stacked",               MakeStackedTab());
    tabs->AddTab("Rendering Styles",      MakeStylesTab());
    tabs->AddTab("Graphic Fills",         MakeGraphicFillTab());

    container->AddChild(tabs);
    return container;
}

} // namespace UltraCanvas
