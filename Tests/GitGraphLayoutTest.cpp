// Tests/GitGraphLayoutTest.cpp
// Unit tests for the Git commit-graph layout core: commit ordering, lane
// assignment under both strategies, merge/octopus handling, multiple roots,
// swimlane banding and edge routing.
//
// Exercises the real UltraCanvasGitGraphLayout code with no UI stack and no
// link dependencies beyond the one source file under test.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasGitGraphLayout.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace UltraCanvas;

static int g_failures = 0;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_failures; } \
        else         { std::printf("  ok:   %s\n", msg); }               \
    } while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static GitGraphCommit MakeCommit(const std::string& sha,
                                 const std::vector<std::string>& parents,
                                 int64_t date = 0,
                                 const std::string& branch = "") {
    GitGraphCommit commit;
    commit.sha        = sha;
    commit.parents    = parents;
    commit.commitDate = date;
    commit.authorDate = date;
    commit.branch     = branch;
    commit.subject    = "commit " + sha;
    return commit;
}

static GitGraphRef MakeBranch(const std::string& name, const std::string& sha) {
    GitGraphRef ref;
    ref.name = name;
    ref.sha  = sha;
    ref.type = GitGraphRefType::LocalBranch;
    return ref;
}

static const GitGraphPlacedEdge* FindEdge(const GitGraphLayoutResult& result,
                                          const std::string& child,
                                          const std::string& parent) {
    for (const GitGraphPlacedEdge& edge : result.edges) {
        if (edge.childSha == child && edge.parentSha == parent) return &edge;
    }
    return nullptr;
}

