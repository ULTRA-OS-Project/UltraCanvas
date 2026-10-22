// Tests/ChartElementBehaviourTest.cpp
// The basic chart elements (line, bar, scatter, area), the waterfall chart and
// the population-pyramid factory, headless.
//
//  1. The line chart's dots follow SetPointRadius (they had a radius of their
//     own that hid the base class's).
//  2. Zoom and pan are off by default, a wheel turn or a drag the chart does
//     nothing with is left to the parent, and with them on the line, area and
//     scatter charts zoom and pan the x axis.
//  3. The plot area follows a new size from SetBounds or the layout's Arrange.
//  4. A CSV's first line is a header only when its x and y are not numbers,
//     so a first row labelled "May" is kept.
//  5. A bar chart's value axis includes 0 and bars rise from the zero line.
//  6. The chart paints the element's own background colour.
//  7. Waterfall: SetShowValueLabels through the base class reaches it; the
//     running totals are not recomputed on every read; LoadFromArray and
//     LoadFromCSV load steps.
//  8. CreatePopulationPyramid lays out its row labels as rows.
//  9. The hover ring is drawn around the point where the chart draws it,
//     by index in DataLabel mode.
// 10. A left press, and its release, are taken only when they start and end
//     a pan; CSV rows that cannot be read are skipped (were plotted at 0,0).
// 12. Axis ticks fall on whole steps (1, 2, 2.5, 5 x 10^n) and the grid on
//     them; a bar chart whose smallest value is 0 starts its axis at 0 (it
//     was padded to -5 % first); a fixed value range; horizontal bars.
// 13. A chart with no data says what SetEmptyMessage set (a line and a
//     smaller one under it), centred, in SetEmptyMessageColor, on the
//     chart's own background (it was a fixed grey with the text placed
//     60 px left of the middle); an empty message leaves the background.
//     SetEmptyIcon puts an icon over it, in one colour and on a badge.
// 14. SetMinimumValueSpan: a value axis at least that wide, centred on the
//     data, so a steady reading does not fill the plot as a sawtooth.
// 11. Eleven charts and diagrams defined a private InvalidateLayout() that
//     only cleared their own cache. It overrode the layout engine's, so a
//     framework invalidation (a new size, visibility) never reached the
//     parent. Each now drops its cache and calls the engine's too.
//
// Pictures are drawn into an offscreen surface and read back pixel by pixel;
// plot areas and ranges are read through a probe subclass.
// Version: 1.7.0 - a minimum value span
// Version: 1.6.0 - the empty-state icon
// Version: 1.5.0 - the empty-state message
// Version: 1.4.0 - whole-step ticks; bars from zero with a zero value; value range; horizontal bars
// Version: 1.3.0 - a framework invalidation of any of eleven charts reaches the parent
// Version: 1.2.0 - a press is taken only when it starts a pan; bad CSV rows are skipped
// Version: 1.1.0 - the hover ring in DataLabel mode
// Last Modified: 2026-10-08
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasChordChart.h"
#include "Plugins/Charts/UltraCanvasCircularProgressChart.h"
#include "Plugins/Charts/UltraCanvasPolarChart.h"
#include "Plugins/Charts/UltraCanvasRadialBarChart.h"
#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "Plugins/Charts/UltraCanvasTimelineChart.h"
#include "Plugins/Diagrams/UltraCanvasFishboneDiagram.h"
#include "Plugins/Diagrams/UltraCanvasMatrixDiagram.h"
#include "Plugins/Diagrams/UltraCanvasParliamentDiagram.h"
#include "Plugins/Diagrams/UltraCanvasSWOTDiagram.h"
#include "Plugins/Diagrams/UltraCanvasTimelineDiagram.h"
#include "Plugins/Diagrams/UltraCanvasWordCloudDiagram.h"
#include "UltraCanvasContainer.h"
#include "Plugins/Charts/UltraCanvasWaterfallChart.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasImage.h"
#include "UltraCanvasPathUtf8.h"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_count = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
    ++g_count;
}

// A white surface to draw one chart into, read back pixel by pixel.
struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;
    int width = 0;
    int height = 0;

    Canvas(int w, int h) : width(w), height(h) {
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!ctx) return;
        cr = static_cast<cairo_t*>(ctx->GetNativeContext());
        Clear();
    }

    void Clear() {
        if (!cr) return;
        cairo_save(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    Color At(double x, double y) const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        const auto px = *reinterpret_cast<const uint32_t*>(
                data + static_cast<int>(std::lround(y)) * stride + static_cast<int>(std::lround(x)) * 4);
        return Color((px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF, 255);
    }

    std::vector<uint32_t> Pixels() const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const int stride = cairo_image_surface_get_stride(s);
        std::vector<uint32_t> out;
        for (int y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const uint32_t*>(data + y * stride);
            out.insert(out.end(), row, row + width);
        }
        return out;
    }
};

bool Near(const Color& a, const Color& b, int tolerance = 12) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

size_t CountDifferences(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    size_t n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i] != b[i]) ++n;
    }
    return n;
}

