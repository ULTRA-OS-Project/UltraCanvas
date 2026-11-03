// Tests/NodeDiagramOverlapTest.cpp
// UltraCanvasNodeDiagram 2.3.0: no two nodes overlap after ResolveOverlaps()
// or after the force-directed layout, even for wide card nodes in a small
// element; pinned nodes stay put; the card fields (icon, sublabel, corner,
// shadow) survive ToJson / FromJson. Headless: no window, no rendering.
// 2.4.0: a label never outgrows its node - the node grows for it (GrowNode,
// ShrinkThenGrow) or the font shrinks (ShrinkFont); the degree-driven
// sizing respects the label's minimum; every layout ends without overlaps;
// the fit survives JSON. Without a window the labels are measured by the
// estimate, which is what these numbers test.
//
// Version: 1.1.0
// Last Modified: 2026-11-03
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasNodeDiagram.h"

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

// The pairs whose rectangles intersect (a gap of less than `margin`).
static int Overlaps(const UltraCanvasNodeDiagram& d, const std::vector<std::string>& ids, double margin = 0.0) {
    int count = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        for (size_t j = i + 1; j < ids.size(); ++j) {
            const NodeDiagramNode* a = d.GetNode(ids[i]);
            const NodeDiagramNode* b = d.GetNode(ids[j]);
            const bool apartX = a->x + a->width + margin <= b->x + 0.01 || b->x + b->width + margin <= a->x + 0.01;
            const bool apartY = a->y + a->height + margin <= b->y + 0.01 || b->y + b->height + margin <= a->y + 0.01;
            if (!apartX && !apartY) {
                ++count;
                if (std::getenv("NODE_OVERLAP_DEBUG"))
                    std::printf("    %s (%.1f,%.1f) / %s (%.1f,%.1f)\n", ids[i].c_str(), a->x, a->y, ids[j].c_str(), b->x, b->y);
            }
        }
    }
    return count;
}

static NodeDiagramNode Card(const std::string& id, double x, double y) {
    NodeDiagramNode n(id, "Device " + id);
    n.shape = NodeShape::Rectangle;
    n.width = 150.0;
    n.height = 40.0;
    n.x = x;
    n.y = y;
    return n;
}

static void TestResolveOverlaps() {
    std::printf("\n[ResolveOverlaps]\n");
    UltraCanvasNodeDiagram d("d", 0, 0, 400, 300);
    std::vector<std::string> ids;
    // Twenty cards piled on a few spots, one of them pinned.
    for (int i = 0; i < 20; ++i) {
        NodeDiagramNode n = Card("n" + std::to_string(i), (i % 3) * 10.0, (i % 2) * 5.0);
        if (i == 0) n.isPinned = true;
        d.AddNode(n);
        ids.push_back(n.id);
    }
    CHECK(Overlaps(d, ids) > 0, "the pile starts overlapping");
    const double pinnedX = d.GetNode("n0")->x, pinnedY = d.GetNode("n0")->y;
    const int passes = d.ResolveOverlaps(8.0);
    CHECK(passes > 0, "it had work to do");
    std::printf("  (%d passes, %d pairs left)\n", passes, Overlaps(d, ids, 8.0));
    CHECK(Overlaps(d, ids, 8.0) == 0, "no two cards closer than the margin afterwards");
    CHECK(d.GetNode("n0")->x == pinnedX && d.GetNode("n0")->y == pinnedY, "the pinned card did not move");
    CHECK(d.ResolveOverlaps(8.0) == 0, "a second call finds nothing to do");
}

static void TestFallbackPlacement() {
    std::printf("\n[the fallback when the passes run out]\n");
    UltraCanvasNodeDiagram d("g", 0, 0, 400, 300);
    std::vector<std::string> ids;
    for (int i = 0; i < 25; ++i) {
        NodeDiagramNode n = Card("p" + std::to_string(i), (i % 4) * 30.0, (i % 3) * 12.0);
        if (i == 7) n.isPinned = true;
        d.AddNode(n);
        ids.push_back(n.id);
    }
    const double pinnedX = d.GetNode("p7")->x, pinnedY = d.GetNode("p7")->y;
    d.ResolveOverlaps(8.0, 1);   // one pass cannot clear 25 piled cards: the placement takes over
    CHECK(Overlaps(d, ids, 8.0) == 0, "no two cards closer than the margin after the fallback");
    CHECK(d.GetNode("p7")->x == pinnedX && d.GetNode("p7")->y == pinnedY, "the pinned card did not move");
}

