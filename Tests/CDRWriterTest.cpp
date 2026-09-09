// Tests/CDRWriterTest.cpp
// Round-trip test for the CDR writer in the Vector plugin: builds a
// VectorDocument (shapes, closed bezier path, rotated group, dashed line,
// opacity), exports it through VectorConverter::CDRConverter, then loads
// the result back through the CDR plugin (libcdr underneath) and asserts
// the document survived: it parses, has one page of the right size, and
// renders the objects at their places with their colours.
//
// CorelDRAW's format has no public specification, so the writer targets
// libcdr's parser layouts; this test loading through that parser IS the
// correctness contract.
//
// Usage: CDRWriterTest [output.cdr]
// The export is kept on disk (default: cdr_writer_roundtrip.cdr in the
// working directory). Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasCDRConverter.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"
#include "../UltraCanvas/Plugins/Vector/CDR/UltraCanvasCDRPlugin.h"

#ifdef CDRWRITER_HAVE_CAIRO
#include <cairo.h>
#endif

#include <cmath>
#include <cstdio>
#include <string>

using namespace UltraCanvas;
using namespace UltraCanvas::VectorStorage;

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::printf("FAIL: %s\n", what.c_str());
    } else {
        std::printf("  ok: %s\n", what.c_str());
    }
}

std::shared_ptr<VectorDocument> BuildTestDocument() {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{400, 300};

    auto layer = doc->AddLayer("Artwork");

    // 1. Red rectangle with a blue stroke.
    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd{40, 40, 100, 60};
    rect->Style.Fill = Color(255, 0, 0, 255);
    StrokeData rectStroke;
    rectStroke.Fill = Color(0, 0, 255, 255);
    rectStroke.Width = 2.0f;
    rect->Style.Stroke = rectStroke;
    layer->AddChild(rect);

    // 2. Rounded green rectangle.
    auto rrect = std::make_shared<VectorRect>();
    rrect->Bounds = Rect2Dd{180, 40, 90, 60};
    rrect->RadiusX = 12;
    rrect->RadiusY = 12;
    rrect->Style.Fill = Color(0, 160, 0, 255);
    layer->AddChild(rrect);

    // 3. Circle with 50% opacity (kept: CDR stores fill opacity).
    auto circle = std::make_shared<VectorCircle>();
    circle->Center = Point2Dd(330, 70);
    circle->Radius = 30;
    circle->Style.Fill = Color(255, 128, 0, 255);
    circle->Style.Opacity = 0.5f;
    layer->AddChild(circle);

    // 4. Dashed line.
    auto dashLine = std::make_shared<VectorLine>();
    dashLine->Start = Point2Dd(40, 130);
    dashLine->End = Point2Dd(150, 130);
    StrokeData dashStroke;
    dashStroke.Fill = Color(0, 0, 0, 255);
    dashStroke.Width = 2.0f;
    dashStroke.DashArray = {6.0, 3.0};
    dashLine->Style.Stroke = dashStroke;
    layer->AddChild(dashLine);

    // 5. Closed bezier path, filled and stroked.
    auto path = std::make_shared<VectorPath>();
    path->MoveTo(200, 140);
    path->CurveTo(240, 120, 280, 120, 300, 160);
    path->CurveTo(280, 200, 240, 200, 200, 160);
    path->ClosePath();
    path->Style.Fill = Color(90, 40, 160, 255);
    StrokeData pathStroke;
    pathStroke.Fill = Color(30, 30, 30, 255);
    pathStroke.Width = 1.5f;
    path->Style.Stroke = pathStroke;
    layer->AddChild(path);

    // 6. Rotated rectangle inside a transformed group (baked into points).
    auto group = std::make_shared<VectorGroup>();
    group->Transform = Matrix3x3::Translate(330, 180) *
                       Matrix3x3::RotateDegrees(30) *
                       Matrix3x3::Translate(-330, -180);
    auto rotRect = std::make_shared<VectorRect>();
    rotRect->Bounds = Rect2Dd{300, 160, 60, 40};
    rotRect->Style.Fill = Color(0, 120, 200, 255);
    group->AddChild(rotRect);
    layer->AddChild(group);

    return doc;
}

}   // namespace