// Reads what the chart worked out: its plot area, its data ranges and where
// it places a point.
template <typename Chart>
class Probe : public Chart {
public:
    using Chart::Chart;

    ChartPlotArea PlotArea() { this->UpdateRenderingCache(); return this->cachedPlotArea; }
    ChartDataBounds Bounds() { this->UpdateRenderingCache(); return this->cachedDataBounds; }
    Color ChartBackground() const { return this->backgroundColor; }

    Point2Dd ScreenOf(size_t index) {
        this->UpdateRenderingCache();
        auto point = this->dataSource->GetPoint(index);
        return this->GetDataPointScreenPosition(index, point);
    }

    double ScreenY(double value) {
        this->UpdateRenderingCache();
        return ChartCoordinateTransform(this->cachedPlotArea, this->cachedDataBounds).DataToScreenY(value);
    }

    void HoverPoint(size_t index) { this->hoveredPointIndex = index; }

    double DataXAt(double screenX) {
        this->UpdateRenderingCache();
        return ChartCoordinateTransform(this->cachedPlotArea, this->cachedDataBounds).ScreenToDataX(screenX);
    }
};

UCEvent Wheel(double x, double y, int delta) {
    UCEvent e;
    e.type = UCEventType::MouseWheel;
    e.pointer = Point2Di(static_cast<int>(x), static_cast<int>(y));
    e.wheelDelta = delta;
    return e;
}

UCEvent Mouse(UCEventType type, double x, double y) {
    UCEvent e;
    e.type = type;
    e.pointer = Point2Di(static_cast<int>(x), static_cast<int>(y));
    e.button = (type == UCEventType::MouseMove) ? UCMouseButton::NoneButton : UCMouseButton::Left;
    return e;
}

std::shared_ptr<ChartDataVector> Series(const std::vector<std::pair<double, double>>& xy) {
    auto data = std::make_shared<ChartDataVector>();
    for (const auto& [x, y] : xy) data->AddPoint(ChartDataPoint(x, y));
    return data;
}

std::shared_ptr<ChartDataVector> Ramp(int count) {
    auto data = std::make_shared<ChartDataVector>();
    for (int i = 0; i < count; ++i) {
        data->AddPoint(ChartDataPoint(i, 10.0 + (i % 3), 0, "P" + std::to_string(i)));
    }
    return data;
}

fs::path WriteFile(const fs::path& dir, const std::string& name, const std::string& text) {
    fs::path p = dir / PathFromUtf8(name);
    std::ofstream out(p, std::ios::binary);
    out << text;
    return p;
}

// ---------------------------------------------------------------------------

void TestLineDotsFollowPointRadius() {
    std::cout << "1. line chart dots follow SetPointRadius\n";
    auto chart = std::make_shared<Probe<UltraCanvasLineChartElement>>("line", 0, 0, 400, 300);
    chart->SetDataSource(Series({{0, 10}, {1, 10}, {2, 10}, {3, 20}}));
    chart->SetShowGrid(false);
    chart->SetShowAxes(false);
    chart->SetShowValueLabels(false);
    chart->SetShowDataPoints(true);
    chart->SetLineColor(Color(0, 0, 255));
    chart->SetPointColor(Color(255, 0, 0));
    chart->SetPointRadius(10.0f);

    Canvas canvas(400, 300);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 300));
    Point2Dd dot = chart->ScreenOf(1);
    Check(Near(canvas.At(dot.x, dot.y - 7), Color(255, 0, 0)),
          "7 px above a dot is inside it with radius 10");
    Check(Near(canvas.At(dot.x, dot.y + 7), Color(255, 0, 0)),
          "7 px below a dot is inside it with radius 10");
    Check(!Near(canvas.At(dot.x, dot.y - 13), Color(255, 0, 0)),
          "13 px above it is not");
}