static void TestForceLayoutWideNodes() {
    std::printf("\n[force layout, wide nodes, small element]\n");
    // The element is smaller than the graph: the old pass clamped the nodes
    // back into it and left them overlapping.
    UltraCanvasNodeDiagram d("f", 0, 0, 300, 200);
    std::vector<std::string> ids;
    d.AddNode(Card("hub", 0, 0));
    ids.push_back("hub");
    for (int i = 0; i < 30; ++i) {
        const std::string id = "leaf" + std::to_string(i);
        d.AddNode(Card(id, 0, 0));
        d.AddLink("l" + std::to_string(i), "hub", id);
        ids.push_back(id);
    }
    d.SetLayout(NodeDiagramLayout::ForceDirected);
    d.RunLayout();
    CHECK(Overlaps(d, ids) == 0, "no card overlaps another after the force layout");
}

static void TestCardJson() {
    std::printf("\n[card fields in JSON]\n");
    UltraCanvasNodeDiagram d("j", 0, 0, 400, 300);
    NodeDiagramNode n = Card("card", 10, 20);
    n.iconPath = "media/icons/smarthome/light.svg";
    n.iconBackground = Color(245, 166, 35, 255);
    n.sublabel = "Living room";
    n.cornerRadius = 10.0;
    n.shadow = true;
    d.AddNode(n);
    d.AddNode(Card("plain", 300, 20));
    const std::string json = d.ToJson();

    UltraCanvasNodeDiagram back("k", 0, 0, 400, 300);
    CHECK(back.FromJson(json), "FromJson reads it");
    const NodeDiagramNode* c = back.GetNode("card");
    CHECK(c && c->iconPath == n.iconPath, "iconPath round-trips");
    CHECK(c && c->iconBackground.r == 245 && c->iconBackground.g == 166, "iconBackground round-trips");
    CHECK(c && c->sublabel == "Living room", "sublabel round-trips");
    CHECK(c && c->cornerRadius == 10.0 && c->shadow, "cornerRadius and shadow round-trip");
    const NodeDiagramNode* p = back.GetNode("plain");
    CHECK(p && p->iconPath.empty() && p->cornerRadius < 0.0 && !p->shadow, "a plain node stays plain");
}

static void TestDetailZoom() {
    std::printf("\n[level of detail]\n");
    UltraCanvasNodeDiagram d("z", 0, 0, 400, 300);
    CHECK(!d.IsShowingIconsOnly(), "off by default");
    d.SetDetailZoom(0.7);
    d.SetZoomLevel(0.5);
    CHECK(d.IsShowingIconsOnly(), "icons only below the detail zoom");
    d.SetZoomLevel(1.0);
    CHECK(!d.IsShowingIconsOnly(), "full cards at or above it");
}

// ---- 2.4.0: labels fit their nodes --------------------------------------

// The demo's organization network in miniature: hubs with long names, leaves
// with names wider than a degree-1 node, sized by degree.
static std::vector<std::string> BuildOrg(UltraCanvasNodeDiagram& d) {
    std::vector<std::string> ids;
    struct Dept { const char* hub; const char* hubLabel; int members; };
    const Dept depts[] = {{"sales", "Sales Support", 8}, {"care", "Quality Control", 3},
                          {"bsc", "Solution Center", 9}, {"exec", "Director", 1}};
    int linkNo = 0;
    for (const Dept& dept : depts) {
        NodeDiagramNode hub(dept.hub, dept.hubLabel);
        hub.shape = NodeShape::Square;
        hub.width = hub.height = 46.0;
        hub.fontSize = 10.0;
        d.AddNode(hub);
        ids.push_back(dept.hub);
        std::vector<std::string> members{dept.hub};
        for (int i = 0; i < dept.members; ++i) {
            const std::string id = std::string(dept.hub) + "_m" + std::to_string(i);
            NodeDiagramNode n(id, "Analyst " + std::to_string(i + 1));
            n.shape = NodeShape::Square;
            n.width = n.height = 34.0;
            n.fontSize = 9.0;
            d.AddNode(n);
            d.AddLink("l" + std::to_string(++linkNo), dept.hub, id);
            ids.push_back(id);
            members.push_back(id);
        }
        d.AddGroup(std::string("g_") + dept.hub, dept.hubLabel, members, Color(0, 0, 0, 255));
    }
    d.AddLink("x1", "exec", "sales");
    d.AddLink("x2", "exec", "bsc");
    d.AddLink("x3", "care", "bsc");
    NodeDiagramSizing sizing;
    sizing.mode = NodeSizeMode::ByDegree;
    sizing.baseSize = 20.0;
    sizing.minSize = 26.0;
    sizing.maxSize = 74.0;
    d.SetNodeSizing(sizing);
    d.SetGroupCohesion(0.14);
    return ids;
}

