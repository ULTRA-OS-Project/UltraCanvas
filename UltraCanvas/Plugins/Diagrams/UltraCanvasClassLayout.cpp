// Plugins/Diagrams/UltraCanvasClassLayout.cpp
// Automatic layout for class diagrams
// Version: 1.0.0
// Last Modified: 2026-08-02
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasClassLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace UltraCanvas {

namespace {

using IndexMap = std::unordered_map<std::string, size_t>;

IndexMap BuildIndex(const std::vector<ClassLayoutNode>& nodes) {
    IndexMap index;
    index.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        index.emplace(nodes[i].id, i);
    }
    return index;
}

// Edges resolved to indices, so the algorithms never touch strings again.
struct ResolvedEdges {
    // parents[i] = indices of i's hierarchical parents (targets of a
    // generalization/realization whose source is i).
    std::vector<std::vector<size_t>> parents;
    std::vector<std::vector<size_t>> children;
    // Undirected neighbours over ALL edges, used for barycentre ordering.
    std::vector<std::vector<size_t>> neighbours;
};

ResolvedEdges Resolve(const std::vector<ClassLayoutNode>& nodes,
                      const std::vector<ClassLayoutEdge>& edges) {
    IndexMap index = BuildIndex(nodes);
    ResolvedEdges resolved;
    resolved.parents.resize(nodes.size());
    resolved.children.resize(nodes.size());
    resolved.neighbours.resize(nodes.size());

    for (const auto& edge : edges) {
        auto sourceIt = index.find(edge.sourceId);
        auto targetIt = index.find(edge.targetId);
        if (sourceIt == index.end() || targetIt == index.end()) continue;  // dangling
        size_t source = sourceIt->second;
        size_t target = targetIt->second;
        if (source == target) continue;  // self edge: no layout meaning

        if (edge.hierarchical) {
            resolved.parents[source].push_back(target);
            resolved.children[target].push_back(source);
        }
        resolved.neighbours[source].push_back(target);
        resolved.neighbours[target].push_back(source);
    }
    return resolved;
}

// Longest-path ranking, iterative so a deep hierarchy cannot blow the stack,
// and cycle-safe: a node currently being resolved does not feed its own rank.
std::vector<int> RankNodes(const ResolvedEdges& edges, size_t count) {
    std::vector<int> rank(count, -1);
    std::vector<char> onPath(count, 0);

    for (size_t start = 0; start < count; ++start) {
        if (rank[start] >= 0) continue;

        // (node, next parent to visit)
        std::vector<std::pair<size_t, size_t>> stack;
        stack.emplace_back(start, 0);
        onPath[start] = 1;

        while (!stack.empty()) {
            size_t node = stack.back().first;
            size_t& cursor = stack.back().second;
            const std::vector<size_t>& parents = edges.parents[node];

            if (cursor < parents.size()) {
                size_t parent = parents[cursor++];
                if (onPath[parent]) continue;      // cycle: ignore this edge
                if (rank[parent] >= 0) continue;   // already known
                onPath[parent] = 1;
                stack.emplace_back(parent, 0);
                continue;
            }

            int best = -1;
            for (size_t parent : parents) {
                if (onPath[parent] && parent != node) {
                    // Parent is an ancestor on the current path (a cycle);
                    // its rank is not final, so it cannot raise ours.
                    continue;
                }
                best = std::max(best, rank[parent]);
            }
            rank[node] = best + 1;
            onPath[node] = 0;
            stack.pop_back();
        }
    }

    for (size_t i = 0; i < count; ++i) {
        if (rank[i] < 0) rank[i] = 0;
    }
    return rank;
}

// Median/barycentre ordering sweep. `order[r]` is the list of node indices in
// rank r, in drawing order.
void ReduceCrossings(std::vector<std::vector<size_t>>& order,
                     const ResolvedEdges& edges,
                     const std::vector<int>& rank,
                     int passes) {
    if (order.size() < 2 || passes <= 0) return;

    std::vector<size_t> positionInRank(rank.size(), 0);
    auto refreshPositions = [&]() {
        for (const auto& rankNodes : order) {
            for (size_t i = 0; i < rankNodes.size(); ++i) {
                positionInRank[rankNodes[i]] = i;
            }
        }
    };
    refreshPositions();

    for (int pass = 0; pass < passes; ++pass) {
        bool downward = (pass % 2) == 0;
        // Sweep from the second rank in the chosen direction so each rank is
        // ordered against an already-placed neighbour rank.
        for (size_t step = 1; step < order.size(); ++step) {
            size_t r = downward ? step : order.size() - 1 - step;
            int neighbourRank = downward ? static_cast<int>(r) - 1
                                         : static_cast<int>(r) + 1;

            std::vector<std::pair<double, size_t>> keyed;
            keyed.reserve(order[r].size());
            for (size_t slot = 0; slot < order[r].size(); ++slot) {
                size_t node = order[r][slot];
                double sum = 0.0;
                int seen = 0;
                for (size_t neighbour : edges.neighbours[node]) {
                    if (rank[neighbour] != neighbourRank) continue;
                    sum += static_cast<double>(positionInRank[neighbour]);
                    ++seen;
                }
                // No neighbour in that rank: keep the current slot, so nodes
                // do not drift for lack of information.
                double key = seen ? (sum / seen) : static_cast<double>(slot);
                keyed.emplace_back(key, node);
            }

            std::stable_sort(keyed.begin(), keyed.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            for (size_t slot = 0; slot < keyed.size(); ++slot) {
                order[r][slot] = keyed[slot].second;
            }
            refreshPositions();
        }
    }
}

// A node's extent along the layout's main axis (rank to rank) and cross axis
// (within a rank).
bool IsHorizontalFlow(ClassLayoutDirection direction) {
    return direction == ClassLayoutDirection::LeftToRight ||
           direction == ClassLayoutDirection::RightToLeft;
}

double MainExtent(const ClassLayoutNode& node, ClassLayoutDirection direction) {
    return IsHorizontalFlow(direction) ? node.width : node.height;
}

double CrossExtent(const ClassLayoutNode& node, ClassLayoutDirection direction) {
    return IsHorizontalFlow(direction) ? node.height : node.width;
}

// Writes main/cross coordinates back as x/y for the requested direction.
void AssignCoordinates(ClassLayoutNode& node, double main, double cross,
                       double mainTotal, double crossTotal,
                       ClassLayoutDirection direction,
                       double marginX, double marginY) {
    switch (direction) {
        case ClassLayoutDirection::TopToBottom:
            node.x = marginX + cross;
            node.y = marginY + main;
            break;
        case ClassLayoutDirection::BottomToTop:
            node.x = marginX + cross;
            node.y = marginY + (mainTotal - main - node.height);
            break;
        case ClassLayoutDirection::LeftToRight:
            node.x = marginX + main;
            node.y = marginY + cross;
            break;
        case ClassLayoutDirection::RightToLeft:
            node.x = marginX + (mainTotal - main - node.width);
            node.y = marginY + cross;
            break;
    }
    (void)crossTotal;
}

// Pushes overlapping neighbours apart, left to right, preserving order.
void SeparateWithinRank(std::vector<double>& crossPositions,
                        const std::vector<double>& crossExtents,
                        double separation) {
    for (size_t i = 1; i < crossPositions.size(); ++i) {
        double minimum = crossPositions[i - 1] + crossExtents[i - 1] + separation;
        if (crossPositions[i] < minimum) crossPositions[i] = minimum;
    }
}

} // namespace

// =============================================================================
// PUBLIC API
// =============================================================================

Rect2Dd UltraCanvasClassLayout::ComputeBounds(const std::vector<ClassLayoutNode>& nodes) {
    if (nodes.empty()) return Rect2Dd(0, 0, 0, 0);

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();

    for (const auto& node : nodes) {
        minX = std::min(minX, node.x);
        minY = std::min(minY, node.y);
        maxX = std::max(maxX, node.x + node.width);
        maxY = std::max(maxY, node.y + node.height);
    }
    return Rect2Dd(minX, minY, maxX - minX, maxY - minY);
}

std::vector<int> UltraCanvasClassLayout::ComputeRanks(
        const std::vector<ClassLayoutNode>& nodes,
        const std::vector<ClassLayoutEdge>& edges) {
    ResolvedEdges resolved = Resolve(nodes, edges);
    return RankNodes(resolved, nodes.size());
}

Rect2Dd UltraCanvasClassLayout::Apply(std::vector<ClassLayoutNode>& nodes,
                                      const std::vector<ClassLayoutEdge>& edges,
                                      const ClassLayoutOptions& options) {
    if (nodes.empty()) return Rect2Dd(0, 0, 0, 0);
    if (options.kind == ClassLayoutKind::Manual) return ComputeBounds(nodes);

    // Pinned nodes keep their positions and take no part in placement.
    std::vector<size_t> movable;
    movable.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (!nodes[i].pinned) movable.push_back(i);
    }
    if (movable.empty()) return ComputeBounds(nodes);

    // ---- Grid ----------------------------------------------------------
    if (options.kind == ClassLayoutKind::Grid) {
        int columns = options.gridColumns > 0
            ? options.gridColumns
            : std::max(1, static_cast<int>(std::ceil(std::sqrt(
                  static_cast<double>(movable.size())))));

        size_t rows = (movable.size() + static_cast<size_t>(columns) - 1) /
                      static_cast<size_t>(columns);

        std::vector<double> columnWidths(static_cast<size_t>(columns), 0.0);
        std::vector<double> rowHeights(rows, 0.0);
        for (size_t slot = 0; slot < movable.size(); ++slot) {
            size_t column = slot % static_cast<size_t>(columns);
            size_t row = slot / static_cast<size_t>(columns);
            const ClassLayoutNode& node = nodes[movable[slot]];
            columnWidths[column] = std::max(columnWidths[column], node.width);
            rowHeights[row] = std::max(rowHeights[row], node.height);
        }

        std::vector<double> columnOffsets(static_cast<size_t>(columns), 0.0);
        for (size_t c = 1; c < columnOffsets.size(); ++c) {
            columnOffsets[c] = columnOffsets[c - 1] + columnWidths[c - 1] + options.nodeSeparation;
        }
        std::vector<double> rowOffsets(rows, 0.0);
        for (size_t r = 1; r < rows; ++r) {
            rowOffsets[r] = rowOffsets[r - 1] + rowHeights[r - 1] + options.rankSeparation;
        }

        for (size_t slot = 0; slot < movable.size(); ++slot) {
            size_t column = slot % static_cast<size_t>(columns);
            size_t row = slot / static_cast<size_t>(columns);
            ClassLayoutNode& node = nodes[movable[slot]];
            node.x = options.marginX + columnOffsets[column];
            node.y = options.marginY + rowOffsets[row];
        }
        return ComputeBounds(nodes);
    }

    // ---- Layered / Tree ------------------------------------------------
    ResolvedEdges resolved = Resolve(nodes, edges);
    std::vector<int> rank = RankNodes(resolved, nodes.size());

    int maxRank = 0;
    for (size_t i : movable) maxRank = std::max(maxRank, rank[i]);

    std::vector<std::vector<size_t>> order(static_cast<size_t>(maxRank) + 1);
    for (size_t i : movable) {
        order[static_cast<size_t>(rank[i])].push_back(i);
    }

    ReduceCrossings(order, resolved, rank, options.crossingReductionPasses);

    // Main-axis offset per rank: each rank starts after the tallest (or widest)
    // box of the previous one.
    std::vector<double> rankMain(order.size(), 0.0);
    std::vector<double> rankExtent(order.size(), 0.0);
    for (size_t r = 0; r < order.size(); ++r) {
        double extent = 0.0;
        for (size_t node : order[r]) {
            extent = std::max(extent, MainExtent(nodes[node], options.direction));
        }
        rankExtent[r] = extent;
        if (r > 0) rankMain[r] = rankMain[r - 1] + rankExtent[r - 1] + options.rankSeparation;
    }

    // Cross-axis positions, rank by rank.
    std::vector<std::vector<double>> crossPositions(order.size());
    for (size_t r = 0; r < order.size(); ++r) {
        std::vector<double> extents;
        extents.reserve(order[r].size());
        for (size_t node : order[r]) {
            extents.push_back(CrossExtent(nodes[node], options.direction));
        }
        crossPositions[r].assign(order[r].size(), 0.0);
        double cursor = 0.0;
        for (size_t slot = 0; slot < order[r].size(); ++slot) {
            crossPositions[r][slot] = cursor;
            cursor += extents[slot] + options.nodeSeparation;
        }
    }

    // Tree refinement: centre each parent over its children, deepest rank
    // first, then re-separate so the centring cannot create overlaps
    // (proposal L3).
    if (options.kind == ClassLayoutKind::Tree && order.size() > 1) {
        std::unordered_map<size_t, std::pair<size_t, size_t>> slotOf;  // node -> (rank, slot)
        for (size_t r = 0; r < order.size(); ++r) {
            for (size_t slot = 0; slot < order[r].size(); ++slot) {
                slotOf[order[r][slot]] = {r, slot};
            }
        }

        for (size_t step = order.size() - 1; step-- > 0;) {
            for (size_t slot = 0; slot < order[step].size(); ++slot) {
                size_t node = order[step][slot];
                double sum = 0.0;
                int seen = 0;
                for (size_t child : resolved.children[node]) {
                    auto it = slotOf.find(child);
                    if (it == slotOf.end()) continue;
                    if (it->second.first != step + 1) continue;  // not the next rank
                    double childCross = crossPositions[it->second.first][it->second.second];
                    sum += childCross + CrossExtent(nodes[child], options.direction) * 0.5;
                    ++seen;
                }
                if (!seen) continue;
                double centre = sum / seen;
                crossPositions[step][slot] =
                    centre - CrossExtent(nodes[node], options.direction) * 0.5;
            }

            std::vector<double> extents;
            extents.reserve(order[step].size());
            for (size_t node : order[step]) {
                extents.push_back(CrossExtent(nodes[node], options.direction));
            }
            SeparateWithinRank(crossPositions[step], extents, options.nodeSeparation);
        }
    }

    // Centre every rank on the widest one, so the drawing is symmetric.
    double crossTotal = 0.0;
    for (size_t r = 0; r < order.size(); ++r) {
        if (order[r].empty()) continue;
        size_t last = order[r].size() - 1;
        double extent = crossPositions[r][last] +
                        CrossExtent(nodes[order[r][last]], options.direction) -
                        crossPositions[r][0];
        crossTotal = std::max(crossTotal, extent);
    }
    double mainTotal = order.empty() ? 0.0 : (rankMain.back() + rankExtent.back());

    for (size_t r = 0; r < order.size(); ++r) {
        if (order[r].empty()) continue;
        size_t last = order[r].size() - 1;
        double rankStart = crossPositions[r][0];
        double rankWidth = crossPositions[r][last] +
                           CrossExtent(nodes[order[r][last]], options.direction) - rankStart;
        double shift = (crossTotal - rankWidth) * 0.5 - rankStart;

        for (size_t slot = 0; slot < order[r].size(); ++slot) {
            ClassLayoutNode& node = nodes[order[r][slot]];
            // Centre the box within its rank band on the main axis, so ranks
            // of mixed heights still line up visually.
            double mainOffset = rankMain[r] +
                (rankExtent[r] - MainExtent(node, options.direction)) * 0.5;
            AssignCoordinates(node, mainOffset, crossPositions[r][slot] + shift,
                              mainTotal, crossTotal, options.direction,
                              options.marginX, options.marginY);
        }
    }

    return ComputeBounds(nodes);
}

} // namespace UltraCanvas
