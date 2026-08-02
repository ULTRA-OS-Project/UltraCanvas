// Apps/DemoApp/UltraCanvasChartEngineExamples.cpp
// Chart engine showcase: one minimal bar-chart content class - DescribeAxes,
// RenderChartContent and a label collector - rendered three times under three
// projections. Everything else on screen (layout negotiation, axes, ticks,
// grid, limiters with solved captions, legend, value-label decluttering,
// hover overlay, clipping) is supplied by UltraCanvasChartEngineElement.
// Version: 1.0.0
// Last Modified: 2026-08-02
// Author: UltraCanvas Framework

#include "UltraCanvasDemo.h"
#include "Plugins/Charts/Engine/UltraCanvasChartEngineElement.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace UltraCanvas {

namespace {

// =============================================================================
// THE ENTIRE CHART. A native engine chart implements phase 2 and two small
// descriptors; the ~90 lines of this class are everything the bar chart adds
// on top of the engine.
// =============================================================================
class EngineDemoBarChart : public UltraCanvasChartEngineElement {
public:
    EngineDemoBarChart(const std::string& id, int x, int y, int w, int h)
        : UltraCanvasChartEngineElement(id, x, y, w, h) {}

    void SetData(std::vector<std::string> names, std::vector<double> values,
                 const Color& color) {
        categoryNames = std::move(names);
        barValues = std::move(values);
        barColor = color;
        MarkEngineDirty(ChartDirty::Data);
    }

    // ---- engine contract ---------------------------------------------------
    void DescribeAxes(ChartAxisSet& axes) override {
        // The projection transposes the content; the chart declares where its
        // axes sit for each orientation.
        const bool horizontal = GetProjectionKind() == ChartProjectionKind::Horizontal;

        ChartAxis value("value");
        value.side = horizontal ? ChartAxisSide::Bottom : ChartAxisSide::Left;
        value.unitPrefix = "$";
        value.compactNumbers = true;             // 1'250'000 -> "$1.3M"
        value.Observe(0.0);                      // bars grow from zero
        value.Observe(barValues);
        axes.Add(value);

        // Explicit ticks put one category label under each bar; the padded
        // range keeps the outer bars fully inside the plot.
        ChartAxis category("quarter");
        category.side = horizontal ? ChartAxisSide::Left : ChartAxisSide::Bottom;
        category.SetRange(-0.6, static_cast<double>(barValues.size()) - 0.4);
        for (size_t i = 0; i < barValues.size(); ++i) {
            category.tickValues.push_back(static_cast<double>(i));
        }
        std::vector<std::string> names = categoryNames;
        category.formatter = [names](double v) {
            const long i = std::lround(v);
            return (i >= 0 && static_cast<size_t>(i) < names.size())
                       ? names[static_cast<size_t>(i)] : std::string();
        };
        axes.Add(category);

        SetGridAxis(0);                          // gridlines from the value ticks
    }

    void RenderChartContent(IRenderContext* ctx, const ChartEngineFrame& frame) override {
        if (barValues.empty() || frame.axes->Count() < 2) return;
        const ChartAxis& value = frame.axes->At(0);
        const ChartAxis& category = frame.axes->At(1);

        for (size_t i = 0; i < barValues.size(); ++i) {
            const double center = category.Normalize(static_cast<double>(i));
            const double half = 0.35 / static_cast<double>(barValues.size());
            const double top = value.Normalize(barValues[i]);
            const double base = value.Normalize(0.0);

            // The bar as projected quad edges. Subdividing the edges is what
            // lets the SAME code render rectangles under Vertical/Horizontal
            // and ring sectors under Polar.
            std::vector<Point2Dd> quad;
            auto edge = [&](double u0, double v0, double u1, double v1) {
                for (int s = 0; s <= 8; ++s) {
                    const double t = s / 8.0;
                    quad.push_back(frame.projection->ToScreen(
                        ChartNormalizedPoint(u0 + (u1 - u0) * t, v0 + (v1 - v0) * t)));
                }
            };
            edge(center - half, base, center + half, base);
            edge(center + half, base, center + half, top);
            edge(center + half, top, center - half, top);
            edge(center - half, top, center - half, base);

            const bool hovered = (static_cast<int>(i) == hoveredBar);
            Color fill = barColor;
            if (hovered) {
                fill.r = static_cast<uint8_t>(std::min(255, fill.r + 40));
                fill.g = static_cast<uint8_t>(std::min(255, fill.g + 40));
                fill.b = static_cast<uint8_t>(std::min(255, fill.b + 40));
            }
            ctx->SetFillPaint(fill);
            ctx->FillLinePath(quad);
            ctx->SetStrokePaint(Color(40, 40, 40, hovered ? 255 : 120));
            ctx->SetStrokeWidth(hovered ? 2.0f : 1.0f);
            ctx->DrawLinePath(quad, true);
        }
    }