// Every placement invariant a well-formed layout must satisfy.
static void CheckInvariants(const GitGraphLayoutResult& result,
                            const std::vector<GitGraphCommit>& commits,
                            const char* label) {
    std::set<std::pair<int, int>> occupied;
    bool duplicateSlot = false;
    bool rowInRange = true;

    for (const GitGraphPlacedCommit& placed : result.commits) {
        if (!occupied.insert({placed.row, placed.lane}).second) duplicateSlot = true;
        if (placed.row < 0 || placed.row >= result.rowCount) rowInRange = false;
    }

    std::printf("  [%s]\n", label);
    CHECK(result.commits.size() == commits.size(), "every commit is placed exactly once");
    CHECK(!duplicateSlot, "no two commits share a (row, lane) slot");
    CHECK(rowInRange, "every row index is inside [0, rowCount)");

    // A parent must never be placed before its child in the resolved order.
    bool ordered = true;
    for (const GitGraphPlacedEdge& edge : result.edges) {
        if (edge.kind == GitGraphEdgeKind::CherryPick) continue;
        if (edge.toRow <= edge.fromRow) ordered = false;
    }
    CHECK(ordered, "every parent edge points forward in row order");
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void TestLinearHistory() {
    std::printf("Linear history (one lane, one edge per step)\n");

    std::vector<GitGraphCommit> commits = {
        MakeCommit("c3", {"c2"}, 300),
        MakeCommit("c2", {"c1"}, 200),
        MakeCommit("c1", {},     100)
    };
    std::vector<GitGraphRef> refs = {MakeBranch("main", "c3")};

    GitGraphLayoutOptions options;
    options.orderMode = GitGraphOrderMode::CommitDate;
    UltraCanvasGitGraphLayout layoutEngine(options);
    const GitGraphLayoutResult result = layoutEngine.Compute(commits, refs);

    CheckInvariants(result, commits, "linear");
    CHECK(result.rowCount == 3, "three rows");
    CHECK(result.LaneCount() == 1, "a linear history occupies a single lane");
    CHECK(result.Find("c3") && result.Find("c3")->row == 0, "newest commit is row 0");
    CHECK(result.Find("c1") && result.Find("c1")->isRoot, "the parentless commit is a root");
    CHECK(result.edges.size() == 2, "two parent edges");
}

static void TestBranchAndMerge() {
    std::printf("Branch and merge (feature leaves the trunk and comes back)\n");

    //   m3 (merge of m2 and f2)
    //   | \
    //   | f2
    //   | f1
    //   m2/
    //   m1
    std::vector<GitGraphCommit> commits = {
        MakeCommit("m3", {"m2", "f2"}, 600),
        MakeCommit("f2", {"f1"},       500),
        MakeCommit("f1", {"m2"},       400),
        MakeCommit("m2", {"m1"},       300),
        MakeCommit("m1", {},           100)
    };
    std::vector<GitGraphRef> refs = {MakeBranch("main", "m3"), MakeBranch("feature", "f2")};

    GitGraphLayoutOptions options;
    options.orderMode   = GitGraphOrderMode::CommitDate;
    options.trunkBranch = "main";
    UltraCanvasGitGraphLayout layoutEngine(options);
    const GitGraphLayoutResult result = layoutEngine.Compute(commits, refs);

    CheckInvariants(result, commits, "branch+merge");

    const GitGraphPlacedCommit* merge = result.Find("m3");
    CHECK(merge && merge->isMerge, "the two-parent commit is flagged as a merge");
    CHECK(merge && merge->lane == 0, "the trunk stays on lane 0");
    CHECK(result.Find("m1")->lane == 0 && result.Find("m2")->lane == 0,
          "every trunk commit is pinned to lane 0");
    CHECK(result.Find("f1")->lane != 0 && result.Find("f2")->lane != 0,
          "feature commits sit off the trunk");
    CHECK(result.Find("f1")->lane == result.Find("f2")->lane,
          "the feature branch keeps one lane for its whole life");

    const GitGraphPlacedEdge* mergeEdge = FindEdge(result, "m3", "f2");
    CHECK(mergeEdge && mergeEdge->kind == GitGraphEdgeKind::Merge,
          "the second parent link is a merge edge");
    CHECK(mergeEdge && mergeEdge->turnAtChild,
          "a merge edge turns next to the merge commit");

    const GitGraphPlacedEdge* branchEdge = FindEdge(result, "f1", "m2");
    CHECK(branchEdge && branchEdge->kind == GitGraphEdgeKind::Parent,
          "the branch point is a first-parent edge");
    CHECK(branchEdge && !branchEdge->turnAtChild,
          "a branch-point edge turns next to the parent");
}

static void TestOctopusMerge() {
    std::printf("Octopus merge (three parents must not corrupt the layout)\n");

    std::vector<GitGraphCommit> commits = {
        MakeCommit("o", {"a", "b", "c"}, 500),
        MakeCommit("a", {"base"},        400),
        MakeCommit("b", {"base"},        300),
        MakeCommit("c", {"base"},        200),
        MakeCommit("base", {},           100)
    };
    std::vector<GitGraphRef> refs = {MakeBranch("main", "o")};

    GitGraphLayoutOptions options;
    options.orderMode   = GitGraphOrderMode::CommitDate;
    options.trunkBranch = "main";
    UltraCanvasGitGraphLayout layoutEngine(options);
    const GitGraphLayoutResult result = layoutEngine.Compute(commits, refs);

    CheckInvariants(result, commits, "octopus");
    CHECK(result.commits.size() == 5, "all five commits placed");
    CHECK(result.edges.size() == 6, "three merge edges plus three parent edges");

    std::set<int> lanes;
    for (const char* sha : {"a", "b", "c"}) lanes.insert(result.Find(sha)->lane);
    CHECK(lanes.size() == 3, "each merged branch gets its own lane");
}

static void TestMultipleRoots() {
    std::printf("Unrelated histories (two roots in one graph)\n");

    std::vector<GitGraphCommit> commits = {
        MakeCommit("a2", {"a1"}, 400),
        MakeCommit("b2", {"b1"}, 300),
        MakeCommit("a1", {},     200),
        MakeCommit("b1", {},     100)
    };
    std::vector<GitGraphRef> refs = {MakeBranch("main", "a2"), MakeBranch("other", "b2")};

    GitGraphLayoutOptions options;
    options.orderMode   = GitGraphOrderMode::CommitDate;
    options.trunkBranch = "main";
    UltraCanvasGitGraphLayout layoutEngine(options);
    const GitGraphLayoutResult result = layoutEngine.Compute(commits, refs);

    CheckInvariants(result, commits, "multi-root");
    CHECK(result.Find("a1")->isRoot && result.Find("b1")->isRoot, "both roots detected");
    CHECK(result.Find("a2")->lane != result.Find("b2")->lane,
          "the two histories occupy different lanes");
    CHECK(result.edges.size() == 2, "no edges are invented between unrelated histories");
}

static void TestBoundaryCommits() {
    std::printf("Truncated history (a parent outside the loaded window)\n");

    std::vector<GitGraphCommit> commits = {
        MakeCommit("c2", {"c1"},      200),
        MakeCommit("c1", {"missing"}, 100)
    };
    std::vector<GitGraphRef> refs = {MakeBranch("main", "c2")};

    GitGraphLayoutOptions options;
    options.orderMode = GitGraphOrderMode::CommitDate;
    UltraCanvasGitGraphLayout layoutEngine(options);
    const GitGraphLayoutResult result = layoutEngine.Compute(commits, refs);

    CheckInvariants(result, commits, "boundary");
    CHECK(result.Find("c1")->isBoundary, "the commit with a missing parent is a boundary");
    CHECK(result.edges.size() == 1, "the dangling edge is dropped, not drawn to nowhere");
}

static void TestLaneStrategies() {
    std::printf("Lane strategies (Compact recycles lanes, Stable does not cross)\n");

    // Two short-lived branches off a trunk. Compact should reuse the freed lane
    // for the second branch; Stable must not hand it a slot that would cross an
    // active lane.
    std::vector<GitGraphCommit> commits = {
        MakeCommit("m5", {"m4", "g1"}, 900),
        MakeCommit("g1", {"m3"},       800),
        MakeCommit("m4", {"m3"},       700),
        MakeCommit("m3", {"m2", "f1"}, 600),
        MakeCommit("f1", {"m1"},       500),
        MakeCommit("m2", {"m1"},       400),
        MakeCommit("m1", {},           100)
    };
    std::vector<GitGraphRef> refs = {MakeBranch("main", "m5")};

    GitGraphLayoutOptions options;
    options.orderMode   = GitGraphOrderMode::CommitDate;
    options.trunkBranch = "main";

    options.laneStrategy = GitGraphLaneStrategy::Compact;
    const GitGraphLayoutResult compact =
        UltraCanvasGitGraphLayout(options).Compute(commits, refs);

    options.laneStrategy = GitGraphLaneStrategy::Stable;
    const GitGraphLayoutResult stable =
        UltraCanvasGitGraphLayout(options).Compute(commits, refs);

    CheckInvariants(compact, commits, "compact");
    CheckInvariants(stable,  commits, "stable");

    CHECK(compact.LaneCount() <= stable.LaneCount() + 1,
          "Compact never needs materially more lanes than Stable");
    CHECK(compact.Find("f1")->lane == 1 && compact.Find("g1")->lane == 1,
          "Compact reuses the freed lane for the next branch");
    CHECK(stable.Find("m1")->lane == 0, "the trunk is pinned under both strategies");
}

static void TestTopologicalOrder() {
    std::printf("Topological order (branch chunks stay contiguous)\n");

    // Dates interleave the two branches; --topo-order must not.
    std::vector<GitGraphCommit> commits = {
        MakeCommit("merge", {"m2", "f2"}, 900),
        MakeCommit("f2",    {"f1"},       800),
        MakeCommit("m2",    {"m1"},       750),
        MakeCommit("f1",    {"base"},     700),
        MakeCommit("m1",    {"base"},     650),
        MakeCommit("base",  {},           100)
    };
    std::vector<GitGraphRef> refs = {MakeBranch("main", "merge")};

    GitGraphLayoutOptions options;
    options.orderMode = GitGraphOrderMode::Topological;
    const GitGraphLayoutResult result =
        UltraCanvasGitGraphLayout(options).Compute(commits, refs);

    CheckInvariants(result, commits, "topological");
    CHECK(result.commits.size() == 6, "topological order emits every commit");
    CHECK(result.Find("base")->row == 5, "the root is last");

    // The first-parent chain m2 -> m1 must be adjacent, since the walk prefers
    // continuing the chain over jumping to the newer f2.
    CHECK(result.Find("m1")->row == result.Find("m2")->row + 1,
          "a first-parent chain is emitted contiguously");
}

static void TestSwimlaneLayout() {
    std::printf("Swimlane layout (git-flow bands either side of the trunk)\n");

    std::vector<GitGraphCommit> commits = {
        MakeCommit("m1", {},             100, "main"),
        MakeCommit("d1", {"m1"},         200, "develop"),
        MakeCommit("f1", {"d1"},         300, "feature"),
        MakeCommit("d2", {"d1", "f1"},   400, "develop"),
        MakeCommit("m2", {"m1", "d2"},   500, "main")
    };
    std::vector<GitGraphRef> refs = {
        MakeBranch("main", "m2"), MakeBranch("develop", "d2"), MakeBranch("feature", "f1")
    };

    GitGraphLayoutOptions options;
    options.orderMode     = GitGraphOrderMode::AsGiven;
    options.layoutMode    = GitGraphLayoutMode::Swimlane;
    options.trunkBranch   = "main";
    options.swimlaneOrder = {"develop", "feature"};
    const GitGraphLayoutResult result =
        UltraCanvasGitGraphLayout(options).Compute(commits, refs);

    CHECK(result.commits.size() == 5, "every commit is banded");
    CHECK(result.swimlanes.size() == 3, "three bands: main, develop, feature");
    CHECK(result.Find("m1")->lane == 0 && result.Find("m2")->lane == 0,
          "the trunk band is lane 0");
    CHECK(result.Find("d1")->lane == result.Find("d2")->lane,
          "a branch's commits share one band");
    CHECK(result.minLane < 0 && result.maxLane > 0,
          "bands are distributed either side of the trunk");
    CHECK(result.Find("f1")->branch == "feature", "the band name is carried through");
}

static void TestSwimlaneDerivedBranches() {
    std::printf("Swimlane without explicit branches (first-parent walk from refs)\n");

    std::vector<GitGraphCommit> commits = {
        MakeCommit("m2", {"m1"}, 400),
        MakeCommit("f1", {"m1"}, 300),
        MakeCommit("m1", {},     100)
    };
    std::vector<GitGraphRef> refs = {MakeBranch("main", "m2"), MakeBranch("topic", "f1")};

    GitGraphLayoutOptions options;
    options.orderMode   = GitGraphOrderMode::CommitDate;
    options.layoutMode  = GitGraphLayoutMode::Swimlane;
    options.trunkBranch = "main";
    const GitGraphLayoutResult result =
        UltraCanvasGitGraphLayout(options).Compute(commits, refs);

    CHECK(result.Find("m2")->branch == "main", "the trunk claims its own chain first");
    CHECK(result.Find("m1")->branch == "main", "shared history belongs to the trunk");
    CHECK(result.Find("f1")->branch == "topic", "the topic tip is claimed by its own ref");
    CHECK(result.Find("f1")->lane != 0, "the topic band is off the trunk");
}

static void TestCherryPickEdge() {
    std::printf("Cherry-pick (a dashed non-parent relation)\n");

    std::vector<GitGraphCommit> commits = {
        MakeCommit("p1", {"m1"}, 300),
        MakeCommit("f1", {"m1"}, 200),
        MakeCommit("m1", {},     100)
    };
    commits[0].cherryPickSource = "f1";

    std::vector<GitGraphRef> refs = {MakeBranch("main", "p1")};

    GitGraphLayoutOptions options;
    options.orderMode = GitGraphOrderMode::CommitDate;
    const GitGraphLayoutResult result =
        UltraCanvasGitGraphLayout(options).Compute(commits, refs);

    const GitGraphPlacedEdge* pick = FindEdge(result, "p1", "f1");
    CHECK(pick != nullptr, "a cherry-pick edge is emitted");
    CHECK(pick && pick->kind == GitGraphEdgeKind::CherryPick, "it is typed as a cherry-pick");
}

static void TestRandomHistories() {
    std::printf("Randomised DAGs (invariants hold across 200 generated histories)\n");

    std::mt19937 rng(20260730u);
    int violations = 0;

    for (int iteration = 0; iteration < 200; ++iteration) {
        const int count = 5 + static_cast<int>(rng() % 40);
        std::vector<GitGraphCommit> commits;
        commits.reserve(count);

        // Build oldest-first so parents always exist, then reverse into git log
        // order (newest first).
        for (int i = 0; i < count; ++i) {
            std::vector<std::string> parents;
            if (i > 0) {
                parents.push_back("c" + std::to_string(rng() % static_cast<unsigned>(i)));
                if (i > 2 && (rng() % 4) == 0) {
                    const std::string extra = "c" + std::to_string(rng() % static_cast<unsigned>(i));
                    if (extra != parents.front()) parents.push_back(extra);
                }
            }
            commits.push_back(MakeCommit("c" + std::to_string(i), parents, i * 10));
        }
        std::reverse(commits.begin(), commits.end());

        std::vector<GitGraphRef> refs = {MakeBranch("main", commits.front().sha)};

        for (GitGraphLaneStrategy strategy : {GitGraphLaneStrategy::Compact,
                                              GitGraphLaneStrategy::Stable}) {
            for (GitGraphOrderMode order : {GitGraphOrderMode::CommitDate,
                                            GitGraphOrderMode::Topological}) {
                GitGraphLayoutOptions options;
                options.orderMode    = order;
                options.laneStrategy = strategy;
                options.trunkBranch  = "main";
                const GitGraphLayoutResult result =
                    UltraCanvasGitGraphLayout(options).Compute(commits, refs);

                if (result.commits.size() != commits.size()) { ++violations; continue; }

                std::set<std::pair<int, int>> occupied;
                for (const GitGraphPlacedCommit& placed : result.commits) {
                    if (!occupied.insert({placed.row, placed.lane}).second) ++violations;
                    if (placed.lane < 0) ++violations;
                }
                for (const GitGraphPlacedEdge& edge : result.edges) {
                    if (edge.toRow <= edge.fromRow) ++violations;
                }
            }
        }
    }

    CHECK(violations == 0, "no placement or ordering invariant was violated");
}

// ---------------------------------------------------------------------------

int main() {
    std::printf("=== GitGraph layout tests ===\n\n");

    TestLinearHistory();
    TestBranchAndMerge();
    TestOctopusMerge();
    TestMultipleRoots();
    TestBoundaryCommits();
    TestLaneStrategies();
    TestTopologicalOrder();
    TestSwimlaneLayout();
    TestSwimlaneDerivedBranches();
    TestCherryPickEdge();
    TestRandomHistories();

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
