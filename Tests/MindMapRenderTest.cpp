// Tests/MindMapRenderTest.cpp
// The mind map's radial cluster marks drawn offscreen: a Balloon map with the
// Ring connector draws a ring through a hub's children (an arc for a fan,
// open towards the parent; none around a branch laid out as a list), connector widths follow the depth (a thick trunk
// from the centre), sizes follow the depth, a triangle node is a triangle,
// and the new options, sizes, widths and shapes survive a JSON round trip and
// reach the SVG export.
//
// With ULTRACANVAS_SCREENSHOT_DIR set, the cases are also written there as
// PNGs for a person to look at.
//
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMindMap.h"
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

const int W = 800, H = 800;
const double kPi = 3.14159265358979323846;

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
        if (x < 0 || y < 0 || x >= W || y >= H) return Color(0, 0, 0, 0);
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

const Color kBackground(252, 252, 253, 255);
const Color kHue(200, 40, 40, 255);     // branch 0: red
const Color kHue2(40, 90, 200, 255);    // branch 1: blue

int ColorDistance(const Color& a, const Color& b) {
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}

bool IsBackground(const Color& c) { return ColorDistance(c, kBackground) < 12; }
bool IsRedInk(const Color& c) { return c.r > c.g + 60 && c.r > c.b + 60; }

struct Map {
    std::shared_ptr<UltraCanvasMindMap> map;
    std::string root, hub, other, tri;
};

// A centre, a red hub with eight children, a blue branch with two, and a
// triangle condition under the blue branch.
Map MakeMap() {
    Map m;
    m.map = CreateMindMap("cluster", 0, 0, W, H);
    UltraCanvasMindMap& map = *m.map;
    MindMapStyle style = map.GetStyle();
    style.backgroundColor = kBackground;
    style.showCollapseHandles = false;
    style.connectorStyle = MindMapConnectorStyle::Ring;
    style.connectorColorMode = MindMapConnectorColorMode::InheritFromBranch;
    style.sizeByDepth = {110.0, 70.0, 40.0};
    style.connectorWidthByDepth = {12.0, 6.0, 2.0};
    map.SetStyle(style);
    MindMapTopicStyle level;
    level.shape = MindMapNodeShape::Circle;
    level.fontSize = 10.0;
    level.paddingX = 4.0;
    level.paddingY = 2.0;
    for (int d = 0; d < 3; ++d) map.SetLevelStyle(d, level);
    map.SetBranchPalette({kHue, kHue2});

    MindMapLayoutOptions options = map.GetLayoutOptions();
    options.structure = MindMapStructure::Balloon;
    options.balloonRing = MindMapBalloonRing::Fan;
    options.balloonTrunkLength = 80.0;
    map.SetLayoutOptions(options);

    m.root = map.SetCentralTopic("Plan");
    m.hub = map.AddTopic(m.root, "Water");
    for (int i = 0; i < 8; ++i) map.AddTopic(m.hub, "W" + std::to_string(i + 1));
    m.other = map.AddTopic(m.root, "Power");
    map.AddTopic(m.other, "Torch");
    m.tri = map.AddTopic(m.other, "Fuel");
    MindMapTopicStyle triangle = level;
    triangle.shape = MindMapNodeShape::Triangle;
    triangle.fillColor = Color(240, 190, 30, 255);
    triangle.borderColor = Color(150, 110, 0, 255);
    triangle.textColor = Color(30, 30, 30, 255);
    map.SetTopicStyle(m.tri, triangle);
    return m;
}

void RenderFitted(Canvas& canvas, UltraCanvasMindMap& map) {
    map.Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));   // lays out with real text metrics
    map.FitView(40.0);
    map.Render(canvas.ctx.get(), Rect2Df(0, 0, W, H));
}

Point2Dd CentreOf(const MindMapTopic& t) {
    return Point2Dd(t.bounds.x + t.bounds.width * 0.5, t.bounds.y + t.bounds.height * 0.5);
}