    // Measure pass: room for the value labels that sit past the bar ends -
    // above them (vertical) or to their right (horizontal).
    void MeasureContent(IRenderContext* ctx, ChartLayoutRequest& request) override {
        if (barValues.empty()) return;
        ctx->SetFontSize(axisFontSize);
        double widest = 0.0;
        ChartAxis probe;                       // same formatting as the value axis
        probe.unitPrefix = "$";
        probe.compactNumbers = true;
        for (double v : barValues) {
            widest = std::max(widest, static_cast<double>(
                ctx->GetTextLineWidth(probe.FormatValue(v))));
        }
        if (GetProjectionKind() == ChartProjectionKind::Horizontal) {
            request.Reserve(ChartAxisEdge::Right, widest + 12.0);
        } else {
            request.Reserve(ChartAxisEdge::Top,
                            ctx->GetTextLineHeight("Ag") + 8.0);
        }
    }

    // Value labels ride the engine's solved label plan: allowSuppress means a
    // crowded chart drops labels instead of overprinting them.
    void CollectChartLabels(IRenderContext* ctx, ChartLabelBroker& broker) override {
        if (Frame().axes->Count() < 2) return;
        const ChartAxis& value = Frame().axes->At(0);
        const ChartAxis& category = Frame().axes->At(1);
        ctx->SetFontSize(axisFontSize);
        const auto maxIt = std::max_element(barValues.begin(), barValues.end());
        for (size_t i = 0; i < barValues.size(); ++i) {
            ChartLabelRequest r;
            r.text = value.FormatValue(barValues[i]);
            r.klass = ChartLabelClass::ValueLabel;
            r.anchor = Frame().projection->ToScreen(ChartNormalizedPoint(
                category.Normalize(static_cast<double>(i)),
                value.Normalize(barValues[i])));
            const Size2Di size = ctx->GetTextLineDimensions(r.text);
            r.textSize = Size2Dd(size.width, size.height);
            r.preferredSide = (GetProjectionKind() == ChartProjectionKind::Horizontal)
                                  ? LabelSide::Right : LabelSide::Top;
            r.allowSuppress = true;
            // The tallest bar's label survives any declutter.
            if (maxIt != barValues.end() &&
                i == static_cast<size_t>(std::distance(barValues.begin(), maxIt))) {
                r.priority = 10;
            }
            broker.Add(r);
        }
    }

    bool OnEvent(const UCEvent& event) override {
        if (event.type == UCEventType::MouseMove) {
            const ChartNormalizedPoint p = Frame().projection
                ? Frame().projection->ToNormalized(
                      Point2Dd(event.pointer.x, event.pointer.y))
                : ChartNormalizedPoint(-1, -1);
            int hit = -1;
            if (Frame().axes->Count() >= 2 && p.v >= -0.02) {
                const ChartAxis& value = Frame().axes->At(0);
                const ChartAxis& category = Frame().axes->At(1);
                for (size_t i = 0; i < barValues.size(); ++i) {
                    const double center = category.Normalize(static_cast<double>(i));
                    const double half = 0.35 / static_cast<double>(barValues.size());
                    if (p.u >= center - half && p.u <= center + half &&
                        p.v <= value.Normalize(barValues[i]) + 0.01) {
                        hit = static_cast<int>(i);
                        break;
                    }
                }
            }
            if (hit != hoveredBar) {
                hoveredBar = hit;               // ChartDirty::Hover territory:
                RequestRedraw();                // no layout, no label re-solve
            }
        } else if (event.type == UCEventType::MouseLeave && hoveredBar != -1) {
            hoveredBar = -1;
            RequestRedraw();
        }
        return UltraCanvasChartElementBase::OnEvent(event);
    }

private:
    std::vector<std::string> categoryNames;
    std::vector<double> barValues;
    Color barColor = Color(68, 119, 170, 255);
    int hoveredBar = -1;
};

std::shared_ptr<EngineDemoBarChart> MakeRevenueChart(const std::string& id,
                                                     int x, int y, int w, int h) {
    auto chart = std::make_shared<EngineDemoBarChart>(id, x, y, w, h);
    chart->SetData({"Q1", "Q2", "Q3", "Q4", "Q5e"},
                   {380000, 545000, 470000, 690000, 615000},
                   Color(68, 119, 170, 255));
    return chart;
}

void AddCommonDecorations(std::shared_ptr<EngineDemoBarChart>& chart) {
    // Limiters (phase 1, slot 400): the average computed from the data, and a
    // target whose caption is solved into the label plan so it cannot collide
    // with the value labels.
    ChartLimiter average;
    average.kind = ChartLimiterKind::Average;
    average.axisIndex = 0;
    average.value = 540000.0;
    average.color = Color(90, 90, 90, 255);
    average.caption = "average";
    average.captionColor = Color(90, 90, 90, 255);
    chart->AddLimiter(average);

    ChartLimiter target;
    target.kind = ChartLimiterKind::Target;
    target.axisIndex = 0;
    target.value = 650000.0;
    target.color = Color(200, 60, 60, 255);
    target.caption = "target $650K";
    chart->AddLimiter(target);

    chart->SetShowLegend(true);
    chart->SetLegendEntries({{"Revenue", Color(68, 119, 170, 255)},
                             {"Target", Color(200, 60, 60, 255)}});
}

} // namespace

