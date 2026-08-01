// Plugins/Charts/Engine/UltraCanvasChartLabels.cpp
// Chart label policy, broker and solved plan
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "Plugins/Charts/Engine/UltraCanvasChartLabels.h"
#include <algorithm>

namespace UltraCanvas {

// =============================================================================
// POLICY
// =============================================================================

void ChartLabelPolicy::SetRole(ChartLabelClass c, bool isSolved, bool isObstacle) {
    const uint32_t bit = static_cast<uint32_t>(c);
    solved &= ~bit;
    obstacles &= ~bit;
    excluded &= ~bit;
    if (isSolved)        solved |= bit;
    else if (isObstacle) obstacles |= bit;
    else                 excluded |= bit;
}

ChartLabelPolicy ChartLabelPolicy::ParallelCoordinates() {
    ChartLabelPolicy p;
    // The axis header chips sit inside the plot band at the top of each axis,
    // so in-plot labels have to steer around them. The chips themselves are
    // decluttered in 1-D along the header row.
    p.SetRole(ChartLabelClass::AxisTitle, /*solved*/ false, /*obstacle*/ true);
    return p;
}

ChartLabelPolicy ChartLabelPolicy::Heatmap() {
    ChartLabelPolicy p;
    // Row and column labels sit against the cell grid, and cell value labels
    // must not collide with them.
    p.SetRole(ChartLabelClass::AxisTick, false, true);
    return p;
}

ChartLabelPolicy ChartLabelPolicy::Radial() {
    ChartLabelPolicy p;
    // Spoke labels ring the plot at arbitrary angles: they genuinely collide
    // with each other and belong in the solver.
    p.SetRole(ChartLabelClass::AxisTitle, true, false);
    return p;
}

// =============================================================================
// PLAN
// =============================================================================

size_t ChartLabelPlan::DrawableCount() const {
    size_t n = 0;
    for (const PlacedChartLabel& l : labels) if (!l.suppressed) ++n;
    return n;
}

// =============================================================================
// BROKER
// =============================================================================

ChartLabelBroker::ChartLabelBroker(const ChartLabelPolicy& labelPolicy,
                                   const LabelPlacementOptions& placementOptions)
    : policy(labelPolicy), options(placementOptions), session(placementOptions) {}

void ChartLabelBroker::AddObstacleRect(const Rect2Dd& rect) {
    session.AddObstacleRect(rect);
}

void ChartLabelBroker::AddObstaclePolyline(const std::vector<Point2Dd>& points,
                                           double halfWidth) {
    session.AddObstaclePolyline(points, halfWidth);
}

void ChartLabelBroker::Add(const ChartLabelRequest& request) {
    requests.push_back(request);
}

void ChartLabelBroker::Add(const std::vector<ChartLabelRequest>& batch) {
    requests.insert(requests.end(), batch.begin(), batch.end());
}

ChartLabelPlan ChartLabelBroker::Solve(uint64_t generation) {
    ChartLabelPlan plan;
    plan.generation = generation;
    plan.labels.reserve(requests.size());

    // Pass 1 - obstacles claim their fixed rectangles before anything is
    // solved, so solved labels steer around them. They are also emitted into
    // the plan, because the chart still has to draw them.
    for (const ChartLabelRequest& r : requests) {
        if (!HasClass(policy.obstacles, r.klass)) continue;
        session.AddObstacleRect(r.fixedBounds);

        PlacedChartLabel placed;
        placed.text = r.text;
        placed.bounds = r.fixedBounds;
        placed.klass = r.klass;
        placed.rotationDegrees = r.rotationDegrees;
        placed.userData = r.userData;
        plan.labels.push_back(placed);
    }

    // Pass 2 - excluded classes are outside the collision world entirely: they
    // neither move nor block. Axis furniture in the margins lives here.
    for (const ChartLabelRequest& r : requests) {
        if (!HasClass(policy.excluded, r.klass)) continue;

        PlacedChartLabel placed;
        placed.text = r.text;
        placed.klass = r.klass;
        placed.rotationDegrees = r.rotationDegrees;
        placed.userData = r.userData;
        placed.bounds = (r.fixedBounds.width > 0.0 || r.fixedBounds.height > 0.0)
            ? r.fixedBounds
            : Rect2Dd(r.anchor.x - r.textSize.width * 0.5,
                      r.anchor.y - r.textSize.height * 0.5,
                      r.textSize.width, r.textSize.height);
        plan.labels.push_back(placed);
    }

    // Pass 3 - the solved classes, in one batch so they see each other as well
    // as everything claimed above.
    std::vector<ShapeLabel> toSolve;
    std::vector<const ChartLabelRequest*> sources;
    toSolve.reserve(requests.size());
    sources.reserve(requests.size());

    for (const ChartLabelRequest& r : requests) {
        if (!HasClass(policy.solved, r.klass)) continue;

        ShapeLabel l;
        l.text = r.text;
        l.textSize = r.textSize;
        l.preferredSide = r.preferredSide;
        l.rotationDegrees = r.rotationDegrees;
        l.priority = r.priority;
        l.allowSuppress = r.allowSuppress;
        l.wantLeaderLine = r.wantLeaderLine;
        if (r.anchorRadius > 0.0) {
            // A mark with real size: give the solver a shape so the label
            // clears the mark rather than the bare centre point.
            LabelShape s;
            s.type = LabelShapeType::Circle;
            s.center = r.anchor;
            s.radius = r.anchorRadius;
            l.shapeIndex = session.AddShape(s);
        } else {
            l.usePointAnchor = true;
            l.anchorPoint = r.anchor;
        }
        toSolve.push_back(l);
        sources.push_back(&r);
    }

    if (!toSolve.empty()) {
        const std::vector<PlacedShapeLabel> solvedLabels = session.Place(toSolve);
        for (size_t i = 0; i < solvedLabels.size(); ++i) {
            const PlacedShapeLabel& s = solvedLabels[i];
            const ChartLabelRequest& r = *sources[i];

            PlacedChartLabel placed;
            placed.text = r.text;
            placed.bounds = s.bounds;
            placed.klass = r.klass;
            placed.rotationDegrees = s.rotationDegrees;
            placed.suppressed = s.suppressed;
            placed.hasLeader = s.hasLeader;
            placed.leaderFrom = s.leaderFrom;
            placed.userData = r.userData;
            plan.labels.push_back(placed);
        }
    }

    return plan;
}

} // namespace UltraCanvas
