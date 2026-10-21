// Tests/NodeDiagramOverlapTest.cpp
// UltraCanvasNodeDiagram 2.3.0: no two nodes overlap after ResolveOverlaps()
// or after the force-directed layout, even for wide card nodes in a small
// element; pinned nodes stay put; the card fields (icon, sublabel, corner,
// shadow) survive ToJson / FromJson. Headless: no window, no rendering.
//
// Version: 1.0.0
// Last Modified: 2026-10-21
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

int main() {
    std::printf("NodeDiagramOverlapTest\n");
    TestResolveOverlaps();
    TestFallbackPlacement();
    TestForceLayoutWideNodes();
    TestCardJson();
    std::printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
