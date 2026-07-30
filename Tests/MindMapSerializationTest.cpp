// Tests/MindMapSerializationTest.cpp
// Round-trip tests for the mind map model's structural restore path.
//
// UltraCanvasMindMap::FromJson populates Topics() directly (a file may name a
// parent before it defines it) and then calls MindMapModel::RestoreStructure to
// rebuild the root, the floating list and the id counter. That restore is the
// part that can silently corrupt a map, so it is tested here against the model
// alone - no UI stack, no render context.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMindMapLayout.h"

#include <algorithm>
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

// Inserts a topic straight into the map, the way a deserializer would.
static void RawInsert(MindMapModel& model, const std::string& id,
                      const std::string& parentId, const std::string& text,
                      const std::vector<std::string>& children = {}) {
    MindMapTopic topic(id, text);
    topic.parentId = parentId;
    topic.childIds = children;
    model.Topics()[id] = topic;
}

static void TestRestoreStructure() {
    std::printf("\n[restore structure]\n");

    MindMapModel model;
    // Deliberately out of order: children are declared before their parent.
    RawInsert(model, "c1", "b1", "Grandchild");
    RawInsert(model, "b1", "root", "Child", {"c1"});
    RawInsert(model, "root", "", "Centre", {"b1"});

    model.RestoreStructure("root");

    CHECK(model.GetRootId() == "root", "declared root is restored");
    CHECK(model.GetTopicCount() == 3, "all topics survive the restore");
    CHECK(model.GetDepth("c1") == 2, "depth is correct after an out-of-order restore");
    CHECK(model.GetPath("c1").size() == 3, "path walks back to the root");
    CHECK(model.FloatingTopicIds().empty(), "a fully parented tree has no floating topics");
}

static void TestRestoreDropsDanglingReferences() {
    std::printf("\n[restore drops dangling references]\n");

    MindMapModel model;
    RawInsert(model, "root", "", "Centre", {"b1", "ghost"});   // "ghost" never defined
    RawInsert(model, "b1", "root", "Child");
    RawInsert(model, "orphan", "missing_parent", "Orphan");    // parent never defined

    model.RestoreStructure("root");

    const MindMapTopic* root = model.GetTopic("root");
    CHECK(root != nullptr, "root survived");
    CHECK(root && root->childIds.size() == 1 && root->childIds[0] == "b1",
          "a child id with no matching topic is dropped");

    const MindMapTopic* orphan = model.GetTopic("orphan");
    CHECK(orphan && orphan->parentId.empty(),
          "a dangling parent reference is cleared");
    CHECK(orphan && orphan->floating,
          "a parentless non-root topic becomes floating");
    CHECK(model.FloatingTopicIds().size() == 1, "the floating list tracks it");

    // The tree must still be walkable without infinite loops or crashes.
    int visited = 0;
    model.ForEachTopic([&](const MindMapTopic&) { ++visited; });
    CHECK(visited == 3, "every topic is reachable after the repair");
}

static void TestRestoreWithMissingRoot() {
    std::printf("\n[restore with a missing root]\n");

    MindMapModel model;
    RawInsert(model, "a", "", "Alpha");
    RawInsert(model, "b", "", "Beta");

    model.RestoreStructure("nonexistent_root");

    CHECK(model.GetRootId().empty(), "an unknown declared root leaves no root");
    CHECK(model.FloatingTopicIds().size() == 2,
          "every parentless topic becomes floating when there is no root");

    // A rootless model must not crash the layout engine.
    MindMapLayoutResult result;
    UltraCanvasMindMapLayout::ComputeLayout(model, MindMapLayoutOptions(),
                                            nullptr, nullptr, result);
    CHECK(result.bounds.size() == 2, "a rootless model still lays out its floating topics");
}

static void TestRestoreIdCounter() {
    std::printf("\n[restore id counter]\n");

    MindMapModel model;
    RawInsert(model, "root", "", "Centre");
    RawInsert(model, "topic_1", "root", "One");
    RawInsert(model, "topic_2", "root", "Two");
    RawInsert(model, "topic_3", "root", "Three");
    model.Topics()["root"].childIds = {"topic_1", "topic_2", "topic_3"};

    model.RestoreStructure("root");

    // The generator must not hand back an id that is already in use, or the
    // new topic would overwrite an existing one.
    std::string generated = model.GenerateId();
    CHECK(model.Topics().find(generated) == model.Topics().end(),
          "a freshly generated id does not collide with restored ids");

    std::string added = model.AddTopic("root", "Four");
    CHECK(!added.empty(), "adding after a restore succeeds");
    CHECK(model.GetTopicCount() == 5, "the new topic did not overwrite an existing one");

    // And repeatedly, to be sure the counter keeps advancing.
    bool unique = true;
    std::vector<std::string> ids;
    for (int i = 0; i < 20; ++i) {
        std::string id = model.AddTopic("root", "T");
        if (id.empty() || std::find(ids.begin(), ids.end(), id) != ids.end()) unique = false;
        ids.push_back(id);
    }
    CHECK(unique, "generated ids stay unique across many additions");
}

static void TestRestoreRootReparenting() {
    std::printf("\n[restore normalises the root]\n");

    MindMapModel model;
    // A malformed file claims the root has a parent.
    RawInsert(model, "root", "b1", "Centre", {"b1"});
    RawInsert(model, "b1", "root", "Child", {"root"});

    model.RestoreStructure("root");

    const MindMapTopic* root = model.GetTopic("root");
    CHECK(root && root->parentId.empty(), "the root's parent is cleared");
    CHECK(root && !root->floating, "the root is never marked floating");

    // GetPath must terminate rather than loop on the mutual reference.
    std::vector<std::string> path = model.GetPath("b1");
    CHECK(!path.empty() && path.size() <= model.GetTopicCount() + 1,
          "path resolution terminates on a malformed cyclic file");
}

static void TestOutlineRoundTrip() {
    std::printf("\n[outline round trip]\n");

    MindMapModel model;
    model.BuildFromOutline({
        {0, "Product"},
        {1, "Research"},
        {2, "Interviews"},
        {2, "Surveys"},
        {1, "Design"},
        {2, "Wireframes"},
    });

    CHECK(model.GetTopicCount() == 6, "outline produced six topics");

    // Rebuild the outline from the tree and compare shape.
    std::vector<std::pair<int, std::string>> rebuilt;
    model.ForEachTopic([&](const MindMapTopic& topic) {
        rebuilt.emplace_back(model.GetDepth(topic.id), topic.text);
    });

    CHECK(rebuilt.size() == 6, "walk visits every topic once");
    CHECK(rebuilt[0].first == 0 && rebuilt[0].second == "Product",
          "walk starts at the central topic");

    bool depthsValid = true;
    for (size_t i = 1; i < rebuilt.size(); ++i) {
        // Pre-order: a topic is never more than one level deeper than the last.
        if (rebuilt[i].first > rebuilt[i - 1].first + 1) depthsValid = false;
    }
    CHECK(depthsValid, "pre-order walk never skips a level");
}

int main() {
    std::printf("UltraCanvasMindMap serialization/restore tests\n");
    std::printf("=============================================\n");

    TestRestoreStructure();
    TestRestoreDropsDanglingReferences();
    TestRestoreWithMissingRoot();
    TestRestoreIdCounter();
    TestRestoreRootReparenting();
    TestOutlineRoundTrip();

    std::printf("\n=============================================\n");
    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