void TestZoomAndPan() {
    std::cout << "2. zoom and pan\n";
    auto line = std::make_shared<Probe<UltraCanvasLineChartElement>>("line", 0, 0, 400, 300);
    auto area = std::make_shared<Probe<UltraCanvasAreaChartElement>>("area", 0, 0, 400, 300);
    auto scatter = std::make_shared<Probe<UltraCanvasScatterPlotElement>>("scatter", 0, 0, 400, 300);
    Check(!line->GetEnableZoom() && !line->GetEnablePan(), "line chart: zoom and pan are off by default");
    Check(!area->GetEnableZoom() && !area->GetEnablePan(), "area chart: zoom and pan are off by default");
    Check(!scatter->GetEnableZoom() && !scatter->GetEnablePan(), "scatter plot: zoom and pan are off by default");

    auto data = Ramp(11);   // x 0..10
    line->SetDataSource(data);
    line->SetEnableTooltips(false);
    ChartPlotArea plot = line->PlotArea();
    double cx = plot.x + plot.width / 2;
    double cy = plot.y + plot.height / 2;
    ChartDataBounds full = line->Bounds();

    Check(!line->OnEvent(Wheel(cx, cy, 1)), "by default the wheel over the plot is left to the parent");

    Check(!line->OnEvent(Mouse(UCEventType::MouseDown, cx, cy)) &&
          !line->OnEvent(Mouse(UCEventType::MouseUp, cx, cy)),
          "a click on a chart that does not pan is left to the parent");

    line->SetEnablePan(true);
    bool pressTaken = line->OnEvent(Mouse(UCEventType::MouseDown, cx, cy));
    bool movedTaken = line->OnEvent(Mouse(UCEventType::MouseMove, cx + 40, cy));
    bool releaseTaken = line->OnEvent(Mouse(UCEventType::MouseUp, cx + 40, cy));
    Check(!pressTaken && !movedTaken && !releaseTaken,
          "a press, drag and release over a chart that is not zoomed in are left to the parent");

    line->SetEnableZoom(true);
    Check(!line->OnEvent(Wheel(plot.x - 30, cy, 1)), "zoom on: the wheel over the axis margin is left to the parent");
    Check(!line->OnEvent(Wheel(cx, cy, -1)), "zoom on: zooming out of the whole range is left to the parent");

    double anchorX = plot.x + plot.width * 0.75;
    double anchorBefore = line->DataXAt(anchorX);
    Check(line->OnEvent(Wheel(anchorX, cy, 1)), "zoom on: wheel up over the plot is taken");
    ChartDataBounds zoomed = line->Bounds();
    Check(zoomed.GetXRange() < full.GetXRange() * 0.85, "wheel up narrows the x range");
    Check(std::abs(line->DataXAt(anchorX) - anchorBefore) < 1e-6,
          "the value under the pointer stays under the pointer");

    bool panPressTaken = line->OnEvent(Mouse(UCEventType::MouseDown, cx, cy));
    bool panTaken = line->OnEvent(Mouse(UCEventType::MouseMove, cx + 40, cy));
    bool panReleaseTaken = line->OnEvent(Mouse(UCEventType::MouseUp, cx + 40, cy));
    ChartDataBounds panned = line->Bounds();
    double expectedShift = 40.0 / plot.width * zoomed.GetXRange();
    Check(panPressTaken && panTaken && panReleaseTaken,
          "the press, drag and release of a pan on a zoomed plot are taken");
    Check(std::abs((zoomed.minX - panned.minX) - expectedShift) < 1e-6 &&
          std::abs(panned.GetXRange() - zoomed.GetXRange()) < 1e-9,
          "dragging right by 40 px shows the data 40 px further left");

    int outTurns = 0;
    while (line->OnEvent(Wheel(cx, cy, -1)) && outTurns < 50) ++outTurns;
    ChartDataBounds back = line->Bounds();
    Check(outTurns > 0 && std::abs(back.minX - full.minX) < 1e-9 && std::abs(back.maxX - full.maxX) < 1e-9,
          "wheel down zooms back out to the whole range, then is left to the parent");

    line->OnEvent(Wheel(cx, cy, 1));
    line->SetEnableZoom(false);
    Check(std::abs(line->Bounds().GetXRange() - full.GetXRange()) < 1e-9, "turning zoom off shows the whole range");

    // DataLabel mode: the evenly spaced points spread out.
    area->SetDataSource(data);
    area->SetEnableTooltips(false);
    area->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
    area->SetEnableZoom(true);
    ChartPlotArea areaPlot = area->PlotArea();
    double gapBefore = area->ScreenOf(6).x - area->ScreenOf(5).x;
    Check(area->OnEvent(Wheel(areaPlot.x + areaPlot.width / 2, areaPlot.y + 10, 1)), "area chart (DataLabel): wheel up is taken");
    double gapAfter = area->ScreenOf(6).x - area->ScreenOf(5).x;
    Check(gapAfter > gapBefore * 1.2 && area->ScreenOf(0).x < areaPlot.x,
          "area chart (DataLabel): points spread out and the first leaves the plot");

    scatter->SetDataSource(data);
    scatter->SetEnableTooltips(false);
    scatter->SetEnableZoom(true);
    ChartPlotArea scatterPlot = scatter->PlotArea();
    double scatterRange = scatter->Bounds().GetXRange();
    scatter->OnEvent(Wheel(scatterPlot.x + scatterPlot.width / 2, scatterPlot.y + 10, 1));
    Check(scatter->Bounds().GetXRange() < scatterRange * 0.85, "scatter plot: wheel up narrows the x range");

    auto bars = std::make_shared<Probe<UltraCanvasBarChartElement>>("bars", 0, 0, 400, 300);
    bars->SetDataSource(data);
    bars->SetEnableTooltips(false);
    bars->SetEnableZoom(true);
    ChartPlotArea barPlot = bars->PlotArea();
    Check(!bars->OnEvent(Wheel(barPlot.x + barPlot.width / 2, barPlot.y + 10, 1)),
          "bar chart (no zoom of its own): the wheel is left to the parent even with zoom on");
}

