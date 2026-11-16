// Tests/NodeDiagramConceptMapTest.cpp
// UltraCanvasNodeDiagram 2.5.0, the concept-map features: a free-placed
// region keeps its bounds, overlaps other boxes and holds nodes that are not
// its members through every layout; an ellipse node grows for its label in
// width and height separately; an annotation has a box on the side asked
// for, wrapped at its width; HighlightPath marks the links between
// consecutive nodes in either direction and leaves their own look alone; all
// of it, and a string with escapes, survives ToJson / FromJson. Headless: no
// window, so texts are measured by the estimate. A cluster title outside its
// box takes no room from it; a link's waypoints are kept. The last parts draw
// it all into an offscreen Cairo surface and read the pixels back - the
// title outside the box, the link through its waypoint, the link cut at its
// target's outline with the arrowhead before it, the cloud as one shape; with
// ULTRACANVAS_SCREENSHOT_DIR set they also write the pictures there as PNGs.
//
// Version: 1.0.0
// Last Modified: 2026-11-16
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasNodeDiagram.h"
#include "UltraCanvasRenderContext.h"

#include <cairo/cairo.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static bool SameRect(const Rect2Dd& a, const Rect2Dd& b) {
    return std::abs(a.x - b.x) < 0.01 && std::abs(a.y - b.y) < 0.01 &&
           std::abs(a.width - b.width) < 0.01 && std::abs(a.height - b.height) < 0.01;
}

static NodeDiagramNode Box(const std::string& id, double x, double y) {
    NodeDiagramNode n(id, id);
    n.shape = NodeShape::Rectangle;
    n.x = x;
    n.y = y;
    n.width = 60.0;
    n.height = 30.0;
    return n;
}

static void TestRegions() {
    std::printf("\n[free-placed regions]\n");
    UltraCanvasNodeDiagram d("d", 0, 0, 800, 600);
    d.SetLayout(NodeDiagramLayout::Manual);
    d.AddNode(Box("a", 100, 100));
    d.AddNode(Box("b", 300, 100));
    d.AddNode(Box("loose", 150, 220));   // in no group, inside both regions

    const Rect2Dd first(50, 50, 300, 300), second(200, 150, 300, 300);
    d.AddRegion("r1", "First", first, Color(220, 80, 60, 70));
    d.AddRegion("r2", "Second", second, Color(60, 160, 220, 70), Color(60, 160, 220, 255));
    CHECK(SameRect(d.GetGroupBounds("r1"), first), "a region's box is the rectangle it was given");
    CHECK(d.GetGroup("r1")->labelColor == Color(220, 80, 60, 255), "its title is the fill colour made opaque");
    CHECK(d.GetGroup("r2")->labelColor == Color(60, 160, 220, 255), "or the border colour when it has a border");

    // A cluster box around a and b lies over both regions.
    d.AddGroup("g", "Members", {"a", "b"}, Color(90, 90, 90, 255));
    const Rect2Dd cluster = d.GetGroupBounds("g");
    const double looseX = d.GetNode("loose")->x, looseY = d.GetNode("loose")->y;
    const double ax = d.GetNode("a")->x, ay = d.GetNode("a")->y;

    d.RunLayout();
    CHECK(SameRect(d.GetGroupBounds("r1"), first) && SameRect(d.GetGroupBounds("r2"), second),
          "two overlapping regions stay where they were put through a layout");
    CHECK(d.GetNode("loose")->x == looseX && d.GetNode("loose")->y == looseY,
          "a node in no group is not pushed out of a region");
    CHECK(d.GetNode("a")->x == ax && d.GetNode("a")->y == ay, "the cluster under the regions did not move");
    // (Its height follows the zoom the layout's fit chose: room for the title.)
    const Rect2Dd after = d.GetGroupBounds("g");
    CHECK(std::abs(after.x - cluster.x) < 0.01 && std::abs(after.width - cluster.width) < 0.01,
          "and its box is still its members'");

    // The same three boxes as member groups would have been moved apart.
    d.AddGroup("solo", "Solo", {"loose"}, Color(90, 90, 90, 255));
    d.SetGroupBounds("solo", Rect2Dd(0, 0, 500, 500));
    CHECK(SameRect(d.GetGroupBounds("solo"), Rect2Dd(0, 0, 500, 500)), "SetGroupBounds places a group");
    d.RunLayout();
    CHECK(d.GetNode("a")->x == ax, "a placed group pushes nothing either");
    d.ClearGroupBounds("solo");
    const Rect2Dd back = d.GetGroupBounds("solo");
    CHECK(back.width < 200.0 && back.x <= d.GetNode("loose")->x, "ClearGroupBounds hands the box back to the members");

    d.AddRegion("flat", "Flat", Rect2Dd(0, 0, 0, 50), Color(0, 0, 0, 40));
    CHECK(d.GetGroupBounds("flat").width <= 0.0, "a region without an area has no box");

    // JSON
    UltraCanvasNodeDiagram e("e", 0, 0, 800, 600);
    CHECK(e.FromJson(d.ToJson()), "FromJson reads it");
    CHECK(e.GetGroup("r2") && e.GetGroup("r2")->freeBounds && SameRect(e.GetGroupBounds("r2"), second),
          "a region round-trips with its bounds");
    CHECK(e.GetGroup("g") && !e.GetGroup("g")->freeBounds, "a cluster stays a cluster");
}

