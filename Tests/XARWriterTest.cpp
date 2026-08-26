// Tests/XARWriterTest.cpp
// Round-trip test for the XAR writer in the Vector plugin: builds a
// VectorDocument covering the writer's feature matrix (shapes, paths with
// beziers and closes, groups with transforms, gradients, strokes, opacity,
// multi-span text), exports it through VectorConverter::XARConverter, then
// loads the result back through the XAR plugin's spec-verified XARDocument
// reader and asserts the structure survived: page size, node-type counts,
// coordinate placement (including the Y-axis flip to millipoints), resolved
// colours, and a parse with no unhandled records and no warnings.
//
// Usage: XARWriterTest [output.xar]
// The export is kept on disk (default: xar_writer_roundtrip.xar in the
// working directory) so it can be inspected with XARProbeTest --render or
// opened in Xara. Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasXARConverter.h"
#include "../UltraCanvas/Plugins/Vector/UltraCanvasVectorStorage.h"
#include "../UltraCanvas/Plugins/Vector/XAR/UltraCanvasXARPlugin.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
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

void CountNodes(const XARNodePtr& node, std::map<XARNodeType, int>& counts) {
    if (!node) return;
    counts[node->type]++;
    for (const auto& child : node->children) CountNodes(child, counts);
}

// Document-order search collecting all nodes of a type.
void Collect(const XARNodePtr& node, XARNodeType type, std::vector<XARNodePtr>& out) {
    if (!node) return;
    if (node->type == type) out.push_back(node);
    for (const auto& child : node->children) Collect(child, type, out);
}