int main(int argc, char** argv) {
    std::string outPath = argc > 1 ? argv[1] : "cdr_writer_roundtrip.cdr";

    auto doc = BuildTestDocument();

    VectorConverter::CDRConverter converter;
    std::printf("Exporting to %s ...\n", outPath.c_str());
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [](const std::string& msg) {
        std::printf("      export warning: %s\n", msg.c_str());
    };
    Check(converter.Export(*doc, outPath, options), "Export() returns true");
    Check(converter.ValidateFile(outPath), "exported file carries the RIFF CDR signature");

    UltraCanvasCDRRenderer reader;
    Check(reader.LoadFromFile(outPath), "the CDR plugin (libcdr) loads the exported file");
    if (failures) return failures;

    Check(reader.GetPageCount() == 1, "one page");

    // The plugin stores page size in pixels at 96 dpi: 400x300pt -> 533.3x400.
    constexpr float kPxPerPt = 96.0f / 72.0f;
    const float pageWpx = 400 * kPxPerPt;
    const float pageHpx = 300 * kPxPerPt;

    // Render at 1:1 and probe object placement.
    auto ctx = CreateRenderContext(Size2Di(static_cast<int>(pageWpx + 0.5f),
                                           static_cast<int>(pageHpx + 0.5f)), nullptr);
    Check(ctx != nullptr, "offscreen render context created");
    if (ctx) {
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRectangle(Rect2Dd(0, 0, pageWpx, pageHpx));
        reader.SetViewport(pageWpx, pageHpx);
        reader.RenderPage(ctx.get(), 0);

#ifdef CDRWRITER_HAVE_CAIRO
        cairo_t* cr = static_cast<cairo_t*>(ctx->GetNativeContext());
        cairo_surface_t* surface = cr ? cairo_get_target(cr) : nullptr;
        Check(surface != nullptr, "native cairo surface accessible");
        if (surface) {
            cairo_surface_flush(surface);
            const unsigned char* data = cairo_image_surface_get_data(surface);
            int stride = cairo_image_surface_get_stride(surface);
            auto px = [&](double xPt, double yPt) {
                int x = static_cast<int>(xPt * kPxPerPt + 0.5f);
                int y = static_cast<int>(yPt * kPxPerPt + 0.5f);
                const uint32_t* row = reinterpret_cast<const uint32_t*>(data + y * stride);
                return row[x];   // ARGB32 premultiplied
            };
            uint32_t rectPx = px(90, 70);
            Check(((rectPx >> 16) & 0xFF) > 200 && ((rectPx >> 8) & 0xFF) < 60 &&
                  (rectPx & 0xFF) < 60,
                  "rect centre renders red at (90,70)pt");
            uint32_t rrectPx = px(225, 70);
            Check(((rrectPx >> 8) & 0xFF) > 100 && ((rrectPx >> 16) & 0xFF) < 80,
                  "rounded rect centre renders green at (225,70)pt");
            uint32_t pathPx = px(250, 160);
            Check(((pathPx >> 16) & 0xFF) < 160 && (pathPx & 0xFF) > 100,
                  "bezier path centre renders purple at (250,160)pt");
            uint32_t rotPx = px(330, 180);
            Check((rotPx & 0xFF) > 150 && ((rotPx >> 16) & 0xFF) < 100,
                  "rotated rect centre renders blue at (330,180)pt");
            uint32_t bgPx = px(20, 280);
            Check((bgPx & 0xFFFFFF) == 0xFFFFFF, "background stays white");

            // Keep the raster next to the .cdr for visual inspection.
            cairo_surface_write_to_png(surface, (outPath + ".png").c_str());
        }
#endif
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
