// Tests/ImportancePerformanceChartRenderTest.cpp
// The importance-performance chart drawn offscreen, in both Slack forms.
//
// The model has its own unit test (ImportancePerformanceModelTest); this one
// covers what the element adds. The zones must be painted where the model
// puts them, in their own colours, because the zone a reader sees is the
// chart's answer. Every factor must be hit where it is drawn, and a pointer
// on a bare stretch of zone must hit that zone. The axes must carry the
// level names and the end captions, and a group hidden from the legend must
// stop answering the pointer.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, each form is also written there as a
// PNG for a person to look at.
//
// Version: 1.0.0
// Last Modified: 2026-10-24
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasImportancePerformanceChart.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

const int kWidth = 900;
const int kHeight = 640;

struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;

    Canvas(int w, int h) {
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (!ctx) return;
        cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    }

    Color PixelAt(int x, int y) const {
        if (!cr) return Color(0, 0, 0, 0);
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const uint32_t p = reinterpret_cast<const uint32_t*>(data + y * cairo_image_surface_get_stride(s))[x];
        return Color((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF, 255);
    }

    void Save(const std::string& name) const {
        const char* dir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR");
        if (!dir || !cr) return;
        const std::string path = std::string(dir) + "/" + name + ".png";
        cairo_surface_write_to_png(cairo_get_target(cr), path.c_str());   // path-string-ok: test output under an ASCII directory the caller chose
        std::cout << "  wrote " << path << "\n";
    }
};

bool Close(const Color& a, const Color& b, int tolerance = 8) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance;
}

UCEvent MouseMove(const Point2Di& at) {
    UCEvent e;
    e.type = UCEventType::MouseMove;
    e.pointer = at;
    return e;
}

int64_t HoverAt(UltraCanvasImportancePerformanceChart& chart, const Point2Dd& p) {
    chart.OnEvent(MouseMove(Point2Di(static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)))));
    return chart.HoveredRegionId();
}

