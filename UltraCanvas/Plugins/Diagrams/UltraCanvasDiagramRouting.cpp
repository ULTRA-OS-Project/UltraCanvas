// Plugins/Diagrams/UltraCanvasDiagramRouting.cpp
// Shared orthogonal connection routing for the diagram family
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework
//
// Extraction of the router developed in UltraCanvasFlowChart.cpp (2.1.4
// cardinal routing, 2.2.0 A*). The algorithm and its constants are unchanged;
// see the header for the API differences and for the three defects in the
// extracted code that are fixed here.

#include "Plugins/Diagrams/UltraCanvasDiagramRouting.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>

namespace UltraCanvas {

namespace {

// A* node state. Coordinates are grid-cell indices, not pixels.
struct AStarNode {
    int x, y;
    int dirFromParent;  // 0=N, 1=E, 2=S, 3=W, -1=start (no parent)
    double g;           // cost from start
    double f;           // g + heuristic
    int parentIdx;      // index into the visited vector, -1 if start
};

struct AStarOpenEntry {
    double f;
    int idx;
    bool operator<(const AStarOpenEntry& o) const { return f > o.f; }  // min-heap
};

inline uint64_t PackCellDir(int x, int y, int d) {
    // (x, y, dir) packed into a single key. x,y < 65536, d in [0,3].
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32)
         | (static_cast<uint64_t>(static_cast<uint32_t>(y)) << 4)
         | static_cast<uint64_t>(d & 0xF);
}

// Step offset for "one cell outside the box" on the given side.
inline std::pair<int, int> SideOffset(DiagramCardinalSide s) {
    switch (s) {
        case DiagramCardinalSide::Top:    return { 0, -1};
        case DiagramCardinalSide::Right:  return { 1,  0};
        case DiagramCardinalSide::Bottom: return { 0,  1};
        case DiagramCardinalSide::Left:   return {-1,  0};
    }
    return {0, 0};
}

// Direction index the path travels when leaving the given side.
inline int SideDir(DiagramCardinalSide s) {
    switch (s) {
        case DiagramCardinalSide::Top:    return 0;  // moving north
        case DiagramCardinalSide::Right:  return 1;  // moving east
        case DiagramCardinalSide::Bottom: return 2;  // moving south
        case DiagramCardinalSide::Left:   return 3;  // moving west
    }
    return 1;
}

// Direction index the path must travel to enter the given side head-on.
inline int RequiredApproachDir(DiagramCardinalSide s) {
    switch (s) {
        case DiagramCardinalSide::Top:    return 2;  // approach by moving down
        case DiagramCardinalSide::Right:  return 3;  // approach by moving left
        case DiagramCardinalSide::Bottom: return 0;  // approach by moving up
        case DiagramCardinalSide::Left:   return 1;  // approach by moving right
    }
    return 0;
}

// Axis comparisons are exact (bar floating-point noise): anything looser lets
// a "nearly aligned" pair through as a segment that is not actually axis
// aligned, which is exactly the diagonal-jog defect this pass exists to remove.
constexpr double kAxisEpsilon = 1e-9;

// Drops consecutive duplicates and collapses collinear runs, so the path has
// one waypoint per corner and no zero-length segments (a zero-length final
// segment makes the arrow-angle computation meaningless).
// Single guarded pass: each candidate is compared against what has actually
// been kept, never against the raw input. Comparing against the raw predecessor
// is subtly wrong — a path that revisits a point (a U-turn around an obstacle)
// has two equal-but-not-adjacent waypoints, and dropping what lies between them
// makes them adjacent, reintroducing the duplicate the pass just removed.
std::vector<Point2Dd> NormalizePath(const std::vector<Point2Dd>& points) {
    std::vector<Point2Dd> out;
    out.reserve(points.size());

    for (const auto& p : points) {
        if (!out.empty() &&
            std::abs(p.x - out.back().x) <= kAxisEpsilon &&
            std::abs(p.y - out.back().y) <= kAxisEpsilon) {
            continue;  // zero-length segment
        }

        // If the point currently on top lies mid-way along a straight run, it
        // is not a corner. A reversal (same axis, opposite direction) IS a
        // corner and is kept.
        while (out.size() >= 2) {
            const Point2Dd& a = out[out.size() - 2];
            const Point2Dd& b = out.back();
            bool inHorizontal  = std::abs(b.y - a.y) <= kAxisEpsilon;
            bool outHorizontal = std::abs(p.y - b.y) <= kAxisEpsilon;
            if (inHorizontal != outHorizontal) break;
            bool sameDirection = inHorizontal ? ((b.x - a.x) * (p.x - b.x) > 0.0)
                                              : ((b.y - a.y) * (p.y - b.y) > 0.0);
            if (!sameDirection) break;
            out.pop_back();
        }

        out.push_back(p);
    }
    return out;
}

// Inserts the corners needed to make every segment axis-aligned. The first
// segment is forced onto the axis of `sourceSide` and the last onto the axis of
// `targetSide`, so the path still leaves and enters its boxes head-on;
// intermediate bends alternate axes.
std::vector<Point2Dd> Orthogonalize(const std::vector<Point2Dd>& points,
                                    DiagramCardinalSide sourceSide,
                                    DiagramCardinalSide targetSide) {
    if (points.size() < 2) return points;

    auto isHorizontal = [](DiagramCardinalSide s) {
        return s == DiagramCardinalSide::Left || s == DiagramCardinalSide::Right;
    };

    std::vector<Point2Dd> out;
    out.reserve(points.size() * 2);
    out.push_back(points.front());

    for (size_t i = 1; i < points.size(); ++i) {
        const Point2Dd a = out.back();
        const Point2Dd b = points[i];
        bool movesX = std::abs(b.x - a.x) > kAxisEpsilon;
        bool movesY = std::abs(b.y - a.y) > kAxisEpsilon;

        if (movesX && movesY) {
            bool horizontalFirst;
            if (out.size() == 1) {
                // Leaving the source: travel along the source side's axis.
                horizontalFirst = isHorizontal(sourceSide);
            } else if (i + 1 == points.size()) {
                // Entering the target: the *final* segment must run along the
                // target side's axis, so bend on the other axis first.
                horizontalFirst = !isHorizontal(targetSide);
            } else {
                const Point2Dd& prev = out[out.size() - 2];
                bool incomingHorizontal = std::abs(prev.y - a.y) <= kAxisEpsilon;
                horizontalFirst = !incomingHorizontal;
            }
            out.push_back(horizontalFirst ? Point2Dd(b.x, a.y) : Point2Dd(a.x, b.y));
        }
        out.push_back(b);
    }
    return out;
}

} // namespace