void TestSizesAndRings() {
    std::cout << "\n[Sizes by depth, the ring, trunk widths]\n";
    Canvas canvas;
    Map m = MakeMap();
    UltraCanvasMindMap& map = *m.map;
    RenderFitted(canvas, map);
    canvas.Save("mindmap_balloon_ring");
    UltraCanvasDiagramViewport& view = map.Viewport();

    const MindMapTopic* root = map.GetTopic(m.root);
    const MindMapTopic* hub = map.GetTopic(m.hub);
    const MindMapTopic* leaf = map.GetTopic(hub->childIds[0]);
    Check(root->bounds.width >= 110.0 - 1e-9 && hub->bounds.width >= 70.0 - 1e-9 &&
              leaf->bounds.width >= 40.0 - 1e-9 && hub->bounds.width < root->bounds.width,
          "sizes by depth: the centre 110, a hub 70, a leaf 40 wide at least");
    Check(std::abs(hub->bounds.width - hub->bounds.height) < 1e-9, "a sized circle stays round");

    // The ring through the hub's children: halfway between two neighbours on
    // it, the branch colour; on the parent's side (the fan's open gap) nothing.
    const Point2Dd c = CentreOf(*hub);
    const Point2Dd a = CentreOf(*map.GetTopic(hub->childIds[2]));
    const Point2Dd b = CentreOf(*map.GetTopic(hub->childIds[3]));
    const double radius = std::hypot(a.x - c.x, a.y - c.y);
    double mid = 0.5 * (std::atan2(a.y - c.y, a.x - c.x) + std::atan2(b.y - c.y, b.x - c.x));
    if (std::abs(std::atan2(a.y - c.y, a.x - c.x) - std::atan2(b.y - c.y, b.x - c.x)) > kPi) mid += kPi;
    Point2Dd onRing(c.x + std::cos(mid) * radius, c.y + std::sin(mid) * radius);
    Color ringInk = canvas.PixelAt(view.WorldToScreenD(onRing));
    Check(IsRedInk(ringInk), "ring: between two children, the ring is drawn in the branch colour");

    const Point2Dd p = CentreOf(*root);
    const double towardsParent = std::atan2(p.y - c.y, p.x - c.x) + 0.35;   // beside the trunk
    Point2Dd gap(c.x + std::cos(towardsParent) * radius, c.y + std::sin(towardsParent) * radius);
    Check(IsBackground(canvas.PixelAt(view.WorldToScreenD(gap))),
          "ring: a fan's ring leaves the side towards the parent open");

    // The trunk from the centre to the hub is 12 wide: 4 px off its middle
    // line is still the branch colour; a twig (2 wide) is not.
    const Point2Dd trunkMid((p.x + c.x) * 0.5, (p.y + c.y) * 0.5);
    double len = std::hypot(c.x - p.x, c.y - p.y);
    Point2Dd normal(-(c.y - p.y) / len, (c.x - p.x) / len);
    Point2Dd offTrunk(trunkMid.x + normal.x * 4.0, trunkMid.y + normal.y * 4.0);
    Check(IsRedInk(canvas.PixelAt(view.WorldToScreenD(offTrunk))),
          "width by depth: the main trunk is thick (4 px off its middle is still ink)");
    const MindMapTopic* torchParent = map.GetTopic(m.other);
    const MindMapTopic* torch = map.GetTopic(torchParent->childIds[0]);
    const Point2Dd t0 = CentreOf(*torchParent), t1 = CentreOf(*torch);
    double tl = std::hypot(t1.x - t0.x, t1.y - t0.y);
    // Partway along the spoke, outside both nodes.
    double along = 0.5 * (torchParent->bounds.width * 0.5 + tl - torch->bounds.width * 0.5) / tl;
    Point2Dd spoke(t0.x + (t1.x - t0.x) * along, t0.y + (t1.y - t0.y) * along);
    Point2Dd spokeNormal(-(t1.y - t0.y) / tl, (t1.x - t0.x) / tl);
    Point2Dd offSpoke(spoke.x + spokeNormal.x * 5.0, spoke.y + spokeNormal.y * 5.0);
    Color spokeInk = canvas.PixelAt(view.WorldToScreenD(spoke));
    Color besideSpoke = canvas.PixelAt(view.WorldToScreenD(offSpoke));
    Check(ColorDistance(spokeInk, kHue2) < 60 && IsBackground(besideSpoke),
          "width by depth: a spoke one level down (6 wide) is thinner: 5 px off it is background");

    // The triangle: its lower middle is filled, its top corners are not.
    const MindMapTopic* tri = map.GetTopic(m.tri);
    Point2Dd inside(tri->bounds.x + tri->bounds.width * 0.5, tri->bounds.y + tri->bounds.height * 0.9);
    Point2Dd corner(tri->bounds.x + tri->bounds.width * 0.08, tri->bounds.y + tri->bounds.height * 0.1);
    Color inColor = canvas.PixelAt(view.WorldToScreenD(inside));
    Check(ColorDistance(inColor, Color(240, 190, 30, 255)) < 40, "triangle: filled near its base");
    Check(IsBackground(canvas.PixelAt(view.WorldToScreenD(corner))), "triangle: its top-left corner is empty");

    // A full ring is drawn closed: on the parent's side too.
    MindMapLayoutOptions full = map.GetLayoutOptions();
    full.balloonRing = MindMapBalloonRing::Full;
    map.SetLayoutOptions(full);
    Canvas fullCanvas;
    RenderFitted(fullCanvas, map);
    fullCanvas.Save("mindmap_balloon_full_ring");
    const MindMapTopic* hub2 = map.GetTopic(m.hub);
    const Point2Dd c2 = CentreOf(*hub2);
    const Point2Dd p2 = CentreOf(*map.GetTopic(m.root));
    // Find the ring radius and the largest gap between children; the middle
    // of every gap is on the ring.
    std::vector<double> angles;
    double r2 = 0.0;
    for (const auto& id : hub2->childIds) {
        Point2Dd k = CentreOf(*map.GetTopic(id));
        angles.push_back(std::atan2(k.y - c2.y, k.x - c2.x));
        r2 = std::hypot(k.x - c2.x, k.y - c2.y);
    }
    std::sort(angles.begin(), angles.end());
    bool allInk = true;
    for (size_t i = 0; i < angles.size(); ++i) {
        double from = angles[i];
        double to = (i + 1 < angles.size()) ? angles[i + 1] : angles[0] + 2.0 * kPi;
        double m2 = 0.5 * (from + to);
        Point2Dd q(c2.x + std::cos(m2) * r2, c2.y + std::sin(m2) * r2);
        // Skip the one gap the trunk runs through: the trunk is ink there anyway.
        Color ink = fullCanvas.PixelAt(map.Viewport().WorldToScreenD(q));
        if (!IsRedInk(ink)) allInk = false;
    }
    (void)p2;
    Check(allInk, "ring: a full ring is closed, ink in the middle of every gap");
}

