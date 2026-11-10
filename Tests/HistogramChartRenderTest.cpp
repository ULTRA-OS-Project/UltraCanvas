// Tests/HistogramChartRenderTest.cpp
// The histogram chart drawn offscreen: Combined overlays red, green and blue
// in their own colours and a single channel shows alone; the logarithmic
// scale; the hover read-out; the legend switching a channel off; and
// SetImage counting a real decoded picture.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, each case is also written there as a
// PNG for a person to look at.
//
// Version: 1.0.0
// Last Modified: 2026-11-10
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasHistogramChart.h"
#include "UltraCanvasImage.h"
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

const int W = 640, H = 360;

struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;
    Canvas() {
        ctx = CreateRenderContext(Size2Di(W, H), nullptr);
        if (ctx) cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    }
    Color PixelAt(const Point2Dd& at) const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const int x = static_cast<int>(std::lround(at.x)), y = static_cast<int>(std::lround(at.y));
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

// 1,000 pixels of (200, 60, 30) and 500 of (40, 180, 120): each channel's
// counts sit at known, separate levels.
ImageHistogram TwoColours() {
    std::vector<uint8_t> px;
    for (int i = 0; i < 1000; ++i) px.insert(px.end(), {200, 60, 30});
    for (int i = 0; i < 500; ++i) px.insert(px.end(), {40, 180, 120});
    return ComputeImageHistogram(px.data(), 1500, 1, 4500, HistogramPixelFormat::RGB8);
}

std::shared_ptr<UltraCanvasHistogramChart> Make(Canvas& canvas, HistogramDisplay display) {
    auto chart = CreateHistogramChart("histogram", 0, 0, W, H);
    chart->SetEnableTooltips(false);
    chart->SetAnimateOnDataChange(false);
    chart->SetHistogram(TwoColours());
    chart->SetDisplay(display);
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
    return chart;
}

// The screen point a little above the base of a level's column.
Point2Dd Column(const UltraCanvasHistogramChart& chart, int level, double up = 0.05) {
    const ChartAxis& counts = chart.Axes().At(0);
    const ChartAxis& levels = chart.Axes().At(1);
    return chart.Frame().projection->ToScreen(
        ChartNormalizedPoint(levels.Normalize(level), counts.Normalize(0.0) + up));
}

bool Reddish(const Color& c) { return c.r > c.g + 40 && c.r > c.b + 40; }
bool Greenish(const Color& c) { return c.g > c.r + 30 && c.g > c.b + 20; }
bool Bluish(const Color& c) { return c.b > c.r + 40 && c.b > c.g + 20; }

void TestCombined() {
    std::cout << "\nCombined\n";
    Canvas canvas;
    if (!canvas.ctx) return;
    auto chart = Make(canvas, HistogramDisplay::Combined);
    Check(chart->ShownChannels().size() == 3, "red, green and blue are drawn");
    Check(chart->GetLegend().GetEntryCount() == 3, "with a legend entry each");
    Check(Reddish(canvas.PixelAt(Column(*chart, 200))), "the red column at level 200 is red");
    Check(Greenish(canvas.PixelAt(Column(*chart, 180))), "the green column at level 180 is green");
    Check(Bluish(canvas.PixelAt(Column(*chart, 30))), "the blue column at level 30 is blue");
    const Color empty = canvas.PixelAt(Column(*chart, 100));
    Check(!Reddish(empty) && !Greenish(empty) && !Bluish(empty), "an empty level is left blank");

    std::string tip;
    Check(chart->HitRegionAt(Column(*chart, 200, 0.5), &tip) == 200, "every level is a hit region, the plot's height");
    Check(tip.find("Level 200") == 0 && tip.find("Red: 1,000 pixels") != std::string::npos &&
              tip.find("Green: 0 pixels") != std::string::npos,
          "the read-out names the level and each channel's count");
    Check(chart->DescribeLevel(60).find("(66.67%)") != std::string::npos, "with its share of the pixels");

    chart->OnLegendEntryToggled(1, false);
    Check(chart->ShownChannels().size() == 2, "switching green off in the legend hides it");
    canvas.Save("histogram_combined");
}