std::shared_ptr<UltraCanvasUIElement>
UltraCanvasDemoApplication::CreateChartEngineExamples() {
    auto container = std::make_shared<UltraCanvasContainer>("EngineContainer", 0, 0, 1200, 810);

    auto titleLabel = std::make_shared<UltraCanvasLabel>("EngineTitle", 20, 10, 1160, 35);
    titleLabel->SetText("Chart Engine - One Chart Definition, Every Engine Service");
    titleLabel->SetFontSize(18);
    titleLabel->SetFontWeight(FontWeight::Bold);
    titleLabel->SetAlignment(TextAlignment::Center);
    titleLabel->SetBackgroundColor(Color(240, 240, 250, 255));
    container->AddChild(titleLabel);

    auto descLabel = std::make_shared<UltraCanvasLabel>("EngineDesc", 20, 52, 1160, 46);
    descLabel->SetText(
        "All three charts below are ONE bar-chart class of ~90 lines implementing only the engine's "
        "content contract: DescribeAxes, RenderChartContent and a label collector. The engine renders "
        "in three phases - everything under the bars (background, tick-derived grid, limiters, axes), "
        "the clipped chart itself, and everything over it (solved labels, legend, hover overlay). "
        "Hover a bar to see the dirty model repaint without re-running layout or the label solver.");
    descLabel->SetFontSize(11);
    descLabel->SetWrap(TextWrap::WrapWord);
    container->AddChild(descLabel);

    // ===== 1: Vertical projection - the full option set =====
    auto verticalLabel = std::make_shared<UltraCanvasLabel>("EngineVLabel", 20, 106, 560, 22);
    verticalLabel->SetText("Vertical projection: compact $ axis, explicit category ticks, grid from ticks, limiters, legend, solved value labels");
    verticalLabel->SetFontSize(12);
    verticalLabel->SetFontWeight(FontWeight::Bold);
    container->AddChild(verticalLabel);

    auto vertical = MakeRevenueChart("engineVertical", 20, 132, 560, 320);
    AddCommonDecorations(vertical);
    vertical->SetChartTitle("Quarterly revenue");
    container->AddChild(vertical);

    // ===== 2: Horizontal projection - the SAME class, transposed =====
    auto horizontalLabel = std::make_shared<UltraCanvasLabel>("EngineHLabel", 600, 106, 580, 22);
    horizontalLabel->SetText("Horizontal projection: the same class - the engine transposes it");
    horizontalLabel->SetFontSize(12);
    horizontalLabel->SetFontWeight(FontWeight::Bold);
    container->AddChild(horizontalLabel);

    auto horizontal = MakeRevenueChart("engineHorizontal", 600, 132, 580, 320);
    horizontal->SetProjectionKind(ChartProjectionKind::Horizontal);
    AddCommonDecorations(horizontal);
    // Configured through the named-property surface, as a by-name creator
    // (template, script, module host) would do it:
    horizontal->SetProperty("title", std::string("Same chart, horizontal"));
    container->AddChild(horizontal);

    // ===== 3: Polar projection - the SAME class as ring sectors =====
    auto polarLabel = std::make_shared<UltraCanvasLabel>("EnginePLabel", 20, 468, 560, 22);
    polarLabel->SetText("Polar projection: still the same class - bars become ring sectors");
    polarLabel->SetFontSize(12);
    polarLabel->SetFontWeight(FontWeight::Bold);
    container->AddChild(polarLabel);

    auto polar = MakeRevenueChart("enginePolar", 20, 494, 560, 300);
    polar->SetProjectionKind(ChartProjectionKind::Polar);
    polar->SetChartTitle("Same chart, polar");
    container->AddChild(polar);

    // ===== Engine option checklist =====
    auto checklist = std::make_shared<UltraCanvasLabel>("EngineChecklist", 600, 468, 580, 330);
    checklist->SetText(
        "Engine services shown on this page:\n\n"
        "- Three-phase render: under (background, grid, limiters, axes) / content (clipped) / over (labels, legend, overlay)\n"
        "- Measure-solve layout: margins come from measured titles, tick labels and the legend - no hardcoded margins\n"
        "- Axis model: linear value axis with compact $ formatting; explicit tickValues for the category axis\n"
        "- Grid derived from the value axis ticks - grid and labels can never disagree\n"
        "- Limiters: average and target lines; captions are solved into the label plan\n"
        "- Label plan: value labels placed by the collision solver, allowSuppress declutters, the tallest bar's label has priority\n"
        "- Legend: measured, reserved in layout, an obstacle for solved labels\n"
        "- Projections: Vertical / Horizontal / Polar from one content implementation\n"
        "- Dirty model: hovering repaints only; data changes rebuild axes, layout and the label plan\n"
        "- IConfigurableElement: the horizontal chart's title was set via SetProperty(\"title\", ...)\n"
        "- Plot-area clipping: bars cannot paint over axes or legend");
    checklist->SetFontSize(11);
    checklist->SetWrap(TextWrap::WrapWord);
    checklist->SetBackgroundColor(Color(248, 248, 252, 255));
    container->AddChild(checklist);

    return container;
}

} // namespace UltraCanvas
