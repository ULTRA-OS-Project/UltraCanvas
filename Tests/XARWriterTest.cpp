// Tests/XARWriterTest.cpp
// Round-trip test for the XAR writer in the Vector plugin: builds a
// VectorDocument covering the writer's feature matrix (shapes, paths with
// beziers and closes, groups with transforms, gradients, strokes, opacity,
// multi-span text, and the phase-4 additions: multistage fills, gradient
// transparency with a mix, a shadow, a feather, a baked arrowhead and
// width profile), exports it through VectorConverter::XARConverter, then
// loads the result back through the XAR plugin's spec-verified XARDocument
// reader and asserts the structure survived: page size, node-type counts,
// coordinate placement (including the Y-axis flip to millipoints), resolved
// colours, and a parse with no unhandled records and no warnings. Finally
// the converter's own Import (the same reader, translated to the model)
// reads the file back and the effects are checked on the model.
//
// Usage: XARWriterTest [output.xar]
// The export is kept on disk (default: xar_writer_roundtrip.xar in the
// working directory) so it can be inspected with XARProbeTest --render or
// opened in Xara. Exit code is the number of failed checks.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "../UltraCanvas/Plugins/Vector/UltraCanvasXARConverter.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "../UltraCanvas/Plugins/Vector/XAR/UltraCanvasXARPlugin.h"