static void TestEllipse() {
    std::printf("\n[ellipse nodes]\n");
    UltraCanvasNodeDiagram d("d", 0, 0, 800, 600);
    d.SetLabelFit(NodeLabelFit::GrowNode);

    double w = 0.0, h = 0.0, cw = 0.0, ch = 0.0;
    d.MinimumNodeSizeForLabel("A fairly long concept name", 10.0, NodeShape::Ellipse, w, h);
    d.MinimumNodeSizeForLabel("A fairly long concept name", 10.0, NodeShape::Circle, cw, ch);
    int textW = 0, textH = 0;
    d.MeasureLabel("A fairly long concept name", 10.0, textW, textH);
    CHECK(w > h * 2.0, "the smallest ellipse for a long name is wider than tall");
    CHECK(w > textW && h > textH, "and larger than the text on both axes");
    CHECK(w * h < cw * ch, "it takes less room than the circle for the same name");

    NodeDiagramNode n("n", "A fairly long concept name");
    n.shape = NodeShape::Ellipse;
    n.x = 100.0;
    n.y = 100.0;
    n.width = 40.0;
    n.height = 30.0;
    n.fontSize = 10.0;
    d.AddNode(n);
    CHECK(d.FitNodesToLabels() == 1, "an ellipse too small for its name grows");
    const NodeDiagramNode* grown = d.GetNode("n");
    CHECK(std::abs(grown->width - w) < 0.01, "to the width the name needs");
    CHECK(grown->height < grown->width * 0.6, "without becoming a circle");
    CHECK(std::abs((grown->x + grown->width * 0.5) - 120.0) < 0.01 &&
          std::abs((grown->y + grown->height * 0.5) - 115.0) < 0.01, "about its centre");
    CHECK(d.GetNodeLabelFontSize("n") == 10.0, "the name keeps its font size in the grown ellipse");

    UltraCanvasNodeDiagram e("e", 0, 0, 800, 600);
    CHECK(e.FromJson(d.ToJson()) && e.GetNode("n") && e.GetNode("n")->shape == NodeShape::Ellipse,
          "the shape round-trips");
}