void TestPlotAreaFollowsSize() {
    std::cout << "3. the plot area follows the size\n";
    auto chart = std::make_shared<Probe<UltraCanvasLineChartElement>>("line", 0, 0, 400, 300);
    chart->SetDataSource(Ramp(5));
    Check(chart->PlotArea().width == 320 && chart->PlotArea().height == 210, "400 x 300: plot 320 x 210");

    chart->SetBounds(0, 0, 600, 300);
    Check(chart->PlotArea().width == 520, "SetBounds to 600 wide: plot 520 wide");

    chart->SetSize(640, 360);
    Check(chart->PlotArea().width == 560 && chart->PlotArea().height == 270, "SetSize 640 x 360: plot 560 x 270");

    CSSLayout::LayoutContext ctx;
    chart->Arrange(Rect2Df(0, 0, 500, 250), ctx);
    Check(chart->PlotArea().width == 420 && chart->PlotArea().height == 160,
          "the layout's Arrange to 500 x 250: plot 420 x 160");
}

void TestCsvHeader(const fs::path& dir) {
    std::cout << "4. CSV header detection\n";
    std::string months = PathToUtf8(WriteFile(dir, "months.csv", "5,120,0,May\n7,140,0,July\n"));
    std::string xy = PathToUtf8(WriteFile(dir, "xy.csv", "x,y\n1,2\n3,4\n"));
    std::string named = PathToUtf8(WriteFile(dir, "named.csv", "Month,Sales\n1,2\n3,4\n"));

    ChartDataVector vec;
    vec.LoadFromCSV(months);
    Check(vec.GetPointCount() == 2 && vec.GetPoint(0).label == "May" && vec.GetPoint(0).x == 5,
          "ChartDataVector: a first row labelled May is data");
    vec.LoadFromCSV(xy);
    Check(vec.GetPointCount() == 2 && vec.GetPoint(0).x == 1, "ChartDataVector: an x,y header is skipped");
    vec.LoadFromCSV(named);
    Check(vec.GetPointCount() == 2 && vec.GetPoint(0).x == 1, "ChartDataVector: a Month,Sales header is skipped");

    ChartDataStream stream(months);
    Check(stream.GetPointCount() == 2 && stream.GetPoint(0).label == "May",
          "ChartDataStream: a first row labelled May is data");
    ChartDataStream streamNamed(named);
    Check(streamNamed.GetPointCount() == 2 && streamNamed.GetPoint(0).x == 1,
          "ChartDataStream: a Month,Sales header is skipped");

    // Rows that cannot be read, after the first line, are skipped too: they
    // were plotted at the origin. CRLF line ends and blank lines read as before.
    std::string messy = PathToUtf8(WriteFile(dir, "messy.csv",
            "x,y\r\n1,10\r\nn/a,oops\r\n\r\n2,20\r\n3\r\n4,40\r\n"));
    vec.LoadFromCSV(messy);
    bool noOrigin = true;
    for (size_t i = 0; i < vec.GetPointCount(); ++i) {
        if (vec.GetPoint(i).x == 0 && vec.GetPoint(i).y == 0) noOrigin = false;
    }
    Check(vec.GetPointCount() == 3 && noOrigin && vec.GetPoint(1).x == 2 && vec.GetPoint(2).y == 40,
          "ChartDataVector: unreadable rows are skipped, not plotted at (0,0)");
    ChartDataStream streamMessy(messy);
    Check(streamMessy.GetPointCount() == 3 && streamMessy.GetPoint(1).x == 2 &&
          streamMessy.GetPoint(2).y == 40,
          "ChartDataStream: unreadable rows are skipped, and the indexes count data rows");
}

void TestBarsFromZero() {
    std::cout << "5. bars rise from zero\n";
    auto chart = std::make_shared<Probe<UltraCanvasBarChartElement>>("bars", 0, 0, 400, 300);
    chart->SetDataSource(Series({{1, 10}, {2, 20}, {3, 30}}));
    Check(chart->Bounds().minY == 0.0 && chart->Bounds().maxY > 30.0, "positive values: the axis starts at 0");
    chart->SetDataSource(Series({{1, -30}, {2, -20}, {3, -10}}));
    Check(chart->Bounds().maxY == 0.0 && chart->Bounds().minY < -30.0, "negative values: the axis ends at 0");

    chart->SetDataSource(Series({{1, -10}, {2, 20}}));
    chart->SetShowGrid(false);
    chart->SetShowAxes(false);
    chart->SetBarBorderWidth(0);
    chart->SetBarColor(Color(0, 200, 0));
    Canvas canvas(400, 300);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 300));
    ChartPlotArea plot = chart->PlotArea();
    double zeroY = chart->ScreenY(0.0);
    double slot = plot.width / 2;
    double negX = plot.x + slot * 0.5;
    double posX = plot.x + slot * 1.5;
    const Color green(0, 200, 0);
    Check(Near(canvas.At(posX, zeroY - 8), green) && !Near(canvas.At(posX, zeroY + 8), green),
          "a positive bar stands on the zero line");
    Check(Near(canvas.At(negX, zeroY + 8), green) && !Near(canvas.At(negX, zeroY - 8), green),
          "a negative bar hangs from the zero line");
}