// Every node at least as large as its label needs at `fontSize`.
static int TooSmallForLabel(const UltraCanvasNodeDiagram& d, const std::vector<std::string>& ids,
                            double fontSizeOrZero) {
    int count = 0;
    for (const std::string& id : ids) {
        const NodeDiagramNode* n = d.GetNode(id);
        double w = 0.0, h = 0.0;
        const double fontSize = fontSizeOrZero > 0.0 ? fontSizeOrZero : n->fontSize;
        d.MinimumNodeSizeForLabel(n->label, fontSize, n->shape, w, h);
        if (n->width + 0.5 < w || n->height + 0.5 < h) {
            ++count;
            if (std::getenv("NODE_OVERLAP_DEBUG"))
                std::printf("    %s is %.0fx%.0f, needs %.0fx%.0f\n", id.c_str(), n->width, n->height, w, h);
        }
    }
    return count;
}

static void TestMinimumNodeSize() {
    std::printf("\n[MinimumNodeSizeForLabel]\n");
    UltraCanvasNodeDiagram d("m", 0, 0, 400, 300);
    double rw = 0, rh = 0, sw = 0, sh = 0, cw = 0, ch = 0, dw = 0, dh = 0, tw = 0, th = 0;
    d.MinimumNodeSizeForLabel("Analyst 5", 9.0, NodeShape::Rectangle, rw, rh);
    d.MinimumNodeSizeForLabel("Analyst 5", 9.0, NodeShape::Square, sw, sh);
    d.MinimumNodeSizeForLabel("Analyst 5", 9.0, NodeShape::Circle, cw, ch);
    d.MinimumNodeSizeForLabel("Analyst 5", 9.0, NodeShape::Diamond, dw, dh);
    d.MinimumNodeSizeForLabel("Analyst 5", 9.0, NodeShape::Triangle, tw, th);
    int textW = 0, textH = 0;
    d.MeasureLabel("Analyst 5", 9.0, textW, textH);
    CHECK(rw > textW && rh > textH && rh < rw, "a rectangle is the text plus padding, wider than tall");
    CHECK(sw == sh && sw == rw, "a square is the rectangle's longer side");
    CHECK(cw == ch && cw > sw, "a circle needs the box's diagonal");
    CHECK(dw == dh && dw > cw, "a diamond needs more than a circle");
    CHECK(tw == th && tw > dw, "a triangle needs the most");
    double ew = 0, eh = 0;
    d.MinimumNodeSizeForLabel("", 9.0, NodeShape::Square, ew, eh);
    CHECK(ew > 0.0 && ew < sw, "an empty label needs only the paddings and a line's height");
}

static void TestGrowNode() {
    std::printf("\n[GrowNode: the node grows for its label]\n");
    UltraCanvasNodeDiagram d("g", 0, 0, 800, 600);
    d.SetLabelFit(NodeLabelFit::Overflow);
    const std::vector<std::string> ids = BuildOrg(d);
    CHECK(TooSmallForLabel(d, ids, 0.0) > 0, "degree sizing alone leaves leaves smaller than their names");
    d.SetLabelFit(NodeLabelFit::GrowNode);
    CHECK(d.GetLabelFit() == NodeLabelFit::GrowNode, "the mode is set");
    CHECK(TooSmallForLabel(d, ids, 0.0) == 0, "switching the fit grows them at once");
    CHECK(Overlaps(d, ids) == 0, "apart");
    d.SetLayout(NodeDiagramLayout::ForceDirected);
    d.RunLayout();
    CHECK(TooSmallForLabel(d, ids, 0.0) == 0, "after the layout every node holds its label at its own font size");
    CHECK(Overlaps(d, ids) == 0, "and no two overlap");
    const NodeDiagramNode* hub = d.GetNode("sales");
    const NodeDiagramNode* leaf = d.GetNode("sales_m1");
    CHECK(hub->width > leaf->width, "a hub is still larger than a leaf");
    CHECK(leaf->width == leaf->height, "a grown square is still square");
    CHECK(d.GetNodeLabelFontSize("sales_m1") == 9.0, "the font is the node's own");
    CHECK(d.FitNodesToLabels() == 0, "nothing left to grow");

    // A label edited to something longer grows its node at the next fit.
    const double before = leaf->width;
    d.UpdateNodeLabel("sales_m1", "Senior Analyst, Northern Region");
    CHECK(d.FitNodesToLabels() == 1, "the renamed node grows");
    CHECK(d.GetNode("sales_m1")->width > before, "wider than before");
    CHECK(Overlaps(d, ids) == 0, "its neighbours moved aside");

    // Shapes that hold less text grow more.
    for (const std::string& id : ids) d.SetNodeShape(id, NodeShape::Diamond);
    CHECK(d.FitNodesToLabels() > 0, "diamonds need more room than squares");
    CHECK(TooSmallForLabel(d, ids, 0.0) == 0 && Overlaps(d, ids) == 0, "and get it, apart");
}

