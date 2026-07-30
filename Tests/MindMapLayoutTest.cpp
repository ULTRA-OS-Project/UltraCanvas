// Tests/MindMapLayoutTest.cpp
// Unit tests for the mind map topic model and layout engine.
//
// The layout engine is pure geometry over the model - no render context, no UI
// stack - so the whole thing is exercised headlessly here. The property that
// matters most is L9: sibling subtrees must never overlap, at any depth, with
// variable node sizes.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMindMapLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

static bool Near(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) <= eps;
}

// Deterministic measurement so expectations don't depend on font metrics:
// 8px per character, 18px tall, wrapped at maxWidth.
static Size2Dd FixedMeasure(const std::string& text, double, bool, double maxWidth) {
    double width = static_cast<double>(text.size()) * 8.0;
    double height = 18.0;
    if (maxWidth > 0.0 && width > maxWidth) {
        int lines = static_cast<int>(std::ceil(width / maxWidth));
        width = maxWidth;
        height = 18.0 * lines;
    }
    return Size2Dd(width, height);
}

static bool RectsOverlap(const Rect2Dd& a, const Rect2Dd& b) {
    // Touching edges are fine; only true interior overlap counts.
    const double eps = 1e-9;
    return (a.x + a.width  > b.x + eps) && (b.x + b.width  > a.x + eps) &&
           (a.y + a.height > b.y + eps) && (b.y + b.height > a.y + eps);
}

// Every laid-out box against every other. O(n^2), fine at test sizes.
static int CountOverlaps(const MindMapLayoutResult& result) {
    std::vector<Rect2Dd> boxes;
    boxes.reserve(result.bounds.size());
    for (const auto& [id, box] : result.bounds) boxes.push_back(box);

    int overlaps = 0;
    for (size_t i = 0; i < boxes.size(); ++i) {
        for (size_t j = i + 1; j < boxes.size(); ++j) {
            if (RectsOverlap(boxes[i], boxes[j])) ++overlaps;
        }
    }
    return overlaps;
}

static MindMapLayoutResult Run(MindMapModel& model, const MindMapLayoutOptions& options) {
    MindMapLayoutResult result;
    UltraCanvasMindMapLayout::ComputeLayout(model, options, FixedMeasure, nullptr, result);
    return result;
}

// =============================================================================

static void TestModelBasics() {
    std::printf("\n[model basics]\n");

    MindMapModel model;
    CHECK(model.IsEmpty(), "new model is empty");

    std::string root = model.SetCentralTopic("Central");
    CHECK(!root.empty() && model.GetRootId() == root, "central topic becomes the root");
    CHECK(model.GetTopicCount() == 1, "one topic after setting the centre");

    // Setting it again renames rather than creating a second root.
    std::string again = model.SetCentralTopic("Renamed");
    CHECK(again == root && model.GetTopicCount() == 1, "re-setting the centre renames it");
    CHECK(model.GetTopic(root)->text == "Renamed", "central topic text updated");

    std::string a = model.AddTopic(root, "A");
    std::string b = model.AddTopic(root, "B");
    std::string a1 = model.AddTopic(a, "A1");
    CHECK(model.GetTopicCount() == 4, "children added");
    CHECK(model.GetTopic(a)->parentId == root, "child records its parent");
    CHECK(model.GetTopic(root)->childIds.size() == 2, "root has two children");

    CHECK(model.AddTopic("nonexistent", "orphan").empty(),
          "adding under an unknown parent fails");

    CHECK(model.GetDepth(root) == 0 && model.GetDepth(a) == 1 && model.GetDepth(a1) == 2,
          "depths follow the tree");
    CHECK(model.GetPath(a1).size() == 3 && model.GetPath(a1).front() == root,
          "GetPath runs root-first and is inclusive");
    CHECK(model.CountDescendants(root) == 3, "descendant count covers the whole subtree");
    CHECK(model.IsDescendantOf(a1, root) && !model.IsDescendantOf(root, a1),
          "ancestry test is directional");
    CHECK(!model.IsDescendantOf(a, a), "a topic is not its own descendant");
    CHECK(model.GetSubtreeIds(a).size() == 2, "subtree ids include the topic itself");

    (void)b;
}

