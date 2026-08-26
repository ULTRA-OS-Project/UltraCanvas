// Tests/SVGConverterTest.cpp
// Round-trip test for the Vector plugin's SVG converter, both directions:
// a VectorDocument (shapes, multi-stop gradient, dashes, opacity, rotated
// group, multi-span text) is exported to SVG, imported back, and compared
// structurally; the exported markup is also decoded through the framework's
// real SVG rendering pipeline (UCImage) and pixel-checked, which proves the
// output is valid SVG to an independent renderer, not just to our importer.
// A hand-written snippet exercises importer robustness (inline style,
// percentages, entities, tspans, defs-referenced gradients).
//
// Usage: SVGConverterTest [output.svg]
// Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorConverter.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"
#include "UltraCanvasImage.h"

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
    doc->Title = "SVG round-trip";

    auto layer = doc->AddLayer("Artwork");

    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd{40, 40, 100, 60};
    rect->Style.Fill = Color(255, 0, 0, 255);
    StrokeData rectStroke;
    rectStroke.Fill = Color(0, 0, 255, 255);
    rectStroke.Width = 2.0f;
    rect->Style.Stroke = rectStroke;
    layer->AddChild(rect);

    // Rounded rect with a THREE-stop gradient: SVG keeps every stop.
    auto rrect = std::make_shared<VectorRect>();
    rrect->Bounds = Rect2Dd{180, 40, 90, 60};
    rrect->RadiusX = 12;
    rrect->RadiusY = 12;
    rrect->Type = VectorElementType::RoundedRectangle;
    LinearGradientData grad;
    grad.Start = Point2Dd(180, 40);
    grad.End = Point2Dd(270, 40);
    grad.Units = GradientUnits::UserSpaceOnUse;
    grad.Stops.push_back(GradientStop(0.0, Color(255, 255, 0, 255)));
    grad.Stops.push_back(GradientStop(0.5, Color(0, 200, 0, 255)));
    grad.Stops.push_back(GradientStop(1.0, Color(255, 0, 255, 255)));
    rrect->Style.Fill = GradientData(grad);
    layer->AddChild(rrect);

    auto circle = std::make_shared<VectorCircle>();
    circle->Center = Point2Dd(330, 70);
    circle->Radius = 30;
    circle->Style.Fill = Color(255, 128, 0, 255);
    circle->Style.Opacity = 0.5f;
    layer->AddChild(circle);

    auto dashLine = std::make_shared<VectorLine>();
    dashLine->Start = Point2Dd(40, 130);
    dashLine->End = Point2Dd(150, 130);
    StrokeData dashStroke;
    dashStroke.Fill = Color(0, 0, 0, 255);
    dashStroke.Width = 2.0f;
    dashStroke.DashArray = {6.0, 3.0};
    dashLine->Style.Stroke = dashStroke;
    layer->AddChild(dashLine);

    auto path = std::make_shared<VectorPath>();
    path->MoveTo(200, 140);
    path->CurveTo(240, 120, 280, 120, 300, 160);
    path->CurveTo(280, 200, 240, 200, 200, 160);
    path->ClosePath();
    path->Style.Fill = Color(90, 40, 160, 255);
    layer->AddChild(path);

    // Rotated group: SVG keeps the transform as an attribute, no baking.
    auto group = std::make_shared<VectorGroup>();
    group->Transform = Matrix3x3::Translate(330, 180) *
                       Matrix3x3::RotateDegrees(30) *
                       Matrix3x3::Translate(-330, -180);
    auto rotRect = std::make_shared<VectorRect>();
    rotRect->Bounds = Rect2Dd{300, 160, 60, 40};
    rotRect->Style.Fill = Color(0, 120, 200, 255);
    group->AddChild(rotRect);
    layer->AddChild(group);

    auto text = std::make_shared<VectorText>();
    text->Position = Point2Dd(40, 250);
    text->BaseStyle.FontFamily = "Liberation Sans";
    text->BaseStyle.FontSize = 18.0f;
    TextSpanData s1;
    s1.Text = "Hello ";
    s1.Style = text->BaseStyle;
    TextSpanData s2;
    s2.Text = "SVG & friends";
    s2.Style = text->BaseStyle;
    s2.Style.Weight = FontWeight::Bold;
    text->Spans.push_back(s1);
    text->Spans.push_back(s2);
    text->Style.Fill = Color(20, 20, 20, 255);
    layer->AddChild(text);

    return doc;
}

template <typename T>
std::shared_ptr<T> ChildAs(const std::shared_ptr<VectorLayer>& layer, size_t i) {
    if (!layer || i >= layer->Children.size()) return nullptr;
    return std::dynamic_pointer_cast<T>(layer->Children[i]);
}

}   // namespace

