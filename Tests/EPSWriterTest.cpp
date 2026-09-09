// Tests/EPSWriterTest.cpp
// Round-trip test for the EPS writer in the Vector plugin: builds the same
// kind of VectorDocument the XAR writer test uses (shapes, closed bezier
// path, rotated group, dashes, multi-span text), exports it through
// VectorConverter::EPSConverter, then feeds the result to the EPS plugin's
// PostScript interpreter and asserts the program is fully understood: DSC
// header parsed (size, title, creator), every operator known, no warnings.
//
// Usage: EPSWriterTest [output.eps]
// The export is kept on disk (default: eps_writer_roundtrip.eps in the
// working directory) so it can be rendered with EPSProbeTest --render or
// ghostscript. Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasEPSConverter.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"
#include "../UltraCanvas/Plugins/Vector/EPS/UltraCanvasEPSPlugin.h"

#ifdef EPSWRITER_HAVE_CAIRO
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
    doc->Title = "EPS writer round-trip";

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

    // 3. Circle with 50% opacity (flattened toward white in EPS).
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

    // 6. Rotated rectangle inside a transformed group.
    auto group = std::make_shared<VectorGroup>();
    group->Transform = Matrix3x3::Translate(330, 180) *
                       Matrix3x3::RotateDegrees(30) *
                       Matrix3x3::Translate(-330, -180);
    auto rotRect = std::make_shared<VectorRect>();
    rotRect->Bounds = Rect2Dd{300, 160, 60, 40};
    rotRect->Style.Fill = Color(0, 120, 200, 255);
    group->AddChild(rotRect);
    layer->AddChild(group);

    // 7. Two-line text with a bold second span.
    auto text = std::make_shared<VectorText>();
    text->Position = Point2Dd(40, 250);
    text->BaseStyle.FontFamily = "Liberation Sans";
    text->BaseStyle.FontSize = 18.0f;
    TextSpanData s1;
    s1.Text = "Hello ";
    s1.Style = text->BaseStyle;
    TextSpanData s2;
    s2.Text = "EPS\nround trip (with parens)";
    s2.Style = text->BaseStyle;
    s2.Style.Weight = FontWeight::Bold;
    text->Spans.push_back(s1);
    text->Spans.push_back(s2);
    text->Style.Fill = Color(20, 20, 20, 255);
    layer->AddChild(text);

    return doc;
}

}   // namespace

int main(int argc, char** argv) {
    std::string outPath = argc > 1 ? argv[1] : "eps_writer_roundtrip.eps";

    auto doc = BuildTestDocument();

    VectorConverter::EPSConverter converter;
    std::printf("Exporting to %s ...\n", outPath.c_str());
    int exportWarnings = 0;
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [&exportWarnings](const std::string& msg) {
        ++exportWarnings;
        std::printf("      export warning: %s\n", msg.c_str());
    };
    Check(converter.Export(*doc, outPath, options), "Export() returns true");
    Check(converter.ValidateFile(outPath), "exported file starts with %!PS");
    // The document deliberately carries one unsupported feature (opacity),
    // which must warn rather than silently change meaning.
    Check(exportWarnings >= 1, "opacity flattening was reported");

    EPSDocument reader;
    Check(reader.LoadFromFile(outPath), "EPSDocument loads the exported file");
    if (failures) return failures;

    Check(std::fabs(reader.GetWidth() - 400.0f) < 0.5f, "page width is 400");
    Check(std::fabs(reader.GetHeight() - 300.0f) < 0.5f, "page height is 300");
    Check(reader.GetCreator() == "UltraCanvas", "creator string round-trips");
    Check(reader.GetTitle() == "EPS writer round-trip", "title round-trips");

    // Interpreting the program is the real assertion: render it through the
    // EPS plugin's PostScript interpreter and require that every operator
    // was understood and nothing needed a recovery warning.
    auto ctx = CreateRenderContext(Size2Di(400, 300), nullptr);
    Check(ctx != nullptr, "offscreen render context created");
    if (ctx) {
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillRectangle(Rect2Dd(0, 0, 400, 300));
        reader.Render(ctx.get(), 1.0f);

        const auto& diag = reader.GetDiagnostics();
        Check(diag.tokenCount > 50, "a plausible number of tokens was interpreted");
        Check(diag.unknownOperators.empty(), "interpreter reports no unknown operators");
        for (const auto& [op, count] : diag.unknownOperators) {
            std::printf("      unknown operator: %s x%zu\n", op.c_str(), count);
        }
        Check(diag.warnings.empty(), "interpreter reports no warnings");
        for (const auto& w : diag.warnings) std::printf("      warning: %s\n", w.c_str());

#ifdef EPSWRITER_HAVE_CAIRO
        // Placement checks in the rendered raster. The interpreter flips the
        // PostScript Y axis back onto the page, so the image is in document
        // orientation: rect 1 covers (40,40)-(140,100), centre (90,70).
        cairo_t* cr = static_cast<cairo_t*>(ctx->GetNativeContext());
        cairo_surface_t* surface = cr ? cairo_get_target(cr) : nullptr;
        Check(surface != nullptr, "native cairo surface accessible");
        if (surface) {
            cairo_surface_flush(surface);
            const unsigned char* data = cairo_image_surface_get_data(surface);
            int stride = cairo_image_surface_get_stride(surface);
            auto px = [&](int x, int y) {
                const uint32_t* row = reinterpret_cast<const uint32_t*>(data + y * stride);
                return row[x];   // ARGB32 premultiplied
            };
            uint32_t rectPx = px(90, 70);
            Check(((rectPx >> 16) & 0xFF) > 200 && ((rectPx >> 8) & 0xFF) < 60 &&
                  (rectPx & 0xFF) < 60,
                  "rect centre renders red at (90,70)");
            uint32_t circlePx = px(330, 70);
            Check(((circlePx >> 16) & 0xFF) > 200 && ((circlePx >> 8) & 0xFF) > 150,
                  "flattened 50% orange circle renders pale at (330,70)");
            uint32_t bgPx = px(10, 290);
            Check((bgPx & 0xFFFFFF) == 0xFFFFFF, "background stays white");
        }
#endif
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