std::shared_ptr<VectorDocument> BuildTestDocument() {
    auto doc = std::make_shared<VectorDocument>();
    doc->Size = Size2Dd{400, 300};
    doc->Title = "XAR writer round-trip";

    auto layer = doc->AddLayer("Artwork");

    // 1. Plain red rectangle with a blue stroke.
    auto rect = std::make_shared<VectorRect>();
    rect->Bounds = Rect2Dd{40, 40, 100, 60};
    rect->Style.Fill = Color(255, 0, 0, 255);
    StrokeData rectStroke;
    rectStroke.Fill = Color(0, 0, 255, 255);
    rectStroke.Width = 2.0f;
    rectStroke.LineJoin = StrokeLineJoin::Round;
    rect->Style.Stroke = rectStroke;
    layer->AddChild(rect);

    // 2. Rounded green rectangle, no stroke.
    auto rrect = std::make_shared<VectorRect>();
    rrect->Bounds = Rect2Dd{180, 40, 90, 60};
    rrect->RadiusX = 12;
    rrect->RadiusY = 12;
    rrect->Style.Fill = Color(0, 160, 0, 255);
    layer->AddChild(rrect);

    // 3. Circle with 50% opacity.
    auto circle = std::make_shared<VectorCircle>();
    circle->Center = Point2Dd(330, 70);
    circle->Radius = 30;
    circle->Style.Fill = Color(255, 128, 0, 255);
    circle->Style.Opacity = 0.5f;
    layer->AddChild(circle);

    // 4. Ellipse with a linear gradient fill.
    auto ellipse = std::make_shared<VectorEllipse>();
    ellipse->Center = Point2Dd(90, 160);
    ellipse->RadiusX = 50;
    ellipse->RadiusY = 30;
    LinearGradientData grad;
    grad.Start = Point2Dd(40, 160);
    grad.End = Point2Dd(140, 160);
    grad.Stops.push_back(GradientStop(0.0, Color(255, 255, 0, 255)));
    grad.Stops.push_back(GradientStop(1.0, Color(255, 0, 255, 255)));
    ellipse->Style.Fill = GradientData(grad);
    layer->AddChild(ellipse);

    // 5. Closed bezier path (a leaf shape), filled and stroked.
    auto path = std::make_shared<VectorPath>();
    path->MoveTo(200, 140);
    path->CurveTo(240, 120, 280, 120, 300, 160);
    path->CurveTo(280, 200, 240, 200, 200, 160);
    path->ClosePath();
    path->Style.Fill = Color(90, 40, 160, 255);
    StrokeData pathStroke;
    pathStroke.Fill = Color(30, 30, 30, 255);
    pathStroke.Width = 1.5f;
    pathStroke.LineCap = StrokeLineCap::Round;
    path->Style.Stroke = pathStroke;
    layer->AddChild(path);

    // 6. Group with a rotation: forces the writer's shape->path fallback.
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
    s2.Text = "XAR\nround trip";
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
    std::string outPath = argc > 1 ? argv[1] : "xar_writer_roundtrip.xar";

    auto doc = BuildTestDocument();

    VectorConverter::XARConverter converter;
    std::printf("Exporting to %s ...\n", outPath.c_str());
    Check(converter.Export(*doc, outPath), "Export() returns true");
    Check(converter.ValidateFile(outPath), "exported file carries the XAR signature");

    XARDocument reader;
    Check(reader.LoadFromFile(outPath), "XARDocument loads the exported file");
    if (failures) return failures;

    const auto& diag = reader.GetDiagnostics();
    Check(diag.recordCount > 20, "a plausible number of records was dispatched");
    Check(diag.warnings.empty(), "reader reports no parse warnings");
    if (!diag.warnings.empty()) {
        for (const auto& w : diag.warnings) std::printf("      warning: %s\n", w.c_str());
    }
    Check(diag.unhandledTags.empty(), "reader reports no unhandled record tags");
    for (const auto& [tag, count] : diag.unhandledTags) {
        std::printf("      unhandled tag %u x%zu\n", tag, count);
    }

    Check(reader.GetProducer() == "UltraCanvas", "producer string round-trips");
    Check(std::fabs(reader.GetWidth() - 400.0f) < 0.5f, "page width is 400");
    Check(std::fabs(reader.GetHeight() - 300.0f) < 0.5f, "page height is 300");

    std::map<XARNodeType, int> counts;
    CountNodes(reader.GetRoot(), counts);
    Check(counts[XARNodeType::Layer] == 1, "one layer");
    Check(counts[XARNodeType::Rectangle] == 2, "two rectangle records (plain + rounded)");
    Check(counts[XARNodeType::Ellipse] == 2, "two ellipse records (circle + ellipse)");
    Check(counts[XARNodeType::Path] == 2, "two path records (bezier leaf + rotated rect)");
    Check(counts[XARNodeType::Group] == 1, "one group");
    Check(counts[XARNodeType::TextStory] == 1, "one text story");
    Check(counts[XARNodeType::TextLine] == 2, "two text lines");
    Check(counts[XARNodeType::TextString] >= 3, "at least three text strings (span splits)");

    // Coordinate check: rect 1 centre in millipoints with the Y-axis flip.
    // Bounds (40,40,100,60) on a 300pt page -> centre (90, 70)pt ->
    // (90000, (300-70)*1000) = (90000, 230000).
    std::vector<XARNodePtr> rects;
    Collect(reader.GetRoot(), XARNodeType::Rectangle, rects);
    if (rects.size() == 2) {
        auto r0 = std::static_pointer_cast<XARRectangleNode>(rects[0]);
        Check(r0->centre.x == 90000 && r0->centre.y == 230000,
              "rect centre lands at (90000, 230000) millipoints");
        Check(r0->majorAxis.x == 50000 && r0->minorAxis.y == 30000,
              "rect half-extents are (50000, 30000) millipoints");
        Check(r0->hasFill && r0->fill.startColor.r == 255 &&
              r0->fill.startColor.g == 0 && r0->fill.startColor.b == 0,
              "rect fill resolves to red through its colour reference");
        Check(r0->hasLine && r0->line.width == 2000,
              "rect stroke width is 2000 millipoints");
        auto r1 = std::static_pointer_cast<XARRectangleNode>(rects[1]);
        Check(r1->isRounded && r1->cornerRadius == 12000,
              "rounded rect keeps its 12000 millipoint corner radius");
    }

    std::vector<XARNodePtr> ellipses;
    Collect(reader.GetRoot(), XARNodeType::Ellipse, ellipses);
    if (ellipses.size() == 2) {
        auto c = std::static_pointer_cast<XAREllipseNode>(ellipses[0]);
        Check(c->centre.x == 330000 && c->centre.y == 230000,
              "circle centre lands at (330000, 230000) millipoints");
        Check(c->hasTransparency, "circle opacity became a transparency record");
        auto e = std::static_pointer_cast<XAREllipseNode>(ellipses[1]);
        Check(e->hasFill && e->fill.type == XARFillType::LinearGradient,
              "ellipse gradient survives as a linear fill");
        Check(e->fill.startColor.r == 255 && e->fill.startColor.g == 255 &&
              e->fill.startColor.b == 0,
              "gradient start colour resolves to yellow");
    }

    std::vector<XARNodePtr> paths;
    Collect(reader.GetRoot(), XARNodeType::Path, paths);
    if (paths.size() == 2) {
        auto p = std::static_pointer_cast<XARPathNode>(paths[0]);
        Check(p->isFilled && p->isStroked, "bezier path is filled and stroked");
        bool sawBezier = false, sawClose = false;
        for (const auto& cmd : p->commands) {
            if (cmd.verb == XARPathVerb::BezierTo) sawBezier = true;
            if (cmd.verb == XARPathVerb::ClosePath) sawClose = true;
        }
        Check(sawBezier && sawClose, "bezier path keeps its curves and close");
    }

    std::vector<XARNodePtr> stories;
    Collect(reader.GetRoot(), XARNodeType::TextStory, stories);
    if (!stories.empty()) {
        auto st = std::static_pointer_cast<XARTextStoryNode>(stories[0]);
        Check(st->position.x == 40000 && st->position.y == 50000,
              "text anchor lands at (40000, 50000) millipoints");
        std::vector<XARNodePtr> strings;
        Collect(st, XARNodeType::TextString, strings);
        bool sawBold = false;
        std::string joined;
        for (const auto& sn : strings) {
            auto ts = std::static_pointer_cast<XARTextStringNode>(sn);
            joined += ts->text;
            if (ts->textAttr.bold) sawBold = true;
        }
        Check(joined == "Hello XARround trip", "text content round-trips");
        Check(sawBold, "the bold span keeps its weight");
        Check(std::static_pointer_cast<XARTextStringNode>(strings.front())
                      ->textAttr.fontSize == 18000,
              "font size is 18000 millipoints");
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