// =============================================================================
// GEOMETRY HELPERS
// =============================================================================

bool UltraCanvasDiagramRouter::IsHorizontalSide(DiagramCardinalSide side) {
    return side == DiagramCardinalSide::Left || side == DiagramCardinalSide::Right;
}

DiagramCardinalSide UltraCanvasDiagramRouter::GetCardinalSide(const Point2Dd& boxCenter,
                                                              const Point2Dd& otherCenter) {
    double dx = otherCenter.x - boxCenter.x;
    double dy = otherCenter.y - boxCenter.y;
    if (std::abs(dx) >= std::abs(dy)) {
        return (dx >= 0.0) ? DiagramCardinalSide::Right : DiagramCardinalSide::Left;
    }
    return (dy >= 0.0) ? DiagramCardinalSide::Bottom : DiagramCardinalSide::Top;
}

Point2Dd UltraCanvasDiagramRouter::GetCardinalPoint(const Rect2Dd& box,
                                                    DiagramCardinalSide side) {
    double cx = box.x + box.width * 0.5;
    double cy = box.y + box.height * 0.5;
    switch (side) {
        case DiagramCardinalSide::Top:    return Point2Dd(cx, box.y);
        case DiagramCardinalSide::Right:  return Point2Dd(box.x + box.width, cy);
        case DiagramCardinalSide::Bottom: return Point2Dd(cx, box.y + box.height);
        case DiagramCardinalSide::Left:   return Point2Dd(box.x, cy);
    }
    return Point2Dd(cx, cy);
}

Point2Dd UltraCanvasDiagramRouter::GetDistributedCardinalPoint(const Rect2Dd& box,
                                                               DiagramCardinalSide side,
                                                               int index, int count,
                                                               double spreadFraction) {
    if (count <= 1 || index < 0 || index >= count) {
        return GetCardinalPoint(box, side);
    }
    // Spread the anchors over the middle `spreadFraction` of the side, so the
    // outermost anchors keep clear of the corners.
    spreadFraction = std::max(0.0, std::min(1.0, spreadFraction));
    double t = (static_cast<double>(index) + 0.5) / static_cast<double>(count);  // (0,1)
    double offset = (t - 0.5) * spreadFraction;                                  // (-f/2, f/2)

    double cx = box.x + box.width * 0.5;
    double cy = box.y + box.height * 0.5;
    switch (side) {
        case DiagramCardinalSide::Top:
            return Point2Dd(cx + offset * box.width, box.y);
        case DiagramCardinalSide::Bottom:
            return Point2Dd(cx + offset * box.width, box.y + box.height);
        case DiagramCardinalSide::Left:
            return Point2Dd(box.x, cy + offset * box.height);
        case DiagramCardinalSide::Right:
            return Point2Dd(box.x + box.width, cy + offset * box.height);
    }
    return Point2Dd(cx, cy);
}

