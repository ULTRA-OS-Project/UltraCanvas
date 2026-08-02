// Tests/ClassLayoutTest.cpp
// Unit tests for the class-diagram layout engine: generalization ranking,
// direction handling, pinned nodes, overlap freedom, grid and tree layouts.
//
// Exercises the real UltraCanvasClassLayout with no UI stack — the layout
// never measures text, so it needs no render context.
//
// Version: 1.0.0
// Last Modified: 2026-08-02
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasClassLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }           \
    } while (0)

static const ClassLayoutNode* Find(const std::vector<ClassLayoutNode>& nodes,
                                   const std::string& id) {
    for (const auto& node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

// True if any two boxes overlap (proposal quality gate Q2).
static bool AnyOverlap(const std::vector<ClassLayoutNode>& nodes) {
    for (size_t i = 0; i < nodes.size(); ++i) {
        for (size_t j = i + 1; j < nodes.size(); ++j) {
            const ClassLayoutNode& a = nodes[i];
            const ClassLayoutNode& b = nodes[j];
            bool separated = a.x + a.width <= b.x + 1e-9 || b.x + b.width <= a.x + 1e-9 ||
                             a.y + a.height <= b.y + 1e-9 || b.y + b.height <= a.y + 1e-9;
            if (!separated) return true;
        }
    }
    return false;
}

// A small inheritance hierarchy plus one association.
static void MakeHierarchy(std::vector<ClassLayoutNode>& nodes,
                          std::vector<ClassLayoutEdge>& edges) {
    nodes = {
        ClassLayoutNode("Account", 180, 90),
        ClassLayoutNode("Checking", 160, 70),
        ClassLayoutNode("Savings", 160, 70),
        ClassLayoutNode("Customer", 200, 140),
        ClassLayoutNode("Premium", 170, 60),
    };
    edges = {
        ClassLayoutEdge("Checking", "Account", true),
        ClassLayoutEdge("Savings", "Account", true),
        ClassLayoutEdge("Premium", "Checking", true),
        ClassLayoutEdge("Customer", "Account", false),  // association, not hierarchy
    };
}

// =============================================================================

static void TestRanking() {
    std::printf("Generalization ranking\n");

    std::vector<ClassLayoutNode> nodes;
    std::vector<ClassLayoutEdge> edges;
    MakeHierarchy(nodes, edges);

    std::vector<int> rank = UltraCanvasClassLayout::ComputeRanks(nodes, edges);
    CHECK(rank.size() == nodes.size(), "one rank per node");
    CHECK(rank[0] == 0, "a class with no parent is rank 0");
    CHECK(rank[1] == 1 && rank[2] == 1, "direct children are rank 1");
    CHECK(rank[3] == 0, "an association does not create a rank");
    CHECK(rank[4] == 2, "a grandchild is rank 2");
}

static void TestRankingHandlesCycles() {
    std::printf("Ranking survives an inheritance cycle\n");

    std::vector<ClassLayoutNode> nodes = {
        ClassLayoutNode("A", 100, 50),
        ClassLayoutNode("B", 100, 50),
        ClassLayoutNode("C", 100, 50),
    };
    std::vector<ClassLayoutEdge> edges = {
        ClassLayoutEdge("A", "B", true),
        ClassLayoutEdge("B", "C", true),
        ClassLayoutEdge("C", "A", true),  // cycle
    };

    std::vector<int> rank = UltraCanvasClassLayout::ComputeRanks(nodes, edges);
    CHECK(rank.size() == 3, "ranking terminates on a cycle");
    for (int value : rank) {
        CHECK(value >= 0 && value < 3, "cycle ranks stay in range");
    }

    ClassLayoutOptions options;
    Rect2Dd bounds = UltraCanvasClassLayout::Apply(nodes, edges, options);
    CHECK(bounds.width > 0 && bounds.height > 0, "a cyclic model still lays out");
    CHECK(!AnyOverlap(nodes), "no overlap even with a cycle");
}

static void TestLayeredLayout() {
    std::printf("Layered layout\n");

    std::vector<ClassLayoutNode> nodes;
    std::vector<ClassLayoutEdge> edges;
    MakeHierarchy(nodes, edges);

    ClassLayoutOptions options;
    options.kind = ClassLayoutKind::Layered;
    options.direction = ClassLayoutDirection::TopToBottom;
    Rect2Dd bounds = UltraCanvasClassLayout::Apply(nodes, edges, options);

    const ClassLayoutNode* account = Find(nodes, "Account");
    const ClassLayoutNode* checking = Find(nodes, "Checking");
    const ClassLayoutNode* premium = Find(nodes, "Premium");

    CHECK(account && checking && premium, "all nodes present after layout");
    CHECK(account->y < checking->y, "parent sits above its child");
    CHECK(checking->y < premium->y, "grandchild sits below its parent");
    CHECK(!AnyOverlap(nodes), "no two boxes overlap");
    CHECK(bounds.width > 0 && bounds.height > 0, "bounding box is non-empty");
    CHECK(bounds.x >= options.marginX - 1e-9 && bounds.y >= options.marginY - 1e-9,
          "layout honours the margin");

    // Rank separation is respected between the first two ranks.
    double gap = checking->y - (account->y + account->height);
    CHECK(gap >= options.rankSeparation - 1e-6, "rank separation is respected");
}

static void TestDirections() {
    std::printf("Layout directions\n");

    auto layoutIn = [](ClassLayoutDirection direction) {
        std::vector<ClassLayoutNode> nodes;
        std::vector<ClassLayoutEdge> edges;
        MakeHierarchy(nodes, edges);
        ClassLayoutOptions options;
        options.direction = direction;
        UltraCanvasClassLayout::Apply(nodes, edges, options);
        return nodes;
    };

    auto topToBottom = layoutIn(ClassLayoutDirection::TopToBottom);
    CHECK(Find(topToBottom, "Account")->y < Find(topToBottom, "Checking")->y,
          "TopToBottom puts the parent above");

    auto bottomToTop = layoutIn(ClassLayoutDirection::BottomToTop);
    CHECK(Find(bottomToTop, "Account")->y > Find(bottomToTop, "Checking")->y,
          "BottomToTop puts the parent below");

    auto leftToRight = layoutIn(ClassLayoutDirection::LeftToRight);
    CHECK(Find(leftToRight, "Account")->x < Find(leftToRight, "Checking")->x,
          "LeftToRight puts the parent to the left");

    auto rightToLeft = layoutIn(ClassLayoutDirection::RightToLeft);
    CHECK(Find(rightToLeft, "Account")->x > Find(rightToLeft, "Checking")->x,
          "RightToLeft puts the parent to the right");

    CHECK(!AnyOverlap(leftToRight), "no overlap in a horizontal flow");
}

static void TestTreeLayout() {
    std::printf("Tree layout\n");

    std::vector<ClassLayoutNode> nodes = {
        ClassLayoutNode("Root", 160, 60),
        ClassLayoutNode("A", 140, 60),
        ClassLayoutNode("B", 140, 60),
        ClassLayoutNode("C", 140, 60),
    };
    std::vector<ClassLayoutEdge> edges = {
        ClassLayoutEdge("A", "Root", true),
        ClassLayoutEdge("B", "Root", true),
        ClassLayoutEdge("C", "Root", true),
    };

    ClassLayoutOptions options;
    options.kind = ClassLayoutKind::Tree;
    UltraCanvasClassLayout::Apply(nodes, edges, options);

    const ClassLayoutNode* root = Find(nodes, "Root");
    double rootCentre = root->x + root->width * 0.5;

    double childMin = 1e9, childMax = -1e9;
    for (const char* id : {"A", "B", "C"}) {
        const ClassLayoutNode* child = Find(nodes, id);
        childMin = std::min(childMin, child->x);
        childMax = std::max(childMax, child->x + child->width);
    }
    double childCentre = (childMin + childMax) * 0.5;

    CHECK(std::abs(rootCentre - childCentre) < 1.0,
          "the parent is centred over its children");
    CHECK(!AnyOverlap(nodes), "no overlap in a tree layout");
}

static void TestGridLayout() {
    std::printf("Grid layout\n");

    std::vector<ClassLayoutNode> nodes;
    for (int i = 0; i < 7; ++i) {
        nodes.push_back(ClassLayoutNode("n" + std::to_string(i), 120 + i * 10, 80));
    }

    ClassLayoutOptions options;
    options.kind = ClassLayoutKind::Grid;
    options.gridColumns = 3;
    UltraCanvasClassLayout::Apply(nodes, {}, options);

    CHECK(!AnyOverlap(nodes), "no overlap in a grid");
    CHECK(std::abs(nodes[0].y - nodes[1].y) < 1e-9 &&
          std::abs(nodes[1].y - nodes[2].y) < 1e-9,
          "the first three nodes share a row");
    CHECK(nodes[3].y > nodes[0].y, "the fourth node starts a new row");
    CHECK(std::abs(nodes[0].x - nodes[3].x) < 1e-9, "columns line up");
}

static void TestPinnedNodes() {
    std::printf("Pinned nodes\n");

    std::vector<ClassLayoutNode> nodes;
    std::vector<ClassLayoutEdge> edges;
    MakeHierarchy(nodes, edges);

    nodes[3].pinned = true;   // Customer
    nodes[3].x = 999.0;
    nodes[3].y = 777.0;

    ClassLayoutOptions options;
    UltraCanvasClassLayout::Apply(nodes, edges, options);

    const ClassLayoutNode* customer = Find(nodes, "Customer");
    CHECK(std::abs(customer->x - 999.0) < 1e-9 && std::abs(customer->y - 777.0) < 1e-9,
          "a pinned node keeps the position it came in with");
    CHECK(Find(nodes, "Account")->x != 999.0, "unpinned nodes are still placed");
}

static void TestManualAndEmpty() {
    std::printf("Manual layout and edge cases\n");

    std::vector<ClassLayoutNode> nodes = {ClassLayoutNode("only", 100, 50)};
    nodes[0].x = 12.0;
    nodes[0].y = 34.0;

    ClassLayoutOptions options;
    options.kind = ClassLayoutKind::Manual;
    Rect2Dd bounds = UltraCanvasClassLayout::Apply(nodes, {}, options);
    CHECK(std::abs(nodes[0].x - 12.0) < 1e-9, "Manual leaves positions alone");
    CHECK(std::abs(bounds.width - 100.0) < 1e-9 && std::abs(bounds.height - 50.0) < 1e-9,
          "Manual still reports the bounding box");

    std::vector<ClassLayoutNode> empty;
    Rect2Dd emptyBounds = UltraCanvasClassLayout::Apply(empty, {}, options);
    CHECK(emptyBounds.width == 0.0 && emptyBounds.height == 0.0,
          "an empty model yields an empty bounding box");

    // An edge naming a node that does not exist must not crash or misplace.
    std::vector<ClassLayoutNode> two = {ClassLayoutNode("a", 80, 40),
                                        ClassLayoutNode("b", 80, 40)};
    std::vector<ClassLayoutEdge> dangling = {ClassLayoutEdge("a", "ghost", true)};
    ClassLayoutOptions layered;
    UltraCanvasClassLayout::Apply(two, dangling, layered);
    CHECK(!AnyOverlap(two), "a dangling edge is ignored safely");
}

// Property test: whatever the shape of the model, nothing overlaps.
static void TestNoOverlapProperty() {
    std::printf("No-overlap property over random models\n");

    std::mt19937 rng(20260802);
    int trials = 0, overlaps = 0;

    for (int trial = 0; trial < 400; ++trial) {
        size_t count = 2 + rng() % 18;
        std::vector<ClassLayoutNode> nodes;
        for (size_t i = 0; i < count; ++i) {
            nodes.push_back(ClassLayoutNode("n" + std::to_string(i),
                                            80.0 + rng() % 200,
                                            40.0 + rng() % 160));
        }

        std::vector<ClassLayoutEdge> edges;
        size_t edgeCount = rng() % (count * 2);
        for (size_t i = 0; i < edgeCount; ++i) {
            size_t source = rng() % count;
            size_t target = rng() % count;
            edges.emplace_back("n" + std::to_string(source),
                               "n" + std::to_string(target),
                               (rng() % 2) == 0);
        }

        ClassLayoutOptions options;
        switch (rng() % 3) {
            case 0: options.kind = ClassLayoutKind::Layered; break;
            case 1: options.kind = ClassLayoutKind::Tree; break;
            default: options.kind = ClassLayoutKind::Grid; break;
        }
        options.direction = static_cast<ClassLayoutDirection>(rng() % 4);

        UltraCanvasClassLayout::Apply(nodes, edges, options);
        ++trials;
        if (AnyOverlap(nodes)) ++overlaps;
    }

    std::printf("  (%d random models laid out)\n", trials);
    CHECK(overlaps == 0, "no model produced overlapping boxes");
}

// =============================================================================

int main() {
    std::printf("=== UltraCanvasClassLayout tests ===\n\n");

    TestRanking();
    TestRankingHandlesCycles();
    TestLayeredLayout();
    TestDirections();
    TestTreeLayout();
    TestGridLayout();
    TestPinnedNodes();
    TestManualAndEmpty();
    TestNoOverlapProperty();

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASSED" : "FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
