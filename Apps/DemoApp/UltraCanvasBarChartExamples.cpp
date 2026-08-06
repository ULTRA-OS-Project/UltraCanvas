// Apps/DemoApp/UltraCanvasBarChartExamples.cpp
// Bar chart showcase on the chart engine: one bar-chart content class covering
// every bar arrangement and fill treatment. Each tab isolates one aspect -
// vertical vs horizontal projection, clustered series, stacked and 100%
// stacked, the fill styles, and bars filled with graphics (texture and
// pictogram). The engine supplies axes, grid, layout, legend and the solved
// value-label plan; this file adds only the bar content contract.
// Version: 1.0.0
// Last Modified: 2026-08-06
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasTabbedContainer.h"
#include "UltraCanvasDropdown.h"
#include "UltraCanvasCheckbox.h"
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
// a bar's quad is painted, never where it is.
// =============================================================================

struct DemoBarSeries {
    std::string name;
    Color color;
    std::vector<double> values;      // one value per category
};

enum class BarArrangement { Grouped, Stacked, PercentStacked };

enum class BarFillStyle { Solid, Gradient, Rounded, Outline, Hatched, Texture, Pictogram };

class EngineBarChart : public UltraCanvasChartEngineElement {
public:
    EngineBarChart(const std::string& id, int x, int y, int w, int h)
        : UltraCanvasChartEngineElement(id, x, y, w, h) {}

    void SetData(std::vector<std::string> categories, std::vector<DemoBarSeries> seriesList) {
        categoryNames = std::move(categories);
        series = std::move(seriesList);
        std::vector<ChartLegendEntry> legend;
        for (const auto& s : series) legend.push_back({s.name, s.color});
        SetLegendEntries(legend);
        SetShowLegend(series.size() > 1);
        MarkEngineDirty(ChartDirty::Data);
    }

    void SetArrangement(BarArrangement a) {
        arrangement = a;                       // the value axis range changes
        MarkEngineDirty(ChartDirty::Data);
    }
    BarArrangement GetArrangement() const { return arrangement; }

    void SetFillStyle(BarFillStyle style) {
        fillStyle = style;
        MarkEngineDirty(ChartDirty::Style);
    }

    // Image used by the Texture and Pictogram fill styles.
    void SetFillImage(const std::string& path) {
        fillImagePath = path;
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
        value.unitPrefix = valuePrefix;
        value.unitSuffix = valueSuffix;
        value.compactNumbers = compactValues;
        if (arrangement == BarArrangement::PercentStacked) {
            value.SetRange(0.0, 100.0);
            value.unitPrefix.clear();
            value.unitSuffix = "%";
            value.compactNumbers = false;
        } else {
            value.Observe(0.0);                // bars grow from zero
            if (arrangement == BarArrangement::Stacked) {
                for (size_t c = 0; c < categoryNames.size(); ++c) {
                    value.Observe(CategoryTotal(c));
                }
            } else {
                for (const auto& s : series) value.Observe(s.values);
            }
        }
        axes.Add(value);

        // Padded linear axis with one explicit tick per category slot: the
        // engine's Category scale spans exactly 0..n-1, which would clip the
        // outer bars, so every bar chart hand-rolls this instead.
        ChartAxis category("category");
        category.side = horizontal ? ChartAxisSide::Left : ChartAxisSide::Bottom;
        category.SetRange(-0.6, static_cast<double>(categoryNames.size()) - 0.4);
        for (size_t i = 0; i < categoryNames.size(); ++i) {
            category.tickValues.push_back(static_cast<double>(i));
        }
        std::vector<std::string> names = categoryNames;
        category.formatter = [names](double v) {
            const long i = std::lround(v);
            return (i >= 0 && static_cast<size_t>(i) < names.size())
                       ? names[static_cast<size_t>(i)] : std::string();
        };
        axes.Add(category);

        SetGridAxis(0);
    }

    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override {
        hoverRects.clear();
        if (series.empty() || categoryNames.empty() || frame.axes->Count() < 2) return;
        const bool orthogonal = GetProjectionKind() != ChartProjectionKind::Polar;

        ForEachBar(*frame.axes, [&](size_t s, size_t c, double displayValue,
                                    double u0, double v0, double u1, double v1) {
            const bool hovered = (static_cast<int>(s) == hoveredSeries &&
                                  static_cast<int>(c) == hoveredCategory);
            if (orthogonal) {
                const Point2Dd a = frame.projection->ToScreen(ChartNormalizedPoint(u0, v0));
                const Point2Dd b = frame.projection->ToScreen(ChartNormalizedPoint(u1, v1));
                const Rect2Dd rect(std::min(a.x, b.x), std::min(a.y, b.y),
                                   std::abs(b.x - a.x), std::abs(b.y - a.y));
                RenderBarRect(ctx, rect, series[s].color, hovered);
                hoverRects.push_back({rect, s, c});
            } else {
                // Polar (and any future projection): the bar as subdivided
                // projected quad edges; the fancy fills need an axis-aligned
                // rectangle, so they fall back to a solid fill here.
                std::vector<Point2Dd> quad = ProjectedQuad(*frame.projection, u0, v0, u1, v1);
                ctx->SetFillPaint(hovered ? Lighten(series[s].color, 40) : series[s].color);
                ctx->FillLinePath(quad);
                ctx->SetStrokePaint(Color(40, 40, 40, 120));
                ctx->SetStrokeWidth(1.0f);
                ctx->DrawLinePath(quad, true);
            }
        });
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
        if (arrangement == BarArrangement::PercentStacked) return;
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

        if (arrangement == BarArrangement::Stacked) {
            // One total on top of each stack.
            for (size_t c = 0; c < categoryNames.size(); ++c) {
                const double total = CategoryTotal(c);
                submit(value.FormatValue(total),
                       category.Normalize(static_cast<double>(c)),
                       value.Normalize(total), 0);
            }
        } else {
            double maxValue = 0.0;
            for (const auto& s : series)
                for (double v : s.values) maxValue = std::max(maxValue, v);
            ForEachBar(*Frame().axes, [&](size_t s, size_t c, double displayValue,
                                          double u0, double v0, double u1, double v1) {
                submit(value.FormatValue(series[s].values[c]),
                       (u0 + u1) * 0.5, v1, series[s].values[c] >= maxValue ? 10 : 0);
            });
        }
    }