void TestSingleChannels() {
    std::cout << "\nOne channel at a time\n";
    Canvas red;
    if (!red.ctx) return;
    auto chart = Make(red, HistogramDisplay::Red);
    Check(chart->ShownChannels().size() == 1 && chart->GetLegend().GetEntryCount() == 0,
          "Red: one channel, no legend");
    Check(Reddish(red.PixelAt(Column(*chart, 200))) && !Greenish(red.PixelAt(Column(*chart, 180))),
          "only the red levels are drawn");
    Check(chart->DescribeLevel(200).find("Green") == std::string::npos, "and the read-out names red alone");
    red.Save("histogram_red");

    Canvas blue;
    auto b = Make(blue, HistogramDisplay::Blue);
    Check(Bluish(blue.PixelAt(Column(*b, 120))) && !Reddish(blue.PixelAt(Column(*b, 200))), "Blue: the blue levels");

    Canvas luma;
    auto l = Make(luma, HistogramDisplay::Luminosity);
    const int l1 = HistogramLuminosity(200, 60, 30), l2 = HistogramLuminosity(40, 180, 120);
    Check(l->GetHistogram().CountAt(HistogramChannel::Luminosity, l1) == 1000 &&
              l->DescribeLevel(l2).find("Luminosity: 500 pixels") != std::string::npos,
          "Luminosity: the pixels' brightness");
    const Color c = luma.PixelAt(Column(*l, l1));
    Check(std::abs(c.r - c.g) < 25 && std::abs(c.g - c.b) < 25 && c.r < 230, "drawn in grey");
    luma.Save("histogram_luminosity");
}

void TestScale() {
    std::cout << "\nScale\n";
    Canvas canvas;
    if (!canvas.ctx) return;
    auto chart = Make(canvas, HistogramDisplay::Combined);
    const double linearTop = chart->Axes().At(0).Normalize(500.0);
    chart->SetScale(HistogramScale::Logarithmic);
    Canvas logCanvas;
    chart->Render(logCanvas.ctx.get(), Rect2Df(0, 0, W, H));
    Check(chart->Axes().At(0).scale == ChartScale::SymLog, "Logarithmic uses the symmetric log scale");
    Check(chart->Axes().At(0).Normalize(500.0) > linearTop + 0.2, "the smaller peak stands much taller");
    Check(std::abs(chart->Axes().At(0).Normalize(0.0) - 0.0) < 1e-9, "and an empty level stays at zero");
    const std::vector<double>& ticks = chart->Axes().At(0).tickValues;
    Check(ticks.size() == 5 && ticks[0] == 0.0 && ticks[1] == 1.0 && ticks[4] == 1000.0,
          "the axis reads in decades: 0, 1, 10, 100, 1K");
    logCanvas.Save("histogram_log");
}

void TestImage() {
    std::cout << "\nSetImage\n";
    // A 4 x 2 picture, encoded as a PNG and decoded the way a file would be.
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 4, 2);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
    cairo_rectangle(cr, 0, 0, 2, 2);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.0, 0.0, 1.0);
    cairo_rectangle(cr, 2, 0, 1, 2);
    cairo_fill(cr);                               // the last column stays transparent
    cairo_destroy(cr);
    std::vector<uint8_t> png;
    cairo_surface_write_to_png_stream(
        surface,
        [](void* closure, const unsigned char* data, unsigned int length) {
            auto* out = static_cast<std::vector<uint8_t>*>(closure);
            out->insert(out->end(), data, data + length);
            return CAIRO_STATUS_SUCCESS;
        },
        &png);
    cairo_surface_destroy(surface);
    auto image = UCImageRaster::LoadFromMemory(png);
    auto chart = CreateHistogramChart("image", 0, 0, W, H);
    const bool counted = chart->SetImage(image);
    const ImageHistogram& h = chart->GetHistogram();
    Check(counted && h.pixelCount == 6, "the decoded picture's six visible pixels are counted (two transparent left out)");
    Check(h.CountAt(HistogramChannel::Red, 255) == 4 && h.CountAt(HistogramChannel::Blue, 255) == 2,
          "four red and two blue");
    Check(!chart->SetImage(nullptr) && chart->GetHistogram().Empty(), "no image: an empty histogram");
}

} // namespace

int main(int, char** argv) {
    std::cout << "UltraCanvas histogram chart\n";
    UCImage::InitializeImageSubsysterm(argv[0]);   // the decoder SetImage's picture goes through
    TestCombined();
    TestSingleChannels();
    TestScale();
    TestImage();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed." : "Some checks FAILED.") << "\n";
    return g_failures == 0 ? 0 : 1;
}
