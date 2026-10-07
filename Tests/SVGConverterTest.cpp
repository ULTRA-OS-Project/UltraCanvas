// Tests/SVGConverterTest.cpp
// Round-trip test for the Vector plugin's SVG converter, both directions:
// a VectorDocument (shapes, multi-stop gradient, dashes, opacity, rotated
// group, multi-span text) is exported to SVG, imported back, and compared
// structurally; the exported markup is also decoded through the framework's
// real SVG rendering pipeline (UCImage) and pixel-checked, which proves the
// output is valid SVG to an independent renderer, not just to our importer.
// A hand-written snippet exercises importer robustness (inline style,
// percentages, entities, tspans, defs-referenced gradients), a styled
// one the <style> cascade (class/id/descendant rules, !important, CDATA),
// and a marked one <marker> drawing (placement, orient, viewBox, units,
// context paint, clipping, inheritance, a self-referencing marker); line-
// gallery arrowheads go out as markers and come back as arrowheads, and
// width profiles and brushes go out as what they draw and come back as
// strokes.
//
// Usage: SVGConverterTest [output.svg]
// Exit code is the number of failed checks.
// Version: 1.5.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "UltraCanvasVectorConverter.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "UltraCanvasImage.h"

#include <cmath>
#include <functional>
#include <cstdio>
#include <string>
#include <vector>

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

    // ===== REAL-WORLD SYNTAX =====
    // What optimised and editor-written files look like, each of which made
    // an imported drawing lose most of its shapes, its placement or its text
    // (media/vector/SVG/robot.svg, photo-camera.svg, Logo_Texter.svg).
    {
        // Numbers run together wherever the next starts with a sign or a
        // second decimal point; a repeated set continues the command.
        PathData pd = ParsePathString("m36.938 423.38-18.759-11.621.95-.16.857.722z");
        Check(pd.commands.size() == 5, "compact path data: five commands (m, 3 implicit l, z)");
        if (pd.commands.size() == 5) {
            Check(pd.commands[1].Type == PathCommandType::LineTo && pd.commands[1].Relative &&
                  std::fabs(pd.commands[1].Parameters[0] + 18.759f) < 1e-3f &&
                  std::fabs(pd.commands[1].Parameters[1] + 11.621f) < 1e-3f,
                  "compact path data: pair after m is a relative lineto");
            Check(std::fabs(pd.commands[2].Parameters[0] - 0.95f) < 1e-4f &&
                  std::fabs(pd.commands[2].Parameters[1] + 0.16f) < 1e-4f &&
                  std::fabs(pd.commands[3].Parameters[0] - 0.857f) < 1e-4f &&
                  std::fabs(pd.commands[3].Parameters[1] - 0.722f) < 1e-4f,
                  "compact path data: \".95-.16.857.722\" is four numbers");
            Check(pd.commands[4].Type == PathCommandType::ClosePath, "compact path data: close");
        }
        PathData arc = ParsePathString("M+10,20a1 1 0 01 5 5e0");
        Check(arc.commands.size() == 2 && arc.commands[1].Parameters.size() == 7 &&
              arc.commands[1].Parameters[3] == 0.0f && arc.commands[1].Parameters[4] == 1.0f &&
              std::fabs(arc.commands[1].Parameters[5] - 5.0f) < 1e-4f &&
              std::fabs(arc.commands[0].Parameters[0] - 10.0f) < 1e-4f,
              "arc flags need no separator, '+' and exponents read");

        // Space-separated transform arguments: the old reader ate the first
        // character of every argument after the first.
        Matrix3x3 t = ParseTransformString("translate(483.572 574.049) scale(1 -1)");
        Check(std::fabs(t.m[1][2] - 574.049) < 1e-3 && std::fabs(t.m[1][1] + 1.0) < 1e-6,
              "space-separated transform arguments keep every digit and sign");
        Point2Dd r = ParseTransformString("rotate(90 10 10)").Transform(Point2Dd(20, 10));
        Check(std::fabs(r.x - 10) < 1e-6 && std::fabs(r.y - 20) < 1e-6,
              "rotate(a cx cy) turns about its centre");

        const char* inherit = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">
  <g fill="#ff0000" stroke="#0000ff"><path d="M0 0h5v5z"/><rect width="5" height="5" stroke="none"/></g>
  <path d="M50 50h5v5z"/>
</svg>)SVG";
        auto id = converter.ImportFromString(inherit, options);
        auto il = (id && !id->Layers.empty()) ? id->Layers[0] : nullptr;
        auto ig = il ? std::dynamic_pointer_cast<VectorGroup>(il->Children[0]) : nullptr;
        auto inPath = ig ? std::dynamic_pointer_cast<VectorPath>(ig->Children[0]) : nullptr;
        auto inRect = ig ? std::dynamic_pointer_cast<VectorRect>(ig->Children[1]) : nullptr;
        auto bare = (il && il->Children.size() > 1) ? std::dynamic_pointer_cast<VectorPath>(il->Children[1]) : nullptr;
        const Color* pf = (inPath && inPath->Style.Fill) ? std::get_if<Color>(&*inPath->Style.Fill) : nullptr;
        Check(pf && pf->r == 255 && pf->b == 0, "a shape inherits its group's fill");
        Check(inPath && inPath->Style.Stroke, "a shape inherits its group's stroke");
        Check(inRect && !inRect->Style.Stroke, "stroke=\"none\" is not overridden by the group");
        const Color* bf = (bare && bare->Style.Fill) ? std::get_if<Color>(&*bare->Style.Fill) : nullptr;
        Check(bf && bf->r == 0 && bf->g == 0 && bf->b == 0 && bf->a == 255,
              "a shape with no fill anywhere is black (the SVG default)");

        // viewBox offset and scale, and a transform on a top-level <g>
        // (which becomes a layer), both land on the page.
        const char* placed = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="10 10 10 10">
  <g transform="translate(2 3)"><rect x="8" y="7" width="10" height="10"/></g>
