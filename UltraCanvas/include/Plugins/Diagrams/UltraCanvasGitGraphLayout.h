// include/Plugins/Diagrams/UltraCanvasGitGraphLayout.h
// Headless layout core for the Git commit-graph element: commit ordering,
// lane assignment and edge routing. Produces plain geometry in (row, lane)
// space - no render context, no fonts, no window - so it can be unit-tested
// on its own (see Tests/GitGraphLayoutTest.cpp).
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#pragma once

#include "Plugins/Diagrams/UltraCanvasGitGraphTypes.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UltraCanvas {

// A commit placed on the grid. `row` runs along the history axis (0 = first in
// the resolved order); `lane` is the cross-axis slot - always >= 0 in Lanes
// mode, and signed either side of the trunk in Swimlane mode.
struct GitGraphPlacedCommit {
    std::string sha;
    int  row  = 0;
    int  lane = 0;
    bool isMerge    = false;
    bool isRoot     = false;
    bool isBoundary = false;        // A parent is missing (truncated history)
    std::string branch;             // Resolved branch name (Swimlane mode)

    // >0 when this node stands in for a folded run of linear commits: the
    // number of commits it replaces, itself included.
    int collapsedCount = 0;
};

struct GitGraphPlacedEdge {
    std::string childSha;
    std::string parentSha;
    int  fromRow  = 0, fromLane = 0;    // Child end
    int  toRow    = 0, toLane   = 0;    // Parent end
    int  colorLane = 0;                 // Lane whose colour the edge takes
    GitGraphEdgeKind kind = GitGraphEdgeKind::Parent;

    // True when the lane change must happen next to the child rather than next
    // to the parent - i.e. a merge edge leaving a merge commit sideways.
    bool turnAtChild = false;
};

// One band in Swimlane mode.
struct GitGraphSwimlane {
    std::string name;
    int  lane = 0;                  // Signed offset from the trunk
    int  firstRow = 0;
    int  lastRow  = 0;
};

struct GitGraphLayoutOptions {
    GitGraphOrderMode    orderMode    = GitGraphOrderMode::AsGiven;
    GitGraphLayoutMode   layoutMode   = GitGraphLayoutMode::Lanes;
    GitGraphLaneStrategy laneStrategy = GitGraphLaneStrategy::Stable;

    // Branch pinned to lane 0. In Lanes mode no other branch may take lane 0;
    // in Swimlane mode this is the trunk the other bands sit either side of.
    std::string trunkBranch = "main";

    // Swimlane band order. Branches not listed are appended in first-seen order.
    std::vector<std::string> swimlaneOrder;

    // Distribute swimlanes alternately above and below the trunk (the git-flow
    // look). When false every band sits on one side.
    bool swimlanesBothSides = true;

    // Commits to leave out of the graph. They keep their place in the history:
    // an edge that passed through a hidden commit is re-pointed at the nearest
    // visible ancestor and typed Skipped, so a filtered graph still shows how
    // the surviving commits relate.
    std::unordered_set<std::string> hiddenCommits;

    // Reorder lane columns to reduce edge crossings (Lanes mode only; the
    // trunk keeps column 0). Skipped when the graph exceeds
    // crossingReductionEdgeBudget edges, since the pass counts crossings
    // pairwise.
    bool   reduceCrossings = false;
    size_t crossingReductionEdgeBudget = 4000;

    // Give commits made at the same moment the same row instead of stacking
    // them (mermaid's parallelCommits). Only applied when the commits are
    // unrelated and land in different lanes, so no edge is ever flattened.
    bool    parallelCommits = false;
    int64_t parallelTolerance = 0;      // Seconds; 0 = exactly equal timestamps

    // Fold a run of plain single-parent commits into one node. Commits that
    // carry a ref, merge, root or appear in keepExpanded are never folded.
    bool   collapseLinearRuns = false;
    int    minCollapsibleRun  = 8;
    std::unordered_set<std::string> keepExpanded;
};

struct GitGraphLayoutResult {
    std::vector<GitGraphPlacedCommit> commits;      // Ordered by row
    std::vector<GitGraphPlacedEdge>   edges;
    std::vector<GitGraphSwimlane>     swimlanes;

    int rowCount = 0;
    int minLane  = 0;
    int maxLane  = 0;
    size_t hiddenCount = 0;         // Commits excluded by the filter

    std::unordered_map<std::string, size_t> indexBySha;   // sha -> index in commits

    int LaneCount() const { return maxLane - minLane + 1; }

    const GitGraphPlacedCommit* Find(const std::string& sha) const {
        auto it = indexBySha.find(sha);
        return it == indexBySha.end() ? nullptr : &commits[it->second];
    }

    void Clear() {
        commits.clear();
        edges.clear();
        swimlanes.clear();
        indexBySha.clear();
        rowCount = 0;
        minLane = 0;
        maxLane = 0;
        hiddenCount = 0;
    }

    // Number of edge pairs that visually cross. Used by the crossing-reduction
    // pass and handy for tests.
    size_t CountCrossings() const;
};

// Ordering + lane assignment. Stateless apart from the options; call Compute()
// with the full commit list and the refs that decorate it.
class UltraCanvasGitGraphLayout {
public:
    UltraCanvasGitGraphLayout() = default;
    explicit UltraCanvasGitGraphLayout(const GitGraphLayoutOptions& options)
        : options(options) {}

    void SetOptions(const GitGraphLayoutOptions& newOptions) { options = newOptions; }
    const GitGraphLayoutOptions& GetOptions() const { return options; }

    // Lay out `commits`, decorated by `refs`. Commits whose parents are absent
    // from the list are marked boundary and their dangling edges dropped.
    GitGraphLayoutResult Compute(const std::vector<GitGraphCommit>& commits,
                                 const std::vector<GitGraphRef>& refs) const;

    // Resolve the commit order on its own (exposed for tests and for callers
    // that want the order without the geometry).
    std::vector<size_t> ResolveOrder(const std::vector<GitGraphCommit>& commits) const;

private:
    GitGraphLayoutOptions options;

    void AssignLanes(const std::vector<GitGraphCommit>& commits,
                     const std::vector<size_t>& order,
                     const std::unordered_map<std::string, size_t>& bySha,
                     const std::vector<bool>& onTrunk,
                     GitGraphLayoutResult& result) const;

    void AssignSwimlanes(const std::vector<GitGraphCommit>& commits,
                         const std::vector<size_t>& order,
                         const std::unordered_map<std::string, size_t>& bySha,
                         const std::vector<GitGraphRef>& refs,
                         GitGraphLayoutResult& result) const;

    // Marks the interior of long linear runs hidden and tags the surviving
    // node with how many commits it now stands for.
    void CollapseRuns(const std::vector<GitGraphCommit>& commits,
                      const std::vector<size_t>& order,
                      const std::unordered_map<std::string, size_t>& bySha,
                      const std::vector<GitGraphRef>& refs,
                      std::unordered_set<std::string>& hidden,
                      std::unordered_map<std::string, int>& runLengths) const;

    // Collapses rows for commits made at the same moment.
    void ApplyParallelRows(const std::vector<GitGraphCommit>& commits,
                           const std::unordered_map<std::string, size_t>& bySha,
                           GitGraphLayoutResult& result) const;

    void BuildEdges(const std::vector<GitGraphCommit>& commits,
                    const std::unordered_map<std::string, size_t>& bySha,
                    GitGraphLayoutResult& result) const;

    // Reorders lane columns (barycentre heuristic, keeping the best measured
    // permutation) to reduce edge crossings.
    void ReduceCrossings(GitGraphLayoutResult& result) const;
};

} // namespace UltraCanvas