    bool OnEvent(const UCEvent& event) override {
        if (event.type == UCEventType::MouseMove) {
            int hitSeries = -1, hitCategory = -1;
            for (const auto& hr : hoverRects) {
                if (event.pointer.x >= hr.rect.x && event.pointer.x <= hr.rect.x + hr.rect.width &&
                    event.pointer.y >= hr.rect.y && event.pointer.y <= hr.rect.y + hr.rect.height) {
                    hitSeries = static_cast<int>(hr.seriesIndex);
                    hitCategory = static_cast<int>(hr.categoryIndex);
                    break;
                }
            }
            if (hitSeries != hoveredSeries || hitCategory != hoveredCategory) {
                hoveredSeries = hitSeries;      // Hover territory: repaint only,
                hoveredCategory = hitCategory;  // no layout, no label re-solve
                RequestRedraw();
            }
        } else if (event.type == UCEventType::MouseLeave && hoveredSeries != -1) {
            hoveredSeries = hoveredCategory = -1;
            RequestRedraw();
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

private:
    struct BarHoverRect {
        Rect2Dd rect;
        size_t seriesIndex = 0;
        size_t categoryIndex = 0;
    };

    double CategoryTotal(size_t c) const {
        double total = 0.0;
        for (const auto& s : series)
            if (c < s.values.size()) total += s.values[c];
        return total;
    }

    // Normalised (u, v) extents of every bar under the current arrangement.
    // Shared by rendering, hover geometry and the label collector so the three
    // can never disagree.
    template <typename Fn>
    void ForEachBar(const ChartAxisSet& axes, Fn&& fn) const {
        const ChartAxis& value = axes.At(0);
        const ChartAxis& category = axes.At(1);
        const size_t n = categoryNames.size();
        const size_t m = series.size();
        if (n == 0 || m == 0) return;
        const double slotHalf = 0.35 / static_cast<double>(n);   // bars fill 70% of a slot

        for (size_t c = 0; c < n; ++c) {
            const double center = category.Normalize(static_cast<double>(c));
            if (arrangement == BarArrangement::Grouped) {
                const double barWidth = (slotHalf * 2.0) / static_cast<double>(m);
                for (size_t s = 0; s < m; ++s) {
                    if (c >= series[s].values.size()) continue;
                    const double u0 = center - slotHalf + barWidth * static_cast<double>(s);
                    fn(s, c, series[s].values[c],
                       u0, value.Normalize(0.0),
                       u0 + barWidth, value.Normalize(series[s].values[c]));
                }
            } else {
                const double total = CategoryTotal(c);
                const double scale = (arrangement == BarArrangement::PercentStacked && total > 0.0)
                                         ? 100.0 / total : 1.0;
                double running = 0.0;
                for (size_t s = 0; s < m; ++s) {
                    if (c >= series[s].values.size()) continue;
                    const double v = series[s].values[c] * scale;
                    fn(s, c, v,
                       center - slotHalf, value.Normalize(running),
                       center + slotHalf, value.Normalize(running + v));
                    running += v;
                }
            }
        }
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

    static Color Lighten(const Color& c, int amount) {
        return Color(static_cast<uint8_t>(std::min(255, c.r + amount)),
                     static_cast<uint8_t>(std::min(255, c.g + amount)),
                     static_cast<uint8_t>(std::min(255, c.b + amount)), c.a);
    }

    void RenderBarRect(IRenderContext* ctx, const Rect2Dd& rect,
                       const Color& seriesColor, bool hovered) {
        const Color fill = hovered ? Lighten(seriesColor, 40) : seriesColor;
        const Color border(40, 40, 40, hovered ? 255 : 120);
        const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;

        switch (fillStyle) {
        case BarFillStyle::Gradient: {
            // Light at the value end, saturated at the base.
            std::vector<GradientStop> stops = {
                GradientStop(0.0, Lighten(fill, 70)),
                GradientStop(1.0, fill)
            };
            auto gradient = horizontal
                ? ctx->CreateLinearGradientPattern(rect.x + rect.width, rect.y, rect.x, rect.y, stops)
                : ctx->CreateLinearGradientPattern(rect.x, rect.y, rect.x, rect.y + rect.height, stops);
            std::vector<Point2Dd> path = {
                {rect.x, rect.y}, {rect.x + rect.width, rect.y},
                {rect.x + rect.width, rect.y + rect.height}, {rect.x, rect.y + rect.height}};
            ctx->SetFillPaint(gradient);
            ctx->FillLinePath(path);
            ctx->SetStrokePaint(border);
            ctx->SetStrokeWidth(hovered ? 2.0f : 1.0f);
            ctx->DrawLinePath(path, true);
            break;
        }
        case BarFillStyle::Rounded: {
            const float radius = static_cast<float>(
                std::min({8.0, rect.width * 0.5, rect.height * 0.5}));
            ctx->DrawFilledRectangle(rect, fill, hovered ? 2.0f : 1.0f, border, radius);
            break;
        }
        case BarFillStyle::Outline: {
            Color ghost = seriesColor;
            ghost.a = hovered ? 80 : 36;
            ctx->DrawFilledRectangle(rect, ghost);
            ctx->SetStrokePaint(seriesColor);
            ctx->SetStrokeWidth(hovered ? 3.0f : 2.0f);
            std::vector<Point2Dd> path = {
                {rect.x, rect.y}, {rect.x + rect.width, rect.y},
                {rect.x + rect.width, rect.y + rect.height}, {rect.x, rect.y + rect.height}};
            ctx->DrawLinePath(path, true);
            break;
        }
        case BarFillStyle::Hatched: {
            Color background = Lighten(seriesColor, 90);
            background.a = hovered ? 255 : 220;
            ctx->DrawFilledRectangle(rect, background);
            ctx->PushState();
            ctx->ClipRect(rect);
            ctx->SetStrokePaint(seriesColor);
            ctx->SetStrokeWidth(1.5f);
            const double span = rect.width + rect.height;
            for (double offset = 0.0; offset < span; offset += 7.0) {
                ctx->DrawLine(Point2Dd(rect.x + offset, rect.y),
                              Point2Dd(rect.x + offset - rect.height, rect.y + rect.height),
                              seriesColor);
            }
            ctx->PopState();
            ctx->DrawFilledRectangle(rect, Colors::Transparent, hovered ? 2.0f : 1.0f, border);
            break;
        }
        case BarFillStyle::Texture: {
            if (!fillImagePath.empty()) {
                ctx->PushState();
                ctx->ClipRect(rect);
                ctx->DrawImage(fillImagePath, rect, ImageFitMode::Cover);
                if (hovered) {
                    ctx->DrawFilledRectangle(rect, Color(255, 255, 255, 60));
                }
                ctx->PopState();
            } else {
                ctx->DrawFilledRectangle(rect, fill);
            }
            ctx->DrawFilledRectangle(rect, Colors::Transparent, hovered ? 2.5f : 1.5f, border);
            break;
        }
        case BarFillStyle::Pictogram: {
            if (!fillImagePath.empty()) {
                ctx->PushState();
                ctx->ClipRect(rect);
                if (horizontal) {
                    // Icons march from the base (left) toward the value end;
                    // the clip cuts the last icon to the exact fraction.
                    const double cell = std::min(rect.height, 34.0);
                    for (double x = rect.x; x < rect.x + rect.width; x += cell) {
                        ctx->DrawImage(fillImagePath,
                                       Rect2Dd(x, rect.y + (rect.height - cell) * 0.5, cell, cell),
                                       ImageFitMode::Contain);
                    }
                } else {
                    const double cell = std::min(rect.width, 34.0);
                    for (double y = rect.y + rect.height - cell;
                         y > rect.y - cell; y -= cell) {
                        ctx->DrawImage(fillImagePath,
                                       Rect2Dd(rect.x + (rect.width - cell) * 0.5, y, cell, cell),
                                       ImageFitMode::Contain);
                    }
                }
                ctx->PopState();
                if (hovered) {
                    ctx->DrawFilledRectangle(rect, Colors::Transparent, 2.0f, border);
                }
            } else {
                ctx->DrawFilledRectangle(rect, fill);
            }
            break;
        }
        case BarFillStyle::Solid:
        default:
            ctx->DrawFilledRectangle(rect, fill, hovered ? 2.0f : 1.0f, border);
            break;
        }
    }

    std::vector<std::string> categoryNames;
    std::vector<DemoBarSeries> series;
    BarArrangement arrangement = BarArrangement::Grouped;
    BarFillStyle fillStyle = BarFillStyle::Solid;
    std::string fillImagePath;
    std::string valuePrefix, valueSuffix;
    bool compactValues = false;
    bool showValueLabels = true;

    std::vector<BarHoverRect> hoverRects;    // screen rects of the last render
    int hoveredSeries = -1;
    int hoveredCategory = -1;
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

// Small control strip: orientation dropdown plus optional extras.
std::shared_ptr<UltraCanvasContainer> MakeControlStrip(
    const std::string& idPrefix,
    const std::vector<std::shared_ptr<EngineBarChart>>& charts,
    std::function<void(UltraCanvasContainer&)> addExtras = nullptr)
{
    auto strip = std::make_shared<UltraCanvasContainer>(idPrefix + "Controls", 0, 30);
    strip->layout.SetFlexRow().SetFlexGap(10)
                 .SetFlexAlignItems(CSSLayout::AlignItems::Center);
    strip->layoutItem.SetFlexGrow(0).SetFlexShrink(0);

    auto label = std::make_shared<UltraCanvasLabel>(idPrefix + "OrientLabel", 70, 20, "Orientation:");
    label->SetFontSize(11);
    label->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
    strip->AddChild(label);

    auto orientation = std::make_shared<UltraCanvasDropdown>(idPrefix + "Orientation", 130, 24);
    orientation->AddItem("Vertical");
    orientation->AddItem("Horizontal");
    orientation->SetSelectedIndex(0);
    orientation->onSelectionChanged = [charts](int index, const DropdownItem&) {
        for (const auto& chart : charts) {
            chart->SetProjectionKind(index == 1 ? ChartProjectionKind::Horizontal
                                                : ChartProjectionKind::Vertical);
        }
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
        "Clustered (grouped) bars: each category slot holds one bar per series, side by side.\n"
        "The legend, the grid and the solved value labels come from the engine. Hover a bar to highlight it.",
        {chart});
    tab->AddChild(MakeControlStrip("BarClustered", {chart}));
    return tab;
}

// ----- TAB 3: STACKED AND 100% STACKED -----
std::shared_ptr<UltraCanvasContainer> MakeStackedTab() {
    auto chart = MakeRevenueChart("BarStacked");
    chart->SetArrangement(BarArrangement::Stacked);
    chart->SetChartTitle("Quarterly revenue by region - stacked");

    auto tab = MakeChartRowTab(
        "BarStacked",
        "Stacked bars: series segments pile up per category and the label plan carries the stack totals.\n"
        "100% stacking rescales every column to its share of the total, with a fixed 0-100% axis.",
        {chart});
    tab->AddChild(MakeControlStrip("BarStacked", {chart},
        [chart](UltraCanvasContainer& strip) {
            auto percent = std::make_shared<UltraCanvasCheckbox>("BarStackedPercent", 140, 24);
            percent->SetText("100% stacked");
            percent->onStateChanged = [chart](CheckedState, CheckedState newState) {
                chart->SetArrangement(newState == CheckedState::Checked
                                          ? BarArrangement::PercentStacked
                                          : BarArrangement::Stacked);
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
        return chart;
    };

    auto tab = std::make_shared<UltraCanvasContainer>("BarStylesTab");
    tab->SetPadding(6);
    tab->layout.SetFlexColumn().SetFlexGap(4)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
    tab->AddChild(MakeCaption(
        "BarStylesCaption",
        "One dataset, four fill treatments of the same bar geometry: a value-direction gradient,\n"
        "rounded corners, an outline (hollow) style and a hatched fill built from a clipped line pattern."));

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
        "Bars filled with graphics: the left chart clips a photo to each bar (texture fill); the right\n"
        "stacks a repeated icon and lets the clip cut the top one to the exact fractional value (pictogram).",
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
        "One bar-chart content class on UltraCanvasChartEngineElement: axes, grid, layout, legend and value labels are engine services.\n"
        "The tabs cover the bar chart family: vertical and horizontal, clustered series, stacked and 100% stacked, fill styles, and graphic fills.");
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