void TestBackgroundIsTheElements() {
    std::cout << "6. the element's background\n";
    auto chart = std::make_shared<Probe<UltraCanvasLineChartElement>>("line", 0, 0, 400, 300);
    chart->SetDataSource(Ramp(5));
    Check(chart->GetBackgroundColor() == Color(255, 255, 255, 255), "a new chart's background is white");

    UltraCanvasUIElement* element = chart.get();
    element->SetBackgroundColor(Color(200, 30, 30, 255));
    Check(chart->ChartBackground() == Color(200, 30, 30, 255), "set through UltraCanvasUIElement, the chart uses it");
    Canvas canvas(400, 300);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 300));
    Check(Near(canvas.At(5, 5), Color(200, 30, 30)), "and paints it");

    chart->SetBackgroundColor(Color(30, 30, 200, 255));
    Check(element->GetBackgroundColor() == Color(30, 30, 200, 255),
          "set through the chart, UltraCanvasUIElement reads it back");
}

void TestWaterfall(const fs::path& dir) {
    std::cout << "7. waterfall chart\n";
    auto data = std::make_shared<WaterfallChartDataVector>();
    data->AddWaterfallPoint("Start", 100);
    data->AddWaterfallPoint("Costs", -30);
    data->AddWaterfallPoint("Total", 0, false, true);

    auto chart = CreateWaterfallChartWithData("waterfall", 0, 0, 400, 300, data);
    Canvas canvas(400, 300);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 300));
    auto withLabels = canvas.Pixels();

    UltraCanvasChartElementBase* base = chart.get();
    base->SetShowValueLabels(false);
    canvas.Clear();
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 300));
    Check(CountDifferences(withLabels, canvas.Pixels()) > 0,
          "SetShowValueLabels(false) through the base class hides the value labels");
    base->SetShowValueLabels(true);

    // Verification, not a fix: the base class draws the waterfall's grid.
    chart->SetShowGrid(false);
    canvas.Clear();
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 300));
    Check(CountDifferences(withLabels, canvas.Pixels()) > 0, "the grid is drawn (by the base class) and can be hidden");

    // A running total that is up to date is returned as it is, not worked
    // out again on every read. Tamper with one to see whether GetPoint
    // recomputes it.
    double costsTotal = data->GetPoint(1).y;
    const_cast<WaterfallChartDataPoint&>(data->GetWaterfallPoint(1)).cumulativeValue = -999.0;
    Check(costsTotal == 70.0 && data->GetPoint(1).y == -999.0,
          "GetPoint does not recompute the running totals when nothing changed");
    data->AddWaterfallPoint("Fees", -5);
    Check(data->GetPoint(1).y == 70.0 && data->GetPoint(3).y == 65.0, "adding a step brings them up to date");

    WaterfallChartDataVector fromArray;
    std::vector<ChartDataPoint> points = {ChartDataPoint(0, 100, 0, "Start"), ChartDataPoint(1, -30, 0, "Costs"),
                                          ChartDataPoint(2, 15, 0, "Other")};
    fromArray.LoadFromArray(points);
    Check(fromArray.GetPointCount() == 3 && fromArray.GetFinalValue() == 85.0 &&
          fromArray.GetWaterfallPoint(1).label == "Costs",
          "LoadFromArray(ChartDataPoint) loads one step per point");

    std::string csv = PathToUtf8(WriteFile(dir, "waterfall.csv",
                                           "Step,Change,Type\nStart,100\nCosts,-30\nMid,,subtotal\nTax,-10\nTotal,,total\n"));
    WaterfallChartDataVector fromCsv;
    fromCsv.LoadFromCSV(csv);
    Check(fromCsv.GetPointCount() == 5 && fromCsv.GetWaterfallPoint(2).isSubtotal &&
          fromCsv.GetWaterfallPoint(4).isTotal && fromCsv.GetFinalValue() == 60.0,
          "LoadFromCSV loads steps, a subtotal and a total");
}

void TestPopulationPyramidRows() {
    std::cout << "8. CreatePopulationPyramid row labels\n";
    std::vector<DivergingCategory> categories;
    categories.emplace_back("Male", Color(100, 150, 200, 255), false);
    categories.emplace_back("Female", Color(200, 100, 150, 255), true);
    auto pyramid = CreatePopulationPyramid("pyramid", 0, 0, 400, 300, {"0-9", "10-19", "20-29"}, categories);
    auto rows = std::dynamic_pointer_cast<DivergingDataSource>(pyramid->GetDataSource());
    Check(rows && rows->GetPointCount() == 3 && rows->GetDivergingPoint(1).rowLabel == "10-19",
          "the row labels are laid out as rows, in order");

    pyramid->AddDataRow("10-19", {{"Male", 1350}, {"Female", 1300}});
    Check(rows && rows->GetPointCount() == 3 && rows->GetDivergingPoint(1).categoryValues.size() == 2 &&
          rows->GetDivergingPoint(0).categoryValues.empty(),
          "AddDataRow fills the row of that label");
}