void CheckForm(bool smooth) {
    const std::string name = smooth ? "Slack zones, smooth" : "Slack zones, stepped";
    std::cout << "\n" << name << "\n";

    Canvas canvas(kWidth, kHeight);
    Check(canvas.ctx != nullptr, "an offscreen context is available");
    if (!canvas.ctx) return;

    ImportancePerformanceModel model = ImportancePerformanceSamples::SlackZones(smooth);
    model.factors = ImportancePerformanceSamples::SampleFactors();
    auto chart = CreateImportancePerformanceChart("ipm", 0, 0, kWidth, kHeight);
    chart->SetEnableTooltips(false);   // no window to show one in
    chart->SetAnimateOnDataChange(false);   // one frame: the marks at full size
    chart->SetModel(model);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));

    const ChartEngineFrame& frame = chart->Frame();
    Check(frame.axes && frame.axes->Count() == 2 && frame.projection, "the chart lays out two axes");
    if (!frame.axes || frame.axes->Count() < 2 || !frame.projection) return;
    const ChartAxis& y = frame.axes->At(0);
    const ChartAxis& x = frame.axes->At(1);
    auto screen = [&](double importance, double performance) {
        return frame.projection->ToScreen(ChartNormalizedPoint(x.Normalize(importance), y.Normalize(performance)));
    };

    // ---- the axes read in Slack's terms --------------------------------------
    Check(x.inverted && y.inverted, "both axes are inverted, 1 at the far end");
    Check(screen(1.0, 5.0).x > screen(9.0, 5.0).x, "the most important factors are on the right");
    Check(screen(5.0, 1.0).y < screen(5.0, 9.0).y, "the best performance is at the top");
    Check(x.FormatValue(2.0) == "Order winning" && x.FormatValue(5.0) == "Qualifying",
          "importance ticks carry the level names");
    Check(y.FormatValue(8.0) == "Worse than", "performance ticks carry the level names");
    Check(x.FormatValue(9.5) == "Low" && x.FormatValue(0.5) == "High",
          "the end captions sit at the screen's ends, whatever the inversion");
    Check(y.FormatValue(9.5) == "Bad" && y.FormatValue(0.5) == "Good", "and on the performance axis too");
    Check(x.FormatValue(3.5).empty(), "a range boundary is a tick mark without a label");

    // ---- the zones are painted where the model puts them ---------------------
    // Points well inside a zone (every neighbour 0.4 away in the same zone,
    // so clear of the boundary and of the band along it), sampled on a grid.
    std::vector<int> matched(model.zones.size(), 0), probed(model.zones.size(), 0);
    std::vector<Point2Dd> bare(model.zones.size(), Point2Dd(-1, -1));
    const std::vector<int> zones = chart->GetFactorZones();
    for (double i = 0.75; i <= 9.25; i += 0.25) {
        for (double p = 0.75; p <= 9.25; p += 0.25) {
            const int z = model.ZoneOf(i, p);
            if (z < 0) continue;
            bool deep = true;
            for (double di : {-0.4, 0.0, 0.4}) {
                for (double dp : {-0.4, 0.0, 0.4}) deep &= model.ZoneOf(i + di, p + dp) == z;
            }
            if (!deep) continue;
            const Point2Dd s = screen(i, p);
            ++probed[static_cast<size_t>(z)];
            if (Close(canvas.PixelAt(static_cast<int>(s.x), static_cast<int>(s.y)), model.zones[static_cast<size_t>(z)].fill)) {
                ++matched[static_cast<size_t>(z)];
            }
            // A bare spot for the hover check: well away from every factor.
            bool clear = true;
            for (const ImportancePerformanceFactor& f : model.factors) {
                const Point2Dd fp = screen(f.importance, f.performance);
                clear &= std::hypot(fp.x - s.x, fp.y - s.y) > 30.0;
            }
            if (clear && bare[static_cast<size_t>(z)].x < 0) bare[static_cast<size_t>(z)] = s;
        }
    }
    for (size_t z = 0; z < model.zones.size(); ++z) {
        Check(probed[z] > 0 && matched[z] >= probed[z] * 7 / 10,
              "zone \"" + model.zones[z].title + "\" is painted in its colour where the model puts it (" +
              std::to_string(matched[z]) + " of " + std::to_string(probed[z]) + ")");
    }

    // ---- the pointer finds factors and zones ---------------------------------
    Check(zones == model.ClassifyFactors(), "the chart reports the model's classification");
    // Group order is first appearance: Delivery 0, Service 1.
    std::vector<size_t> perGroup(2, 0);
    bool allHit = true;
    for (const ImportancePerformanceFactor& f : model.factors) {
        const size_t g = f.group == "Delivery" ? 0 : 1;
        const int64_t expected = static_cast<int64_t>(g) * 1000000 + static_cast<int64_t>(perGroup[g]++);
        const int64_t hit = HoverAt(*chart, screen(f.importance, f.performance));
        if (hit != expected) {
            std::cout << "    " << f.label << ": hit " << hit << ", expected " << expected << "\n";
            allHit = false;
        }
    }
    Check(allHit, "every factor is hit where it is drawn");
    for (size_t z = 0; z < model.zones.size(); ++z) {
        if (bare[z].x < 0) continue;
        Check(HoverAt(*chart, bare[z]) == 4000000000LL + static_cast<int64_t>(z),
              "a bare spot of \"" + model.zones[z].title + "\" hits the zone");
    }

    // Off the plot - over the axis labels - no zone answers, though the
    // outlines run past the plot.
    const Point2Dd belowPlot(frame.plotArea.x + frame.plotArea.width * 0.5,
                             frame.plotArea.y + frame.plotArea.height + 12.0);
    Check(HoverAt(*chart, belowPlot) == -1, "a pointer over the axis labels hits no zone");

    canvas.Save(smooth ? "importance_performance_smooth" : "importance_performance_stepped");

    // ---- a hidden group stops answering ---------------------------------------
    chart->OnLegendEntryToggled(1, false);
    Canvas second(kWidth, kHeight);
    if (!second.ctx) return;
    chart->Render(second.ctx.get(), Rect2Df(0, 0, kWidth, kHeight));
    const ImportancePerformanceFactor* service = nullptr;
    for (const ImportancePerformanceFactor& f : model.factors) {
        if (f.group == "Service") { service = &f; break; }
    }
    if (service) {
        const int64_t hit = HoverAt(*chart, screen(service->importance, service->performance));
        Check(hit < 1000000 || hit >= 4000000000LL, "a factor of a hidden group is not hit");
    }
}

} // namespace

int main() {
    std::cout << "==========================================\n";
    std::cout << "  Importance-Performance Chart Render Test\n";
    std::cout << "==========================================\n";

    CheckForm(false);
    CheckForm(true);

    std::cout << "\n==========================================\n";
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED.\n";
    return 1;
}