void TestListBranchHasNoRing() {
    std::cout << "\n[A list branch inside a balloon map]\n";
    Canvas canvas;
    Map m = MakeMap();
    UltraCanvasMindMap& map = *m.map;
    map.GetTopic(m.other)->structureOverride = MindMapStructure::LogicRight;
    RenderFitted(canvas, map);
    canvas.Save("mindmap_balloon_list_branch");
    const MindMapTopic* power = map.GetTopic(m.other);
    const Point2Dd c = CentreOf(*power);
    const Point2Dd p = CentreOf(*map.GetTopic(m.root));
    const Point2Dd k0 = CentreOf(*map.GetTopic(power->childIds[0]));
    const Point2Dd k1 = CentreOf(*map.GetTopic(power->childIds[1]));
    const double outward = c.x - p.x;   // which way is away from the centre
    Check((k0.x - c.x) * outward > 0.0 && (k1.x - c.x) * outward > 0.0,
          "the list branch grows away from the centre, not over its trunk");
    // Around the topic, at its children's distance: a ring would be ink all
    // the way round; without one, only the odd crossing is.
    const double r = 0.5 * (std::hypot(k0.x - c.x, k0.y - c.y) + std::hypot(k1.x - c.x, k1.y - c.y));
    int samples = 0, ink = 0;
    for (int i = 0; i < 48; ++i) {
        const double a = 2.0 * kPi * i / 48.0;
        const Point2Dd q = map.Viewport().WorldToScreenD({c.x + std::cos(a) * r, c.y + std::sin(a) * r});
        if (q.x < 0 || q.y < 0 || q.x >= W || q.y >= H) continue;
        ++samples;
        if (!IsBackground(canvas.PixelAt(q))) ++ink;
    }
    Check(samples >= 12 && ink * 4 < samples, "no ring is drawn around a topic laid out as a list");
}