</svg>)SVG";
        auto pdoc = converter.ImportFromString(placed, options);
        auto pl = (pdoc && !pdoc->Layers.empty()) ? pdoc->Layers[0] : nullptr;
        Rect2Dd pb = pl ? pl->GetBoundingBox() : Rect2Dd{};
        Check(pl && !pl->Transform && std::fabs(pb.x) < 1e-6 && std::fabs(pb.y) < 1e-6 &&
              std::fabs(pb.width - 20) < 1e-6 && std::fabs(pb.height - 20) < 1e-6,
              "viewBox offset/scale and a layer transform map the drawing onto the page");
        Check(pdoc && std::fabs(pdoc->ViewBox.x) < 1e-9 && std::fabs(pdoc->ViewBox.width - 20) < 1e-9,
              "the page becomes the viewBox");

        // <clipPath> becomes a definition an element points at; an <image>
        // that is SVG arrives as an editable group placed on its box, with
        // its own clip paths renamed into this document (libcdr's PowerClip
        // output is exactly this).
        const std::string clipped = std::string(R"SVG(<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 100 100">
  <defs><clipPath id="c"><circle cx="50" cy="50" r="20"/></clipPath></defs>
  <rect width="100" height="100" fill="red" clip-path="url(#c)"/>
  <image x="20" y="30" width="40" height="40" xlink:href="data:image/svg+xml;base64,)SVG") + "PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIxMCIgaGVpZ2h0PSIxMCIgdmlld0JveD0iMCAwIDEwIDEwIj48ZGVmcz48Y2xpcFBhdGggaWQ9ImMiPjxyZWN0IHdpZHRoPSI1IiBoZWlnaHQ9IjUiLz48L2NsaXBQYXRoPjwvZGVmcz48cmVjdCB3aWR0aD0iMTAiIGhlaWdodD0iMTAiIGZpbGw9IiMwMGZmMDAiIGNsaXAtcGF0aD0idXJsKCNjKSIvPjwvc3ZnPg==" + R"SVG("/>
