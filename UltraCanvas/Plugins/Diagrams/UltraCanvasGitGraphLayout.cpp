// Plugins/Diagrams/UltraCanvasGitGraphLayout.cpp
// Commit ordering, lane assignment and edge routing for UltraCanvasGitGraph.
// Pure geometry - no rendering dependencies.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasGitGraphLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <functional>
#include <set>
#include <tuple>

namespace UltraCanvas {

namespace {

// One slot in the active-lane table used by the sweep.
struct LaneSlot {
    bool        active    = false;
    bool        reserved  = false;      // Lane 0 when a trunk branch is pinned
    std::string expecting;              // Sha this lane is waiting to place
};

// Leftmost free slot (Compact) or the first slot with nothing active to its
// right (Stable). Both append when no slot qualifies.
//
// `hint` is the lane the new one will be drawn next to (the merge commit's own
// lane). When set, Compact takes the free slot closest to it instead of the
// leftmost, which keeps a merged-in branch beside the commit that merges it
// and removes crossings at no cost.
int AllocateLane(std::vector<LaneSlot>& lanes, GitGraphLaneStrategy strategy,
                 int hint = -1) {
    if (strategy == GitGraphLaneStrategy::Compact) {
        int best = -1;
        int bestDistance = 0;
        for (size_t i = 0; i < lanes.size(); ++i) {
            if (lanes[i].active || lanes[i].reserved) continue;
            const int candidate = static_cast<int>(i);
            if (hint < 0) { best = candidate; break; }       // Leftmost free

            const int distance = std::abs(candidate - hint);
            if (best < 0 || distance < bestDistance) {
                best = candidate;
                bestDistance = distance;
            }
        }
        if (best >= 0) {
            lanes[best].active = true;
            lanes[best].expecting.clear();
            return best;
        }
    } else {
        // Stable: drop trailing free slots so the new branch takes the
        // leftmost position that introduces no crossing, then append.
        while (!lanes.empty() && !lanes.back().active && !lanes.back().reserved) {
            lanes.pop_back();
        }
    }

    lanes.push_back(LaneSlot{true, false, std::string()});
    return static_cast<int>(lanes.size()) - 1;
}

void FreeLane(std::vector<LaneSlot>& lanes, int lane) {
    if (lane < 0 || lane >= static_cast<int>(lanes.size())) return;
    lanes[lane].active = false;
    lanes[lane].expecting.clear();
}

} // namespace

// ===== ORDERING =====

std::vector<size_t> UltraCanvasGitGraphLayout::ResolveOrder(
        const std::vector<GitGraphCommit>& commits) const {

    const size_t n = commits.size();
    std::vector<size_t> order;
    order.reserve(n);

    switch (options.orderMode) {
        case GitGraphOrderMode::AsGiven: {
            for (size_t i = 0; i < n; ++i) order.push_back(i);
            break;
        }

        case GitGraphOrderMode::CommitDate:
        case GitGraphOrderMode::AuthorDate: {
            const bool useAuthorDate = (options.orderMode == GitGraphOrderMode::AuthorDate);
            for (size_t i = 0; i < n; ++i) order.push_back(i);
            std::stable_sort(order.begin(), order.end(),
                             [&](size_t a, size_t b) {
                                 const int64_t da = useAuthorDate ? commits[a].authorDate
                                                                  : commits[a].commitDate;
                                 const int64_t db = useAuthorDate ? commits[b].authorDate
                                                                  : commits[b].commitDate;
                                 return da > db;   // Newest first, as git log does
                             });
            break;
        }

        case GitGraphOrderMode::Topological: {
            // Kahn's algorithm over the child->parent edges, emitting a commit
            // only once every child of it has been emitted. Among the ready
            // set we prefer the first parent of the commit just emitted, which
            // keeps a branch's commits contiguous instead of interleaving them
            // by date - this is what `git log --topo-order` does.
            std::unordered_map<std::string, size_t> bySha;
            bySha.reserve(n * 2);
            for (size_t i = 0; i < n; ++i) bySha.emplace(commits[i].sha, i);

            std::vector<int> pendingChildren(n, 0);
            for (size_t i = 0; i < n; ++i) {
                for (const std::string& parent : commits[i].parents) {
                    auto it = bySha.find(parent);
                    if (it != bySha.end()) ++pendingChildren[it->second];
                }
            }

            // Ready set keyed by (newest date first, then input order) so the
            // result is deterministic.
            auto key = [&](size_t i) {
                return std::make_tuple(-commits[i].commitDate, i);
            };
            std::set<std::tuple<int64_t, size_t>> ready;
            for (size_t i = 0; i < n; ++i) {
                if (pendingChildren[i] == 0) ready.insert(key(i));
            }

            std::string preferred;
            while (!ready.empty()) {
                size_t pick;
                bool picked = false;

                if (!preferred.empty()) {
                    auto it = bySha.find(preferred);
                    if (it != bySha.end() && pendingChildren[it->second] == 0) {
                        auto k = key(it->second);
                        if (ready.erase(k) > 0) {
                            pick = it->second;
                            picked = true;
                        }
                    }
                }

                if (!picked) {
                    auto it = ready.begin();
                    pick = std::get<1>(*it);
                    ready.erase(it);
                }

                order.push_back(pick);
                preferred = commits[pick].parents.empty() ? std::string()
                                                          : commits[pick].parents.front();

                for (const std::string& parent : commits[pick].parents) {
                    auto it = bySha.find(parent);
                    if (it == bySha.end()) continue;
                    if (--pendingChildren[it->second] == 0) {
                        ready.insert(key(it->second));
                    }
                }
            }

            // Cycles cannot occur in a well-formed history, but a corrupt or
            // hand-built input must not silently lose commits.
            if (order.size() < n) {
                std::vector<bool> emitted(n, false);
                for (size_t idx : order) emitted[idx] = true;
                for (size_t i = 0; i < n; ++i) {
                    if (!emitted[i]) order.push_back(i);
                }
            }
            break;
        }
    }

    // Filtered commits are ordered with everything else (so a topological walk
    // stays correct) and only then dropped, which keeps the surviving rows in
    // the order they would have had.
    if (!options.hiddenCommits.empty()) {
        std::vector<size_t> visible;
        visible.reserve(order.size());
        for (size_t index : order) {
            if (options.hiddenCommits.count(commits[index].sha) == 0) visible.push_back(index);
        }
        order.swap(visible);
    }

    return order;
}

// ===== LANE ASSIGNMENT =====

void UltraCanvasGitGraphLayout::AssignLanes(
        const std::vector<GitGraphCommit>& commits,
        const std::vector<size_t>& order,
        const std::unordered_map<std::string, size_t>& bySha,
        const std::vector<bool>& onTrunk,
        GitGraphLayoutResult& result) const {

    const bool trunkPinned =
        std::find(onTrunk.begin(), onTrunk.end(), true) != onTrunk.end();

    std::vector<LaneSlot> lanes;
    if (trunkPinned) {
        lanes.push_back(LaneSlot{false, true, std::string()});   // Lane 0 = trunk
    }

    for (size_t row = 0; row < order.size(); ++row) {
        const size_t idx = order[row];
        const GitGraphCommit& commit = commits[idx];

        // Lanes waiting for this commit; the leftmost keeps it, the rest end here.
        std::vector<int> matched;
        for (size_t i = 0; i < lanes.size(); ++i) {
            if (lanes[i].active && lanes[i].expecting == commit.sha) {
                matched.push_back(static_cast<int>(i));
            }
        }

        int lane;
        if (trunkPinned && onTrunk[idx]) {
            lane = 0;
            lanes[0].active = true;
        } else if (!matched.empty()) {
            lane = matched.front();
        } else {
            lane = AllocateLane(lanes, options.laneStrategy);
        }

        for (int other : matched) {
            if (other != lane) FreeLane(lanes, other);
        }

        // The first parent continues this lane; a missing or trunk-bound first
        // parent ends it (the branch line runs into the trunk instead).
        bool laneContinues = false;
        if (!commit.parents.empty()) {
            auto it = bySha.find(commit.parents.front());
            if (it != bySha.end()) {
                const bool parentOnTrunk = trunkPinned && onTrunk[it->second];
                if (!parentOnTrunk || lane == 0) {
                    lanes[lane].expecting = commit.parents.front();
                    lanes[lane].active = true;
                    laneContinues = true;
                }
            }
        }
        if (!laneContinues) FreeLane(lanes, lane);

        // Reserve a lane for every additional parent so the merged-in branch is
        // already in place by the time we reach it.
        for (size_t p = 1; p < commit.parents.size(); ++p) {
            const std::string& parentSha = commit.parents[p];
            auto it = bySha.find(parentSha);
            if (it == bySha.end()) continue;
            if (trunkPinned && onTrunk[it->second]) continue;

            bool alreadyExpected = false;
            for (const LaneSlot& slot : lanes) {
                if (slot.active && slot.expecting == parentSha) {
                    alreadyExpected = true;
                    break;
                }
            }
            if (alreadyExpected) continue;

            const int mergeLane = AllocateLane(lanes, options.laneStrategy, lane);
            lanes[mergeLane].expecting = parentSha;
        }

        GitGraphPlacedCommit placed;
        placed.sha    = commit.sha;
        placed.row    = static_cast<int>(row);
        placed.lane   = lane;
        placed.isMerge = commit.IsMerge();
        placed.isRoot  = commit.IsRoot();
        placed.branch  = commit.branch;

        result.indexBySha[placed.sha] = result.commits.size();
        result.commits.push_back(placed);

        result.maxLane = std::max(result.maxLane, lane);
    }

    result.rowCount = static_cast<int>(order.size());
    result.minLane  = 0;
}

// ===== SWIMLANE ASSIGNMENT =====

void UltraCanvasGitGraphLayout::AssignSwimlanes(
        const std::vector<GitGraphCommit>& commits,
        const std::vector<size_t>& order,
        const std::unordered_map<std::string, size_t>& bySha,
        const std::vector<GitGraphRef>& refs,
        GitGraphLayoutResult& result) const {

    const size_t n = commits.size();

    // 1. Branch membership. An explicit GitGraphCommit::branch always wins;
    //    anything left over is claimed by a first-parent walk down from each
    //    branch ref, trunk first, in ref order. Git does not record which
    //    branch a commit was made on, so this walk is the reconstruction every
    //    git-flow style diagram relies on.
    std::vector<std::string> branchOf(n);
    for (size_t i = 0; i < n; ++i) branchOf[i] = commits[i].branch;

    std::vector<const GitGraphRef*> walkOrder;
    for (const GitGraphRef& ref : refs) {
        if (ref.type == GitGraphRefType::LocalBranch && ref.name == options.trunkBranch) {
            walkOrder.push_back(&ref);
        }
    }
    for (const GitGraphRef& ref : refs) {
        if (ref.type != GitGraphRefType::LocalBranch && ref.type != GitGraphRefType::RemoteBranch) {
            continue;
        }
        if (ref.name == options.trunkBranch) continue;
        walkOrder.push_back(&ref);
    }

    for (const GitGraphRef* ref : walkOrder) {
        std::string cursor = ref->sha;
        while (!cursor.empty()) {
            auto it = bySha.find(cursor);
            if (it == bySha.end()) break;
            const size_t idx = it->second;
            if (!branchOf[idx].empty()) break;        // Already owned by a nearer ref
            branchOf[idx] = ref->name;
            cursor = commits[idx].parents.empty() ? std::string()
                                                  : commits[idx].parents.front();
        }
    }

    for (size_t i = 0; i < n; ++i) {
        if (branchOf[i].empty()) branchOf[i] = options.trunkBranch;
    }

    // 2. Band order: the trunk, then the configured order, then first-seen.
    std::vector<std::string> bandNames;
    auto addBand = [&](const std::string& name) {
        if (name.empty()) return;
        if (std::find(bandNames.begin(), bandNames.end(), name) == bandNames.end()) {
            bandNames.push_back(name);
        }
    };
    addBand(options.trunkBranch);
    for (const std::string& name : options.swimlaneOrder) addBand(name);
    for (size_t row = 0; row < order.size(); ++row) addBand(branchOf[order[row]]);

    // 3. Band -> signed lane. Alternate above/below the trunk for the git-flow
    //    look, or stack them all on one side.
    std::unordered_map<std::string, int> laneOfBand;
    for (size_t b = 0; b < bandNames.size(); ++b) {
        if (bandNames[b] == options.trunkBranch) {
            laneOfBand[bandNames[b]] = 0;
            continue;
        }
        const int ordinal = static_cast<int>(laneOfBand.size());   // 1, 2, 3, ...
        if (options.swimlanesBothSides) {
            const int step = (ordinal + 1) / 2;
            laneOfBand[bandNames[b]] = (ordinal % 2 == 1) ? -step : step;
        } else {
            laneOfBand[bandNames[b]] = ordinal;
        }
    }

    // 4. Place.
    std::unordered_map<std::string, GitGraphSwimlane> bands;
    for (size_t row = 0; row < order.size(); ++row) {
        const size_t idx = order[row];
        const GitGraphCommit& commit = commits[idx];
        const std::string& band = branchOf[idx];
        const int lane = laneOfBand.count(band) ? laneOfBand[band] : 0;

        GitGraphPlacedCommit placed;
        placed.sha     = commit.sha;
        placed.row     = static_cast<int>(row);
        placed.lane    = lane;
        placed.isMerge = commit.IsMerge();
        placed.isRoot  = commit.IsRoot();
        placed.branch  = band;

        result.indexBySha[placed.sha] = result.commits.size();
        result.commits.push_back(placed);

        result.minLane = std::min(result.minLane, lane);
        result.maxLane = std::max(result.maxLane, lane);

        auto bandIt = bands.find(band);
        if (bandIt == bands.end()) {
            GitGraphSwimlane lane_;
            lane_.name     = band;
            lane_.lane     = lane;
            lane_.firstRow = static_cast<int>(row);
            lane_.lastRow  = static_cast<int>(row);
            bands.emplace(band, lane_);
        } else {
            bandIt->second.firstRow = std::min(bandIt->second.firstRow, static_cast<int>(row));
            bandIt->second.lastRow  = std::max(bandIt->second.lastRow,  static_cast<int>(row));
        }
    }

    for (const std::string& name : bandNames) {
        auto it = bands.find(name);
        if (it != bands.end()) result.swimlanes.push_back(it->second);
    }

    result.rowCount = static_cast<int>(order.size());
}

// ===== EDGES =====

void UltraCanvasGitGraphLayout::BuildEdges(
        const std::vector<GitGraphCommit>& commits,
        const std::unordered_map<std::string, size_t>& bySha,
        GitGraphLayoutResult& result) const {

    // Walks up from a parent that was filtered out until it reaches a commit
    // that is actually on the graph, so a filtered history still shows how the
    // surviving commits relate. Memoized: a long hidden run is walked once.
    std::unordered_map<std::string, std::string> resolved;
    std::function<std::string(const std::string&, bool&)> resolveVisibleAncestor =
        [&](const std::string& sha, bool& viaHidden) -> std::string {
            if (result.Find(sha)) return sha;
            viaHidden = true;

            auto cached = resolved.find(sha);
            if (cached != resolved.end()) return cached->second;

            // Breadth-first so the nearest visible ancestor wins.
            std::deque<std::string> queue{sha};
            std::unordered_set<std::string> visited{sha};
            std::string answer;
            while (!queue.empty() && answer.empty()) {
                const std::string current = queue.front();
                queue.pop_front();

                auto source = bySha.find(current);
                if (source == bySha.end()) continue;
                for (const std::string& parent : commits[source->second].parents) {
                    if (!visited.insert(parent).second) continue;
                    if (result.Find(parent)) { answer = parent; break; }
                    queue.push_back(parent);
                }
            }
            resolved[sha] = answer;
            return answer;
        };

    std::unordered_set<std::string> emitted;

    for (GitGraphPlacedCommit& child : result.commits) {
        auto childSourceIt = bySha.find(child.sha);
        if (childSourceIt == bySha.end()) continue;
        const GitGraphCommit& commit = commits[childSourceIt->second];

        for (size_t p = 0; p < commit.parents.size(); ++p) {
            bool viaHidden = false;
            const std::string target = resolveVisibleAncestor(commit.parents[p], viaHidden);
            const GitGraphPlacedCommit* parent = target.empty() ? nullptr : result.Find(target);
            if (!parent) {
                child.isBoundary = true;      // History truncated above this commit
                continue;
            }

            GitGraphPlacedEdge edge;
            edge.childSha  = child.sha;
            edge.parentSha = parent->sha;
            edge.fromRow   = child.row;
            edge.fromLane  = child.lane;
            edge.toRow     = parent->row;
            edge.toLane    = parent->lane;
            edge.kind      = viaHidden
                                 ? GitGraphEdgeKind::Skipped
                                 : ((p == 0) ? GitGraphEdgeKind::Parent
                                             : GitGraphEdgeKind::Merge);

            // A first-parent edge runs down the child's lane and turns in at the
            // parent (a branch growing out of its base); a merge edge leaves the
            // merge commit sideways and then runs down the parent's lane.
            edge.turnAtChild = (p > 0);
            edge.colorLane   = (p == 0) ? child.lane : parent->lane;

            // Filtering can collapse several parents onto one ancestor.
            if (!emitted.insert(edge.childSha + ">" + edge.parentSha).second) continue;

            result.edges.push_back(edge);
        }

        if (!commit.cherryPickSource.empty()) {
            const GitGraphPlacedCommit* source = result.Find(commit.cherryPickSource);
            if (source) {
                GitGraphPlacedEdge edge;
                edge.childSha    = child.sha;
                edge.parentSha   = source->sha;
                edge.fromRow     = child.row;
                edge.fromLane    = child.lane;
                edge.toRow       = source->row;
                edge.toLane      = source->lane;
                edge.kind        = GitGraphEdgeKind::CherryPick;
                edge.turnAtChild = true;
                edge.colorLane   = child.lane;
                result.edges.push_back(edge);
            }
        }
    }
}

// ===== COLLAPSING LINEAR RUNS =====

void UltraCanvasGitGraphLayout::CollapseRuns(
        const std::vector<GitGraphCommit>& commits,
        const std::vector<size_t>& order,
        const std::unordered_map<std::string, size_t>& bySha,
        const std::vector<GitGraphRef>& refs,
        std::unordered_set<std::string>& hidden,
        std::unordered_map<std::string, int>& runLengths) const {

    if (!options.collapseLinearRuns || options.minCollapsibleRun < 2) return;

    // A commit is foldable only if nothing else needs to point at it: no ref, no
    // merge, no root, exactly one parent and exactly one child.
    std::unordered_set<std::string> decorated;
    for (const GitGraphRef& ref : refs) decorated.insert(ref.sha);

    std::unordered_map<std::string, int> childCount;
    for (size_t index : order) {
        for (const std::string& parent : commits[index].parents) {
            if (bySha.count(parent)) ++childCount[parent];
        }
    }

    auto foldable = [&](size_t index) {
        const GitGraphCommit& commit = commits[index];
        if (commit.parents.size() != 1) return false;        // Merge or root
        if (!bySha.count(commit.parents.front())) return false;
        if (decorated.count(commit.sha)) return false;
        if (options.keepExpanded.count(commit.sha)) return false;
        if (!commit.cherryPickSource.empty()) return false;

        auto children = childCount.find(commit.sha);
        return children != childCount.end() && children->second == 1;
    };

    // Walk the resolved order and fold maximal runs of foldable commits. The
    // first commit of a run survives and carries the count; the rest are hidden
    // and their edges bridge through the existing skipped-edge path.
    size_t position = 0;
    while (position < order.size()) {
        if (!foldable(order[position])) { ++position; continue; }

        size_t end = position;
        while (end + 1 < order.size() && foldable(order[end + 1])) {
            // Only fold commits that really follow each other in history.
            const GitGraphCommit& current = commits[order[end]];
            const GitGraphCommit& next    = commits[order[end + 1]];
            if (current.parents.front() != next.sha) break;
            ++end;
        }

        const int length = static_cast<int>(end - position) + 1;
        if (length >= options.minCollapsibleRun) {
            runLengths[commits[order[position]].sha] = length;
            for (size_t i = position + 1; i <= end; ++i) {
                hidden.insert(commits[order[i]].sha);
            }
        }
        position = end + 1;
    }
}

// ===== PARALLEL ROWS =====

void UltraCanvasGitGraphLayout::ApplyParallelRows(
        const std::vector<GitGraphCommit>& commits,
        const std::unordered_map<std::string, size_t>& bySha,
        GitGraphLayoutResult& result) const {

    if (!options.parallelCommits || result.commits.size() < 2) return;

    auto dateOf = [&](const GitGraphPlacedCommit& placed) -> int64_t {
        auto it = bySha.find(placed.sha);
        return (it == bySha.end()) ? 0 : commits[it->second].commitDate;
    };

    // Two commits may share a row only when neither is the other's parent -
    // otherwise an edge would collapse to zero length and the "parents come
    // later" invariant would break.
    auto related = [&](const GitGraphPlacedCommit& a, const GitGraphPlacedCommit& b) {
        auto ia = bySha.find(a.sha);
        auto ib = bySha.find(b.sha);
        if (ia == bySha.end() || ib == bySha.end()) return true;

        for (const std::string& parent : commits[ia->second].parents) {
            if (parent == b.sha) return true;
        }
        for (const std::string& parent : commits[ib->second].parents) {
            if (parent == a.sha) return true;
        }
        return false;
    };

    int nextRow = 0;
    size_t groupStart = 0;
    std::vector<int> assigned(result.commits.size(), 0);

    while (groupStart < result.commits.size()) {
        size_t groupEnd = groupStart;

        while (groupEnd + 1 < result.commits.size()) {
            const GitGraphPlacedCommit& candidate = result.commits[groupEnd + 1];
            const int64_t candidateDate = dateOf(candidate);

            bool joins = true;
            for (size_t i = groupStart; i <= groupEnd; ++i) {
                const GitGraphPlacedCommit& member = result.commits[i];
                if (std::llabs(candidateDate - dateOf(member)) > options.parallelTolerance ||
                    member.lane == candidate.lane ||
                    related(member, candidate)) {
                    joins = false;
                    break;
                }
            }
            if (!joins) break;
            ++groupEnd;
        }

        for (size_t i = groupStart; i <= groupEnd; ++i) assigned[i] = nextRow;
        ++nextRow;
        groupStart = groupEnd + 1;
    }

    for (size_t i = 0; i < result.commits.size(); ++i) result.commits[i].row = assigned[i];
    result.rowCount = nextRow;
}

// ===== CROSSING REDUCTION =====

size_t GitGraphLayoutResult::CountCrossings() const {
    // Each edge is treated as a straight segment in (row, lane) space. Two
    // segments cross when their row spans overlap and their lane order swaps
    // across that overlap. Edges meeting at a shared commit are not crossings.
    auto laneAt = [](const GitGraphPlacedEdge& edge, double row) {
        const double span = static_cast<double>(edge.toRow - edge.fromRow);
        if (std::fabs(span) < 1e-9) return static_cast<double>(edge.fromLane);
        const double t = (row - edge.fromRow) / span;
        return edge.fromLane + t * (edge.toLane - edge.fromLane);
    };

    size_t crossings = 0;
    for (size_t i = 0; i < edges.size(); ++i) {
        const GitGraphPlacedEdge& a = edges[i];
        const double aLow  = std::min(a.fromRow, a.toRow);
        const double aHigh = std::max(a.fromRow, a.toRow);

        for (size_t j = i + 1; j < edges.size(); ++j) {
            const GitGraphPlacedEdge& b = edges[j];
            if (a.childSha == b.childSha || a.childSha == b.parentSha ||
                a.parentSha == b.childSha || a.parentSha == b.parentSha) {
                continue;
            }

            const double low  = std::max(aLow,  static_cast<double>(std::min(b.fromRow, b.toRow)));
            const double high = std::min(aHigh, static_cast<double>(std::max(b.fromRow, b.toRow)));
            if (low >= high) continue;

            const double startDelta = laneAt(a, low)  - laneAt(b, low);
            const double endDelta   = laneAt(a, high) - laneAt(b, high);
            if (startDelta * endDelta < 0.0) ++crossings;
        }
    }
    return crossings;
}

void UltraCanvasGitGraphLayout::ReduceCrossings(GitGraphLayoutResult& result) const {
    if (result.commits.empty() || result.maxLane <= 1) return;

    const int laneCount = result.maxLane + 1;

    // Lanes that touch each other through an edge. The barycentre pass pulls
    // connected lanes towards each other, which is what removes crossings.
    std::vector<std::vector<int>> neighbours(static_cast<size_t>(laneCount));
    for (const GitGraphPlacedEdge& edge : result.edges) {
        if (edge.fromLane == edge.toLane) continue;
        if (edge.fromLane < 0 || edge.toLane < 0) continue;
        if (edge.fromLane >= laneCount || edge.toLane >= laneCount) continue;
        neighbours[static_cast<size_t>(edge.fromLane)].push_back(edge.toLane);
        neighbours[static_cast<size_t>(edge.toLane)].push_back(edge.fromLane);
    }

    // columnOf[lane] -> drawn column. Lane 0 is pinned: it is either the trunk
    // or the leftmost lane, and moving it is never what the reader wants.
    std::vector<int> columnOf(static_cast<size_t>(laneCount));
    for (int lane = 0; lane < laneCount; ++lane) columnOf[static_cast<size_t>(lane)] = lane;

    auto applyColumns = [&](const std::vector<int>& mapping, GitGraphLayoutResult& target) {
        for (GitGraphPlacedCommit& placed : target.commits) {
            if (placed.lane >= 0 && placed.lane < laneCount) {
                placed.lane = mapping[static_cast<size_t>(placed.lane)];
            }
        }
        for (GitGraphPlacedEdge& edge : target.edges) {
            if (edge.fromLane >= 0 && edge.fromLane < laneCount) {
                edge.fromLane = mapping[static_cast<size_t>(edge.fromLane)];
            }
            if (edge.toLane >= 0 && edge.toLane < laneCount) {
                edge.toLane = mapping[static_cast<size_t>(edge.toLane)];
            }
            if (edge.colorLane >= 0 && edge.colorLane < laneCount) {
                edge.colorLane = mapping[static_cast<size_t>(edge.colorLane)];
            }
        }
    };

    std::vector<int> bestMapping = columnOf;
    size_t bestCrossings = result.CountCrossings();

    for (int iteration = 0; iteration < 8 && bestCrossings > 0; ++iteration) {
        // Barycentre of each lane in the current column assignment.
        std::vector<std::pair<double, int>> ranked;
        ranked.reserve(static_cast<size_t>(laneCount));
        for (int lane = 0; lane < laneCount; ++lane) {
            const std::vector<int>& linked = neighbours[static_cast<size_t>(lane)];
            double barycentre = columnOf[static_cast<size_t>(lane)];
            if (!linked.empty()) {
                double total = 0.0;
                for (int other : linked) total += columnOf[static_cast<size_t>(other)];
                barycentre = total / static_cast<double>(linked.size());
            }
            ranked.emplace_back(barycentre, lane);
        }

        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
                             return a.first < b.first;
                         });