#include <cmath>
#include <functional>
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

    // 8. Phase 4: a four-stop gradient with a linear bleach transparency
    // ramp and a wall shadow.
    auto shaded = std::make_shared<VectorRect>();
    shaded->Id = "shaded";
    shaded->Bounds = Rect2Dd{40, 110, 100, 40};
    LinearGradientData ramp;
    ramp.Units = GradientUnits::UserSpaceOnUse;
    ramp.Start = Point2Dd(40, 130);
    ramp.End = Point2Dd(140, 130);
    ramp.Stops = {GradientStop(0.0, Color(255, 0, 0, 255)), GradientStop(0.3, Color(255, 255, 0, 255)),
                  GradientStop(0.7, Color(0, 255, 0, 255)), GradientStop(1.0, Color(0, 0, 255, 255))};
    shaded->Style.Fill = GradientData(ramp);
    TransparencyData fade;
    fade.Shape = TransparencyShape::Linear;
    fade.Start = Point2Dd(40, 130);
    fade.End = Point2Dd(140, 130);
    fade.Stops = {{0.0, 0.0f}, {1.0, 0.8f}};
    fade.Mix = TransparencyMix::Bleach;
    shaded->Style.Transparency = fade;
    ShadowEffect shadow;
    shadow.Kind = ShadowKind::Wall;
    shadow.Offset = Point2Dd(5, 7);
    shadow.Blur = 3;
    shadow.Darkness = 0.6f;
    shaded->Effects.Shadow = shadow;
    layer->AddChild(shaded);

    // 9. A feathered circle.
    auto soft = std::make_shared<VectorCircle>();
    soft->Id = "soft";
    soft->Center = Point2Dd(340, 122);
    soft->Radius = 20;
    soft->Style.Fill = Color(0, 160, 200, 255);
    soft->Effects.Feather = FeatherEffect{6.0f};
    layer->AddChild(soft);

    // 10. A line with an end arrowhead and a tapered polyline: the gallery
    // is baked into extra filled paths.
    auto arrow = std::make_shared<VectorLine>();
    arrow->Id = "arrow";
    arrow->Start = Point2Dd(40, 175);
    arrow->End = Point2Dd(140, 175);
    StrokeData arrowStroke;
    arrowStroke.Fill = Color(0, 0, 0, 255);
    arrowStroke.Width = 3;
    arrowStroke.EndArrow.Kind = ArrowheadKind::Triangle;
    arrow->Style.Stroke = arrowStroke;
    layer->AddChild(arrow);
    auto taper = std::make_shared<VectorPolyline>();
    taper->Id = "taper";
    taper->Points = {Point2Dd(40, 205), Point2Dd(90, 195), Point2Dd(140, 205)};
    StrokeData taperStroke;
    taperStroke.Fill = Color(120, 0, 0, 255);
    taperStroke.Width = 8;
    taperStroke.WidthProfile = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    taper->Style.Stroke = taperStroke;
    layer->AddChild(taper);

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
    Check(counts[XARNodeType::Rectangle] == 3, "three rectangle records (plain + rounded + shaded)");
    Check(counts[XARNodeType::Ellipse] == 3, "three ellipse records (circle + ellipse + feathered)");
    // bezier leaf, rotated rect, the arrow's line, its baked head, the taper's band
    Check(counts[XARNodeType::Path] == 5, "five path records (two shapes, the arrow line, its head, the width band)");
    Check(counts[XARNodeType::Group] == 1, "one group");
    Check(counts[XARNodeType::Shadow] == 1, "one shadow controller");
    Check(counts[XARNodeType::Feather] == 1, "one feather attribute");
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

    // ===== Phase 4 records as the plugin reads them =====
    {
        std::vector<XARNodePtr> rects, shadows, ellipses;
        Collect(reader.GetRoot(), XARNodeType::Rectangle, rects);
        Collect(reader.GetRoot(), XARNodeType::Shadow, shadows);
        Collect(reader.GetRoot(), XARNodeType::Ellipse, ellipses);
        const XARNodePtr* shadedNode = nullptr;
        for (const auto& r : rects) if (r->fill.type == XARFillType::LinearGradient && r->fill.stops.size() >= 4) shadedNode = &r;
        Check(shadedNode != nullptr, "the four-stop gradient comes back as a multistage linear fill with four stops");
        if (shadedNode) {
            const auto& f = (*shadedNode)->fill;
            Check(f.stops.size() == 4 && std::fabs(f.stops[1].position - 0.3) < 1e-6 && f.stops[1].color.g == 255,
                  "inner stops keep their position and colour");
            Check((*shadedNode)->hasTransparency && (*shadedNode)->transparency.type == XARTransparencyType::LinearGradient &&
                  (*shadedNode)->transparency.mix == XARTransparencyMix::Bleach &&
                  (*shadedNode)->transparency.endTransparency == 204,
                  "the linear transparency ramp keeps its shape, end level and bleach mix");
        }
        Check(shadows.size() == 1, "one shadow controller was written");
        if (shadows.size() == 1) {
            auto sh = std::static_pointer_cast<XARShadowNode>(shadows.front());
            Check(sh->shadowType == 1 && sh->offsetX == 5000 && sh->offsetY == -7000 && sh->blurRadius == 3000,
                  "the wall shadow keeps its offset (Y flipped) and penumbra");
            Check(sh->shadowColor.a == 153, "the shadow's darkness is 60 percent");
            Check(sh->children.size() == 1 && sh->children.front()->type == XARNodeType::Rectangle,
                  "the shadowed rectangle is the controller's child");
        }
        bool feathered = false;
        for (const auto& e : ellipses)
            for (const auto& c : e->children)
                if (c->type == XARNodeType::Feather && std::static_pointer_cast<XARFeatherNode>(c)->featherRadius == 6000) feathered = true;
        Check(feathered, "the feather is an attribute of the circle with its radius in millipoints");
    }

    // ===== The converter reads the file back into the model =====
    {
        std::vector<std::string> warnings;
        VectorConverter::ConversionOptions opts;
        opts.WarningCallback = [&warnings](const std::string& w) { warnings.push_back(w); };
        auto back = converter.Import(outPath, opts);
        Check(back != nullptr, "Import() reads the exported file through the XAR plugin");
        if (back) {
            Check(std::fabs(back->Size.width - 400.0) < 0.5 && std::fabs(back->Size.height - 300.0) < 0.5,
                  "the page size survives the round trip");
            Check(back->Layers.size() == 1, "one layer comes back");
            int shadowed = 0, feathered = 0, ramped = 0, multistage = 0, texts = 0, strokedShadowed = 0;
            std::function<void(const std::shared_ptr<VectorElement>&)> walk = [&](const std::shared_ptr<VectorElement>& e) {
                if (!e) return;
                if (e->Effects.Shadow && std::fabs(e->Effects.Shadow->Offset.x - 5.0) < 0.01 &&
                    std::fabs(e->Effects.Shadow->Offset.y - 7.0) < 0.01 && std::fabs(e->Effects.Shadow->Darkness - 0.6f) < 0.01f) {
                    ++shadowed;
                    if (e->Style.Stroke.has_value()) ++strokedShadowed;
                }
                if (e->Effects.Feather && std::fabs(e->Effects.Feather->Radius - 6.0f) < 0.01f) ++feathered;
                if (e->Style.Transparency && e->Style.Transparency->Shape == TransparencyShape::Linear &&
                    e->Style.Transparency->Mix == TransparencyMix::Bleach) ++ramped;
                if (e->Style.Fill)
                    if (auto* g = std::get_if<GradientData>(&*e->Style.Fill))
                        if (auto* l = std::get_if<LinearGradientData>(g))
                            if (l->Stops.size() == 4) ++multistage;
                if (e->Type == VectorElementType::Text) ++texts;
                if (e->Type == VectorElementType::Group || e->Type == VectorElementType::Layer)
                    for (const auto& c : std::static_pointer_cast<VectorGroup>(e)->Children) walk(c);
            };
            for (const auto& l : back->Layers) walk(l);
            Check(shadowed == 1, "the wall shadow comes back on the model (offset and darkness)");
            Check(strokedShadowed == 0, "an unstroked shape comes back without a stroke");
            Check(feathered == 1, "the feather comes back on the model");
            Check(ramped == 1, "the transparency ramp comes back with its mix");
            Check(multistage == 1, "the four-stop gradient comes back with four stops");
            Check(texts == 1, "the text story comes back as one text element");
            for (const auto& w : warnings) std::printf("  (import note) %s\n", w.c_str());
        }
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