Point2Dd UltraCanvasDiagramRouter::ComputeLongestSegmentAnchor(
        const std::vector<Point2Dd>& waypoints) {
    if (waypoints.size() < 2) return Point2Dd(0, 0);
    if (waypoints.size() == 2) {
        return Point2Dd((waypoints[0].x + waypoints[1].x) * 0.5,
                        (waypoints[0].y + waypoints[1].y) * 0.5);
    }
    size_t bestIdx = 0;
    double bestLen = 0.0;
    for (size_t i = 1; i < waypoints.size(); ++i) {
        double dx = waypoints[i].x - waypoints[i - 1].x;
        double dy = waypoints[i].y - waypoints[i - 1].y;
        double len = std::abs(dx) + std::abs(dy);  // Manhattan; segments are axis-aligned
        if (len > bestLen) {
            bestLen = len;
            bestIdx = i;
        }
    }
    return Point2Dd((waypoints[bestIdx - 1].x + waypoints[bestIdx].x) * 0.5,
                    (waypoints[bestIdx - 1].y + waypoints[bestIdx].y) * 0.5);
}

double UltraCanvasDiagramRouter::ComputeApproachAngle(DiagramCardinalSide targetSide) {
    switch (targetSide) {
        case DiagramCardinalSide::Top:    return  M_PI * 0.5;   // arriving going down
        case DiagramCardinalSide::Bottom: return -M_PI * 0.5;   // arriving going up
        case DiagramCardinalSide::Left:   return  0.0;          // arriving going right
        case DiagramCardinalSide::Right:  return  M_PI;         // arriving going left
    }
    return 0.0;
}

double UltraCanvasDiagramRouter::ComputeFinalSegmentAngle(
        const std::vector<Point2Dd>& waypoints) {
    if (waypoints.size() < 2) return 0.0;
    const Point2Dd& a = waypoints[waypoints.size() - 2];
    const Point2Dd& b = waypoints.back();
    return std::atan2(b.y - a.y, b.x - a.x);
}

// =============================================================================
// CARDINAL (L / Z) ROUTING
// =============================================================================

std::vector<Point2Dd> UltraCanvasDiagramRouter::ComputeCardinalPath(
        const Point2Dd& start, const Point2Dd& end,
        DiagramCardinalSide sourceSide, DiagramCardinalSide targetSide) {
    bool srcH = IsHorizontalSide(sourceSide);
    bool tgtH = IsHorizontalSide(targetSide);

    std::vector<Point2Dd> waypoints;
    waypoints.push_back(start);

    if (srcH != tgtH) {
        // L-shape: 1 corner.
        Point2Dd corner = srcH ? Point2Dd(end.x, start.y) : Point2Dd(start.x, end.y);
        waypoints.push_back(corner);
    } else if (srcH) {
        // Z-shape on the horizontal axis: vertical bend at midX.
        double midX = (start.x + end.x) * 0.5;
        waypoints.push_back(Point2Dd(midX, start.y));
        waypoints.push_back(Point2Dd(midX, end.y));
    } else {
        // Z-shape on the vertical axis: horizontal bend at midY.
        double midY = (start.y + end.y) * 0.5;
        waypoints.push_back(Point2Dd(start.x, midY));
        waypoints.push_back(Point2Dd(end.x, midY));
    }

    waypoints.push_back(end);
    // A degenerate geometry (start and end already sharing an axis) makes the
    // corner coincide with an endpoint; normalising keeps every segment real.
    return NormalizePath(waypoints);
}

// =============================================================================
// OBSTACLE TEST
// =============================================================================