static void TestAnnotations() {
    std::printf("\n[annotations]\n");
    UltraCanvasNodeDiagram d("d", 0, 0, 800, 600);
    d.AddNode(Box("n", 200, 200));
    CHECK(d.GetNodeAnnotationBounds("n").width <= 0.0, "no annotation, no box");
    CHECK(d.GetNodeAnnotationBounds("nobody").width <= 0.0, "an unknown node has none");

    const std::string shortText = "Short note";
    const std::string longText = "A much longer note that cannot fit on one line of a hundred and sixty units "
                                 "and so has to wrap onto several lines beside its node.";
    d.SetNodeAnnotation("n", shortText);
    const NodeDiagramNode* n = d.GetNode("n");
    Rect2Dd box = d.GetNodeAnnotationBounds("n");
    CHECK(box.width > 0.0 && box.height > 0.0, "an annotation has a box");
    CHECK(std::abs(box.x - (n->x + n->width + n->annotationGap)) < 0.01, "to the right of the node by the gap");
    CHECK(box.y >= n->y && box.y + box.height <= n->y + n->height, "a short one sits within the node's height");
    const double oneLine = box.height;

    d.SetNodeAnnotation("n", longText);
    box = d.GetNodeAnnotationBounds("n");
    CHECK(box.width <= n->annotationMaxWidth + 0.01, "a long one wraps at the maximum width");
    CHECK(box.height > oneLine * 2.5, "onto several lines");
    CHECK(std::abs(box.y - n->y) < 0.01, "and starts at the node's top once it is taller than the node");

    d.SetNodeAnnotation("n", "one\ntwo\nthree");
    CHECK(d.GetNodeAnnotationBounds("n").height > oneLine * 2.5, "a line break starts a line");

    d.SetNodeAnnotation("n", shortText, NodeAnnotationPosition::Left);
    box = d.GetNodeAnnotationBounds("n");
    CHECK(std::abs(box.x + box.width - (n->x - n->annotationGap)) < 0.01, "Left: ends a gap before the node");
    d.SetNodeAnnotation("n", shortText, NodeAnnotationPosition::Top);
    box = d.GetNodeAnnotationBounds("n");
    CHECK(std::abs(box.y + box.height - (n->y - n->annotationGap)) < 0.01, "Top: ends a gap above it");
    CHECK(std::abs((box.x + box.width * 0.5) - (n->x + n->width * 0.5)) < 0.01, "centred on the node");
    d.SetNodeAnnotation("n", shortText, NodeAnnotationPosition::Bottom);
    box = d.GetNodeAnnotationBounds("n");
    CHECK(std::abs(box.y - (n->y + n->height + n->annotationGap)) < 0.01, "Bottom: starts a gap below it");

    const double x = n->x, y = n->y, w = n->width, h = n->height;
    d.SetNodeAnnotation("n", longText);
    CHECK(n->x == x && n->y == y && n->width == w && n->height == h, "the node itself is unchanged by its annotation");

    // JSON, with every escape a paragraph can hold.
    const std::string tricky = "He said \"no\".\nA back\\slash, a\ttab.";
    d.SetNodeAnnotation("n", tricky, NodeAnnotationPosition::Left);
    d.GetNode("n")->annotationMaxWidth = 220.0;
    d.GetNode("n")->annotationColor = Color(10, 20, 30, 255);
    d.UpdateNodeLabel("n", "Quote \" and \\");
    UltraCanvasNodeDiagram e("e", 0, 0, 800, 600);
    CHECK(e.FromJson(d.ToJson()), "FromJson reads it");
    const NodeDiagramNode* back = e.GetNode("n");
    CHECK(back && back->annotation == tricky, "the text round-trips, quotes, line break and backslash included");
    CHECK(back && back->annotationPosition == NodeAnnotationPosition::Left && back->annotationMaxWidth == 220.0 &&
          back->annotationColor == Color(10, 20, 30, 255), "with its side, width and colour");
    CHECK(back && back->label == "Quote \" and \\", "and a label with a quote and a backslash does too");
}

