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
//
// Pictures are drawn into an offscreen surface and read back pixel by pixel;
// plot areas and ranges are read through a probe subclass.
// Version: 1.2.0 - a press is taken only when it starts a pan; bad CSV rows are skipped
// Version: 1.1.0 - the hover ring in DataLabel mode
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "Plugins/Charts/UltraCanvasSpecificChartElements.h"
#include "Plugins/Charts/UltraCanvasWaterfallChart.h"
#include "Plugins/Charts/UltraCanvasDivergingBarChart.h"
#include "UltraCanvasRenderContext.h"
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

} // namespace

int main() {
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

    std::error_code ec;
    fs::remove_all(dir, ec);

    std::cout << (g_count - g_failures) << " of " << g_count << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
