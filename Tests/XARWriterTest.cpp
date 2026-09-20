// Tests/XARWriterTest.cpp
// Round-trip test for the XAR writer in the Vector plugin: builds a
// VectorDocument covering the writer's feature matrix (shapes, paths with
// beziers and closes, groups with transforms, gradients, strokes, opacity,
// multi-span text, and the phase-4 additions: multistage fills, gradient
// transparency with a mix, a shadow, a feather, native and baked
// arrowheads, a width profile and a brush), exports it through VectorConverter::XARConverter, then
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
// Version: 1.1.0
// Last Modified: 2026-09-18
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
    shaded->Bounds = Rect2Dd{240, 222, 110, 34};
    LinearGradientData ramp;
    ramp.Units = GradientUnits::UserSpaceOnUse;
    ramp.Start = Point2Dd(240, 239);
    ramp.End = Point2Dd(350, 239);
    ramp.Stops = {GradientStop(0.0, Color(255, 0, 0, 255)), GradientStop(0.3, Color(255, 255, 0, 255)),
                  GradientStop(0.7, Color(0, 255, 0, 255)), GradientStop(1.0, Color(0, 0, 255, 255))};
    shaded->Style.Fill = GradientData(ramp);
    TransparencyData fade;
    fade.Shape = TransparencyShape::Linear;
    fade.Start = Point2Dd(240, 239);
    fade.End = Point2Dd(350, 239);
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

    // 10. A line with Xara's own arrowheads at both ends (a spot at the
    // start, the straight arrow at the end: line attributes, no baking) and
    // a tapered polyline, whose width band is baked into a filled path.
    auto arrow = std::make_shared<VectorLine>();
    arrow->Id = "arrow";
    arrow->Start = Point2Dd(40, 205);
    arrow->End = Point2Dd(140, 205);
    StrokeData arrowStroke;
    arrowStroke.Fill = Color(0, 0, 0, 255);
    arrowStroke.Width = 3;
    arrowStroke.StartArrow.Kind = ArrowheadKind::Spot;
    arrowStroke.EndArrow.Kind = ArrowheadKind::StraightArrow;
    arrow->Style.Stroke = arrowStroke;
    layer->AddChild(arrow);
    auto taper = std::make_shared<VectorPolyline>();
    taper->Id = "taper";
    taper->Points = {Point2Dd(40, 224), Point2Dd(90, 216), Point2Dd(140, 224)};
    StrokeData taperStroke;
    taperStroke.Fill = Color(120, 0, 0, 255);
    taperStroke.Width = 8;
    taperStroke.WidthProfile = {{0.0f, 1.0f}, {1.0f, 0.0f}};
    taper->Style.Stroke = taperStroke;
    layer->AddChild(taper);

    // 11. A line with a bar tail (not one of Xara's own arrowheads, so it
    // is baked) and a doubled straight head (Xara's, written as the line
    // attribute with its scale).
    auto barred = std::make_shared<VectorLine>();
    barred->Id = "barred";
    barred->Start = Point2Dd(40, 240);
    barred->End = Point2Dd(140, 240);
    StrokeData barredStroke;
    barredStroke.Fill = Color(0, 0, 120, 255);
    barredStroke.Width = 2;
    barredStroke.StartArrow.Kind = ArrowheadKind::Bar;
    barredStroke.EndArrow.Kind = ArrowheadKind::StraightArrow;
    barredStroke.EndArrow.Scale = 2.0f;
    barred->Style.Stroke = barredStroke;
    layer->AddChild(barred);

    // 12. A brushed line: a one-dot stamp repeated along the line, saved as
    // plain stamped shapes plus the marker the reader rebuilds the brush from.
    auto stamp = std::make_shared<VectorGroup>();
    auto dot = std::make_shared<VectorEllipse>();
    dot->Center = Point2Dd(0, 0);
    dot->RadiusX = 1;
    dot->RadiusY = 1;
    dot->Style.Fill = Color(0, 120, 0, 255);
    stamp->AddChild(dot);
    auto brushed = std::make_shared<VectorLine>();
    brushed->Id = "brushed";
    brushed->Start = Point2Dd(40, 260);
    brushed->End = Point2Dd(140, 260);
    StrokeData brushStroke;
    brushStroke.Fill = Color(0, 120, 0, 255);
    brushStroke.Width = 6;
    brushStroke.Brush = BrushData{stamp, 1.6f, 1.0f, true};
    brushed->Style.Stroke = brushStroke;
    layer->AddChild(brushed);

    // ----- phase 5: the depth containers and effects -----
    // 13. A ClipView: a circle keyhole over two stripes.
    auto clip = std::make_shared<VectorClipView>();
    clip->Id = "clip";
    auto keyhole = std::make_shared<VectorCircle>();
    keyhole->Center = Point2Dd(240, 270);
    keyhole->Radius = 20;
    keyhole->Style.Fill = Color(0, 0, 0, 255);
    clip->AddChild(keyhole);
    for (int i = 0; i < 2; ++i) {
        auto stripe = std::make_shared<VectorRect>();
        stripe->Bounds = Rect2Dd(220 + i * 20, 250, 10, 40);
        stripe->Style.Fill = Color(0, 100, 200, 255);
        clip->AddChild(stripe);
    }
    layer->AddChild(clip);

    // 14. A contoured rectangle: three outward steps to yellow.
    auto contoured = std::make_shared<VectorRect>();
    contoured->Id = "contoured";
    contoured->Bounds = Rect2Dd(300, 250, 40, 30);
    contoured->Style.Fill = Color(200, 0, 0, 255);
    ContourEffect contour;
    contour.Steps = 3;
    contour.Width = 12;
    contour.Colour = Color(255, 220, 0, 255);
    contour.Blend = ColourBlendKind::Rainbow;
    contoured->Effects.Contour = contour;
    layer->AddChild(contoured);

    // 15. A blend of two circles in four steps.
    auto blend = std::make_shared<VectorBlend>();
    blend->Id = "blend";
    blend->Steps = 4;
    blend->ColourEffect = ColourBlendKind::AltRainbow;
    auto ba = std::make_shared<VectorCircle>();
    ba->Center = Point2Dd(30, 275); ba->Radius = 8; ba->Style.Fill = Color(255, 0, 0, 255);
    auto bb = std::make_shared<VectorCircle>();
    bb->Center = Point2Dd(120, 275); bb->Radius = 12; bb->Style.Fill = Color(0, 0, 255, 255);
    blend->AddChild(ba);
    blend->AddChild(bb);
    layer->AddChild(blend);

    // 16. A perspective mould over a square.
    auto mould = std::make_shared<VectorMould>();
    mould->Id = "mould";
    mould->Kind = MouldKind::Perspective;
    mould->Threshold = 64;
    auto moulded = std::make_shared<VectorRect>();
    moulded->Bounds = Rect2Dd(0, 0, 40, 40);
    moulded->Style.Fill = Color(0, 160, 0, 255);
    mould->AddChild(moulded);
    {
        PathData shape;
        auto cmd = [&](PathCommandType t, std::initializer_list<float> v) { PathCommand c; c.Type = t; c.Parameters = v; shape.commands.push_back(c); };
        cmd(PathCommandType::MoveTo, {150, 250});
        cmd(PathCommandType::LineTo, {190, 250});
        cmd(PathCommandType::LineTo, {200, 290});
        cmd(PathCommandType::LineTo, {140, 290});
        cmd(PathCommandType::ClosePath, {});
        shape.Closed = true;
        mould->Shape = shape;
    }
    layer->AddChild(mould);

    // 17. A bevelled rectangle.
    auto bevelled = std::make_shared<VectorRect>();
    bevelled->Id = "bevelled";
    bevelled->Bounds = Rect2Dd(350, 250, 40, 30);
    bevelled->Style.Fill = Color(120, 120, 200, 255);
    BevelEffect bevel;
    bevel.Kind = BevelKind::Round;
    bevel.Indent = 6;
    bevel.LightAngle = 120;
    bevel.Contrast = 0.7f;
    bevel.Tilt = 40;
    bevelled->Effects.Bevel = bevel;
    layer->AddChild(bevelled);

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
    // plain + rounded + shaded, the two clipped stripes, the contoured and
    // bevelled ones, the mould's source
    Check(counts[XARNodeType::Rectangle] == 8, "eight rectangle records");
    // circle + ellipse + feathered, the keyhole and the two blended circles,
    // plus the brush's stamped copies of its dot
    Check(counts[XARNodeType::Ellipse] >= 15 && counts[XARNodeType::Ellipse] <= 18,
          "six ellipse records plus the brush's stamped dots");
    // bezier leaf, rotated rect, the arrow's line, the taper's polyline and
    // its band, the barred line and its baked bar, the brushed line
    // ... plus the moulded result
    Check(counts[XARNodeType::Path] == 9, "nine path records (two shapes, three lines, the width band, the bar, the taper, the moulded square)");
    // the rotated one, one marker group each for the taper, the bar and the
    // brush, the brush's stamps group and one group per stamped copy
    Check(counts[XARNodeType::Group] >= 14 && counts[XARNodeType::Group] <= 17,
          "the rotated group, three marker groups, the stamps group and one group per stamp");
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
    if (paths.size() >= 2) {
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

        // Line gallery records: Xara's own arrowhead is a line attribute, the
        // rest is baked under a group carrying the marker user value.
        int nativeEnds = 0, scaledEnds = 0, spotStarts = 0;
        for (const auto& p : paths) {
            auto pn = std::static_pointer_cast<XARPathNode>(p);
            if (!pn->hasLine) continue;
            if (pn->line.endArrowRef == -2) {
                ++nativeEnds;
                if (std::fabs(pn->line.endArrowWidthScale - 6.0f) < 0.01f &&
                    std::fabs(pn->line.endArrowHeightScale - 6.0f) < 0.01f) ++scaledEnds;
            }
            if (pn->line.startArrowRef == -5 && std::fabs(pn->line.startArrowWidthScale - 3.0f) < 0.01f) ++spotStarts;
        }
        Check(nativeEnds == 2, "the two straight heads are ARROWTAIL line attributes (Xara ref -2)");
        Check(scaledEnds == 1, "the doubled head's FIXED16 scales are 6 (twice Xara's default 3)");
        Check(spotStarts == 1, "the spot is an ARROWHEAD attribute (ref -5) at Xara's default size");
        std::vector<XARNodePtr> groups;
        Collect(reader.GetRoot(), XARNodeType::Group, groups);
        int markers = 0, brushMarkers = 0, stampCopies = 0;
        for (const auto& g : groups) {
            auto m = g->userValues.find("UltraCanvas.LineGallery");
            if (m == g->userValues.end()) continue;
            ++markers;
            if (m->second.find("brush=1") != std::string::npos) {
                ++brushMarkers;
                // last child: the group of stamped copies
                if (!g->children.empty()) {
                    std::vector<XARNodePtr> dots;
                    Collect(g->children.back(), XARNodeType::Ellipse, dots);
                    stampCopies = static_cast<int>(dots.size());
                }
            }
        }
        Check(markers == 3, "three line-gallery marker groups (taper, bar, brush)");
        Check(brushMarkers == 1, "one of them is a brush");
        // 100pt line, stamp height 6pt, spacing 1.6 stamp widths -> ~11 copies
        Check(stampCopies >= 9 && stampCopies <= 12, "the brush is written as stamped ellipse copies");
        std::printf("      (stamp copies: %d)\n", stampCopies);

        // Phase 5 controllers, as Xara's tree has them.
        std::vector<XARNodePtr> clips, contours, blends, moulds, bevels;
        Collect(reader.GetRoot(), XARNodeType::ClipView, clips);
        Collect(reader.GetRoot(), XARNodeType::Contour, contours);
        Collect(reader.GetRoot(), XARNodeType::Blend, blends);
        Collect(reader.GetRoot(), XARNodeType::Mould, moulds);
        Collect(reader.GetRoot(), XARNodeType::Bevel, bevels);
        Check(clips.size() == 1 && clips[0]->children.size() == 4 && clips[0]->children[1]->type == XARNodeType::ClipViewMarker,
              "the ClipView controller holds the keyhole, the marker and the two stripes");
        Check(contours.size() == 1, "one contour controller");
        if (contours.size() == 1) {
            const auto& c = static_cast<const XARContourNode&>(*contours[0]);
            Check(c.steps == 3 && c.width == -12000 && c.colourBlend == 1 && !c.insetPath,
                  "the contour controller carries 3 steps, an outer width of 12000 mp and the rainbow blend");
            bool stepsNode = false;
            for (const auto& ch : c.children)
                if (ch->type == XARNodeType::ContourSteps && ch->hasFill && ch->fill.startColor.r == 255 && ch->fill.startColor.g == 220) stepsNode = true;
            Check(stepsNode, "the contour node carries the contour colour as its fill");
        }
        Check(blends.size() == 1, "one blend");
        if (blends.size() == 1) {
            const auto& b = static_cast<const XARBlendNode&>(*blends[0]);
            int blenders = 0, shapes = 0;
            for (const auto& ch : b.children) { if (ch->type == XARNodeType::Blender) ++blenders; else ++shapes; }
            Check(b.numSteps == 4 && b.colourEffect == 2 && b.antialiased, "the blend record carries 4 steps and the alt-rainbow effect");
            Check(blenders == 1 && shapes == 2, "a blender sits between the two blended shapes");
        }
        Check(moulds.size() == 1, "one mould");
        if (moulds.size() == 1) {
            const auto& m = static_cast<const XARMouldNode&>(*moulds[0]);
            int paths = 0, groups = 0, results = 0;
            bool bounds = false;
            for (const auto& ch : m.children) {
                if (ch->type == XARNodeType::MouldPath) ++paths;
                else if (ch->type == XARNodeType::MouldGroup) { ++groups; bounds = static_cast<const XARMouldGroupNode&>(*ch).hasBounds; }
                else ++results;
            }
            Check(m.isPerspective && m.threshold == 64, "the mould is a perspective with its threshold");
            Check(paths == 1 && groups == 1 && bounds && results == 1, "the mould holds its shape, the bounded source group and the moulded result");
        }
        Check(bevels.size() == 1, "one bevel controller");
        if (bevels.size() == 1) {
            const auto& b = static_cast<const XARBevelNode&>(*bevels[0]);
            Check(b.bevelType == 1 && b.indent == 6000 && b.lightAngle == 120 && !b.outer && b.contrast == 70 && b.tilt == 40,
                  "the bevel record carries the round profile, 6000 mp indent, light 120, contrast 70, tilt 40");
            bool ink = false;
            for (const auto& ch : b.children) if (ch->type == XARNodeType::BevelInk) ink = true;
            Check(ink, "the bevel node is under its controller");
        }
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
            int tapers = 0, brushes = 0, barred = 0, plainArrows = 0, groups = 0;
            int clipViews = 0, contoured = 0, blends = 0, moulds = 0, bevelled = 0;
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
                if (e->Style.Stroke) {
                    const auto& st = *e->Style.Stroke;
                    if (st.HasWidthProfile() && st.WidthProfile.size() == 2 &&
                        std::fabs(st.WidthProfile[1].Factor) < 0.01f && std::fabs(st.Width - 8.0f) < 0.01f) ++tapers;
                    if (st.HasBrush() && std::fabs(st.Brush->Spacing - 1.6f) < 0.01f &&
                        st.Brush->Stamp->Children.size() == 1 &&
                        (st.Brush->Stamp->Children.front()->Type == VectorElementType::Ellipse ||
                         st.Brush->Stamp->Children.front()->Type == VectorElementType::Circle)) ++brushes;
                    if (st.StartArrow.Kind == ArrowheadKind::Bar && st.EndArrow.Kind == ArrowheadKind::StraightArrow &&
                        std::fabs(st.EndArrow.Scale - 2.0f) < 0.01f) ++barred;
                    if (st.StartArrow.Kind == ArrowheadKind::Spot && st.EndArrow.Kind == ArrowheadKind::StraightArrow &&
                        std::fabs(st.StartArrow.Scale - 1.0f) < 0.01f && std::fabs(st.EndArrow.Scale - 1.0f) < 0.01f) ++plainArrows;
                }
                if (e->Type == VectorElementType::Group) ++groups;
                if (e->Type == VectorElementType::ClipView) {
                    auto cv = std::static_pointer_cast<VectorClipView>(e);
                    if (cv->Keyholes == 1 && cv->Children.size() == 3 && cv->Children[0]->Type == VectorElementType::Circle) ++clipViews;
                }
                if (e->Effects.Contour && e->Effects.Contour->Steps == 3 && std::fabs(e->Effects.Contour->Width - 12.0f) < 0.01f &&
                    e->Effects.Contour->Blend == ColourBlendKind::Rainbow && e->Effects.Contour->Colour.g == 220) ++contoured;
                if (e->Type == VectorElementType::Blend) {
                    auto b = std::static_pointer_cast<VectorBlend>(e);
                    if (b->Steps == 4 && b->ColourEffect == ColourBlendKind::AltRainbow && b->Children.size() == 2) ++blends;
                }
                if (e->Type == VectorElementType::Mould) {
                    auto m = std::static_pointer_cast<VectorMould>(e);
                    Point2Dd corners[4];
                    if (m->Kind == MouldKind::Perspective && m->Children.size() == 1 && m->ShapeCorners(corners) &&
                        std::fabs(corners[0].x - 150) < 0.01 && std::fabs(corners[0].y - 250) < 0.01 &&
                        std::fabs(corners[2].x - 200) < 0.01 && std::fabs(corners[2].y - 290) < 0.01 &&
                        std::fabs(m->SourceBounds.width - 40) < 0.01) ++moulds;
                }
                if (e->Effects.Bevel && e->Effects.Bevel->Kind == BevelKind::Round && std::fabs(e->Effects.Bevel->Indent - 6.0f) < 0.01f &&
                    std::fabs(e->Effects.Bevel->LightAngle - 120.0f) < 0.01f && std::fabs(e->Effects.Bevel->Contrast - 0.7f) < 0.01f) ++bevelled;
                if (IsGroupType(e->Type))
                    for (const auto& c : std::static_pointer_cast<VectorGroup>(e)->Children) walk(c);
            };
            for (const auto& l : back->Layers) walk(l);
            Check(shadowed == 1, "the wall shadow comes back on the model (offset and darkness)");
            Check(strokedShadowed == 0, "an unstroked shape comes back without a stroke");
            Check(feathered == 1, "the feather comes back on the model");
            Check(ramped == 1, "the transparency ramp comes back with its mix");
            Check(multistage == 1, "the four-stop gradient comes back with four stops");
            Check(texts == 1, "the text story comes back as one text element");
            Check(plainArrows == 1, "Xara's spot and straight arrowheads read back as those kinds at Scale 1");
            Check(barred == 1, "the baked bar tail and the doubled head read back on the stroke");
            Check(tapers == 1, "the width profile reads back as a stroke, not as the baked band");
            Check(brushes == 1, "the brush reads back with its spacing and one-dot stamp");
            Check(groups == 1, "only the rotated group stays a group (the marker groups unwrap)");
            Check(clipViews == 1, "the ClipView comes back with its circle keyhole and two stripes");
            Check(contoured == 1, "the contour comes back as an effect: steps, outward width, blend and colour");
            Check(blends == 1, "the blend comes back with its steps, colour effect and both shapes");
            Check(moulds == 1, "the mould comes back as a perspective with its shape corners in order and its source bounds");
            Check(bevelled == 1, "the bevel comes back as an effect with its profile, indent, light and contrast");
            for (const auto& w : warnings) std::printf("  (import note) %s\n", w.c_str());
        }
    }

    std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