void TestHoverRingOnThePoint() {
    std::cout << "9. the hover ring sits on the point in DataLabel mode\n";
    // x values that are not the indexes: by value the second point would sit
    // near the left edge, by index it sits a third of the way across.
    auto chart = std::make_shared<Probe<UltraCanvasScatterPlotElement>>("scatter", 0, 0, 400, 300);
    chart->SetDataSource(Series({{0, 10}, {1, 20}, {2, 15}, {30, 12}}));
    chart->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
    chart->SetShowGrid(false);
    chart->SetShowAxes(false);
    chart->SetPointColor(Color(0, 0, 255));
    chart->SetEnableSelection(true);
    chart->HoverPoint(1);

    Canvas canvas(400, 300);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 300));
    Point2Dd dot = chart->ScreenOf(1);
    // The ring: radius 8, stroked 2 px wide, in red - a red pixel 6 to 10 px
    // to each side of the point (the point need not sit on a pixel centre).
    auto redBeside = [&](int side) {
        for (int d = 6; d <= 10; ++d) {
            if (Near(canvas.At(dot.x + side * d, dot.y), Color(255, 0, 0), 30)) return true;
        }
        return false;
    };
    Check(redBeside(-1) && redBeside(1),
          "the red ring is drawn around the hovered point where it is drawn");
}

// A chart laid out in a container, then invalidated the way the framework
// does it (SetVisible, SetElementSize and the rest call InvalidateLayout
// through the element base): the container's measure must be dropped too.
template <typename Chart>
void CheckInvalidationReachesParent(const std::string& name) {
    auto parent = std::make_shared<UltraCanvasContainer>("parent", 0, 0, 400, 300);
    auto chart = std::make_shared<Chart>("chart", 0, 0, 200, 150);
    parent->AddChild(chart);
    CSSLayout::LayoutContext ctx;
    CSSLayout::MeasureConstraints c{ { CSSLayout::ConstraintMode::Exact, 400.0f },
                                     { CSSLayout::ConstraintMode::Exact, 300.0f } };
    parent->Measure(c, ctx);
    parent->Arrange(Rect2Df{ 0, 0, 400, 300 }, ctx);
    const bool measuredBefore = parent->measured.valid;
    static_cast<CSSLayout::Element&>(*chart).InvalidateLayout();
    Check(measuredBefore && !parent->measured.valid,
          name + ": a framework invalidation reaches the parent");
}

void TestInvalidationReachesParent() {
    std::cout << "11. a chart's invalidation reaches the layout engine\n";
    CheckInvalidationReachesParent<UltraCanvasChordChart>("chord chart");
    CheckInvalidationReachesParent<UltraCanvasCircularProgressChart>("circular progress chart");
    CheckInvalidationReachesParent<UltraCanvasPolarChart>("polar chart");
    CheckInvalidationReachesParent<UltraCanvasRadialBarChart>("radial bar chart");
    CheckInvalidationReachesParent<UltraCanvasTimelineChart>("timeline chart");
    CheckInvalidationReachesParent<UltraCanvasFishboneDiagram>("fishbone diagram");
    CheckInvalidationReachesParent<UltraCanvasMatrixDiagram>("matrix diagram");
    CheckInvalidationReachesParent<UltraCanvasParliamentDiagram>("parliament diagram");
    CheckInvalidationReachesParent<UltraCanvasSWOTDiagram>("SWOT diagram");
    CheckInvalidationReachesParent<UltraCanvasTimelineDiagram>("timeline diagram");
    CheckInvalidationReachesParent<UltraCanvasWordCloudElement>("word cloud");
}

} // namespace