int main(int argc, char** argv) {
    std::string outPath = argc > 1 ? argv[1] : "svg_roundtrip.svg";

    // The renderer check below decodes through UCImage, which needs the
    // image subsystem (vips) an application normally initializes in main().
    UCImage::InitializeImageSubsysterm("SVGConverterTest");

    auto doc = BuildTestDocument();
    VectorConverter::SVGConverter converter;
    VectorConverter::ConversionOptions options;
    options.WarningCallback = [](const std::string& msg) {
        std::printf("      warning: %s\n", msg.c_str());
    };

    std::string svg = converter.ExportToString(*doc, options);
    Check(!svg.empty(), "ExportToString produces output");
    Check(converter.ValidateData(svg), "output validates as SVG");
    Check(converter.Export(*doc, outPath, options), "Export() writes the file");
    Check(svg.find("<linearGradient") != std::string::npos, "gradient lands in <defs>");
    Check(svg.find("stroke-dasharray=\"6 3\"") != std::string::npos, "dash array serialized");
    Check(svg.find("&amp;") != std::string::npos, "text content is XML-escaped");
    Check(svg.find("transform=\"matrix(") != std::string::npos, "group transform serialized");

    // ===== IMPORT THE EXPORT =====
    auto back = converter.ImportFromString(svg, options);
    Check(back != nullptr, "exported SVG imports back");
    if (!back) return failures;

    Check(std::fabs(back->Size.width - 400) < 0.01 &&
          std::fabs(back->Size.height - 300) < 0.01, "page size round-trips");
    Check(back->Title == "SVG round-trip", "title round-trips");
    Check(back->Layers.size() == 1, "one layer");
    auto layer = back->Layers.empty() ? nullptr : back->Layers[0];
    Check(layer && layer->Children.size() == 7, "seven elements round-trip");

    auto rect = ChildAs<VectorRect>(layer, 0);
    Check(rect && std::fabs(rect->Bounds.x - 40) < 0.01 &&
          std::fabs(rect->Bounds.width - 100) < 0.01, "rect geometry round-trips");
    if (rect) {
        const Color* fc = rect->Style.Fill ? std::get_if<Color>(&*rect->Style.Fill) : nullptr;
        Check(fc && fc->r == 255 && fc->g == 0 && fc->b == 0, "rect fill colour round-trips");
        Check(rect->Style.Stroke && std::fabs(rect->Style.Stroke->Width - 2.0f) < 0.01f,
              "rect stroke width round-trips");
    }

    auto rrect = ChildAs<VectorRect>(layer, 1);
    if (rrect && rrect->Style.Fill) {
        const GradientData* g = std::get_if<GradientData>(&*rrect->Style.Fill);
        const LinearGradientData* lg = g ? std::get_if<LinearGradientData>(g) : nullptr;
        Check(lg && lg->Stops.size() == 3, "all three gradient stops round-trip");
        Check(lg && lg->Stops[1].color.g == 200 &&
              std::fabs(lg->Stops[1].position - 0.5) < 0.001,
              "middle gradient stop keeps colour and offset");
        Check(lg && lg->Units == GradientUnits::UserSpaceOnUse,
              "gradient units round-trip");
    } else {
        Check(false, "rounded rect with gradient fill round-trips");
    }

    auto circle = ChildAs<VectorCircle>(layer, 2);
    Check(circle && std::fabs(circle->Style.Opacity - 0.5f) < 0.01f,
          "circle opacity round-trips");

    auto dash = ChildAs<VectorLine>(layer, 3);
    Check(dash && dash->Style.Stroke && dash->Style.Stroke->DashArray.size() == 2 &&
          std::fabs(dash->Style.Stroke->DashArray[0] - 6.0) < 0.01,
          "dash array round-trips");

    auto path = ChildAs<VectorPath>(layer, 4);
    Check(path && path->Path.commands.size() == 4, "path keeps its four commands");
    Check(path && path->Path.commands[1].Type == PathCommandType::CurveTo &&
          path->Path.commands[3].Type == PathCommandType::ClosePath,
          "path command types round-trip");

    auto group = ChildAs<VectorGroup>(layer, 5);
    Check(group && group->Transform.has_value(), "group transform round-trips");
    if (group && group->Transform) {
        // The rotation must survive numerically: cos(30deg) in m00/m11.
        Check(std::fabs(group->Transform->m[0][0] - 0.866f) < 0.01f &&
              std::fabs(group->Transform->m[0][1] + 0.5f) < 0.01f,
              "rotation matrix values survive");
        auto inner = group->Children.empty()
                ? nullptr : std::dynamic_pointer_cast<VectorRect>(group->Children[0]);
        Check(inner != nullptr, "group child survives");
    }

    auto text = ChildAs<VectorText>(layer, 6);
    Check(text && text->Spans.size() == 2, "two text spans round-trip");
    Check(text && text->Spans.size() == 2 && text->Spans[1].Text == "SVG & friends" &&
          text->Spans[1].Style.Weight == FontWeight::Bold,
          "bold span text and weight round-trip (entities unescaped)");
    Check(text && text->BaseStyle.FontFamily == "Liberation Sans" &&
          std::fabs(text->BaseStyle.FontSize - 18.0f) < 0.01f,
          "font family and size round-trip");

    // ===== INDEPENDENT RENDERER =====
    // Decode the exported SVG through the framework's real SVG pipeline; the
    // pixels prove the markup is valid SVG, not merely self-consistent.
    {
        std::vector<uint8_t> bytes(svg.begin(), svg.end());
        auto img = UCImage::LoadFromMemory(bytes);
        Check(img && img->GetWidth() > 0, "the SVG renderer accepts the export");
        if (img) {
            auto pm = img->GetPixmap(400, 300, ImageFitMode::Contain, 1.0f);
            Check(pm != nullptr, "the export rasterizes");
            if (pm) {
                const uint32_t* px = pm->GetPixelData();
                int w = pm->GetRawWidth();
                auto at = [&](int x, int y) { return px[y * w + x]; };
                uint32_t rectPx = at(90, 70);
                Check(((rectPx >> 16) & 0xFF) > 200 && ((rectPx >> 8) & 0xFF) < 60,
                      "rect renders red at (90,70)");
                uint32_t gradPx = at(225, 70);
                Check(((gradPx >> 8) & 0xFF) > 120,
                      "gradient midpoint renders green at (225,70)");
                uint32_t rotPx = at(330, 180);
                Check((rotPx & 0xFF) > 120 && ((rotPx >> 16) & 0xFF) < 100,
                      "rotated rect renders blue at (330,180)");
            }
        }
    }

    // ===== IMPORTER ROBUSTNESS =====
    {
        const char* handWritten = R"SVG(<?xml version="1.0"?>
<svg xmlns="http://www.w3.org/2000/svg" width="8.333in" height="200" viewBox="0 0 800 200">
  <defs>
    <radialGradient id="rg" cx="0.5" cy="0.5" r="0.5">
      <stop offset="0%" stop-color="#fff"/>
      <stop offset="100%" stop-color="rgb(0,0,255)" stop-opacity="0.8"/>
    </radialGradient>
  </defs>
  <g style="fill:#00ff00; stroke: black; stroke-width: 3">
    <rect x="10" y="10" width="50" height="50" rx="5"/>
    <ellipse cx="120" cy="35" rx="40" ry="20" fill="url(#rg)"/>
  </g>
  <text x="10" y="120" font-size="16pt">A &lt;tag&gt; <tspan font-weight="bold" x="10" y="150">and more</tspan></text>
  <polygon points="200,10 250,60 200,60" fill="purple" visibility="hidden"/>
</svg>)SVG";
        auto hd = converter.ImportFromString(handWritten, options);
        Check(hd != nullptr, "hand-written SVG imports");
        if (hd) {
            Check(std::fabs(hd->Size.width - 800) < 0.5, "in-unit width converts (8.333in -> 800)");
            auto l = hd->Layers.empty() ? nullptr : hd->Layers[0];
            Check(l && l->Children.size() == 3, "three top-level nodes");
            auto g = l ? std::dynamic_pointer_cast<VectorGroup>(l->Children[0]) : nullptr;
            Check(g && g->Children.size() == 2, "group carries two shapes");
            if (g) {
                const Color* gc = g->Style.Fill ? std::get_if<Color>(&*g->Style.Fill) : nullptr;
                Check(gc && gc->g == 255 && gc->r == 0, "inline style fill parsed");
                Check(g->Style.Stroke && std::fabs(g->Style.Stroke->Width - 3.0f) < 0.01f,
                      "inline style stroke parsed");
                auto el = std::dynamic_pointer_cast<VectorEllipse>(g->Children[1]);
                const GradientData* eg = (el && el->Style.Fill)
                        ? std::get_if<GradientData>(&*el->Style.Fill) : nullptr;
                const RadialGradientData* rg = eg ? std::get_if<RadialGradientData>(eg) : nullptr;
                Check(rg && rg->Stops.size() == 2 && rg->Stops[1].color.b == 255 &&
                      rg->Stops[1].color.a == 204,
                      "radial gradient with stop-opacity resolves through url(#id)");
            }
            auto txt = l ? std::dynamic_pointer_cast<VectorText>(l->Children[1]) : nullptr;
            Check(txt && txt->Spans.size() == 2 && txt->Spans[0].Text.find("<tag>") != std::string::npos,
                  "entities decode and tspans split");
            Check(txt && std::fabs(txt->BaseStyle.FontSize - 16.0f * 96.0f / 72.0f) < 0.1f,
                  "pt font size converts to user units");
            auto poly = l ? std::dynamic_pointer_cast<VectorPolygon>(l->Children[2]) : nullptr;
            Check(poly && !poly->Style.Visible, "visibility:hidden imports");
        }
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