bool UltraCanvasDiagramRouter::PathHasObstacles(const std::vector<Point2Dd>& path,
                                                const std::vector<DiagramObstacle>& obstacles,
                                                double inset) {
    if (path.size() < 2) return false;

    for (const auto& obstacle : obstacles) {
        const Rect2Dd& o = obstacle.bounds;
        double l = o.x + inset, r = o.x + o.width - inset;
        double t = o.y + inset, b = o.y + o.height - inset;

        for (size_t i = 1; i < path.size(); ++i) {
            const Point2Dd& a = path[i - 1];
            const Point2Dd& c = path[i];
            // All segments are axis-aligned by construction.
            if (std::abs(a.y - c.y) < 0.5) {
                // Horizontal segment at y = a.y.
                double y = a.y;
                if (y < t || y > b) continue;
                double xMin = std::min(a.x, c.x);
                double xMax = std::max(a.x, c.x);
                if (xMax >= l && xMin <= r) return true;
            } else if (std::abs(a.x - c.x) < 0.5) {
                // Vertical segment at x = a.x.
                double x = a.x;
                if (x < l || x > r) continue;
                double yMin = std::min(a.y, c.y);
                double yMax = std::max(a.y, c.y);
                if (yMax >= t && yMin <= b) return true;
            }
        }
    }
    return false;
}

// =============================================================================
// A* ROUTING
// =============================================================================