void TestAxisTicksAndBars() {
    std::cout << "12. whole-step ticks, the bar axis, value range, horizontal bars\n";

    // The ticks a person would mark.
    ChartAxisTicks t = ComputeChartAxisTicks(0.0, 149.1, 6);
    Check(t.step == 25.0 && t.values.size() == 6 && t.values.front() == 0.0 && t.values.back() == 125.0,
          "0..149.1: a tick every 25, 0 to 125 (was 0, 24.85, 49.7 ...)");
    Check(t.Format(125.0) == "125" && t.decimals == 0, "and they read as whole numbers");
    ChartAxisTicks fine = ComputeChartAxisTicks(0.0, 1.0, 4);
    Check(fine.step == 0.25 && fine.Format(0.75) == "0.75", "0..1 in four: 0.25 steps, two decimals");
    ChartAxisTicks tenths = ComputeChartAxisTicks(0.1, 0.7, 6);
    Check(tenths.values.size() == 7 && tenths.values[2] == 0.3, "0.1..0.7: exact tenths (0.3, not 0.30000004)");
    ChartAxisTicks big = ComputeChartAxisTicks(0.0, 48000.0, 6);
    Check(big.Format(40000.0) == "40K" && big.Format(0.0) == "0K", "0..48000: 10K steps labelled in K");
    ChartAxisTicks across = ComputeChartAxisTicks(-7.1, 149.1, 6);
    Check(across.values.front() == 0.0 && across.Format(0.0) == "0", "-7.1..149.1: no tick below the data, 0 reads 0");

    // A zero value no longer pushes the axis below zero.
    auto bars = std::make_shared<Probe<UltraCanvasBarChartElement>>("bars", 0, 0, 600, 360);
    auto data = std::make_shared<ChartDataVector>();
    const char* names[] = {"TV", "Kettle", "Fridge", "Towel rail", "Workstation"};
    const double watts[] = {88, 0, 95, 0, 142};
    for (int i = 0; i < 5; ++i) data->AddPoint(ChartDataPoint(i, watts[i], 0, names[i]));
    bars->SetDataSource(data);
    bars->SetXAxisLabelMode(XAxisLabelMode::DataLabel);
    ChartDataBounds b = bars->Bounds();
    Check(b.minY == 0.0, "values 0..142: the axis starts at 0 (was -7.1)");
    Check(b.maxY == 160.0, "and ends at the next whole tick past the padding, 160 (was 149.1)");

    // A fixed range wins, and bars past it stop at the edge.
    bars->SetValueRange(0.0, 100.0);
    b = bars->Bounds();
    Check(b.minY == 0.0 && b.maxY == 100.0, "SetValueRange(0, 100): the axis is 0..100");
    const Rect2Dd clipped = bars->GetBarRect(4);
    Check(std::abs(clipped.y - bars->PlotArea().y) < 0.5, "a 142 W bar on a 0..100 axis stops at the top of the plot");
    bars->SetValueRange(500.0, 500.0);
    Check(bars->Bounds().maxY == 100.0, "an empty range is ignored");
    bars->ClearValueRange();
    Check(bars->Bounds().maxY == 160.0, "ClearValueRange: fitted to the data again");

    // Horizontal: a row per category, from the left edge of the plot.
    bars->SetOrientation(BarChartOrientation::Horizontal);
    const ChartPlotArea plot = bars->PlotArea();
    const Rect2Dd tv = bars->GetBarRect(0), work = bars->GetBarRect(4), kettle = bars->GetBarRect(1);
    Check(plot.x >= 0.2 * 600 - 0.5 && plot.x <= 0.4 * 600 + 0.5, "horizontal: room on the left for the names");
    Check(std::abs(tv.x - plot.x) < 0.5 && tv.y < work.y, "bars start at the zero line, first category on top");
    Check(std::abs(work.width - plot.width * 142.0 / 160.0) < 1.0 && kettle.width < 0.5,
          "a bar's length is its value along the bottom axis; a zero bar has none");
    Check(tv.height > 0 && std::abs((work.y - tv.y) - 4 * plot.height / 5) < 0.5, "one row per category");

    bars->SetShowGrid(false);
    bars->SetShowAxes(false);
    bars->SetBarBorderWidth(0);
    bars->SetBarColor(Color(0, 200, 0));
    Canvas canvas(600, 360);
    bars->Render(canvas.ctx.get(), Rect2Df(0, 0, 600, 360));
    const double midY = work.y + work.height / 2;
    Check(Near(canvas.At(work.x + work.width - 3, midY), Color(0, 200, 0)) &&
          !Near(canvas.At(work.x + work.width + 3, midY), Color(0, 200, 0)),
          "and it is drawn where GetBarRect says");

    // A point's own colour wins over the series colour.
    data->AddPoint(ChartDataPoint(5, 50, 0, "Lamp", 0.0, Color(200, 0, 0)));
    bars->SetDataSource(data);
    canvas.Clear();
    bars->Render(canvas.ctx.get(), Rect2Df(0, 0, 600, 360));
    const Rect2Dd lamp = bars->GetBarRect(5);
    Check(Near(canvas.At(lamp.x + lamp.width / 2, lamp.y + lamp.height / 2), Color(200, 0, 0)),
          "a point with its own colour is drawn in it");
}