        std::vector<int> candidate(static_cast<size_t>(laneCount), 0);
        int nextColumn = 1;
        for (const std::pair<double, int>& entry : ranked) {
            if (entry.second == 0) continue;              // Lane 0 keeps column 0
            candidate[static_cast<size_t>(entry.second)] = nextColumn++;
        }

        // Measure the candidate on a copy before committing to it.
        GitGraphLayoutResult trial = result;
        applyColumns(candidate, trial);
        const size_t crossings = trial.CountCrossings();

        columnOf = candidate;
        if (crossings < bestCrossings) {
            bestCrossings = crossings;
            bestMapping = candidate;
        }
    }

    bool changed = false;
    for (int lane = 0; lane < laneCount; ++lane) {
        if (bestMapping[static_cast<size_t>(lane)] != lane) changed = true;
    }
    if (!changed) return;

    applyColumns(bestMapping, result);

    result.maxLane = 0;
    for (const GitGraphPlacedCommit& placed : result.commits) {
        result.maxLane = std::max(result.maxLane, placed.lane);
        result.minLane = std::min(result.minLane, placed.lane);
    }
}

// ===== ENTRY POINT =====

GitGraphLayoutResult UltraCanvasGitGraphLayout::Compute(
        const std::vector<GitGraphCommit>& commits,
        const std::vector<GitGraphRef>& refs) const {

    GitGraphLayoutResult result;
    if (commits.empty()) return result;

    std::unordered_map<std::string, size_t> bySha;
    bySha.reserve(commits.size() * 2);
    for (size_t i = 0; i < commits.size(); ++i) {
        bySha.emplace(commits[i].sha, i);
    }

    // Collapsing is expressed through the same hidden-commit machinery as
    // filtering, so the run interior bridges with skipped edges for free.
    UltraCanvasGitGraphLayout effective(*this);
    std::unordered_map<std::string, int> runLengths;
    if (options.collapseLinearRuns) {
        std::unordered_set<std::string> hidden = options.hiddenCommits;
        CollapseRuns(commits, ResolveOrder(commits), bySha, refs, hidden, runLengths);
        effective.options.hiddenCommits = hidden;
    }

    const std::vector<size_t> order = effective.ResolveOrder(commits);

    if (options.layoutMode == GitGraphLayoutMode::Swimlane) {
        effective.AssignSwimlanes(commits, order, bySha, refs, result);
    } else {
        // Trunk membership: the first-parent chain from the pinned branch's tip.
        std::vector<bool> onTrunk(commits.size(), false);
        if (!options.trunkBranch.empty()) {
            std::string tip;
            for (const GitGraphRef& ref : refs) {
                if ((ref.type == GitGraphRefType::LocalBranch ||
                     ref.type == GitGraphRefType::RemoteBranch) &&
                    ref.name == options.trunkBranch) {
                    tip = ref.sha;
                    break;
                }
            }
            if (tip.empty()) {
                for (const GitGraphCommit& commit : commits) {
                    if (commit.branch == options.trunkBranch) { tip = commit.sha; break; }
                }
            }

            std::string cursor = tip;
            while (!cursor.empty()) {
                auto it = bySha.find(cursor);
                if (it == bySha.end()) break;
                if (onTrunk[it->second]) break;          // Guard against cycles
                onTrunk[it->second] = true;
                cursor = commits[it->second].parents.empty()
                             ? std::string()
                             : commits[it->second].parents.front();
            }
        }

        effective.AssignLanes(commits, order, bySha, onTrunk, result);
    }

    for (GitGraphPlacedCommit& placed : result.commits) {
        auto run = runLengths.find(placed.sha);
        if (run != runLengths.end()) placed.collapsedCount = run->second;
    }

    ApplyParallelRows(commits, bySha, result);
    effective.BuildEdges(commits, bySha, result);

    result.hiddenCount = 0;
    for (const GitGraphCommit& commit : commits) {
        if (effective.options.hiddenCommits.count(commit.sha) > 0) ++result.hiddenCount;
    }

    // Column reordering only makes sense for lane graphs - a swimlane band is
    // semantic, so its position is not ours to change.
    if (options.reduceCrossings &&
        options.layoutMode == GitGraphLayoutMode::Lanes &&
        result.edges.size() <= options.crossingReductionEdgeBudget) {
        ReduceCrossings(result);
    }

    return result;
}

} // namespace UltraCanvas