static void TestRemoveAndMove() {
    std::printf("\n[remove and reparent]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("Root");
    std::string a = model.AddTopic(root, "A");
    std::string b = model.AddTopic(root, "B");
    std::string a1 = model.AddTopic(a, "A1");
    std::string a2 = model.AddTopic(a, "A2");

    // Reparent a subtree.
    CHECK(model.MoveTopic(a1, b), "reparent succeeds");
    CHECK(model.GetTopic(a1)->parentId == b, "moved topic records the new parent");
    CHECK(model.GetTopic(a)->childIds.size() == 1, "old parent lost the child");
    CHECK(model.GetTopic(b)->childIds.size() == 1, "new parent gained the child");

    // Cycles must be refused.
    CHECK(!model.MoveTopic(root, a), "the central topic cannot be reparented");
    CHECK(!model.MoveTopic(b, a1), "moving a topic under its own descendant is refused");
    CHECK(!model.MoveTopic(a, a), "moving a topic under itself is refused");

    // Sibling reordering.
    std::string c = model.AddTopic(root, "C");
    CHECK(model.GetTopic(root)->childIds[2] == c, "new sibling appended last");
    CHECK(model.MoveTopicToIndex(c, 0), "reorder succeeds");
    CHECK(model.GetTopic(root)->childIds[0] == c, "sibling moved to the front");
    CHECK(!model.MoveTopicToIndex(c, 0), "reordering to the same index reports no change");

    // Removal takes the whole subtree.
    std::string b1 = model.AddTopic(b, "B1");
    (void)b1;
    size_t before = model.GetTopicCount();
    std::vector<std::string> removed = model.RemoveTopic(b);
    CHECK(removed.size() == 3, "removing B takes B, A1 and B1");
    CHECK(model.GetTopicCount() == before - 3, "topic count drops by the subtree size");
    CHECK(model.GetTopic(b) == nullptr, "removed topic is gone");
    CHECK(std::find(model.GetTopic(root)->childIds.begin(),
                    model.GetTopic(root)->childIds.end(), b) ==
          model.GetTopic(root)->childIds.end(),
          "removed topic is unlinked from its parent");

    (void)a2;
}

static void TestRelationships() {
    std::printf("\n[relationships]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("Root");
    std::string a = model.AddTopic(root, "A");
    std::string b = model.AddTopic(root, "B");

    CHECK(model.AddRelationship(MindMapRelationship("r1", a, b)), "relationship added");
    CHECK(!model.AddRelationship(MindMapRelationship("r1", b, a)),
          "duplicate relationship id refused");
    CHECK(!model.AddRelationship(MindMapRelationship("r2", a, "missing")),
          "relationship to an unknown topic refused");
    CHECK(model.Relationships().size() == 1, "exactly one relationship stored");

    // Removing an endpoint must remove the relationship with it.
    model.RemoveTopic(b);
    CHECK(model.Relationships().empty(),
          "relationship dropped when an endpoint is removed");
}

static void TestOutlineAndOrdinals() {
    std::printf("\n[outline build and numbering]\n");

    MindMapModel model;
    model.BuildFromOutline({
        {0, "Root"},
        {1, "First"},
        {2, "First child"},
        {2, "Second child"},
        {1, "Second"},
        {3, "Over-indented"},   // clamped to one deeper than "Second"
    });

    CHECK(model.GetTopicCount() == 6, "all outline lines became topics");
    const MindMapTopic* root = model.GetTopic(model.GetRootId());
    CHECK(root && root->text == "Root", "level 0 became the central topic");
    CHECK(root && root->childIds.size() == 2, "two level-1 topics");

    const MindMapTopic* first = model.GetTopic(root->childIds[0]);
    CHECK(first && first->childIds.size() == 2, "level-2 topics attached to the right parent");

    const MindMapTopic* second = model.GetTopic(root->childIds[1]);
    CHECK(second && second->childIds.size() == 1,
          "an over-indented line is clamped to one level deeper");

    model.AssignOrdinals();
    CHECK(root->ordinal.empty(), "the central topic has no ordinal");
    CHECK(model.GetTopic(root->childIds[0])->ordinal == "1", "first branch is 1");
    CHECK(model.GetTopic(root->childIds[1])->ordinal == "2", "second branch is 2");
    CHECK(model.GetTopic(first->childIds[1])->ordinal == "1.2", "nested ordinal is 1.2");
}

static void TestBalancedLayout() {
    std::printf("\n[balanced layout]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("Centre");
    std::vector<std::string> branches;
    for (int i = 0; i < 4; ++i) {
        branches.push_back(model.AddTopic(root, "Branch " + std::to_string(i)));
        for (int j = 0; j < 3; ++j) {
            model.AddTopic(branches.back(), "Leaf " + std::to_string(i) + std::to_string(j));
        }
    }

    MindMapLayoutOptions options;
    options.structure = MindMapStructure::Balanced;
    options.balancePolicy = MindMapBalancePolicy::AlternateByIndex;
    MindMapLayoutResult result = Run(model, options);

    CHECK(result.bounds.size() == model.GetTopicCount(), "every topic is positioned");
    CHECK(CountOverlaps(result) == 0, "no two boxes overlap");

    Rect2Dd rootBox = result.Get(root);
    CHECK(Near(rootBox.x + rootBox.width * 0.5, 0.0) &&
          Near(rootBox.y + rootBox.height * 0.5, 0.0),
          "the central topic is centred on the origin");

    // Alternating policy: branches 0 and 2 right, 1 and 3 left.
    CHECK(model.GetTopic(branches[0])->resolvedSide == MindMapTopicSide::Right &&
          model.GetTopic(branches[1])->resolvedSide == MindMapTopicSide::Left &&
          model.GetTopic(branches[2])->resolvedSide == MindMapTopicSide::Right &&
          model.GetTopic(branches[3])->resolvedSide == MindMapTopicSide::Left,
          "AlternateByIndex splits branches right/left in turn");

    CHECK(result.Get(branches[0]).x > rootBox.x + rootBox.width,
          "a right-side branch sits clear to the right of the root");
    CHECK(result.Get(branches[1]).x + result.Get(branches[1]).width < rootBox.x,
          "a left-side branch sits clear to the left of the root");

    // Side is inherited by the whole subtree, and leaves continue outward.
    bool inherited = true, outward = true;
    for (const auto& leafId : model.GetTopic(branches[1])->childIds) {
        if (model.GetTopic(leafId)->resolvedSide != MindMapTopicSide::Left) inherited = false;
        if (result.Get(leafId).x >= result.Get(branches[1]).x) outward = false;
    }
    CHECK(inherited, "subtree inherits its branch's side");
    CHECK(outward, "left-side leaves continue growing leftward");

    // Branch index is recorded for the colour cascade.
    bool branchIndexed = true;
    for (size_t i = 0; i < branches.size(); ++i) {
        for (const auto& id : model.GetSubtreeIds(branches[i])) {
            if (model.GetTopic(id)->branchIndex != static_cast<int>(i)) branchIndexed = false;
        }
    }
    CHECK(branchIndexed, "every topic records its depth-1 branch index");
    CHECK(model.GetTopic(root)->branchIndex == -1, "the central topic has no branch index");
}

static void TestBalancePolicies() {
    std::printf("\n[balance policies]\n");

    auto build = [](MindMapModel& model, std::vector<std::string>& branches) {
        std::string root = model.SetCentralTopic("Centre");
        // Branch 0 is heavy (5 leaves), the rest are light (1 leaf each).
        for (int i = 0; i < 4; ++i) {
            branches.push_back(model.AddTopic(root, "B" + std::to_string(i)));
            int leaves = (i == 0) ? 5 : 1;
            for (int j = 0; j < leaves; ++j) {
                model.AddTopic(branches.back(), "L");
            }
        }
    };

    {
        MindMapModel model;
        std::vector<std::string> branches;
        build(model, branches);
        MindMapLayoutOptions options;
        options.balancePolicy = MindMapBalancePolicy::SplitInHalf;
        Run(model, options);
        CHECK(model.GetTopic(branches[0])->resolvedSide == MindMapTopicSide::Right &&
              model.GetTopic(branches[1])->resolvedSide == MindMapTopicSide::Right &&
              model.GetTopic(branches[2])->resolvedSide == MindMapTopicSide::Left &&
              model.GetTopic(branches[3])->resolvedSide == MindMapTopicSide::Left,
              "SplitInHalf puts the first half right");
    }
    {
        MindMapModel model;
        std::vector<std::string> branches;
        build(model, branches);
        MindMapLayoutOptions options;
        options.balancePolicy = MindMapBalancePolicy::MinimiseSubtreeWeight;
        Run(model, options);
        // The heavy branch goes right first; the three light ones should then
        // all land left to balance the leaf load (5 vs 3).
        CHECK(model.GetTopic(branches[0])->resolvedSide == MindMapTopicSide::Right,
              "MinimiseSubtreeWeight places the first branch right");
        int leftCount = 0;
        for (size_t i = 1; i < branches.size(); ++i) {
            if (model.GetTopic(branches[i])->resolvedSide == MindMapTopicSide::Left) ++leftCount;
        }
        CHECK(leftCount == 3, "the lighter branches balance against the heavy one");
    }
    {
        // An explicit side overrides the policy.
        MindMapModel model;
        std::vector<std::string> branches;
        build(model, branches);
        model.GetTopic(branches[0])->side = MindMapTopicSide::Left;
        MindMapLayoutOptions options;
        options.balancePolicy = MindMapBalancePolicy::AlternateByIndex;
        Run(model, options);
        CHECK(model.GetTopic(branches[0])->resolvedSide == MindMapTopicSide::Left,
              "an explicit side wins over the balance policy");
    }
}

static void TestLogicStructures() {
    std::printf("\n[logic chart structures]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("Root");
    std::vector<std::string> branches;
    for (int i = 0; i < 5; ++i) branches.push_back(model.AddTopic(root, "B"));

    MindMapLayoutOptions options;
    options.structure = MindMapStructure::LogicRight;
    MindMapLayoutResult result = Run(model, options);

    bool allRight = true;
    for (const auto& id : branches) {
        if (result.Get(id).x <= result.Get(root).x) allRight = false;
    }
    CHECK(allRight, "LogicRight puts every branch to the right");
    CHECK(CountOverlaps(result) == 0, "LogicRight produces no overlaps");

    options.structure = MindMapStructure::LogicLeft;
    result = Run(model, options);
    bool allLeft = true;
    for (const auto& id : branches) {
        if (result.Get(id).x >= result.Get(root).x) allLeft = false;
    }
    CHECK(allLeft, "LogicLeft puts every branch to the left");
    CHECK(CountOverlaps(result) == 0, "LogicLeft produces no overlaps");
}

static void TestOrgChartStructures() {
    std::printf("\n[org chart structures]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("CEO");
    std::vector<std::string> reports;
    for (int i = 0; i < 3; ++i) {
        reports.push_back(model.AddTopic(root, "Director " + std::to_string(i)));
        for (int j = 0; j < 2; ++j) model.AddTopic(reports.back(), "Manager");
    }

    MindMapLayoutOptions options;
    options.structure = MindMapStructure::OrgChartDown;
    MindMapLayoutResult result = Run(model, options);

    CHECK(CountOverlaps(result) == 0, "OrgChartDown produces no overlaps");
    bool below = true;
    for (const auto& id : reports) {
        if (result.Get(id).y <= result.Get(root).y) below = false;
    }
    CHECK(below, "OrgChartDown places children below the root");

    // Siblings are stacked horizontally and stay ordered.
    CHECK(result.Get(reports[0]).x < result.Get(reports[1]).x &&
          result.Get(reports[1]).x < result.Get(reports[2]).x,
          "OrgChartDown keeps siblings in order left to right");

    options.structure = MindMapStructure::OrgChartUp;
    result = Run(model, options);
    bool above = true;
    for (const auto& id : reports) {
        if (result.Get(id).y >= result.Get(root).y) above = false;
    }
    CHECK(above, "OrgChartUp places children above the root");
    CHECK(CountOverlaps(result) == 0, "OrgChartUp produces no overlaps");
}

static void TestRadialLayout() {
    std::printf("\n[radial layout]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("Hub");
    std::vector<std::string> branches;
    for (int i = 0; i < 8; ++i) branches.push_back(model.AddTopic(root, "Spoke"));

    MindMapLayoutOptions options;
    options.structure = MindMapStructure::Radial;
    MindMapLayoutResult result = Run(model, options);

    CHECK(CountOverlaps(result) == 0, "radial layout produces no overlaps");

    // Equal-weight branches must land on one ring at a uniform radius.
    double expectedRadius = options.radialRingGap;
    bool onRing = true;
    for (const auto& id : branches) {
        Rect2Dd box = result.Get(id);
        double cx = box.x + box.width * 0.5;
        double cy = box.y + box.height * 0.5;
        if (!Near(std::sqrt(cx * cx + cy * cy), expectedRadius, 1e-6)) onRing = false;
    }
    CHECK(onRing, "equal-weight branches sit on one ring of uniform radius");

    // They should also be spread out, not bunched: check angular spacing.
    std::vector<double> angles;
    for (const auto& id : branches) {
        Rect2Dd box = result.Get(id);
        angles.push_back(std::atan2(box.y + box.height * 0.5, box.x + box.width * 0.5));
    }
    std::sort(angles.begin(), angles.end());
    bool spread = true;
    for (size_t i = 1; i < angles.size(); ++i) {
        if (angles[i] - angles[i - 1] < 0.1) spread = false;
    }
    CHECK(spread, "branches are angularly separated");

    // Deeper generations move to an outer ring.
    std::string deep = model.AddTopic(branches[0], "Deep");
    result = Run(model, options);
    Rect2Dd deepBox = result.Get(deep);
    double dcx = deepBox.x + deepBox.width * 0.5;
    double dcy = deepBox.y + deepBox.height * 0.5;
    CHECK(std::sqrt(dcx * dcx + dcy * dcy) > expectedRadius + 1.0,
          "a depth-2 topic sits on an outer ring");
}

static void TestCollapse() {
    std::printf("\n[collapse]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("Root");
    std::string a = model.AddTopic(root, "A");
    std::string a1 = model.AddTopic(a, "A1");
    std::string a1x = model.AddTopic(a1, "A1x");
    std::string b = model.AddTopic(root, "B");

    MindMapLayoutOptions options;
    MindMapLayoutResult result = Run(model, options);
    CHECK(result.Has(a1) && result.Has(a1x), "descendants are laid out when expanded");

    model.GetTopic(a)->collapsed = true;
    result = Run(model, options);
    CHECK(result.Has(a), "a collapsed topic is still laid out");
    CHECK(!result.Has(a1) && !result.Has(a1x),
          "descendants of a collapsed topic are not laid out");
    CHECK(result.Has(b), "an unrelated branch is unaffected");

    CHECK(model.IsHiddenByCollapse(a1) && model.IsHiddenByCollapse(a1x),
          "IsHiddenByCollapse sees through the whole ancestor chain");
    CHECK(!model.IsHiddenByCollapse(a), "the collapsed topic itself is not hidden");
    CHECK(!model.IsHiddenByCollapse(b), "an unrelated topic is not hidden");

    // Collapsing frees the space its subtree occupied.
    model.GetTopic(a)->collapsed = false;
    MindMapLayoutResult expanded = Run(model, options);
    model.GetTopic(a)->collapsed = true;
    MindMapLayoutResult collapsed = Run(model, options);
    CHECK(collapsed.contentBounds.GetWidth() < expanded.contentBounds.GetWidth(),
          "collapsing shrinks the content bounds");
}

static void TestNoOverlapStress() {
    std::printf("\n[non-overlap stress]\n");

    // Deep, ragged, variable-width tree - the case that breaks naive layouts.
    MindMapModel model;
    std::string root = model.SetCentralTopic("Stress test root");

    int counter = 0;
    std::function<void(const std::string&, int)> grow =
        [&](const std::string& parentId, int depth) {
            if (depth > 5) return;
            // Ragged fan-out, and labels of very different lengths.
            int children = 2 + ((depth + counter) % 3);
            for (int i = 0; i < children; ++i) {
                std::string label(1 + ((counter * 7) % 24), 'x');
                std::string id = model.AddTopic(parentId, label);
                ++counter;
                grow(id, depth + 1);
            }
        };
    grow(root, 1);

    CHECK(model.GetTopicCount() > 100, "stress tree is large enough to be meaningful");

    for (auto structure : {MindMapStructure::Balanced,
                           MindMapStructure::LogicRight,
                           MindMapStructure::OrgChartDown}) {
        MindMapLayoutOptions options;
        options.structure = structure;
        MindMapLayoutResult result = Run(model, options);
        int overlaps = CountOverlaps(result);
        const char* name = (structure == MindMapStructure::Balanced) ? "Balanced"
                         : (structure == MindMapStructure::LogicRight) ? "LogicRight"
                         : "OrgChartDown";
        char msg[128];
        std::snprintf(msg, sizeof(msg),
                      "%s: %zu topics laid out with zero overlaps",
                      name, result.bounds.size());
        CHECK(overlaps == 0, msg);
    }
}

static void TestFloatingTopics() {
    std::printf("\n[floating topics]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("Root");
    model.AddTopic(root, "Branch");
    std::string note = model.AddFloatingTopic("Legend", 500.0, -300.0);

    MindMapLayoutOptions options;
    MindMapLayoutResult result = Run(model, options);

    CHECK(result.Has(note), "floating topic is laid out");
    CHECK(Near(result.Get(note).x, 500.0) && Near(result.Get(note).y, -300.0),
          "floating topic keeps the position it was given");
    CHECK(result.contentBounds.maxX >= 500.0,
          "content bounds include floating topics");

    // A floating topic is not part of the root's child list.
    CHECK(model.GetTopic(root)->childIds.size() == 1,
          "floating topic is not attached to the root");

    // Reparenting a floating topic clears its floating flag.
    CHECK(model.MoveTopic(note, root), "floating topic can be reparented");
    CHECK(!model.GetTopic(note)->floating, "reparenting clears the floating flag");
    CHECK(model.FloatingTopicIds().empty(), "floating list no longer tracks it");
}

static void TestSpacingAndSizing() {
    std::printf("\n[spacing and sizing]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("R");
    std::string a = model.AddTopic(root, "A");
    std::string b = model.AddTopic(root, "B");
    model.GetTopic(a)->side = MindMapTopicSide::Right;
    model.GetTopic(b)->side = MindMapTopicSide::Right;

    MindMapLayoutOptions options;
    options.siblingGap = 20.0;
    MindMapLayoutResult result = Run(model, options);

    Rect2Dd boxA = result.Get(a);
    Rect2Dd boxB = result.Get(b);
    // Whichever sibling is on top, the positive value is the gap between them.
    double gap = std::max(boxB.y - (boxA.y + boxA.height),
                          boxA.y - (boxB.y + boxB.height));
    CHECK(Near(gap, 20.0, 1e-6), "siblingGap is honoured exactly between siblings");

    // A larger rootGap must push branches further out.
    options.rootGap = 60.0;
    double nearX = Run(model, options).Get(a).x;
    options.rootGap = 200.0;
    double farX = Run(model, options).Get(a).x;
    CHECK(farX - nearX > 130.0, "rootGap pushes branches away from the centre");

    // Weight-driven sizing makes a heavy node bigger.
    options.sizeFromWeight = true;
    model.GetTopic(a)->weight = 4.0;
    result = Run(model, options);
    CHECK(result.Get(a).width > result.Get(b).width &&
          result.Get(a).height > result.Get(b).height,
          "sizeFromWeight enlarges a heavier topic");
}

static void TestEdgeCases() {
    std::printf("\n[edge cases]\n");

    // Empty model.
    {
        MindMapModel model;
        MindMapLayoutOptions options;
        MindMapLayoutResult result = Run(model, options);
        CHECK(result.bounds.empty(), "empty model lays out nothing");
        CHECK(!result.contentBounds.IsUsable(), "empty model has unusable content bounds");
    }
    // Root only.
    {
        MindMapModel model;
        std::string root = model.SetCentralTopic("Solo");
        MindMapLayoutResult result = Run(model, MindMapLayoutOptions());
        CHECK(result.bounds.size() == 1, "a lone central topic is laid out");
        CHECK(result.contentBounds.IsUsable(), "a lone topic still gives usable bounds");
        CHECK(result.Has(root), "the root is present");
    }
    // Empty label still gets a usable box.
    {
        MindMapModel model;
        std::string root = model.SetCentralTopic("");
        MindMapLayoutResult result = Run(model, MindMapLayoutOptions());
        CHECK(result.Get(root).height > 0.0, "an empty label still has height");
    }
    // Layout is repeatable: same input, same output.
    {
        MindMapModel model;
        model.BuildFromOutline({{0, "Root"}, {1, "A"}, {2, "A1"}, {1, "B"}, {1, "C"}});
        MindMapLayoutOptions options;
        MindMapLayoutResult first = Run(model, options);
        MindMapLayoutResult second = Run(model, options);
        bool identical = first.bounds.size() == second.bounds.size();
        for (const auto& [id, box] : first.bounds) {
            if (!second.Has(id)) { identical = false; break; }
            Rect2Dd other = second.Get(id);
            if (!Near(box.x, other.x) || !Near(box.y, other.y) ||
                !Near(box.width, other.width) || !Near(box.height, other.height)) {
                identical = false;
                break;
            }
        }
        CHECK(identical, "layout is deterministic across repeated runs");
    }
    // The built-in estimator is used when no measure function is supplied.
    {
        MindMapModel model;
        std::string root = model.SetCentralTopic("Measured without a callback");
        MindMapLayoutResult result;
        UltraCanvasMindMapLayout::ComputeLayout(model, MindMapLayoutOptions(),
                                                nullptr, nullptr, result);
        CHECK(result.Get(root).width > 0.0 && result.Get(root).height > 0.0,
              "layout falls back to the built-in text estimator");
    }
}

static void TestAlignmentPasses() {
    std::printf("\n[alignment post-passes]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("Centre");
    // Deliberately uneven label lengths, so unaligned columns would be ragged.
    std::vector<std::string> branches;
    const char* labels[] = {"A", "Bee bee bee", "C", "Dee dee dee dee"};
    for (const char* label : labels) {
        branches.push_back(model.AddTopic(root, label));
        model.AddTopic(branches.back(), "leaf");
    }

    MindMapLayoutOptions options;
    options.structure = MindMapStructure::Balanced;
    options.alignLevelColumns = true;
    MindMapLayoutResult result = Run(model, options);

    CHECK(CountOverlaps(result) == 0, "column alignment produces no overlaps");

    // Every depth-1 topic on a given side must share an inner edge.
    double rightEdge = 0.0, leftEdge = 0.0;
    bool haveRight = false, haveLeft = false, columnsAligned = true;
    for (const auto& id : branches) {
        const MindMapTopic* topic = model.GetTopic(id);
        Rect2Dd box = result.Get(id);
        if (topic->resolvedSide == MindMapTopicSide::Right) {
            if (!haveRight) { rightEdge = box.x; haveRight = true; }
            else if (!Near(box.x, rightEdge, 1e-6)) columnsAligned = false;
        } else {
            double edge = box.x + box.width;
            if (!haveLeft) { leftEdge = edge; haveLeft = true; }
            else if (!Near(edge, leftEdge, 1e-6)) columnsAligned = false;
        }
    }
    CHECK(columnsAligned, "same-depth topics share a column edge on each side");

    // Row alignment pairs the Nth left topic with the Nth right topic.
    options.alignSideRows = true;
    result = Run(model, options);
    CHECK(CountOverlaps(result) == 0, "row alignment produces no overlaps");

    std::vector<double> leftRows, rightRows;
    for (const auto& id : branches) {
        Rect2Dd box = result.Get(id);
        double centre = box.y + box.height * 0.5;
        if (model.GetTopic(id)->resolvedSide == MindMapTopicSide::Right) {
            rightRows.push_back(centre);
        } else {
            leftRows.push_back(centre);
        }
    }
    std::sort(leftRows.begin(), leftRows.end());
    std::sort(rightRows.begin(), rightRows.end());
    bool rowsPaired = leftRows.size() == rightRows.size();
    for (size_t i = 0; rowsPaired && i < leftRows.size(); ++i) {
        if (!Near(leftRows[i], rightRows[i], 1e-6)) rowsPaired = false;
    }
    CHECK(rowsPaired, "the Nth topic on each side shares a row centre");

    // Alignment is off by default, so existing maps are unaffected.
    MindMapLayoutOptions plain;
    MindMapLayoutResult plainResult = Run(model, plain);
    CHECK(!plain.alignLevelColumns && !plain.alignSideRows,
          "alignment is off by default");
    CHECK(CountOverlaps(plainResult) == 0, "the default path still has no overlaps");
}

static void TestManualStructure() {
    std::printf("\n[manual structure]\n");

    MindMapModel model;
    std::string root = model.SetCentralTopic("Centre");
    std::string pinned = model.AddTopic(root, "Pinned");
    std::string floated = model.AddTopic(root, "Auto placed");

    MindMapTopic* pinnedTopic = model.GetTopic(pinned);
    pinnedTopic->hasManualPosition = true;
    pinnedTopic->manualX = 640.0;
    pinnedTopic->manualY = -275.0;

    MindMapLayoutOptions options;
    options.structure = MindMapStructure::Manual;
    MindMapLayoutResult result = Run(model, options);

    CHECK(Near(result.Get(pinned).x, 640.0) && Near(result.Get(pinned).y, -275.0),
          "an authored position is honoured exactly");
    CHECK(result.Has(floated), "a topic without an authored position is still placed");
    CHECK(!Near(result.Get(floated).x, 640.0),
          "the auto-placed topic keeps its computed position");
    CHECK(result.contentBounds.maxX >= 640.0,
          "content bounds account for authored positions");

    // Switching structures must not lose the authored position.
    options.structure = MindMapStructure::Balanced;
    MindMapLayoutResult balanced = Run(model, options);
    CHECK(!Near(balanced.Get(pinned).x, 640.0),
          "Balanced ignores the authored position");
    CHECK(model.GetTopic(pinned)->hasManualPosition,
          "the authored position survives a structure switch");

    options.structure = MindMapStructure::Manual;
    MindMapLayoutResult back = Run(model, options);
    CHECK(Near(back.Get(pinned).x, 640.0),
          "switching back restores the authored position");
}

int main() {
    std::printf("UltraCanvasMindMap model & layout tests\n");
    std::printf("=======================================\n");

    TestModelBasics();
    TestRemoveAndMove();
    TestRelationships();
    TestOutlineAndOrdinals();
    TestBalancedLayout();
    TestBalancePolicies();
    TestLogicStructures();
    TestOrgChartStructures();
    TestRadialLayout();
    TestCollapse();
    TestNoOverlapStress();
    TestFloatingTopics();
    TestSpacingAndSizing();
    TestEdgeCases();
    TestAlignmentPasses();
    TestManualStructure();

    std::printf("\n=======================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