void TestJsonAndSvg() {
    std::cout << "\n[JSON round trip and SVG]\n";
    Map m = MakeMap();
    UltraCanvasMindMap& map = *m.map;
    MindMapLayoutOptions options = map.GetLayoutOptions();
    options.balloonRing = MindMapBalloonRing::Full;
    options.balloonFanSweep = 240.0;
    options.balloonGap = 14.0;
    options.balloonTrunkLength = 120.0;
    options.radialStartAngle = 0.25;
    map.SetLayoutOptions(options);
    map.GetTopic(m.other)->structureOverride = MindMapStructure::LogicRight;
    const std::string json = map.ToJson();

    auto copy = CreateMindMap("copy", 0, 0, W, H);
    Check(copy->FromJson(json), "the JSON reads back");
    const MindMapLayoutOptions& back = copy->GetLayoutOptions();
    Check(back.structure == MindMapStructure::Balloon && back.balloonRing == MindMapBalloonRing::Full,
          "the Balloon structure and the ring kind");
    Check(back.balloonFanSweep == 240.0 && back.balloonGap == 14.0 && back.balloonTrunkLength == 120.0 &&
              back.radialStartAngle == 0.25,
          "the fan sweep, gap, trunk length and start angle");
    Check(copy->GetStyle().connectorStyle == MindMapConnectorStyle::Ring, "the Ring connector style");
    Check(copy->GetStyle().sizeByDepth == std::vector<double>({110.0, 70.0, 40.0}), "sizes by depth");
    Check(copy->GetStyle().connectorWidthByDepth == std::vector<double>({12.0, 6.0, 2.0}),
          "connector widths by depth");
    const MindMapTopic* tri = copy->GetTopic(m.tri);
    Check(tri && tri->styleOverride && tri->styleOverride->shape == MindMapNodeShape::Triangle,
          "a triangle topic stays a triangle");
    const MindMapTopic* other = copy->GetTopic(m.other);
    Check(other && other->structureOverride == MindMapStructure::LogicRight,
          "a branch's own structure inside the balloon map");

    Canvas canvas;
    RenderFitted(canvas, map);
    const std::string svg = map.ToSvg();
    Check(svg.find("<polygon") != std::string::npos, "the SVG export draws the triangle as a polygon");
}

} // namespace

int main() {
    std::cout << "====================================\n";
    std::cout << "  UltraCanvas Mind Map - radial cluster marks\n";
    std::cout << "====================================\n";
    TestSizesAndRings();
    TestListBranchHasNoRing();
    TestJsonAndSvg();
    std::cout << "\n====================================\n";
    if (g_failures == 0) {
        std::cout << "All tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " test(s) FAILED.\n";
    return 1;
}