static void TestHighlightPath() {
    std::printf("\n[path highlighting]\n");
    UltraCanvasNodeDiagram d("d", 0, 0, 800, 600);
    for (int i = 0; i < 5; ++i) d.AddNode(Box("n" + std::to_string(i), i * 100.0, 0.0));
    const Color grey(120, 120, 120, 255), red(220, 40, 40, 255), blue(40, 40, 220, 255);
    d.AddLink("l01", "n0", "n1", grey);
    d.AddLink("l21", "n2", "n1", grey);      // points against the path
    d.AddLink("l23", "n2", "n3", grey);
    d.AddLink("l23b", "n3", "n2", grey);     // a second link between the same two
    d.AddLink("l04", "n0", "n4", grey);      // off the path

    CHECK(d.HighlightPath({"n0", "n1", "n2", "n3"}, red, 5.0) == 4, "every link between consecutive nodes is marked");
    CHECK(d.GetLink("l21")->highlighted, "whichever way it points");
    CHECK(d.GetLink("l23")->highlighted && d.GetLink("l23b")->highlighted, "both links of a double one");
    CHECK(!d.GetLink("l04")->highlighted, "a link off the path is not");
    CHECK(d.GetLink("l01")->highlightColor == red && d.GetLink("l01")->highlightWidth == 5.0, "in the colour and width given");
    CHECK(d.GetLink("l01")->lineColor == grey && d.GetLink("l01")->lineWidth == 2.0, "the link's own look is kept");
    CHECK(d.GetHighlightedLinkIds().size() == 4, "GetHighlightedLinkIds lists them");

    CHECK(d.HighlightPath({"n1", "n4"}, blue) == 0, "a pair with no link between them marks nothing");
    CHECK(d.HighlightPath({"n0"}, blue) == 0 && d.HighlightPath({}, blue) == 0, "nor does a path of one node or none");
    CHECK(d.HighlightPath({"n4", "n0", "n1"}, blue) == 2, "a second path adds to the first");
    CHECK(d.GetLink("l01")->highlightColor == blue, "and takes over a link they share");
    CHECK(d.GetLink("l21")->highlightColor == red, "leaving the rest of the first");

    UltraCanvasNodeDiagram e("e", 0, 0, 800, 600);
    CHECK(e.FromJson(d.ToJson()), "FromJson reads it");
    CHECK(e.GetHighlightedLinkIds().size() == 5, "the highlights round-trip");
    CHECK(e.GetLink("l21") && e.GetLink("l21")->highlightColor == red && e.GetLink("l21")->highlightWidth == 5.0 &&
          e.GetLink("l21")->lineColor == grey, "with their colour and width, beside the link's own");

    d.ClearLinkHighlight("l04");
    CHECK(!d.GetLink("l04")->highlighted && d.GetLink("l01")->highlighted, "ClearLinkHighlight clears one");
    d.HighlightLink("l04", red);
    CHECK(d.GetLink("l04")->highlighted && d.GetLink("l04")->highlightWidth == 4.0, "HighlightLink marks one");
    d.ClearHighlights();
    CHECK(d.GetHighlightedLinkIds().empty(), "ClearHighlights clears them all");
    CHECK(d.GetLink("l01")->lineColor == grey, "and the links are as they were");
}

// An offscreen surface and its pixels.
struct Canvas {
    std::unique_ptr<IRenderContext> ctx;
    cairo_t* cr = nullptr;

    Canvas(int w, int h) {
        ctx = CreateRenderContext(Size2Di(w, h), nullptr);
        if (ctx) cr = static_cast<cairo_t*>(ctx->GetNativeContext());
    }

    Color PixelAt(int x, int y) const {
        cairo_surface_t* s = cairo_get_target(cr);
        cairo_surface_flush(s);
        const unsigned char* data = cairo_image_surface_get_data(s);
        const uint32_t p = reinterpret_cast<const uint32_t*>(data + y * cairo_image_surface_get_stride(s))[x];
        return Color((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF, 255);
    }

    // Pixels darker than mid-grey in a rectangle: text.
    int DarkPixels(int x0, int y0, int x1, int y1) const {
        int count = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const Color c = PixelAt(x, y);
                if (c.r < 128 && c.g < 128 && c.b < 128) ++count;
            }
        }
        return count;
    }

    void Save(const std::string& name) const {
        const char* dir = std::getenv("ULTRACANVAS_SCREENSHOT_DIR");
        if (!dir || !cr) return;
        const std::string path = std::string(dir) + "/" + name + ".png";
        cairo_surface_write_to_png(cairo_get_target(cr), path.c_str());   // path-string-ok: test output under an ASCII directory the caller chose
        std::printf("  wrote %s\n", path.c_str());
    }
};