std::vector<Point2Dd> UltraCanvasDiagramRouter::RouteAStar(
        const Point2Dd& start, const Point2Dd& end,
        DiagramCardinalSide sourceSide, DiagramCardinalSide targetSide,
        const std::vector<DiagramObstacle>& obstacles,
        const DiagramRoutingOptions& options) {
    const double gridSize = options.gridSize > 0.0 ? options.gridSize : 20.0;
    const Rect2Dd& area = options.routingArea;

    // Grid extent in world coordinates, relative to the routing area origin.
    int gridW = std::max(2, static_cast<int>(std::ceil(area.width  / gridSize)));
    int gridH = std::max(2, static_cast<int>(std::ceil(area.height / gridSize)));

    const double pad = gridSize * options.obstaclePaddingCells;

    // A cell is blocked if its centre sits inside any obstacle (plus padding,
    // so paths do not graze borders) or falls outside the routing area.
    auto cellBlocked = [&](int cx, int cy) -> bool {
        if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) return true;
        double wx = area.x + (cx + 0.5) * gridSize;
        double wy = area.y + (cy + 0.5) * gridSize;
        for (const auto& obstacle : obstacles) {
            const Rect2Dd& o = obstacle.bounds;
            if (wx >= o.x - pad && wx <= o.x + o.width + pad &&
                wy >= o.y - pad && wy <= o.y + o.height + pad) {
                return true;
            }
        }
        return false;
    };

    auto worldToCell = [&](const Point2Dd& p) -> std::pair<int, int> {
        return {
            static_cast<int>(std::floor((p.x - area.x) / gridSize)),
            static_cast<int>(std::floor((p.y - area.y) / gridSize))
        };
    };

    auto srcCell = worldToCell(start);
    auto tgtCell = worldToCell(end);
    auto srcOff  = SideOffset(sourceSide);
    auto tgtOff  = SideOffset(targetSide);

    // Step ONE cell outside the source/target so the start/goal of the search
    // sit in free space (the cells inside those boxes are blocked for everyone
    // else, but we are using them as anchors).
    int sx = srcCell.first  + srcOff.first;
    int sy = srcCell.second + srcOff.second;
    int gx = tgtCell.first  + tgtOff.first;
    int gy = tgtCell.second + tgtOff.second;

    if (cellBlocked(sx, sy) || cellBlocked(gx, gy)) {
        return {};  // Anchor cells themselves are blocked: A* cannot start.
    }

    const int initialDir = SideDir(sourceSide);
    const int goalDir    = RequiredApproachDir(targetSide);

    // 4-connected grid moves: dx, dy per direction (0=N, 1=E, 2=S, 3=W).
    static const int DX[4] = { 0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1,  0};

    auto manhattan = [&](int x, int y) -> double {
        return static_cast<double>(std::abs(x - gx) + std::abs(y - gy));
    };

    std::vector<AStarNode> visited;
    visited.reserve(256);
    std::priority_queue<AStarOpenEntry> open;
    std::unordered_map<uint64_t, double> bestG;

    AStarNode startNode{sx, sy, initialDir, 0.0, manhattan(sx, sy), -1};
    visited.push_back(startNode);
    open.push({startNode.f, 0});
    bestG[PackCellDir(sx, sy, initialDir)] = 0.0;

    const double turnPenalty = options.turnPenalty;
    const int maxExpansions = options.maxExpansions;
    int expansions = 0;
    int goalIdx = -1;

    while (!open.empty() && expansions < maxExpansions) {
        AStarOpenEntry top = open.top();
        open.pop();
        ++expansions;

        // NOTE: a *copy*, not a reference. The expansion loop below pushes into
        // `visited`, and a reallocation would leave a reference dangling — a
        // use-after-free that FlowChart 2.2.0 shipped with and that fires as
        // soon as the search outgrows the reserved capacity.
        const AStarNode cur = visited[top.idx];

        // Goal: cell matches AND we arrived facing the right way.
        if (cur.x == gx && cur.y == gy && cur.dirFromParent == goalDir) {
            goalIdx = top.idx;
            break;
        }

        // Stale entry (a better g was already found): skip.
        uint64_t curKey = PackCellDir(cur.x, cur.y, cur.dirFromParent);
        auto itG = bestG.find(curKey);
        if (itG != bestG.end() && cur.g > itG->second + 0.001) continue;

        for (int d = 0; d < 4; ++d) {
            int nx = cur.x + DX[d];
            int ny = cur.y + DY[d];
            if (cellBlocked(nx, ny)) continue;

            double stepCost = 1.0;
            if (cur.dirFromParent != -1 && d != cur.dirFromParent) {
                stepCost += turnPenalty;
            }
            double ng = cur.g + stepCost;
            uint64_t nkey = PackCellDir(nx, ny, d);
            auto itB = bestG.find(nkey);
            if (itB != bestG.end() && ng >= itB->second - 0.001) continue;
            bestG[nkey] = ng;

            int parentIdx = top.idx;
            AStarNode nn{nx, ny, d, ng, ng + manhattan(nx, ny), parentIdx};
            visited.push_back(nn);
            open.push({nn.f, static_cast<int>(visited.size()) - 1});
        }
    }

    if (goalIdx < 0) return {};

    // Reconstruct the cell path (goal -> start, then reverse).
    std::vector<std::pair<int, int>> cellPath;
    int idx = goalIdx;
    while (idx != -1) {
        const AStarNode& n = visited[idx];
        cellPath.emplace_back(n.x, n.y);
        idx = n.parentIdx;
    }
    std::reverse(cellPath.begin(), cellPath.end());

    // Convert to world waypoints (cell centres), keeping only corners so long
    // straight runs collapse to a single segment.
    std::vector<Point2Dd> raw;
    raw.reserve(cellPath.size());
    for (auto& c : cellPath) {
        raw.push_back(Point2Dd(area.x + (c.first  + 0.5) * gridSize,
                               area.y + (c.second + 0.5) * gridSize));
    }

    std::vector<Point2Dd> simplified;
    simplified.push_back(raw.front());
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        double dx1 = raw[i].x - raw[i - 1].x, dy1 = raw[i].y - raw[i - 1].y;
        double dx2 = raw[i + 1].x - raw[i].x, dy2 = raw[i + 1].y - raw[i].y;
        // Collinear iff both steps run on the same axis with the same sign.
        bool h1 = std::abs(dx1) > 0.5, h2 = std::abs(dx2) > 0.5;
        bool sameSign = (h1 ? (dx1 * dx2 > 0.0) : (dy1 * dy2 > 0.0));
        if (h1 == h2 && sameSign) continue;
        simplified.push_back(raw[i]);
    }
    if (raw.size() > 1) simplified.push_back(raw.back());

    // Stitch the exact endpoints onto the simplified cell path, then insert
    // whatever corners are needed to keep every segment axis-aligned. The
    // orthogonalizer forces the first segment onto the source side's axis and
    // the last onto the target side's, so the path still meets both boxes
    // head-on.
    std::vector<Point2Dd> stitched;
    stitched.reserve(simplified.size() + 2);
    stitched.push_back(start);
    for (const auto& p : simplified) stitched.push_back(p);
    stitched.push_back(end);

    return NormalizePath(Orthogonalize(stitched, sourceSide, targetSide));
}

// =============================================================================
// TOP-LEVEL ENTRY POINT
// =============================================================================

std::vector<Point2Dd> UltraCanvasDiagramRouter::ComputeOrthogonalPath(
        const Point2Dd& start, const Point2Dd& end,
        DiagramCardinalSide sourceSide, DiagramCardinalSide targetSide,
        const std::vector<DiagramObstacle>& obstacles,
        const DiagramRoutingOptions& options) {
    auto cheap = ComputeCardinalPath(start, end, sourceSide, targetSide);
    if (!PathHasObstacles(cheap, obstacles, options.obstacleInset)) {
        return cheap;
    }
    auto astar = RouteAStar(start, end, sourceSide, targetSide, obstacles, options);
    if (astar.empty()) {
        return cheap;  // silent fallback: a visible connection beats none
    }
    return astar;
}

} // namespace UltraCanvas
