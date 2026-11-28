// Tests/SpiralChartRenderTest.cpp
// The spiral plot drawn offscreen: the spiral fills its circle from the inner
// radius out; the ribbon colours winter blue and summer red on the right
// rays, year after year; the band is filled where values are high and empty
// where they are low; the line is drawn; each sample's piece of ring is its
// hit region; the colour bar takes room only for the ribbon; a new period
// rewinds the spiral; hover reports the sample.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, the cases are also written there as
// PNGs for a person to look at.
//
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/Charts/UltraCanvasSpiralChart.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

const int W = 640, H = 640;

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

bool Reddish(const Color& c) { return c.r > c.b + 60; }
bool Bluish(const Color& c) { return c.b > c.r + 60; }

std::shared_ptr<UltraCanvasSpiralChart> Make(Canvas& canvas, const UltraCanvasSpiralModel& model,
                                             SpiralDisplay display = SpiralDisplay::Ribbon) {
    auto chart = CreateSpiralChart("spiral", 0, 0, W, H);
    chart->SetEnableTooltips(false);
    chart->SetAnimateOnDataChange(false);
    chart->SetModel(model);
    chart->SetDisplay(display);
    chart->SetColormap(HeatmapColormap::RdBu, true);     // cold blue, warm red
    chart->Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
    return chart;
}

double Distance(const Point2Dd& a, const Point2Dd& b) { return std::hypot(a.x - b.x, a.y - b.y); }

void TestRibbon() {
    std::cout << "\nRibbon\n";
    Canvas canvas;
    if (!canvas.ctx) return;
    const UltraCanvasSpiralModel model = UltraCanvasSpiralModel::DailyTemperature();
    auto chart = Make(canvas, model);
    const double r = chart->OuterRadius();
    Check(r > 200.0 && Distance(chart->Centre(), Point2Dd(W / 2.0, chart->Centre().y)) < 1.0,
          "the spiral fills a large circle, centred");
    Check(std::abs(Distance(chart->ScreenPoint(0.0, -0.5), chart->Centre()) - 0.18 * r) < 0.5 &&
              std::abs(Distance(chart->ScreenPoint(3 * 365.0, 0.5), chart->Centre()) - r) < 0.5,
          "from the inner radius to the outer");
    const Point2Dd top = chart->ScreenPoint(365.0, 0.0);
    Check(std::abs(top.x - chart->Centre().x) < 1.0 && top.y < chart->Centre().y, "a year's turn starts at 12 o'clock");
    // Mid-January and mid-July of each year: cold and warm, on opposite rays.
    // (The outer part of the ring: the year's label sits on its middle, at
    // the start of the turn.)
    bool winters = true, summers = true;
    for (int year = 0; year < 3; ++year) {
        if (!Bluish(canvas.PixelAt(chart->ScreenPoint(year * 365.0 + 15.0, 0.35)))) winters = false;
        if (!Reddish(canvas.PixelAt(chart->ScreenPoint(year * 365.0 + 196.0, 0.0)))) summers = false;
    }
    Check(winters, "every January is blue");
    Check(summers, "every July is red");
    const Point2Dd jan = chart->ScreenPoint(15.0, 0.0), jul = chart->ScreenPoint(196.0, 0.0);
    Check((jan.x - chart->Centre().x) * (jul.x - chart->Centre().x) < 0.0, "on opposite sides of the spiral");

    std::string tip;
    const int64_t id = chart->HitRegionAt(chart->ScreenPoint(400.5, 0.3), &tip);
    Check(id == 400, "each day's piece of ring is its hit region");
    Check(tip.find("Temperature") != std::string::npos && tip.rfind("400\n", 0) == 0,
          "its tooltip: the time and the value");
    Check(chart->Frame().plotArea.y + chart->Frame().plotArea.height < H - 40.0, "room under the spiral for the colour bar");
    canvas.Save("spiral_ribbon");
}

void TestBandAndLine() {
    std::cout << "\nBand and line\n";
    UltraCanvasSpiralModel model = UltraCanvasSpiralModel::HourlyVisits();
    Canvas band;
    if (!band.ctx) return;
    auto chart = Make(band, model, SpiralDisplay::Band);
    chart->SetColor(Color(40, 80, 220, 255));
    Canvas banded;
    chart->Render(banded.ctx.get(), Rect2Df(0, 0, W, H));
    // Monday 10:00 of week 2 is busy: the band reaches well up the ring (the
    // third Thursday's sale sets the top of the scale, so a busy hour is
    // about 40 % of it).
    const double busy = 168.0 + 10.5, quiet = 168.0 + 3.5;
    Check(Bluish(banded.PixelAt(chart->ScreenPoint(busy, -0.3))), "a busy hour's band reaches up its ring");
    Check(!Bluish(banded.PixelAt(chart->ScreenPoint(quiet, 0.2))), "a quiet hour's band stays low");
    Check(chart->Frame().plotArea.y + chart->Frame().plotArea.height > H - 30.0, "no colour bar for the band");
    banded.Save("spiral_band");

    Canvas line;
    chart->SetDisplay(SpiralDisplay::Line);
    chart->Render(line.ctx.get(), Rect2Df(0, 0, W, H));
    int drawn = 0;
    for (int k = 0; k < 40; ++k) {
        const Color c = line.PixelAt(chart->ScreenPoint(busy, -0.5 + k / 40.0));
        if (Bluish(c)) ++drawn;
    }
    Check(drawn > 0 && drawn < 10, "the line crosses the busy hour's ring once, thinly");
    line.Save("spiral_line");
}

void TestPeriodAndHover() {
    std::cout << "\nPeriod and hover\n";
    Canvas canvas;
    if (!canvas.ctx) return;
    auto chart = Make(canvas, UltraCanvasSpiralModel::HourlyVisits());
    Check(chart->GetModel().TurnCount() == 4, "a week per turn: four rings");
    chart->SetPeriod(24.0);
    Canvas daily;
    chart->Render(daily.ctx.get(), Rect2Df(0, 0, W, H));
    Check(chart->GetModel().TurnCount() == 28, "a day per turn: 28 rings");
    const Point2Dd a = chart->ScreenPoint(10.0), b = chart->ScreenPoint(34.0);
    Check(std::abs(std::atan2(a.y - chart->Centre().y, a.x - chart->Centre().x) -
                   std::atan2(b.y - chart->Centre().y, b.x - chart->Centre().x)) < 1e-6,
          "10:00 on Monday and Tuesday on one ray");
    daily.Save("spiral_daily");

    int reported = -2;
    chart->onSampleHover = [&reported](int index) { reported = index; };
    chart->OnHitRegionHoverChanged(42);
    Check(reported == 42 && chart->GetHoveredSample() == 42, "hovering reports the sample");
    chart->OnHitRegionHoverChanged(-1);
    Check(reported == -1, "leaving reports -1");
    Check(chart->DescribeSample(2 * 168 + 3 * 24 + 15).find("The sale") != std::string::npos,
          "a sample's note is in its tooltip");
}

} // namespace

int main() {
    std::cout << "UltraCanvas spiral chart\n";
    TestRibbon();
    TestBandAndLine();
    TestPeriodAndHover();
    std::cout << "\n" << (g_failures == 0 ? "All checks passed." : "Some checks FAILED.") << "\n";
    return g_failures == 0 ? 0 : 1;
}