static bool Near(const Color& c, int r, int g, int b, int tolerance = 12) {
    return std::abs(c.r - r) <= tolerance && std::abs(c.g - g) <= tolerance && std::abs(c.b - b) <= tolerance;
}

// Zoom 1 and no pan, so a world unit is a pixel and each thing can be looked
// for where it was put.
static void TestRender() {
    std::printf("\n[drawn offscreen]\n");
    const int W = 600, H = 400;
    Canvas canvas(W, H);
    CHECK(canvas.ctx != nullptr, "an offscreen context is available");
    if (!canvas.ctx) return;

    UltraCanvasNodeDiagram d("d", 0, 0, W, H);
    d.SetTheme(NodeDiagramTheme::Minimal);
    d.SetBackgroundColor(Color(255, 255, 255, 255));
    d.SetLayout(NodeDiagramLayout::Manual);
    d.SetLabelFit(NodeLabelFit::Overflow);   // sizes as given

    // Two half-transparent regions that overlap in (150..250, 150..250).
    d.AddRegion("red", "", Rect2Dd(50, 50, 200, 200), Color(255, 0, 0, 128));
    d.AddRegion("blue", "", Rect2Dd(150, 150, 200, 200), Color(0, 0, 255, 128));

    // A wide ellipse without a label.
    NodeDiagramNode oval("oval", "");
    oval.shape = NodeShape::Ellipse;
    oval.x = 300; oval.y = 40; oval.width = 200; oval.height = 60;
    oval.fillColor = oval.borderColor = Color(0, 160, 0, 255);
    oval.borderWidth = 1.0;
    d.AddNode(oval);

    // A node in the blue region - not its member - with a note on its right.
    NodeDiagramNode noted("noted", "");
    noted.shape = NodeShape::Rectangle;
    noted.x = 380; noted.y = 200; noted.width = 40; noted.height = 30;
    noted.fillColor = noted.borderColor = Color(200, 200, 200, 255);
    noted.annotation = "A note beside the node that is long enough to need a second line.";
    noted.annotationColor = Color(0, 0, 0, 255);
    noted.annotationFontSize = 11.0;
    noted.annotationMaxWidth = 150.0;
    d.AddNode(noted);

    // Two small nodes and a thin grey link between them along y = 375.
    for (int i = 0; i < 2; ++i) {
        NodeDiagramNode end(i == 0 ? "p" : "q", "");
        end.shape = NodeShape::Square;
        end.x = i == 0 ? 20 : 550; end.y = 360; end.width = 30; end.height = 30;
        end.fillColor = end.borderColor = Color(200, 200, 200, 255);
        d.AddNode(end);
    }
    NodeDiagramLink link("pq", "p", "q");
    link.directed = false;
    link.style = LinkStyle::Straight;
    link.lineColor = Color(128, 128, 128, 255);
    link.lineWidth = 2.0;
    d.AddLink(link);

    const Rect2Df all(0, 0, W, H);
    d.Render(canvas.ctx.get(), all);
    CHECK(d.GetZoomLevel() == 1.0 && d.GetPanOffset().x == 0.0, "drawn at zoom 1 without a pan");

    // Regions
    CHECK(Near(canvas.PixelAt(100, 100), 255, 127, 127), "the red region washes its rectangle");
    CHECK(Near(canvas.PixelAt(300, 300), 127, 127, 255), "the blue one its own");
    CHECK(Near(canvas.PixelAt(200, 200), 127, 63, 191), "where they overlap, both are there");
    CHECK(Near(canvas.PixelAt(30, 30), 255, 255, 255), "outside them the background is untouched");
    CHECK(d.GetNode("noted")->x == 380.0 && d.GetNode("noted")->y == 200.0,
          "a frame leaves a node inside a region where it is");

    // Ellipse: centre and both ends of the long axis filled, the corners of its box not.
    CHECK(Near(canvas.PixelAt(400, 70), 0, 160, 0), "the ellipse is filled at its centre");
    CHECK(Near(canvas.PixelAt(306, 70), 0, 160, 0) && Near(canvas.PixelAt(494, 70), 0, 160, 0),
          "and out to both ends of its width, where a circle of its height would not reach");
    CHECK(Near(canvas.PixelAt(304, 44), 255, 255, 255) && Near(canvas.PixelAt(496, 96), 255, 255, 255),
          "the corners of its box are background: it is not a rectangle");

    // Annotation: text to the right of the node, on more than one line.
    const NodeDiagramNode* n = d.GetNode("noted");
    const int noteLeft = static_cast<int>(n->x + n->width + n->annotationGap);
    CHECK(canvas.DarkPixels(noteLeft, 150, noteLeft + 152, 300) > 150, "the note is drawn to the right of its node");
    CHECK(canvas.DarkPixels(noteLeft + 156, 150, W, 300) == 0, "and wraps at its maximum width");
    CHECK(canvas.DarkPixels(noteLeft, 150, noteLeft + 152, 204) > 0 &&
          canvas.DarkPixels(noteLeft, 216, noteLeft + 152, 300) > 0, "onto more than one line");
    CHECK(canvas.DarkPixels(300, 150, static_cast<int>(n->x), 300) == 0, "nothing is written on the other side");

    // Highlight
    CHECK(Near(canvas.PixelAt(300, 375), 128, 128, 128, 40), "the plain link is grey");
    CHECK(Near(canvas.PixelAt(300, 378), 255, 255, 255), "and two units wide");
    CHECK(d.HighlightPath({"p", "q"}, Color(255, 0, 255, 255), 8.0) == 1, "the path has its one link");
    d.Render(canvas.ctx.get(), all);
    CHECK(Near(canvas.PixelAt(300, 375), 255, 0, 255), "highlighted, it is drawn in the path's colour");
    CHECK(Near(canvas.PixelAt(300, 378), 255, 0, 255), "at the path's width");
    canvas.Save("node-diagram-concept-map");
    d.ClearHighlights();
    d.Render(canvas.ctx.get(), all);
    CHECK(Near(canvas.PixelAt(300, 375), 128, 128, 128, 40) && Near(canvas.PixelAt(300, 378), 255, 255, 255),
          "cleared, it is the thin grey link again");
}