static void TestShrinkFont() {
    std::printf("\n[ShrinkFont: the font shrinks for its node]\n");
    UltraCanvasNodeDiagram d("s", 0, 0, 800, 600);
    d.SetLabelFit(NodeLabelFit::ShrinkFont);
    NodeDiagramNode n("qc", "Quality Control");
    n.shape = NodeShape::Square;
    n.width = n.height = 60.0;
    n.fontSize = 11.0;
    d.AddNode(n);
    d.AddNode("short", "QC", 200, 0, 60.0);
    CHECK(d.FitNodesToLabels() == 0, "the nodes do not grow in this mode");
    CHECK(d.GetNode("qc")->width == 60.0, "the authored size stays");
    const double fitted = d.GetNodeLabelFontSize("qc");
    CHECK(fitted < 11.0 && fitted >= 7.0, "the long name is drawn smaller, not below the minimum");
    CHECK(d.GetNodeLabelFontSize("short") == 10.0, "a name that fits keeps its size (the simple API's 10)");

    NodeDiagramLabelFit cfg = d.GetLabelFitConfig();
    cfg.minFontSize = 9.0;
    d.SetLabelFitConfig(cfg);
    CHECK(d.GetNodeLabelFontSize("qc") >= 9.0, "the minimum is honoured");
}

static void TestShrinkThenGrowDefault() {
    std::printf("\n[ShrinkThenGrow, the default]\n");
    UltraCanvasNodeDiagram d("t", 0, 0, 800, 600);
    CHECK(d.GetLabelFit() == NodeLabelFit::ShrinkThenGrow, "the default shrinks first and grows the rest");
    const std::vector<std::string> ids = BuildOrg(d);
    d.SetLayout(NodeDiagramLayout::ForceDirected);
    d.RunLayout();
    CHECK(TooSmallForLabel(d, ids, 7.0) == 0, "every node holds its label at the minimum font size");
    CHECK(Overlaps(d, ids) == 0, "no two overlap");
    const double leafFont = d.GetNodeLabelFontSize("sales_m1");
    CHECK(leafFont >= 7.0 && leafFont <= 9.0, "a leaf's name is drawn between the minimum and its own size");

    d.SetLabelFit(NodeLabelFit::Overflow);
    CHECK(d.GetNodeLabelFontSize("sales_m1") == 9.0, "Overflow draws at the node's own size");
    CHECK(TooSmallForLabel(d, ids, 0.0) > 0, "and leaves the nodes as the data sized them");
}

static void TestOtherLayoutsSeparate() {
    std::printf("\n[circular, grid and hierarchical layouts end apart]\n");
    const NodeDiagramLayout layouts[] = {NodeDiagramLayout::Circular, NodeDiagramLayout::Grid,
                                         NodeDiagramLayout::Hierarchical};
    for (NodeDiagramLayout layout : layouts) {
        UltraCanvasNodeDiagram d("o", 0, 0, 300, 200);
        std::vector<std::string> ids;
        d.AddNode(Card("root", 0, 0));
        ids.push_back("root");
        for (int i = 0; i < 12; ++i) {
            const std::string id = "c" + std::to_string(i);
            d.AddNode(Card(id, 0, 0));
            d.AddLink("l" + std::to_string(i), "root", id);
            ids.push_back(id);
        }
        d.SetLayout(layout);
        d.RunLayout();
        CHECK(Overlaps(d, ids) == 0, "no two cards overlap after the layout");
    }
}

static void TestLabelFitJson() {
    std::printf("\n[label fit in JSON]\n");
    UltraCanvasNodeDiagram d("j", 0, 0, 400, 300);
    NodeDiagramLabelFit cfg;
    cfg.mode = NodeLabelFit::GrowNode;
    cfg.minFontSize = 8.0;
    cfg.paddingX = 10.0;
    cfg.paddingY = 5.0;
    d.SetLabelFitConfig(cfg);
    d.AddNode("a", "A", 0, 0);
    UltraCanvasNodeDiagram back("k", 0, 0, 400, 300);
    CHECK(back.FromJson(d.ToJson()), "FromJson reads it");
    const NodeDiagramLabelFit got = back.GetLabelFitConfig();
    CHECK(got.mode == NodeLabelFit::GrowNode, "the mode round-trips");
    CHECK(got.minFontSize == 8.0 && got.paddingX == 10.0 && got.paddingY == 5.0, "and the numbers");
}

int main() {
    std::printf("NodeDiagramOverlapTest\n");
    TestResolveOverlaps();
    TestFallbackPlacement();
    TestForceLayoutWideNodes();
    TestCardJson();
    TestDetailZoom();
    TestMinimumNodeSize();
    TestGrowNode();
    TestShrinkFont();
    TestShrinkThenGrowDefault();
    TestOtherLayoutsSeparate();
    TestLabelFitJson();
    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