void TestEmptyMessage(const fs::path& dir) {
    std::cout << "13. the empty-state message\n";
    const Color background(250, 246, 230, 255);
    // The box round every pixel that is not the background.
    struct Ink { int left = 1 << 30, right = -1, top = 1 << 30, bottom = -1, darkest = 255 * 3; Color tone; };
    auto draw = [&](const std::shared_ptr<UltraCanvasLineChartElement>& chart, Ink& ink) {
        Canvas canvas(400, 200);
        chart->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 200));
        for (int y = 0; y < 200; ++y) {
            for (int x = 0; x < 400; ++x) {
                const Color c = canvas.At(x, y);
                if (Near(c, background, 3)) continue;
                ink.left = std::min(ink.left, x);
                ink.right = std::max(ink.right, x);
                ink.top = std::min(ink.top, y);
                ink.bottom = std::max(ink.bottom, y);
                if (c.r + c.g + c.b < ink.darkest) { ink.darkest = c.r + c.g + c.b; ink.tone = c; }
            }
        }
        return Near(canvas.At(3, 3), background, 3) && Near(canvas.At(396, 196), background, 3);
    };
    auto chart = std::make_shared<UltraCanvasLineChartElement>("empty", 0, 0, 400, 200);
    chart->SetBackgroundColor(background);
    Check(chart->GetEmptyMessage() == "No data to display", "a new chart says \"No data to display\"");

    Ink plain;
    Check(draw(chart, plain), "with no data the chart paints its own background (was a fixed grey)");
    Check(plain.right > 0 && std::abs((plain.left + plain.right) / 2 - 200) <= 6,
          "and the message is centred (was 60 px left of the middle)");

    chart->SetEmptyMessage("Nothing is drawing power", "All 5 metered plugs are idle.");
    chart->SetEmptyMessageColor(Color(20, 90, 200, 255));
    Ink two;
    draw(chart, two);
    Check(two.top < 100 && two.bottom > 100 && two.bottom - two.top > 24,
          "a message and its detail make two lines across the middle");
    Check(std::abs((two.left + two.right) / 2 - 200) <= 6, "centred");
    Check(Near(two.tone, Color(20, 90, 200), 40), "in the colour set");

    // An icon over the message: a black square, painted blue on a pale badge.
    const fs::path iconPath = dir / "empty-icon.png";
    {
        cairo_surface_t* icon = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32);
        cairo_t* cr = cairo_create(icon);
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_rectangle(cr, 4, 4, 24, 24);
        cairo_fill(cr);
        cairo_surface_write_to_png(icon, PathToUtf8(iconPath).c_str());
        cairo_destroy(cr);
        cairo_surface_destroy(icon);
    }
    chart->SetEmptyIcon(PathToUtf8(iconPath), Color(20, 90, 200, 255), Color(225, 235, 250, 255));
    Ink withIcon;
    {
        Canvas canvas(400, 200);
        chart->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 200));
        // Icon 54 + gap 10 + text 40 = 104 tall, from y 48: the badge spans
        // 48..102 round x 200, the icon 61..89 inside it.
        Check(Near(canvas.At(178, 52), Color(225, 235, 250), 6), "the badge is drawn above the message");
        Check(Near(canvas.At(200, 75), Color(20, 90, 200), 20), "the icon on it in the colour set");
        draw(chart, withIcon);
    }
    Check(withIcon.top < 55 && withIcon.bottom > two.bottom + 20,
          "the message moves down to make room, the whole block still centred");
    chart->SetEmptyIcon("");
    Check(chart->GetEmptyIcon().empty(), "an empty path takes the icon away");

    chart->SetEmptyMessage("");
    Ink none;
    Check(draw(chart, none) && none.right < 0, "an empty message draws the background alone");

    chart->SetDataSource(Ramp(5));
    Canvas canvas(400, 200);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, 400, 200));
    Check(!Near(canvas.At(200, 100), background, 3) || !Near(canvas.At(300, 120), background, 3),
          "with data the chart is drawn, not the message");
}

void TestMinimumValueSpan() {
    std::cout << "14. a minimum value span\n";
    auto data = std::make_shared<ChartDataVector>();
    std::vector<ChartDataPoint> points;
    for (int i = 0; i < 10; ++i) points.emplace_back(i, i % 2 ? 20.7 : 20.6, 0.0, "", 0.0);
    data->LoadFromArray(points);
    auto chart = std::make_shared<Probe<UltraCanvasLineChartElement>>("span", 0, 0, 400, 300);
    chart->SetDataSource(data);
    const ChartDataBounds fitted = chart->Bounds();
    Check(fitted.maxY - fitted.minY < 0.2, "without one the axis fits 20.6-20.7 (a sawtooth across the plot)");
    chart->SetMinimumValueSpan(2.0);
    const ChartDataBounds wide = chart->Bounds();
    Check(wide.maxY - wide.minY >= 2.0, "with 2.0 the axis spans at least 2");
    Check(std::fabs((wide.minY + wide.maxY) / 2.0 - 20.65) < 1e-9, "centred on the data");
    const double low = chart->ScreenY(20.6), high = chart->ScreenY(20.7);
    const ChartPlotArea area = chart->PlotArea();
    Check(std::fabs(low - high) < area.height * 0.1, "so the swing takes under a tenth of the plot");
    // Data wider than the floor is left alone.
    for (auto& p : points) p.y *= 10.0;
    data->LoadFromArray(points);
    chart->SetDataSource(data);
    const ChartDataBounds tall = chart->Bounds();
    Check(tall.minY < 206.0 + 1e-9 && tall.maxY > 207.0 - 1e-9 && tall.maxY - tall.minY < 2.0 + 1e-9 + 0.2,
          "a span already wider than the floor is not changed");
}

int main() {
    UCImage::InitializeImageSubsysterm("ChartElementBehaviourTest");   // the empty-state icon
    fs::path dir = fs::temp_directory_path() / "uc_chart_element_test";
    fs::create_directories(dir);

    TestLineDotsFollowPointRadius();
    TestZoomAndPan();
    TestPlotAreaFollowsSize();
    TestCsvHeader(dir);
    TestBarsFromZero();
    TestBackgroundIsTheElements();
    TestWaterfall(dir);
    TestPopulationPyramidRows();
    TestHoverRingOnThePoint();
    TestInvalidationReachesParent();
    TestAxisTicksAndBars();
    TestEmptyMessage(dir);
    TestMinimumValueSpan();

    std::error_code ec;
    fs::remove_all(dir, ec);

    std::cout << (g_count - g_failures) << " of " << g_count << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