static void TestOutsideTitlesAndWaypoints() {
    std::printf("\n[outside titles, waypoints]\n");
    UltraCanvasNodeDiagram d("d", 0, 0, 800, 600);
    d.SetZoomLevel(0.25);   // a title is four times its size in world units here
    d.AddNode(Box("a", 100, 100));
    d.AddNode(Box("b", 300, 100));
    NodeDiagramGroup g("g", "A title");
    g.nodeIds = {"a", "b"};
    g.padding = 10.0;
    d.AddGroup(g);
    const Rect2Dd inside = d.GetGroupBounds("g");
    CHECK(inside.height > 30.0 + 20.0 + 1.0, "a title inside the box makes the box taller at a low zoom");
    const GroupLabelPosition outside[] = {
        GroupLabelPosition::OutsideTopLeft, GroupLabelPosition::OutsideTopCenter, GroupLabelPosition::OutsideTopRight,
        GroupLabelPosition::OutsideBottomLeft, GroupLabelPosition::OutsideBottomCenter,
        GroupLabelPosition::OutsideBottomRight, GroupLabelPosition::OutsideLeftTop,
        GroupLabelPosition::OutsideLeftBottom, GroupLabelPosition::OutsideRightTop,
        GroupLabelPosition::OutsideRightBottom};
    bool allTight = true, allRoundTrip = true;
    for (GroupLabelPosition position : outside) {
        d.GetGroup("g")->labelPosition = position;
        const Rect2Dd box = d.GetGroupBounds("g");
        if (std::abs(box.height - 50.0) > 0.01 || std::abs(box.y - 90.0) > 0.01) allTight = false;
        UltraCanvasNodeDiagram e("e", 0, 0, 800, 600);
        if (!e.FromJson(d.ToJson()) || !e.GetGroup("g") || e.GetGroup("g")->labelPosition != position) allRoundTrip = false;
    }
    CHECK(allTight, "a title outside it does not: the box is its members and the padding, in all ten places");
    CHECK(allRoundTrip, "each outside position round-trips through JSON");
    d.GetGroup("g")->labelPosition = GroupLabelPosition::BottomRight;
    UltraCanvasNodeDiagram f("f", 0, 0, 800, 600);
    CHECK(f.FromJson(d.ToJson()) && f.GetGroup("g")->labelPosition == GroupLabelPosition::BottomRight,
          "and so does BottomRight");

    d.AddLink("ab", "a", "b");
    d.SetLinkWaypoints("ab", {Point2Dd(150.5, -40.0), Point2Dd(250.0, -60.25)});
    d.AddLinkWaypoint("ab", Point2Dd(300.0, 0.0));
    CHECK(d.GetLink("ab")->waypoints.size() == 3, "SetLinkWaypoints and AddLinkWaypoint lead a link through points");
    const double ax = d.GetNode("a")->x;
    d.RunLayout();
    CHECK(d.GetNode("a")->x == ax && d.GetLink("ab")->waypoints[0].y == -40.0, "a layout leaves them where they are");
    UltraCanvasNodeDiagram h("h", 0, 0, 800, 600);
    CHECK(h.FromJson(d.ToJson()) && h.GetLink("ab"), "FromJson reads it");
    const std::vector<Point2Dd>& back = h.GetLink("ab")->waypoints;
    CHECK(back.size() == 3 && back[0].x == 150.5 && back[0].y == -40.0 && back[1].y == -60.25 && back[2].x == 300.0,
          "the waypoints round-trip, in order");
    d.ClearLinkWaypoints("ab");
    CHECK(d.GetLink("ab")->waypoints.empty(), "ClearLinkWaypoints takes them back");
}