</svg>)SVG";
        auto cdoc = converter.ImportFromString(clipped, options);
        auto cl = (cdoc && !cdoc->Layers.empty()) ? cdoc->Layers[0] : nullptr;
        auto crect = (cl && !cl->Children.empty()) ? std::dynamic_pointer_cast<VectorRect>(cl->Children[0]) : nullptr;
        Check(crect && crect->Style.ClipPath && *crect->Style.ClipPath == "c" &&
              std::dynamic_pointer_cast<VectorClipPath>(cdoc->GetDefinition("c")) != nullptr,
              "clip-path=url(#id) points at a VectorClipPath definition");
        auto img = (cl && cl->Children.size() > 1) ? std::dynamic_pointer_cast<VectorGroup>(cl->Children[1]) : nullptr;
        Check(img && img->Transform && std::fabs(img->Transform->m[0][0] - 4.0) < 1e-9 &&
              std::fabs(img->Transform->m[0][2] - 20.0) < 1e-9 && std::fabs(img->Transform->m[1][2] - 30.0) < 1e-9,
              "an SVG image becomes a group mapped onto its box");
        std::shared_ptr<VectorRect> innerRect;
        std::function<void(const std::shared_ptr<VectorElement>&)> find = [&](const std::shared_ptr<VectorElement>& e) {
            if (auto r = std::dynamic_pointer_cast<VectorRect>(e)) innerRect = r;
            if (auto g = std::dynamic_pointer_cast<VectorGroup>(e)) for (auto& c : g->Children) find(c);
        };
        if (img) find(img);
        Check(innerRect && innerRect->Style.ClipPath && *innerRect->Style.ClipPath != "c" &&
              cdoc->GetDefinition(*innerRect->Style.ClipPath) != nullptr,
              "the nested image's clip path is carried over under its own name");
    }

    // ===== CSS STYLE SHEETS =====
    // Diagrams style their boxes by class from a <style> block; skipping it
    // imported every such box black (an architecture diagram whose page
    // background, `.container { fill: #f8f9fa; rx: 8 }`, came in black).
    {
        const char* styled = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 200">
  <defs>
    <style>
      /* a comment */
      .box { fill: #f8f9fa; stroke: #343a40; stroke-width: 2; rx: 8; }
      rect.hot { fill: #ff0000; }
      #special { fill: #00ff00; }
      .forced { fill: #111111 !important; }
      g .inner { fill: #0000ff; }
      .label { font-family: Arial, sans-serif; font-size: 16px; font-weight: bold; text-anchor: middle; fill: #2c3e50; }
    </style>
  </defs>
  <style type="text/css"><![CDATA[ .late { fill: #123456; } ]]></style>
  <style media="print">.box { fill: #ff00ff; }</style>
  <rect class="box" x="0" y="0" width="400" height="200"/>
  <rect class="box hot" x="10" y="10" width="20" height="20" fill="#000000"/>
  <rect id="special" class="hot box" x="40" y="10" width="20" height="20"/>
  <rect class="box" x="70" y="10" width="20" height="20" style="fill: #abcdef"/>
  <rect class="forced" x="100" y="10" width="20" height="20" style="fill: #abcdef"/>
  <g><circle class="inner" cx="150" cy="20" r="10"/></g>
  <circle class="inner" cx="180" cy="20" r="10"/>
  <rect class="late" x="200" y="10" width="20" height="20"/>
  <text class="label" x="200" y="100">Title</text>
</svg>)SVG";
        std::vector<std::string> notes;
        VectorConverter::ConversionOptions cssOptions;
        cssOptions.WarningCallback = [&notes](const std::string& msg) { notes.push_back(msg); };
        auto sdoc = converter.ImportFromString(styled, cssOptions);
        auto sl = (sdoc && !sdoc->Layers.empty()) ? sdoc->Layers[0] : nullptr;
        Check(sl && sl->Children.size() == 9, "styled SVG: nine elements");
        auto fillOf = [](const std::shared_ptr<VectorElement>& e) -> const Color* {
            return (e && e->Style.Fill) ? std::get_if<Color>(&*e->Style.Fill) : nullptr;
        };
        auto isColor = [&](const std::shared_ptr<VectorElement>& e, uint8_t r, uint8_t g, uint8_t b) {
            const Color* c = fillOf(e);
            return c && c->r == r && c->g == g && c->b == b;
        };

        auto bg = ChildAs<VectorRect>(sl, 0);
        Check(isColor(bg, 0xf8, 0xf9, 0xfa), "a class rule fills the shape (not black)");
        Check(bg && bg->Style.Stroke && std::fabs(bg->Style.Stroke->Width - 2.0f) < 0.01f,
              "a class rule sets stroke and stroke-width");
        Check(bg && std::fabs(bg->RadiusX - 8.0f) < 0.01f && bg->Type == VectorElementType::RoundedRectangle,
              "rx from a style sheet rounds the rect (SVG 2 geometry property)");
        Check(isColor(ChildAs<VectorRect>(sl, 1), 255, 0, 0),
              "a more specific rule wins, and beats the presentation attribute");
        Check(isColor(ChildAs<VectorRect>(sl, 2), 0, 255, 0), "an id rule beats class rules");
        Check(isColor(ChildAs<VectorRect>(sl, 3), 0xab, 0xcd, 0xef), "style=\"...\" beats the style sheet");
        Check(isColor(ChildAs<VectorRect>(sl, 4), 0x11, 0x11, 0x11), "!important in the sheet beats style=\"...\"");
        auto g = ChildAs<VectorGroup>(sl, 5);
        Check(g && !g->Children.empty() && isColor(g->Children[0], 0, 0, 255),
              "a descendant selector matches inside its ancestor");
        Check(isColor(ChildAs<VectorCircle>(sl, 6), 0, 0, 0),
              "a descendant selector does not match outside its ancestor");
        Check(isColor(ChildAs<VectorRect>(sl, 7), 0x12, 0x34, 0x56),
              "a top-level <style> in CDATA applies");
        auto label = ChildAs<VectorText>(sl, 8);
        Check(label && label->BaseStyle.Anchor == TextAnchor::Middle &&
              std::fabs(label->BaseStyle.FontSize - 16.0f) < 0.01f &&
              label->BaseStyle.Weight == FontWeight::Bold && label->BaseStyle.FontFamily == "Arial" &&
              isColor(label, 0x2c, 0x3e, 0x50),
              "a class rule styles text (anchor, size, weight, family, fill)");
        bool styleNote = false;
        for (const auto& n : notes) styleNote = styleNote || n.find("style") != std::string::npos;
        Check(!styleNote, "no reader note about <style>");
    }

    // ===== MARKERS =====
    // Diagram connectors draw their arrowheads with <marker>; skipping it
    // left every arrow a bare line. Each marker is drawn as shapes grouped
    // with its line.
    {
        const char* marked = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 100">
  <style>.arrow { stroke: #ff0000; stroke-width: 2; marker-end: url(#head); }</style>
  <defs>
    <marker id="head" markerWidth="10" markerHeight="7" refX="9" refY="3.5" orient="auto">
      <polygon points="0 0, 10 3.5, 0 7" fill="#2c3e50"/>
    </marker>
    <marker id="dot" viewBox="0 0 10 10" refX="5" refY="5" markerWidth="4" markerHeight="4" markerUnits="userSpaceOnUse">
      <circle cx="5" cy="5" r="5" fill="context-stroke"/>
    </marker>
    <marker id="rev" markerWidth="10" markerHeight="10" orient="auto-start-reverse"><path d="M0 0 L5 0"/></marker>
    <marker id="big" markerWidth="2" markerHeight="2"><rect width="10" height="10"/></marker>
    <marker id="self" markerWidth="5" markerHeight="5"><line x2="5" y2="5" stroke="black" marker-end="url(#self)"/></marker>
  </defs>
  <line class="arrow" x1="10" y1="10" x2="10" y2="50"/>
  <polyline points="20 10, 60 10, 60 50" fill="none" stroke="#00ff00" marker-mid="url(#dot)"/>
  <path d="M100 10 L140 10" stroke="black" marker-start="url(#rev)" marker-end="url(#rev)"/>
  <path d="M100 50 C 120 50 140 70 140 90" fill="none" stroke="black" marker-end="url(#head)"/>
  <line x1="150" y1="10" x2="190" y2="10" stroke="black" marker-end="url(#big)"/>
  <g marker-end="url(#head)"><line x1="0" y1="90" x2="40" y2="90" stroke="blue"/></g>
  <line x1="150" y1="50" x2="190" y2="50" stroke="black" marker-end="url(#self)"/>
  <line x1="0" y1="0" x2="5" y2="5" stroke="black"/>
</svg>)SVG";
        std::vector<std::string> notes;
        VectorConverter::ConversionOptions markerOptions;
        markerOptions.WarningCallback = [&notes](const std::string& msg) { notes.push_back(msg); };
        auto mdoc = converter.ImportFromString(marked, markerOptions);
        auto ml = (mdoc && !mdoc->Layers.empty()) ? mdoc->Layers[0] : nullptr;
        Check(ml && ml->Children.size() == 8, "marked SVG: eight elements");
        // The marker drawings of top-level element i (the group's children
        // after the shape itself).
        auto drawings = [&](size_t i) {
            std::vector<std::shared_ptr<VectorGroup>> out;
            auto g = ChildAs<VectorGroup>(ml, i);
            if (g) for (size_t k = 1; k < g->Children.size(); ++k)
                if (auto d = std::dynamic_pointer_cast<VectorGroup>(g->Children[k])) out.push_back(d);
            return out;
        };
        auto near = [](const std::shared_ptr<VectorGroup>& d, Point2Dd content, double x, double y) {
            if (!d || !d->Transform) return false;
            const Point2Dd p = d->Transform->Transform(content);
            return std::fabs(p.x - x) < 1e-6 && std::fabs(p.y - y) < 1e-6;
        };

        auto arrow = ChildAs<VectorGroup>(ml, 0);
        auto head = drawings(0);
        Check(arrow && arrow->Children.size() == 2 && std::dynamic_pointer_cast<VectorLine>(arrow->Children[0]) &&
              head.size() == 1, "marker-end from a CSS class: the line and its arrowhead become one group");
        Check(!head.empty() && near(head[0], Point2Dd(9, 3.5), 10, 50) && near(head[0], Point2Dd(10, 3.5), 10, 52),
              "the arrowhead's refX/refY sits on the line's end, turned along it and scaled by stroke-width");
        const Color* hc = (!head.empty() && !head[0]->Children.empty() && head[0]->Children[0]->Style.Fill)
                ? std::get_if<Color>(&*head[0]->Children[0]->Style.Fill) : nullptr;
        Check(hc && hc->r == 0x2c && hc->g == 0x3e && hc->b == 0x50, "the arrowhead keeps the marker's own fill");

        auto dot = drawings(1);
        Check(dot.size() == 1 && near(dot[0], Point2Dd(5, 5), 60, 10) && near(dot[0], Point2Dd(10, 5), 62, 10),
              "marker-mid on the middle vertex only, viewBox mapped, markerUnits=userSpaceOnUse unscaled");
        const Color* dc = (!dot.empty() && !dot[0]->Children.empty() && dot[0]->Children[0]->Style.Fill)
                ? std::get_if<Color>(&*dot[0]->Children[0]->Style.Fill) : nullptr;
        Check(dc && dc->g == 255 && dc->r == 0, "fill=\"context-stroke\" takes the line's stroke");

        auto rev = drawings(2);
        Check(rev.size() == 2 && near(rev[0], Point2Dd(5, 0), 95, 10) && near(rev[1], Point2Dd(5, 0), 145, 10),
              "orient=auto-start-reverse turns the start marker round, not the end one");
        auto curve = drawings(3);
        Check(curve.size() == 1 && near(curve[0], Point2Dd(10, 3.5), 140, 91),
              "an arrowhead on a curve follows the curve's tangent at its end");
        auto big = drawings(4);
        Check(big.size() == 1 && big[0]->Style.ClipPath &&
              std::dynamic_pointer_cast<VectorClipPath>(mdoc->GetDefinition(*big[0]->Style.ClipPath)) != nullptr,
              "content larger than the marker is clipped to its viewport");
        Check(head.size() == 1 && !head[0]->Style.ClipPath, "content inside the viewport is not clipped");
        auto inGroup = ChildAs<VectorGroup>(ml, 5);
        auto inherited = (inGroup && !inGroup->Children.empty())
                ? std::dynamic_pointer_cast<VectorGroup>(inGroup->Children[0]) : nullptr;
        Check(inherited && inherited->Children.size() == 2, "marker-end inherits from a <g>");
        auto self = drawings(6);
        Check(self.size() == 1 && !self[0]->Children.empty() &&
              std::dynamic_pointer_cast<VectorLine>(self[0]->Children[0]) != nullptr,
              "a marker that uses itself is drawn once, not forever");
        Check(ChildAs<VectorLine>(ml, 7) != nullptr, "a line without markers stays a plain line");
        bool markerNote = false;
        for (const auto& n : notes) markerNote = markerNote || n.find("marker") != std::string::npos;
        Check(!markerNote, "no reader note about <marker>");
    }

    // ===== ARROWHEADS OUT AND BACK =====
    // The line gallery's arrowheads were not written at all, so an arrow
    // drawn in ArtCreator lost its heads when saved as SVG. They are written
    // as markers every SVG reader draws, and come back as arrowheads.
    {
        auto adoc = std::make_shared<VectorDocument>();
        adoc->Size = Size2Dd{400, 300};
        auto al = adoc->AddLayer("Arrows");
        auto arrowStroke = [](Color c, float width) {
            StrokeData st;
            st.Fill = c;
            st.Width = width;
            return st;
        };
        auto line = std::make_shared<VectorLine>();
        line->Start = Point2Dd(40, 150);
        line->End = Point2Dd(140, 150);
        line->Style.Stroke = arrowStroke(Color(255, 0, 0, 255), 2.0f);
        line->Style.Stroke->EndArrow = ArrowheadData{ArrowheadKind::Triangle, 3.0f};
        line->Style.Stroke->StartArrow = ArrowheadData{ArrowheadKind::OpenArrow, 1.0f};
        al->AddChild(line);
        auto curve = std::make_shared<VectorPath>();
        curve->Path = ParsePathString("M 40 220 C 80 200 120 240 160 220");
        curve->Style.Stroke = arrowStroke(Color(0, 0, 255, 255), 3.0f);
        curve->Style.Stroke->EndArrow = ArrowheadData{ArrowheadKind::StraightArrow, 1.0f};
        al->AddChild(curve);
        auto closed = std::make_shared<VectorPolygon>();
        closed->Points = {Point2Dd(250, 50), Point2Dd(300, 50), Point2Dd(275, 90)};
        closed->Style.Stroke = arrowStroke(Color(0, 0, 0, 255), 1.0f);
        closed->Style.Stroke->EndArrow = ArrowheadData{ArrowheadKind::Triangle, 1.0f};
        al->AddChild(closed);
        auto twin = std::make_shared<VectorLine>();
        twin->Start = Point2Dd(40, 100);
        twin->End = Point2Dd(140, 100);
        twin->Style.Stroke = arrowStroke(Color(255, 0, 0, 255), 2.0f);
        twin->Style.Stroke->EndArrow = ArrowheadData{ArrowheadKind::Triangle, 3.0f};
        al->AddChild(twin);

        const std::string out = converter.ExportToString(*adoc, options);
        auto count = [&](const std::string& what) {
            size_t n = 0;
            for (size_t at = out.find(what); at != std::string::npos; at = out.find(what, at + 1)) ++n;
            return n;
        };
        Check(out.find("marker-end=\"url(#") != std::string::npos &&
              out.find("marker-start=\"url(#") != std::string::npos &&
              out.find("data-ultracanvas-arrowhead=\"triangle\"") != std::string::npos,
              "arrowheads are written as marker-start / marker-end");
        Check(count("<marker ") == 3, "the same arrowhead in the same colour is written once");
        const size_t poly = out.find("<polygon");
        Check(poly != std::string::npos &&
              out.substr(poly, out.find("/>", poly) - poly).find("marker-") == std::string::npos,
              "a closed shape gets no markers (the renderer draws none on it)");

        auto back = converter.ImportFromString(out, options);
        auto bl = (back && !back->Layers.empty()) ? back->Layers[0] : nullptr;
        Check(bl && bl->Children.size() == 4, "arrowed shapes come back as four shapes, not groups");
        auto bline = ChildAs<VectorLine>(bl, 0);
        Check(bline && bline->Style.Stroke && bline->Style.Stroke->EndArrow.Kind == ArrowheadKind::Triangle &&
              std::fabs(bline->Style.Stroke->EndArrow.Scale - 3.0f) < 1e-4f &&
              bline->Style.Stroke->StartArrow.Kind == ArrowheadKind::OpenArrow,
              "the line's arrowheads come back as arrowheads, kind and size");
        auto bcurve = ChildAs<VectorPath>(bl, 1);
        Check(bcurve && bcurve->Style.Stroke &&
              bcurve->Style.Stroke->EndArrow.Kind == ArrowheadKind::StraightArrow &&
              !bcurve->Style.Stroke->StartArrow.IsSet(),
              "a Xara arrowhead on a curve comes back, and only at its end");

        // Another SVG reader draws them: the triangle's body below the line.
        std::vector<uint8_t> bytes(out.begin(), out.end());
        auto img = UCImage::LoadFromMemory(bytes);
        auto pm = img ? img->GetPixmap(400, 300, ImageFitMode::Contain, 1.0f) : nullptr;
        if (pm) {
            const uint32_t px = pm->GetPixelData()[153 * pm->GetRawWidth() + 124];
            Check(((px >> 16) & 0xFF) > 200 && ((px >> 8) & 0xFF) < 80,
                  "librsvg draws the end arrowhead from the marker");
        } else {
            Check(false, "the export with arrowheads rasterizes");
        }
    }

    // ===== WIDTH PROFILES AND BRUSHES OUT AND BACK =====
    // SVG has neither, and the writer wrote a plain constant-width stroke:
    // a tapered line came out as a uniform one and a brushed line as a bare
    // stroke. They are written as what the renderer draws, and the reader
    // rebuilds the stroke from the group's data.
    {
        auto gdoc = std::make_shared<VectorDocument>();
        gdoc->Size = Size2Dd{400, 300};
        auto gl = gdoc->AddLayer("Gallery");
        auto taper = std::make_shared<VectorPath>();
        taper->Path = ParsePathString("M 40 60 L 360 60");
        StrokeData taperStroke;
        taperStroke.Fill = Color(255, 0, 0, 255);
        taperStroke.Width = 6.0f;
        taperStroke.WidthProfile = {{0.0f, 0.2f}, {0.5f, 2.0f}, {1.0f, 0.2f}};
        taperStroke.EndArrow = ArrowheadData{ArrowheadKind::Triangle, 1.0f};
        taper->Style.Stroke = taperStroke;
        gl->AddChild(taper);
        auto brushed = std::make_shared<VectorLine>();
        brushed->Start = Point2Dd(40, 150);
        brushed->End = Point2Dd(360, 150);
        auto stamp = std::make_shared<VectorGroup>();
        auto dot = std::make_shared<VectorCircle>();
        dot->Center = Point2Dd(0, 0);
        dot->Radius = 5;
        dot->Style.Fill = Color(0, 0, 255, 255);
        stamp->AddChild(dot);
        StrokeData brushStroke;
        brushStroke.Fill = Color(0, 0, 255, 255);
        brushStroke.Width = 8.0f;
        BrushData brush;
        brush.Stamp = stamp;
        brush.Spacing = 1.5f;
        brushStroke.Brush = brush;
        brushed->Style.Stroke = brushStroke;
        gl->AddChild(brushed);
        auto ring = std::make_shared<VectorRect>();
        ring->Bounds = Rect2Dd{40, 220, 100, 50};
        ring->Style.Fill = Color(255, 255, 0, 255);
        StrokeData ringStroke;
        ringStroke.Width = 4.0f;
        ringStroke.WidthProfile = {{0.0f, 1.0f}, {1.0f, 3.0f}};
        ring->Style.Stroke = ringStroke;
        gl->AddChild(ring);

        const std::string out = converter.ExportToString(*gdoc, options);
        Check(out.find("data-ultracanvas-width-profile=\"0 0.2 0.5 2 1 0.2\"") != std::string::npos &&
              out.find("data-ultracanvas-brush=\"ucstamp") != std::string::npos &&
              out.find("<use href=\"#ucstamp") != std::string::npos,
              "a width profile and a brush are written with their data, the stamps as <use>s");
        Check(out.find("stroke=\"#ff0000\"") == std::string::npos && out.find("stroke=\"#0000ff\"") == std::string::npos,
              "no constant-width stroke is drawn in place of the profile or the brush");

        auto back = converter.ImportFromString(out, options);
        auto bl = (back && !back->Layers.empty()) ? back->Layers[0] : nullptr;
        Check(bl && bl->Children.size() == 3, "the three shapes come back as three shapes, not groups");
        auto btaper = ChildAs<VectorPath>(bl, 0);
        const StrokeData* ts = (btaper && btaper->Style.Stroke) ? &*btaper->Style.Stroke : nullptr;
        const Color* tc = ts ? std::get_if<Color>(&ts->Fill) : nullptr;
        Check(ts && ts->WidthProfile.size() == 3 && std::fabs(ts->WidthProfile[1].T - 0.5f) < 1e-4f &&
              std::fabs(ts->WidthProfile[1].Factor - 2.0f) < 1e-4f && std::fabs(ts->Width - 6.0f) < 1e-4f &&
              tc && tc->r == 255 && tc->b == 0 && ts->EndArrow.Kind == ArrowheadKind::Triangle,
              "the width profile comes back on the stroke, with its width, colour and arrowhead");
        auto bbrushed = ChildAs<VectorLine>(bl, 1);
        const StrokeData* bs = (bbrushed && bbrushed->Style.Stroke) ? &*bbrushed->Style.Stroke : nullptr;
        auto bdot = (bs && bs->HasBrush() && !bs->Brush->Stamp->Children.empty())
                ? std::dynamic_pointer_cast<VectorCircle>(bs->Brush->Stamp->Children[0]) : nullptr;
        Check(bs && bs->HasBrush() && std::fabs(bs->Brush->Spacing - 1.5f) < 1e-4f && bs->Brush->Rotate &&
              bdot && std::fabs(bdot->Radius - 5.0f) < 1e-4f,
              "the brush comes back with its spacing and its stamp");
        bool stampLeft = false;
        if (back) for (const auto& [id, def] : back->Definitions) stampLeft = stampLeft || id.rfind("ucstamp", 0) == 0;
        Check(!stampLeft, "the stamp moves into the brush, not into the document's definitions");
        auto bring = ChildAs<VectorRect>(bl, 2);
        const Color* rf = (bring && bring->Style.Fill) ? std::get_if<Color>(&*bring->Style.Fill) : nullptr;
        Check(bring && rf && rf->r == 255 && rf->g == 255 && bring->Style.Stroke &&
              bring->Style.Stroke->WidthProfile.size() == 2,
              "a closed shape keeps its fill and its profiled stroke");

        // Another SVG reader draws the band and the stamps.
        std::vector<uint8_t> bytes(out.begin(), out.end());
        auto img = UCImage::LoadFromMemory(bytes);
        auto pm = img ? img->GetPixmap(400, 300, ImageFitMode::Contain, 1.0f) : nullptr;
        if (pm) {
            auto at = [&](int x, int y) { return pm->GetPixelData()[y * pm->GetRawWidth() + x]; };
            const uint32_t wide = at(200, 65), stamped = at(40, 152), gap = at(46, 150);
            Check(((wide >> 16) & 0xFF) > 200 && ((wide >> 8) & 0xFF) < 80,
                  "librsvg draws the band twice the width at the middle");
            Check((stamped & 0xFF) > 200 && ((stamped >> 16) & 0xFF) < 80, "librsvg draws a stamp");
            Check((gap & 0xFF) < 128, "between the stamps there is no stroke");
        } else {
            Check(false, "the export with a profile and a brush rasterizes");
        }
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
