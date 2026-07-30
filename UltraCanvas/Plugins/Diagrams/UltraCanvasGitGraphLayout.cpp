// Plugins/Diagrams/UltraCanvasGitGraphLayout.cpp
// Commit ordering, lane assignment and edge routing for UltraCanvasGitGraph.
// Pure geometry - no rendering dependencies.
//
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasGitGraphLayout.h"

#include <algorithm>
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
int AllocateLane(std::vector<LaneSlot>& lanes, GitGraphLaneStrategy strategy) {
    if (strategy == GitGraphLaneStrategy::Compact) {
        for (size_t i = 0; i < lanes.size(); ++i) {
            if (!lanes[i].active && !lanes[i].reserved) {
                lanes[i].active = true;
                lanes[i].expecting.clear();
                return static_cast<int>(i);
            }
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

            const int mergeLane = AllocateLane(lanes, options.laneStrategy);
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

    for (GitGraphPlacedCommit& child : result.commits) {
        auto childSourceIt = bySha.find(child.sha);
        if (childSourceIt == bySha.end()) continue;
        const GitGraphCommit& commit = commits[childSourceIt->second];

        for (size_t p = 0; p < commit.parents.size(); ++p) {
            const GitGraphPlacedCommit* parent = result.Find(commit.parents[p]);
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
            edge.kind      = (p == 0) ? GitGraphEdgeKind::Parent : GitGraphEdgeKind::Merge;

            // A first-parent edge runs down the child's lane and turns in at the
            // parent (a branch growing out of its base); a merge edge leaves the
            // merge commit sideways and then runs down the parent's lane.
            edge.turnAtChild = (p > 0);
            edge.colorLane   = (p == 0) ? child.lane : parent->lane;

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

    const std::vector<size_t> order = ResolveOrder(commits);

    if (options.layoutMode == GitGraphLayoutMode::Swimlane) {
        AssignSwimlanes(commits, order, bySha, refs, result);
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

        AssignLanes(commits, order, bySha, onTrunk, result);
    }

    BuildEdges(commits, bySha, result);
    return result;
}

} // namespace UltraCanvas