// Pixels that are not the white background, in a rectangle.
static int InkPixels(const Canvas& canvas, int x0, int y0, int x1, int y1) {
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (!Near(canvas.PixelAt(x, y), 255, 255, 255, 6)) ++count;
        }
    }
    return count;
}

static void TestRenderLinksTitlesCloud() {
    std::printf("\n[links, titles and the cloud, drawn offscreen]\n");
    const int W = 600, H = 400;
    Canvas canvas(W, H);
    if (!canvas.ctx) return;

    UltraCanvasNodeDiagram d("d", 0, 0, W, H);
    d.SetTheme(NodeDiagramTheme::Minimal);
    d.SetBackgroundColor(Color(255, 255, 255, 255));
    d.SetLayout(NodeDiagramLayout::Manual);
    d.SetLabelFit(NodeLabelFit::Overflow);

    // A region with nothing in it but its fill, titled above and then beside.
    d.AddRegion("r", "Outside", Rect2Dd(60, 60, 160, 60), Color(0, 0, 255, 40));
    d.GetGroup("r")->labelColor = Color(0, 0, 0, 255);
    d.GetGroup("r")->labelPosition = GroupLabelPosition::OutsideTopLeft;

    // A cloud.
    NodeDiagramNode cloud("cloud", "");
    cloud.shape = NodeShape::Cloud;
    cloud.x = 340; cloud.y = 40; cloud.width = 200; cloud.height = 120;
    cloud.fillColor = cloud.borderColor = Color(0, 160, 0, 255);
    cloud.borderWidth = 1.0;
    d.AddNode(cloud);

    // A directed link from a square to an ellipse that is all outline: no
    // fill, so whatever is drawn under it shows.
    NodeDiagramNode from("from", "");
    from.shape = NodeShape::Square;
    from.x = 40; from.y = 285; from.width = 30; from.height = 30;
    from.fillColor = from.borderColor = Color(200, 200, 200, 255);
    d.AddNode(from);
    NodeDiagramNode to("to", "");
    to.shape = NodeShape::Ellipse;
    to.x = 400; to.y = 270; to.width = 120; to.height = 60;
    to.fillColor = Color(0, 0, 0, 0);
    to.borderColor = Color(0, 0, 0, 0);
    to.borderWidth = 0.0;
    d.AddNode(to);
    NodeDiagramLink link("l", "from", "to");
    link.style = LinkStyle::Straight;
    link.lineColor = Color(128, 128, 128, 255);
    link.lineWidth = 2.0;
    d.AddLink(link);   // directed by default; along y = 300

    const Rect2Df all(0, 0, W, H);
    d.Render(canvas.ctx.get(), all);
    canvas.Save("node-diagram-links-titles-cloud");

    // Title
    CHECK(canvas.DarkPixels(60, 30, 220, 58) > 30, "OutsideTopLeft: the title is written above the box");
    CHECK(canvas.DarkPixels(60, 60, 220, 120) == 0, "and not in it");
    CHECK(canvas.DarkPixels(0, 30, 58, 130) == 0, "starting at the box's left edge");

    // Cloud: one shape, edge to edge, its corners empty.
    CHECK(Near(canvas.PixelAt(440, 100), 0, 160, 0), "the cloud is filled at its centre");
    CHECK(Near(canvas.PixelAt(350, 115), 0, 160, 0) && Near(canvas.PixelAt(530, 100), 0, 160, 0),
          "out to its left and right edges");
    CHECK(Near(canvas.PixelAt(440, 150), 0, 160, 0) && Near(canvas.PixelAt(440, 45), 0, 160, 0),
          "and from its base to its top");
    CHECK(Near(canvas.PixelAt(343, 43), 255, 255, 255) && Near(canvas.PixelAt(537, 43), 255, 255, 255) &&
          Near(canvas.PixelAt(343, 157), 255, 255, 255), "with the corners of its box left empty");
    CHECK(InkPixels(canvas, 541, 40, 600, 220) == 0 && InkPixels(canvas, 340, 162, 540, 220) == 0,
          "nothing of it is drawn outside its box");

    // The link stops at the ellipse's outline (x = 400 on its axis).
    CHECK(Near(canvas.PixelAt(200, 300), 128, 128, 128, 40), "the link is drawn up to its target");
    CHECK(InkPixels(canvas, 402, 290, 520, 310) == 0, "and stops at the target's outline, not at its centre");
    CHECK(InkPixels(canvas, 386, 302, 399, 308) > 0 && InkPixels(canvas, 386, 292, 399, 298) > 0,
          "with its arrowhead before the outline, on both sides of the line");
    CHECK(InkPixels(canvas, 200, 302, 300, 308) == 0, "the line itself has no such width");

    // Title beside the box; the link led through a waypoint.
    d.GetGroup("r")->labelPosition = GroupLabelPosition::OutsideRightBottom;
    d.SetLinkWaypoints("l", {Point2Dd(230, 220)});
    d.Render(canvas.ctx.get(), all);
    CHECK(canvas.DarkPixels(60, 30, 220, 58) == 0 && canvas.DarkPixels(222, 100, 320, 122) > 30,
          "OutsideRightBottom: the title is beside the box, at its foot");
    CHECK(Near(canvas.PixelAt(230, 220), 128, 128, 128, 40), "a link with a waypoint passes through it");
    CHECK(InkPixels(canvas, 200, 296, 260, 304) == 0, "and no longer runs straight between its nodes");
    CHECK(InkPixels(canvas, 420, 285, 500, 315) == 0, "it still stops at the outline");
    link.id = "curve";
    link.style = LinkStyle::Bezier;
    link.waypoints = {Point2Dd(230, 360)};
    d.AddLink(link);
    d.Render(canvas.ctx.get(), all);
    canvas.Save("node-diagram-waypoints");
    CHECK(InkPixels(canvas, 226, 356, 234, 364) > 0, "a Bezier link curves through its waypoint too");
}

int main() {
    std::printf("NodeDiagramConceptMapTest\n");
    TestRegions();
    TestEllipse();
    TestAnnotations();
    TestHighlightPath();
    TestOutsideTitlesAndWaypoints();
    TestRender();
    TestRenderLinksTitlesCloud();
    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
