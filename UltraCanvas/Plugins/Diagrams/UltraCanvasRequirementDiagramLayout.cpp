// Plugins/Diagrams/UltraCanvasRequirementDiagramLayout.cpp
// UltraCanvasRequirementDiagram - layout, visibility, viewport, geometry and
// connector routing
// Version: 3.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasRequirementDiagram.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace UltraCanvas {

// =============================================================================
// VISIBILITY (filters + collapse)
// =============================================================================

void UltraCanvasRequirementDiagram::SetKindVisible(RequirementNodeKind kind, bool visible) {
    if (visible) hiddenKinds.erase(kind); else hiddenKinds.insert(kind);
    ApplyFilters();
}

void UltraCanvasRequirementDiagram::SetCategoryVisible(const std::string& category,
                                                        bool visible) {
    if (visible) hiddenCategories.erase(category); else hiddenCategories.insert(category);
    ApplyFilters();
}

void UltraCanvasRequirementDiagram::SetRiskVisible(RequirementRisk risk, bool visible) {
    if (visible) hiddenRisks.erase(risk); else hiddenRisks.insert(risk);
    ApplyFilters();
}

void UltraCanvasRequirementDiagram::SetStatusVisible(const std::string& status, bool visible) {
    if (visible) hiddenStatuses.erase(status); else hiddenStatuses.insert(status);
    ApplyFilters();
}

void UltraCanvasRequirementDiagram::ClearFilters() {
    hiddenKinds.clear();
    hiddenCategories.clear();
    hiddenRisks.clear();
    hiddenStatuses.clear();
    ApplyFilters();
}

void UltraCanvasRequirementDiagram::ApplyFilters() {
    for (const auto& id : model.GetNodeOrder()) {
        RequirementNode* node = model.GetNode(id);
        if (!node) continue;
        node->filtered =
            hiddenKinds.count(node->kind) > 0 ||
            (!node->category.empty() && hiddenCategories.count(node->category) > 0) ||
            hiddenRisks.count(node->risk) > 0 ||
            (!node->status.empty() && hiddenStatuses.count(node->status) > 0);
    }
    // A filter changes which boxes take part in the layout, so positions must
    // be recomputed, not just redrawn.
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    InvalidateRouting();
}

bool UltraCanvasRequirementDiagram::IsHiddenByCollapse(const std::string& id) const {
    // Walk up the containment chain; any collapsed ancestor hides this node.
    std::string current = model.GetParentId(id);
    int guard = 0;
    while (!current.empty() && guard++ < 1000) {
        const RequirementNode* parent = model.GetNode(current);
        if (!parent) break;
        if (parent->collapsed) return true;
        current = model.GetParentId(current);
    }
    return false;
}

bool UltraCanvasRequirementDiagram::IsDisplayed(const RequirementNode& node) const {
    return !node.filtered && !IsHiddenByCollapse(node.id);
}

bool UltraCanvasRequirementDiagram::IsNodeDisplayed(const std::string& id) const {
    const RequirementNode* node = model.GetNode(id);
    return node && IsDisplayed(*node);
}

void UltraCanvasRequirementDiagram::SetNodeCollapsed(const std::string& id, bool collapsed) {
    RequirementNode* node = model.GetNode(id);
    if (!node || node->collapsed == collapsed) return;
    node->collapsed = collapsed;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    InvalidateRouting();
}

void UltraCanvasRequirementDiagram::ToggleNodeCollapsed(const std::string& id) {
    if (const RequirementNode* node = model.GetNode(id)) {
        SetNodeCollapsed(id, !node->collapsed);
    }
}

bool UltraCanvasRequirementDiagram::IsNodeCollapsed(const std::string& id) const {
    const RequirementNode* node = model.GetNode(id);
    return node && node->collapsed;
}

void UltraCanvasRequirementDiagram::ExpandAll() {
    for (const auto& id : model.GetNodeOrder()) {
        if (RequirementNode* node = model.GetNode(id)) node->collapsed = false;
    }
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    InvalidateRouting();
}

void UltraCanvasRequirementDiagram::CollapseAll() {
    for (const auto& id : model.GetNodeOrder()) {
        RequirementNode* node = model.GetNode(id);
        if (node && !model.GetChildIds(id).empty()) node->collapsed = true;
    }
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    InvalidateRouting();
}

// =============================================================================
// TRACE HIGHLIGHTING
// =============================================================================

void UltraCanvasRequirementDiagram::HighlightTraceChain(const std::string& nodeId,
                                                         RequirementTraceDirection direction,
                                                         int depth) {
    highlightedNodes = model.GetTraceChain(nodeId, direction, depth);
    highlightActive = !highlightedNodes.empty();

    for (const auto& id : model.GetNodeOrder()) {
        if (RequirementNode* node = model.GetNode(id)) {
            node->dimmed = highlightActive && highlightedNodes.count(id) == 0;
        }
    }
    // A relation stays lit only when BOTH its endpoints are in the chain -
    // otherwise a highlighted box would trail edges into dimmed territory.
    for (auto& relation : model.GetRelations()) {
        relation.dimmed = highlightActive &&
                          (highlightedNodes.count(relation.sourceId) == 0 ||
                           highlightedNodes.count(relation.targetId) == 0);
    }
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::ClearHighlight() {
    highlightActive = false;
    highlightedNodes.clear();
    for (const auto& id : model.GetNodeOrder()) {
        if (RequirementNode* node = model.GetNode(id)) node->dimmed = false;
    }
    for (auto& relation : model.GetRelations()) relation.dimmed = false;
    RequestRedraw();
}

// =============================================================================
// LAYOUT HIERARCHY
// =============================================================================

void UltraCanvasRequirementDiagram::SetLayoutRelationKinds(
        const std::set<RequirementRelationKind>& kinds) {
    layoutRelationKinds = kinds;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    RequestRedraw();
}

std::vector<std::string> UltraCanvasRequirementDiagram::LayoutChildIds(
        const std::string& nodeId) const {
    std::vector<std::string> children;
    for (const auto& r : model.GetRelations()) {
        if (layoutRelationKinds.count(r.kind) == 0) continue;
        if (r.sourceId != nodeId) continue;
        const RequirementNode* child = model.GetNode(r.targetId);
        if (!child || child->filtered) continue;
        if (std::find(children.begin(), children.end(), r.targetId) == children.end()) {
            children.push_back(r.targetId);
        }
    }
    return children;
}

std::vector<std::string> UltraCanvasRequirementDiagram::LayoutRootIds() const {
    std::vector<std::string> roots;
    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (!node || node->filtered) continue;

        bool hasParent = false;
        for (const auto& r : model.GetRelations()) {
            if (layoutRelationKinds.count(r.kind) == 0) continue;
            if (r.targetId != id) continue;
            const RequirementNode* parent = model.GetNode(r.sourceId);
            if (parent && !parent->filtered) { hasParent = true; break; }
        }
        if (!hasParent) roots.push_back(id);
    }
    return roots;
}

// =============================================================================
// LAYOUT
// =============================================================================

void UltraCanvasRequirementDiagram::SetLayoutMode(RequirementLayoutMode mode) {
    layoutMode = mode;
    if (mode != RequirementLayoutMode::Manual) layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetLayoutOrientation(RequirementOrientation o) {
    orientation = o;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetLevelGap(double gap) {
    style.levelGap = gap;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
}

void UltraCanvasRequirementDiagram::SetSiblingGap(double gap) {
    style.siblingGap = gap;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
}

void UltraCanvasRequirementDiagram::SetSubtreeGap(double gap) {
    style.subtreeGap = gap;
    if (layoutMode != RequirementLayoutMode::Manual) layoutDirty = true;
}

void UltraCanvasRequirementDiagram::SetDefaultRouting(RequirementRouting routing) {
    defaultRouting = routing;
    InvalidateRouting();
}

void UltraCanvasRequirementDiagram::SetObstacleAvoidance(bool enabled) {
    obstacleAvoidance = enabled;
    InvalidateRouting();
}

void UltraCanvasRequirementDiagram::RunLayout() {
    layoutDirty = true;
    // Give callers usable coordinates immediately, before the first frame has
    // measured text. The layout repeats with real metrics next Render().
    if (measurementDirty) {
        for (const auto& id : model.GetNodeOrder()) {
            if (RequirementNode* node = model.GetNode(id)) EstimateNodeSize(*node);
        }
    }
    if (layoutMode == RequirementLayoutMode::ContainmentTree) {
        ApplyContainmentTreeLayout();
    } else if (layoutMode == RequirementLayoutMode::Layered) {
        ApplyLayeredLayout();
    } else if (layoutMode == RequirementLayoutMode::ForceDirected) {
        ApplyForceDirectedLayout();
    }
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::ComputeLevelDepths(const std::string& nodeId, int depth,
                                                        std::map<int, double>& levelSize,
                                                        std::set<std::string>& visited) {
    if (!visited.insert(nodeId).second) return;
    const RequirementNode* node = model.GetNode(nodeId);
    if (!node) return;

    // Depth-axis extent of a level = the tallest (or widest) box on it.
    const bool vertical = (orientation == RequirementOrientation::TopDown ||
                           orientation == RequirementOrientation::BottomUp);
    const double extent = vertical ? node->height : node->width;
    auto it = levelSize.find(depth);
    if (it == levelSize.end() || it->second < extent) levelSize[depth] = extent;

    if (node->collapsed) return;   // its sub-tree occupies no space
    for (const auto& childId : LayoutChildIds(nodeId)) {
        ComputeLevelDepths(childId, depth + 1, levelSize, visited);
    }
}

double UltraCanvasRequirementDiagram::LayoutSubtree(const std::string& nodeId, int depth,
                                                     double crossOffset,
                                                     std::map<int, double>& levelOrigin,
                                                     std::set<std::string>& visited) {
    // Tidy tree over variable-size boxes: each subtree occupies a contiguous
    // band on the sibling axis, the parent is centred over its children, and a
    // leaf occupies exactly its own size. Returns the band width consumed.
    //
    // Positions are computed in a canonical TopDown frame (sibling axis = x,
    // depth axis = y) and rotated afterwards by OrientPositions().
    RequirementNode* node = model.GetNode(nodeId);
    if (!node) return 0.0;
    if (!visited.insert(nodeId).second) return 0.0;   // cycle guard

    const bool vertical = (orientation == RequirementOrientation::TopDown ||
                           orientation == RequirementOrientation::BottomUp);
    const double ownSize = vertical ? node->width : node->height;
    const double depthPos = levelOrigin[depth];

    // A pinned node keeps the position the caller (or a drag) gave it; the
    // layout still reserves its band so siblings do not run into it.
    const bool place = !node->pinned;

    const std::vector<std::string> children =
        node->collapsed ? std::vector<std::string>() : LayoutChildIds(nodeId);

    if (children.empty()) {
        if (place) {
            if (vertical) { node->x = crossOffset; node->y = depthPos; }
            else          { node->y = crossOffset; node->x = depthPos; }
        }
        return ownSize;
    }

    double cursor = crossOffset;
    double firstCenter = 0.0, lastCenter = 0.0;
    bool haveFirst = false;

    for (size_t i = 0; i < children.size(); ++i) {
        const double consumed = LayoutSubtree(children[i], depth + 1, cursor,
                                              levelOrigin, visited);
        if (consumed <= 0.0) continue;

        if (const RequirementNode* child = model.GetNode(children[i])) {
            const double childCenter = vertical ? child->x + child->width / 2.0
                                                : child->y + child->height / 2.0;
            if (!haveFirst) { firstCenter = childCenter; haveFirst = true; }
            lastCenter = childCenter;
        }
        cursor += consumed;
        if (i + 1 < children.size()) cursor += style.siblingGap;
    }

    const double bandWidth = std::max(cursor - crossOffset, ownSize);
    const double parentCross = haveFirst
        ? (firstCenter + lastCenter) / 2.0 - ownSize / 2.0
        : crossOffset + (bandWidth - ownSize) / 2.0;

    if (place) {
        if (vertical) { node->x = parentCross; node->y = depthPos; }
        else          { node->y = parentCross; node->x = depthPos; }
    }
    return bandWidth;
}

void UltraCanvasRequirementDiagram::ApplyContainmentTreeLayout() {
    if (model.GetNodeCount() == 0) return;

    if (model.HasContainmentCycle()) {
        RequirementWarning w;
        w.kind = RequirementWarning::Kind::ContainmentCycle;
        w.message = "Containment cycle detected - falling back to grid layout";
        if (onValidationWarning) onValidationWarning(w);
        ApplyGridFallbackLayout();
        layoutDirty = false;
        return;
    }

    const bool vertical = (orientation == RequirementOrientation::TopDown ||
                           orientation == RequirementOrientation::BottomUp);

    // Depth-axis origin of every level, from the largest box on each level.
    std::map<int, double> levelSize;
    {
        std::set<std::string> visited;
        for (const auto& rootId : LayoutRootIds()) {
            ComputeLevelDepths(rootId, 0, levelSize, visited);
        }
    }

    std::map<int, double> levelOrigin;
    double depthCursor = 0.0;
    for (const auto& [depth, size] : levelSize) {
        levelOrigin[depth] = depthCursor;
        depthCursor += size + style.levelGap;
    }

    // Lay out every root tree in order, separated by subtreeGap.
    std::set<std::string> visited;
    double crossCursor = 0.0;
    for (const auto& rootId : LayoutRootIds()) {
        const double consumed = LayoutSubtree(rootId, 0, crossCursor, levelOrigin, visited);
        if (consumed > 0.0) crossCursor += consumed + style.subtreeGap;
    }
    // Anything still unplaced (hidden by a collapse, or orphaned by a dangling
    // relation) goes after the trees so it never overlaps them.
    for (const auto& id : model.GetNodeOrder()) {
        if (visited.count(id)) continue;
        RequirementNode* node = model.GetNode(id);
        if (!node || node->filtered || IsHiddenByCollapse(id)) continue;
        visited.insert(id);
        if (node->pinned) continue;   // keeps the caller's position
        const double origin = levelOrigin.empty() ? 0.0 : levelOrigin.begin()->second;
        if (vertical) {
            node->x = crossCursor;
            node->y = origin;
            crossCursor += node->width + style.subtreeGap;
        } else {
            node->y = crossCursor;
            node->x = origin;
            crossCursor += node->height + style.subtreeGap;
        }
    }

    OrientPositions();
    layoutDirty = false;
}

void UltraCanvasRequirementDiagram::ApplyLayeredLayout() {
    // Sugiyama-lite for topologies that are not trees (reference image 4's
    // trace web): longest-path layering, barycentre crossing reduction, then
    // coordinate assignment from the real box sizes.
    if (model.GetNodeCount() == 0) return;

    std::vector<std::string> active;
    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (node && !node->filtered && !IsHiddenByCollapse(id)) active.push_back(id);
    }
    if (active.empty()) return;

    // ---- edges that define the hierarchy ---------------------------------
    std::map<std::string, std::vector<std::string>> successors;   // source -> targets
    std::map<std::string, std::vector<std::string>> predecessors;
    std::set<std::string> activeSet(active.begin(), active.end());
    for (const auto& r : model.GetRelations()) {
        if (layoutRelationKinds.count(r.kind) == 0) continue;
        if (!activeSet.count(r.sourceId) || !activeSet.count(r.targetId)) continue;
        if (r.sourceId == r.targetId) continue;
        successors[r.sourceId].push_back(r.targetId);
        predecessors[r.targetId].push_back(r.sourceId);
    }

    // ---- longest-path layering (cycle-safe: an edge into an already-placed
    //      node simply does not push it deeper) ---------------------------
    std::map<std::string, int> layer;
    for (const auto& id : active) layer[id] = 0;
    for (size_t pass = 0; pass < active.size(); ++pass) {
        bool changed = false;
        for (const auto& id : active) {
            for (const auto& target : successors[id]) {
                if (layer[target] < layer[id] + 1) {
                    layer[target] = layer[id] + 1;
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }

    int maxLayer = 0;
    for (const auto& [id, level] : layer) { (void)id; maxLayer = std::max(maxLayer, level); }

    std::vector<std::vector<std::string>> layers(static_cast<size_t>(maxLayer) + 1);
    for (const auto& id : active) layers[static_cast<size_t>(layer[id])].push_back(id);

    // ---- barycentre ordering, a few down/up sweeps -----------------------
    std::map<std::string, double> position;
    for (auto& row : layers) {
        for (size_t i = 0; i < row.size(); ++i) position[row[i]] = static_cast<double>(i);
    }
    for (int sweep = 0; sweep < 4; ++sweep) {
        const bool downward = (sweep % 2 == 0);
        for (size_t li = 0; li < layers.size(); ++li) {
            const size_t index = downward ? li : layers.size() - 1 - li;
            auto& row = layers[index];
            std::map<std::string, double> barycentre;
            for (const auto& id : row) {
                const auto& neighbours = downward ? predecessors[id] : successors[id];
                if (neighbours.empty()) { barycentre[id] = position[id]; continue; }
                double sum = 0.0;
                for (const auto& n : neighbours) sum += position[n];
                barycentre[id] = sum / static_cast<double>(neighbours.size());
            }
            std::stable_sort(row.begin(), row.end(),
                             [&barycentre](const std::string& a, const std::string& b) {
                                 return barycentre[a] < barycentre[b];
                             });
            for (size_t i = 0; i < row.size(); ++i) position[row[i]] = static_cast<double>(i);
        }
    }

    // ---- coordinates -----------------------------------------------------
    const bool vertical = (orientation == RequirementOrientation::TopDown ||
                           orientation == RequirementOrientation::BottomUp);
    double depthCursor = 0.0;
    for (auto& row : layers) {
        double layerExtent = 0.0;
        for (const auto& id : row) {
            const RequirementNode* node = model.GetNode(id);
            if (node) layerExtent = std::max(layerExtent, vertical ? node->height : node->width);
        }
        double crossCursor = 0.0;
        for (const auto& id : row) {
            RequirementNode* node = model.GetNode(id);
            if (!node) continue;
            // Pinned nodes keep their coordinates but still consume a slot.
            if (vertical) {
                if (!node->pinned) {
                    node->x = crossCursor;
                    node->y = depthCursor + (layerExtent - node->height) / 2.0;
                }
                crossCursor += node->width + style.siblingGap;
            } else {
                if (!node->pinned) {
                    node->y = crossCursor;
                    node->x = depthCursor + (layerExtent - node->width) / 2.0;
                }
                crossCursor += node->height + style.siblingGap;
            }
        }
        depthCursor += layerExtent + style.levelGap;
    }

    // Centre each layer on the widest one so the result is not left-ragged.
    double widest = 0.0;
    std::vector<double> layerSpan(layers.size(), 0.0);
    for (size_t i = 0; i < layers.size(); ++i) {
        double span = 0.0;
        for (const auto& id : layers[i]) {
            const RequirementNode* node = model.GetNode(id);
            if (!node) continue;
            span = std::max(span, vertical ? node->x + node->width : node->y + node->height);
        }
        layerSpan[i] = span;
        widest = std::max(widest, span);
    }
    for (size_t i = 0; i < layers.size(); ++i) {
        const double shift = (widest - layerSpan[i]) / 2.0;
        if (shift <= 0.0) continue;
        for (const auto& id : layers[i]) {
            RequirementNode* node = model.GetNode(id);
            if (!node || node->pinned) continue;
            if (vertical) node->x += shift; else node->y += shift;
        }
    }

    OrientPositions();
    layoutDirty = false;
}

void UltraCanvasRequirementDiagram::OrientPositions() {
    // The layouts run in a canonical frame with the depth axis growing
    // positive. BottomUp and RightLeft are the same layout mirrored.
    if (orientation == RequirementOrientation::TopDown ||
        orientation == RequirementOrientation::LeftRight) {
        return;
    }

    const bool vertical = (orientation == RequirementOrientation::BottomUp);
    double maxDepth = 0.0;
    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (!node) continue;
        maxDepth = std::max(maxDepth, vertical ? node->y + node->height : node->x + node->width);
    }
    for (const auto& id : model.GetNodeOrder()) {
        RequirementNode* node = model.GetNode(id);
        if (!node || node->pinned) continue;
        if (vertical) node->y = maxDepth - (node->y + node->height);
        else          node->x = maxDepth - (node->x + node->width);
    }
}

void UltraCanvasRequirementDiagram::ApplyGridFallbackLayout() {
    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(
        static_cast<double>(model.GetNodeCount())))));
    double x = 0.0, y = 0.0, rowHeight = 0.0;
    int column = 0;

    for (const auto& id : model.GetNodeOrder()) {
        RequirementNode* node = model.GetNode(id);
        if (!node || node->filtered) continue;
        if (!node->pinned) {
            node->x = x;
            node->y = y;
        }
        rowHeight = std::max(rowHeight, node->height);
        x += node->width + style.siblingGap;
        if (++column >= columns) {
            column = 0;
            x = 0.0;
            y += rowHeight + style.levelGap;
            rowHeight = 0.0;
        }
    }
}

Rect2Dd UltraCanvasRequirementDiagram::GetContentBounds() const {
    bool any = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;

    auto include = [&](double x, double y, double w, double h) {
        if (!any) { minX = x; minY = y; maxX = x + w; maxY = y + h; any = true; }
        else {
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x + w);
            maxY = std::max(maxY, y + h);
        }
    };

    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (!node || !IsDisplayed(*node)) continue;
        include(node->x, node->y, node->width, node->height);
    }
    for (const auto& callout : model.GetCallouts()) {
        include(callout.x, callout.y, callout.width, callout.height);
    }
    if (!any) return Rect2Dd(0.0, 0.0, 0.0, 0.0);
    return Rect2Dd(minX, minY, maxX - minX, maxY - minY);
}

// =============================================================================
// VIEWPORT
// =============================================================================

void UltraCanvasRequirementDiagram::ClampZoom() {
    zoomLevel = std::max(minZoom, std::min(maxZoom, zoomLevel));
}

void UltraCanvasRequirementDiagram::SetZoomLevel(double zoom) {
    zoomLevel = zoom;
    ClampZoom();
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::SetPanOffset(double x, double y) {
    panOffset.x = x;
    panOffset.y = y;
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::ZoomIn(double factor) {
    // Zoom about the centre of the element so the view does not drift.
    const double cx = GetWidth() / 2.0;
    const double cy = GetHeight() / 2.0;
    const double oldZoom = zoomLevel;
    zoomLevel *= factor;
    ClampZoom();
    panOffset.x = cx - (cx - panOffset.x) * (zoomLevel / oldZoom);
    panOffset.y = cy - (cy - panOffset.y) * (zoomLevel / oldZoom);
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::ZoomOut(double factor) {
    ZoomIn(1.0 / factor);
}

void UltraCanvasRequirementDiagram::FitView(double padding) {
    if (measurementDirty) {
        // Node sizes are still heuristic - fitting now would use the wrong
        // bounds. Repeat the fit once the next frame has measured the text.
        fitPending = true;
    }
    const Rect2Dd bounds = GetContentBounds();
    if (bounds.width <= 0.0 || bounds.height <= 0.0) return;

    const double topInset = (titleConfig.visible ? titleConfig.height : 0.0) +
                            (frameConfig.visible ? frameConfig.tabHeight + frameConfig.margin
                                                 : 0.0);
    const double availableW = std::max(1.0, GetWidth() - padding * 2.0);
    const double availableH = std::max(1.0, GetHeight() - padding * 2.0 - topInset);

    zoomLevel = std::min(availableW / bounds.width, availableH / bounds.height);
    ClampZoom();

    panOffset.x = padding + (availableW - bounds.width * zoomLevel) / 2.0 -
                  bounds.x * zoomLevel;
    panOffset.y = topInset + padding + (availableH - bounds.height * zoomLevel) / 2.0 -
                  bounds.y * zoomLevel;

    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasRequirementDiagram::CenterOn(double worldX, double worldY) {
    panOffset.x = GetWidth() / 2.0 - worldX * zoomLevel;
    panOffset.y = GetHeight() / 2.0 - worldY * zoomLevel;
    NotifyViewportChange();
    RequestRedraw();
}

Point2Dd UltraCanvasRequirementDiagram::ScreenToWorld(const Point2Di& screenPos) const {
    return Point2Dd((screenPos.x - panOffset.x) / zoomLevel,
                    (screenPos.y - panOffset.y) / zoomLevel);
}

Point2Di UltraCanvasRequirementDiagram::WorldToScreen(const Point2Dd& worldPos) const {
    return Point2Di(static_cast<int>(worldPos.x * zoomLevel + panOffset.x),
                    static_cast<int>(worldPos.y * zoomLevel + panOffset.y));
}

Point2Dd UltraCanvasRequirementDiagram::SnapPoint(const Point2Dd& p) const {
    if (!snapGrid.enabled) return p;
    return Point2Dd(std::round(p.x / snapGrid.snapX) * snapGrid.snapX,
                    std::round(p.y / snapGrid.snapY) * snapGrid.snapY);
}

Rect2Dd UltraCanvasRequirementDiagram::NodeRect(const RequirementNode& node) const {
    return Rect2Dd(node.x, node.y, node.width, node.height);
}

void UltraCanvasRequirementDiagram::SetSnapToGrid(bool enabled) {
    snapGrid.enabled = enabled;
}

void UltraCanvasRequirementDiagram::SetSnapGrid(double snapX, double snapY) {
    snapGrid.snapX = snapX;
    snapGrid.snapY = snapY;
}

// =============================================================================
// CONNECTOR ANCHORS
// =============================================================================

UltraCanvasRequirementDiagram::NodeFace
UltraCanvasRequirementDiagram::ChooseFaceTowards(const RequirementNode& from,
                                                  const RequirementNode& to) const {
    // Prefer the axis on which the two boxes are actually separated. On a tree
    // the levels never overlap vertically, so a child two columns away still
    // leaves through its top face and the connector runs straight up instead
    // of looping around the side. Only when the boxes overlap on both axes
    // does the dominant-centre-distance rule decide.
    if (to.y >= from.y + from.height) return NodeFace::Bottom;
    if (to.y + to.height <= from.y)   return NodeFace::Top;
    if (to.x >= from.x + from.width)  return NodeFace::Right;
    if (to.x + to.width <= from.x)    return NodeFace::Left;

    const double dx = (to.x + to.width / 2.0) - (from.x + from.width / 2.0);
    const double dy = (to.y + to.height / 2.0) - (from.y + from.height / 2.0);
    if (std::abs(dx) > std::abs(dy)) return dx >= 0.0 ? NodeFace::Right : NodeFace::Left;
    return dy >= 0.0 ? NodeFace::Bottom : NodeFace::Top;
}

void UltraCanvasRequirementDiagram::CountFaceUsage(const std::string& nodeId, NodeFace face,
                                                    const std::string& relationId,
                                                    int& totalCount, int& myIndex) const {
    totalCount = 0;
    myIndex = 0;
    const RequirementNode* node = model.GetNode(nodeId);
    if (!node) return;

    // Storage order in `relations` keeps the slot index stable across frames.
    for (const auto& r : model.GetRelations()) {
        // Containment is drawn as a shared bus and never claims a face slot;
        // neither does a relation that is not drawn at all.
        if (r.kind == RequirementRelationKind::Containment || !r.visible) continue;

        const std::string otherId = (r.sourceId == nodeId) ? r.targetId
                                  : (r.targetId == nodeId) ? r.sourceId
                                                            : std::string();
        if (otherId.empty()) continue;
        const RequirementNode* other = model.GetNode(otherId);
        if (!other || !IsDisplayed(*other)) continue;

        if (ChooseFaceTowards(*node, *other) != face) continue;
        if (r.id == relationId) myIndex = totalCount;
        totalCount++;
    }
    if (totalCount == 0) totalCount = 1;
}

Point2Dd UltraCanvasRequirementDiagram::AnchorPoint(const RequirementNode& node, NodeFace face,
                                                     int slotIndex, int slotCount) const {
    const double fraction = (static_cast<double>(slotIndex) + 1.0) /
                            (static_cast<double>(std::max(1, slotCount)) + 1.0);
    switch (face) {
        case NodeFace::Top:    return Point2Dd(node.x + node.width * fraction, node.y);
        case NodeFace::Bottom: return Point2Dd(node.x + node.width * fraction,
                                               node.y + node.height);
        case NodeFace::Left:   return Point2Dd(node.x, node.y + node.height * fraction);
        case NodeFace::Right:  return Point2Dd(node.x + node.width,
                                               node.y + node.height * fraction);
    }
    return Point2Dd(node.x + node.width / 2.0, node.y + node.height / 2.0);
}

void UltraCanvasRequirementDiagram::BuildOrthogonalPath(const Point2Dd& from, NodeFace fromFace,
                                                         const Point2Dd& to, NodeFace toFace,
                                                         std::vector<Point2Dd>& outPath) const {
    // Leave the source perpendicular to its face, approach the target
    // perpendicular to its face, and join the two stubs with a Z.
    const double stub = 14.0;
    Point2Dd a = from, b = to;

    switch (fromFace) {
        case NodeFace::Top:    a.y -= stub; break;
        case NodeFace::Bottom: a.y += stub; break;
        case NodeFace::Left:   a.x -= stub; break;
        case NodeFace::Right:  a.x += stub; break;
    }
    switch (toFace) {
        case NodeFace::Top:    b.y -= stub; break;
        case NodeFace::Bottom: b.y += stub; break;
        case NodeFace::Left:   b.x -= stub; break;
        case NodeFace::Right:  b.x += stub; break;
    }

    outPath.push_back(from);
    outPath.push_back(a);

    if (fromFace == NodeFace::Top || fromFace == NodeFace::Bottom) {
        const double midY = (a.y + b.y) / 2.0;
        outPath.push_back(Point2Dd(a.x, midY));
        outPath.push_back(Point2Dd(b.x, midY));
    } else {
        const double midX = (a.x + b.x) / 2.0;
        outPath.push_back(Point2Dd(midX, a.y));
        outPath.push_back(Point2Dd(midX, b.y));
    }

    outPath.push_back(b);
    outPath.push_back(to);
}

// -----------------------------------------------------------------------------
// Obstacle-aware orthogonal routing (A* over a coarse grid)
// -----------------------------------------------------------------------------

bool UltraCanvasRequirementDiagram::BuildAvoidingPath(const RequirementRelation& relation,
                                                       const Point2Dd& from, NodeFace fromFace,
                                                       const Point2Dd& to, NodeFace toFace,
                                                       std::vector<Point2Dd>& outPath) const {
    const double cell = std::max(4.0, style.routingGridSize);
    const double clearance = std::max(0.0, style.routingClearance);

    // Search area: the content, inflated so a route may go around the outside.
    Rect2Dd area = GetContentBounds();
    if (area.width <= 0.0 || area.height <= 0.0) return false;
    const double margin = cell * 6.0;
    area.x -= margin; area.y -= margin;
    area.width += margin * 2.0; area.height += margin * 2.0;

    const int columns = static_cast<int>(std::ceil(area.width / cell)) + 1;
    const int rows = static_cast<int>(std::ceil(area.height / cell)) + 1;
    if (columns <= 1 || rows <= 1) return false;
    if (static_cast<long long>(columns) * rows > style.routingMaxCells) return false;

    auto toCell = [&](const Point2Dd& p) {
        return std::pair<int, int>(
            std::max(0, std::min(columns - 1, static_cast<int>((p.x - area.x) / cell))),
            std::max(0, std::min(rows - 1, static_cast<int>((p.y - area.y) / cell))));
    };
    auto toWorld = [&](int cx, int cy) {
        return Point2Dd(area.x + (cx + 0.5) * cell, area.y + (cy + 0.5) * cell);
    };

    // ---- obstacles: every displayed box except the two endpoints ---------
    std::vector<bool> blocked(static_cast<size_t>(columns) * rows, false);
    for (const auto& id : model.GetNodeOrder()) {
        if (id == relation.sourceId || id == relation.targetId) continue;
        const RequirementNode* node = model.GetNode(id);
        if (!node || !IsDisplayed(*node)) continue;

        const auto [x0, y0] = toCell(Point2Dd(node->x - clearance, node->y - clearance));
        const auto [x1, y1] = toCell(Point2Dd(node->x + node->width + clearance,
                                              node->y + node->height + clearance));
        for (int cy = y0; cy <= y1; ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                blocked[static_cast<size_t>(cy) * columns + cx] = true;
            }
        }
    }

    // Start and goal are the stub points just outside each face, so the route
    // leaves and arrives perpendicular as it does in the direct Z route.
    const double stub = std::max(cell, 14.0);
    Point2Dd startPoint = from, goalPoint = to;
    switch (fromFace) {
        case NodeFace::Top:    startPoint.y -= stub; break;
        case NodeFace::Bottom: startPoint.y += stub; break;
        case NodeFace::Left:   startPoint.x -= stub; break;
        case NodeFace::Right:  startPoint.x += stub; break;
    }
    switch (toFace) {
        case NodeFace::Top:    goalPoint.y -= stub; break;
        case NodeFace::Bottom: goalPoint.y += stub; break;
        case NodeFace::Left:   goalPoint.x -= stub; break;
        case NodeFace::Right:  goalPoint.x += stub; break;
    }

    const auto [startX, startY] = toCell(startPoint);
    const auto [goalX, goalY] = toCell(goalPoint);
    const size_t startIndex = static_cast<size_t>(startY) * columns + startX;
    const size_t goalIndex = static_cast<size_t>(goalY) * columns + goalX;
    // The stubs themselves must be walkable, whatever the clearance says.
    blocked[startIndex] = false;
    blocked[goalIndex] = false;
    if (startIndex == goalIndex) return false;

    // ---- A* with a turn penalty so straight runs win ---------------------
    const double turnPenalty = 2.0;
    const size_t cellCount = static_cast<size_t>(columns) * rows;
    std::vector<double> gScore(cellCount, std::numeric_limits<double>::infinity());
    std::vector<int> cameFrom(cellCount, -1);
    std::vector<int> arrivalDir(cellCount, -1);   // 0=+x 1=-x 2=+y 3=-y

    struct Entry { double f; size_t index; };
    struct Compare { bool operator()(const Entry& a, const Entry& b) const { return a.f > b.f; } };
    std::priority_queue<Entry, std::vector<Entry>, Compare> open;

    auto heuristic = [&](size_t index) {
        const int cx = static_cast<int>(index % columns);
        const int cy = static_cast<int>(index / columns);
        return static_cast<double>(std::abs(cx - goalX) + std::abs(cy - goalY));
    };

    gScore[startIndex] = 0.0;
    open.push({heuristic(startIndex), startIndex});

    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    bool found = false;

    while (!open.empty()) {
        const Entry current = open.top();
        open.pop();
        if (current.index == goalIndex) { found = true; break; }
        if (current.f > gScore[current.index] + heuristic(current.index) + 1e-9) continue;

        const int cx = static_cast<int>(current.index % columns);
        const int cy = static_cast<int>(current.index / columns);
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + dx[d];
            const int ny = cy + dy[d];
            if (nx < 0 || ny < 0 || nx >= columns || ny >= rows) continue;
            const size_t next = static_cast<size_t>(ny) * columns + nx;
            if (blocked[next]) continue;

            double cost = 1.0;
            if (arrivalDir[current.index] >= 0 && arrivalDir[current.index] != d) {
                cost += turnPenalty;
            }
            const double tentative = gScore[current.index] + cost;
            if (tentative + 1e-9 >= gScore[next]) continue;

            gScore[next] = tentative;
            cameFrom[next] = static_cast<int>(current.index);
            arrivalDir[next] = d;
            open.push({tentative + heuristic(next), next});
        }
    }
    if (!found) return false;

    // ---- rebuild, then drop collinear points -----------------------------
    std::vector<Point2Dd> cells;
    for (int index = static_cast<int>(goalIndex); index >= 0; index = cameFrom[index]) {
        cells.push_back(toWorld(index % columns, index / columns));
        if (index == static_cast<int>(startIndex)) break;
    }
    std::reverse(cells.begin(), cells.end());
    if (cells.size() < 2) return false;

    std::vector<Point2Dd> simplified;
    simplified.push_back(cells.front());
    for (size_t i = 1; i + 1 < cells.size(); ++i) {
        const bool sameX = std::abs(cells[i - 1].x - cells[i].x) < 1e-6 &&
                           std::abs(cells[i].x - cells[i + 1].x) < 1e-6;
        const bool sameY = std::abs(cells[i - 1].y - cells[i].y) < 1e-6 &&
                           std::abs(cells[i].y - cells[i + 1].y) < 1e-6;
        if (!sameX && !sameY) simplified.push_back(cells[i]);
    }
    simplified.push_back(cells.back());

    // Snap the two ends onto the real anchor points, keeping the last segment
    // perpendicular so the arrowhead points into the face.
    outPath.clear();
    outPath.push_back(from);
    for (const auto& p : simplified) outPath.push_back(p);
    outPath.push_back(to);
    return true;
}

void UltraCanvasRequirementDiagram::BuildRelationPath(const RequirementRelation& relation,
                                                       std::vector<Point2Dd>& outPath) const {
    outPath.clear();

    // Cached routes survive until any box moves (routingGeneration bumps).
    auto cached = routeCache.find(relation.id);
    if (cached != routeCache.end() && cached->second.first == routingGeneration) {
        outPath = cached->second.second;
        return;
    }

    const RequirementNode* source = model.GetNode(relation.sourceId);
    const RequirementNode* target = model.GetNode(relation.targetId);
    if (!source || !target) return;

    // A self-relation cannot use faces at all - it gets a loop on the box.
    if (relation.sourceId == relation.targetId) {
        int loopIndex = 0;
        for (const auto& r : model.GetRelations()) {
            if (r.sourceId != r.targetId || r.sourceId != relation.sourceId) continue;
            if (r.id == relation.id) break;
            loopIndex++;
        }
        BuildSelfLoopPath(*source, loopIndex, outPath);
        routeCache[relation.id] = {routingGeneration, outPath};
        return;
    }

    const NodeFace sourceFace = ChooseFaceTowards(*source, *target);
    const NodeFace targetFace = ChooseFaceTowards(*target, *source);

    int sourceSlots = 1, sourceIndex = 0, targetSlots = 1, targetIndex = 0;
    CountFaceUsage(relation.sourceId, sourceFace, relation.id, sourceSlots, sourceIndex);
    CountFaceUsage(relation.targetId, targetFace, relation.id, targetSlots, targetIndex);

    Point2Dd from = AnchorPoint(*source, sourceFace, sourceIndex, sourceSlots);
    Point2Dd to = AnchorPoint(*target, targetFace, targetIndex, targetSlots);

    // Parallel relations between the same pair are fanned apart along each
    // face, so two arrows between two boxes never coincide.
    const double fan = MultiEdgeOffset(relation);
    if (fan != 0.0) {
        const bool sourceVertical = (sourceFace == NodeFace::Top ||
                                     sourceFace == NodeFace::Bottom);
        const bool targetVertical = (targetFace == NodeFace::Top ||
                                     targetFace == NodeFace::Bottom);
        if (sourceVertical) from.x += fan; else from.y += fan;
        if (targetVertical) to.x += fan;   else to.y += fan;
        // Keep the anchors on their own box.
        from.x = std::max(source->x, std::min(source->x + source->width, from.x));
        from.y = std::max(source->y, std::min(source->y + source->height, from.y));
        to.x = std::max(target->x, std::min(target->x + target->width, to.x));
        to.y = std::max(target->y, std::min(target->y + target->height, to.y));
    }

    RequirementRouting routing =
        relation.useDefaultRouting ? defaultRouting : relation.routing;
    // Bus routing only means anything for containment, which is drawn
    // separately; anything else falls back to orthogonal.
    if (routing == RequirementRouting::Bus) routing = RequirementRouting::Orthogonal;

    switch (routing) {
        case RequirementRouting::Straight:
            outPath.push_back(from);
            outPath.push_back(to);
            break;

        case RequirementRouting::Curved: {
            // Sampled cubic bezier so hit-testing and the arrowhead use the
            // same polyline as the stroke.
            const double dx = to.x - from.x;
            const double dy = to.y - from.y;
            const double bend = std::max(30.0, std::sqrt(dx * dx + dy * dy) * 0.35);
            Point2Dd c1 = from, c2 = to;
            switch (sourceFace) {
                case NodeFace::Top:    c1.y -= bend; break;
                case NodeFace::Bottom: c1.y += bend; break;
                case NodeFace::Left:   c1.x -= bend; break;
                case NodeFace::Right:  c1.x += bend; break;
            }
            switch (targetFace) {
                case NodeFace::Top:    c2.y -= bend; break;
                case NodeFace::Bottom: c2.y += bend; break;
                case NodeFace::Left:   c2.x -= bend; break;
                case NodeFace::Right:  c2.x += bend; break;
            }
            const int segments = 18;
            for (int i = 0; i <= segments; ++i) {
                const double t = static_cast<double>(i) / segments;
                const double u = 1.0 - t;
                outPath.push_back(Point2Dd(
                    u * u * u * from.x + 3 * u * u * t * c1.x +
                        3 * u * t * t * c2.x + t * t * t * to.x,
                    u * u * u * from.y + 3 * u * u * t * c1.y +
                        3 * u * t * t * c2.y + t * t * t * to.y));
            }
            break;
        }
        case RequirementRouting::Orthogonal:
        case RequirementRouting::Bus:
        default:
            // A* first when asked for; the direct Z route is the fallback and
            // is always correct, just not obstacle-aware.
            if (!obstacleAvoidance ||
                !BuildAvoidingPath(relation, from, sourceFace, to, targetFace, outPath)) {
                outPath.clear();
                BuildOrthogonalPath(from, sourceFace, to, targetFace, outPath);
            }
            break;
    }

    routeCache[relation.id] = {routingGeneration, outPath};
}

// =============================================================================
// HIT TESTING
// =============================================================================

double UltraCanvasRequirementDiagram::DistanceToSegment(const Point2Dd& p, const Point2Dd& a,
                                                         const Point2Dd& b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1e-9) {
        const double px = p.x - a.x, py = p.y - a.y;
        return std::sqrt(px * px + py * py);
    }
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared;
    t = std::max(0.0, std::min(1.0, t));
    const double ex = p.x - (a.x + t * dx);
    const double ey = p.y - (a.y + t * dy);
    return std::sqrt(ex * ex + ey * ey);
}

bool UltraCanvasRequirementDiagram::PointInNode(const RequirementNode& node,
                                                 const Point2Dd& worldPos) const {
    if (ResolveShape(node) == RequirementNodeShape::Oval) {
        const double rx = node.width / 2.0, ry = node.height / 2.0;
        if (rx <= 0.0 || ry <= 0.0) return false;
        const double nx = (worldPos.x - (node.x + rx)) / rx;
        const double ny = (worldPos.y - (node.y + ry)) / ry;
        return nx * nx + ny * ny <= 1.0;
    }
    return NodeRect(node).Contains(worldPos);
}

std::string UltraCanvasRequirementDiagram::FindNodeAt(const Point2Di& screenPos) const {
    const Point2Dd world = ScreenToWorld(screenPos);
    const std::vector<std::string>& order = model.GetNodeOrder();
    // Reverse draw order: the topmost box wins.
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const RequirementNode* node = model.GetNode(*it);
        if (!node || !IsDisplayed(*node)) continue;
        if (PointInNode(*node, world)) return *it;
    }
    return "";
}

std::string UltraCanvasRequirementDiagram::FindCalloutAt(const Point2Di& screenPos) const {
    const Point2Dd world = ScreenToWorld(screenPos);
    const auto& callouts = model.GetCallouts();
    for (auto it = callouts.rbegin(); it != callouts.rend(); ++it) {
        if (Rect2Dd(it->x, it->y, it->width, it->height).Contains(world)) return it->id;
    }
    return "";
}

Point2Dd UltraCanvasRequirementDiagram::CollapseTogglePosition(
        const RequirementNode& node) const {
    // Bottom-RIGHT corner, not bottom-centre: the centre is where the
    // containment bus leaves the box and carries the ⊕ crosshair, and the two
    // symbols must not sit on top of each other.
    return Point2Dd(node.x + node.width - style.toggleRadius - 2.0,
                    node.y + node.height);
}

std::string UltraCanvasRequirementDiagram::FindCollapseToggleAt(
        const Point2Di& screenPos) const {
    const Point2Dd world = ScreenToWorld(screenPos);
    const double radius = style.toggleRadius + 3.0;   // a little slack for the mouse
    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (!node || !IsDisplayed(*node)) continue;
        if (model.GetChildIds(id).empty()) continue;
        const Point2Dd centre = CollapseTogglePosition(*node);
        const double dx = world.x - centre.x, dy = world.y - centre.y;
        if (dx * dx + dy * dy <= radius * radius) return id;
    }
    return "";
}

std::string UltraCanvasRequirementDiagram::FindRelationAt(const Point2Di& screenPos) const {
    const Point2Dd world = ScreenToWorld(screenPos);
    const double tolerance = 6.0 / std::max(0.0001, zoomLevel);

    for (const auto& relation : model.GetRelations()) {
        if (!relation.visible) continue;   // not drawn, so not clickable
        const RequirementNode* source = model.GetNode(relation.sourceId);
        const RequirementNode* target = model.GetNode(relation.targetId);
        if (!source || !target || !IsDisplayed(*source) || !IsDisplayed(*target)) continue;

        std::vector<Point2Dd> path;
        if (relation.kind == RequirementRelationKind::Containment) {
            // The bus route: parent bottom -> spine -> child top.
            const Point2Dd parentAnchor(source->x + source->width / 2.0,
                                        source->y + source->height);
            const Point2Dd childAnchor(target->x + target->width / 2.0, target->y);
            const double spineY = (parentAnchor.y + childAnchor.y) / 2.0;
            path = {parentAnchor, Point2Dd(parentAnchor.x, spineY),
                    Point2Dd(childAnchor.x, spineY), childAnchor};
        } else {
            BuildRelationPath(relation, path);
        }
        for (size_t i = 1; i < path.size(); ++i) {
            if (DistanceToSegment(world, path[i - 1], path[i]) <= tolerance) return relation.id;
        }
    }
    return "";
}


// =============================================================================
// FORCE-DIRECTED LAYOUT (phase 3)
// =============================================================================

void UltraCanvasRequirementDiagram::ApplyForceDirectedLayout() {
    // Spring model for trace webs that have no usable hierarchy. Deterministic
    // on purpose: a circular seed rather than random placement, so repeated
    // Reset gives the same arrangement (NodeDiagram 2.0.4's lesson).
    std::vector<std::string> active;
    for (const auto& id : model.GetNodeOrder()) {
        const RequirementNode* node = model.GetNode(id);
        if (node && !node->filtered && !IsHiddenByCollapse(id)) active.push_back(id);
    }
    if (active.empty()) return;

    const size_t count = active.size();
    std::map<std::string, size_t> index;
    std::vector<double> px(count), py(count), vx(count, 0.0), vy(count, 0.0);

    const double radius = std::max(180.0, style.forceLinkDistance *
                                            static_cast<double>(count) / 6.0);
    for (size_t i = 0; i < count; ++i) {
        index[active[i]] = i;
        const double angle = 2.0 * 3.14159265358979323846 *
                             static_cast<double>(i) / static_cast<double>(count);
        const RequirementNode* node = model.GetNode(active[i]);
        px[i] = node && node->pinned ? node->x : radius * std::cos(angle);
        py[i] = node && node->pinned ? node->y : radius * std::sin(angle);
    }

    // Every visible relation is a spring, whatever its kind - on a trace web
    // that is exactly the structure worth showing.
    struct Edge { size_t a, b; };
    std::vector<Edge> edges;
    for (const auto& r : model.GetRelations()) {
        auto ia = index.find(r.sourceId);
        auto ib = index.find(r.targetId);
        if (ia == index.end() || ib == index.end() || ia->second == ib->second) continue;
        edges.push_back({ia->second, ib->second});
    }

    const double linkDistance = style.forceLinkDistance;
    const double charge = style.forceCharge;
    for (int iteration = 0; iteration < style.forceIterations; ++iteration) {
        const double cooling = 1.0 - static_cast<double>(iteration) /
                                      static_cast<double>(style.forceIterations);

        for (size_t i = 0; i < count; ++i) {
            for (size_t j = i + 1; j < count; ++j) {
                double dx = px[j] - px[i];
                double dy = py[j] - py[i];
                double distanceSquared = dx * dx + dy * dy;
                if (distanceSquared < 1.0) { dx = 0.6; dy = 0.4; distanceSquared = 1.0; }
                const double distance = std::sqrt(distanceSquared);
                const double force = charge / distanceSquared;
                const double fx = force * dx / distance;
                const double fy = force * dy / distance;
                vx[i] -= fx; vy[i] -= fy;
                vx[j] += fx; vy[j] += fy;
            }
        }
        for (const auto& edge : edges) {
            double dx = px[edge.b] - px[edge.a];
            double dy = py[edge.b] - py[edge.a];
            const double distance = std::max(0.01, std::sqrt(dx * dx + dy * dy));
            const double force = (distance - linkDistance) * 0.06;
            const double fx = force * dx / distance;
            const double fy = force * dy / distance;
            vx[edge.a] += fx; vy[edge.a] += fy;
            vx[edge.b] -= fx; vy[edge.b] -= fy;
        }
        // Weak pull to the origin keeps disconnected components from drifting.
        for (size_t i = 0; i < count; ++i) {
            vx[i] -= px[i] * 0.006;
            vy[i] -= py[i] * 0.006;
            px[i] += vx[i] * cooling * 0.5;
            py[i] += vy[i] * cooling * 0.5;
            vx[i] *= 0.82;
            vy[i] *= 0.82;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        RequirementNode* node = model.GetNode(active[i]);
        if (!node || node->pinned) continue;
        node->x = px[i] - node->width / 2.0;
        node->y = py[i] - node->height / 2.0;
    }

    // Force-directed with discrete timesteps can leave residual overlaps;
    // push any remaining pair apart so the result is always readable.
    for (int pass = 0; pass < 24; ++pass) {
        bool moved = false;
        for (size_t i = 0; i < count; ++i) {
            RequirementNode* a = model.GetNode(active[i]);
            if (!a) continue;
            for (size_t j = i + 1; j < count; ++j) {
                RequirementNode* b = model.GetNode(active[j]);
                if (!b) continue;
                const double gap = style.siblingGap;
                const double overlapX = std::min(a->x + a->width + gap, b->x + b->width + gap) -
                                        std::max(a->x, b->x);
                const double overlapY = std::min(a->y + a->height + gap, b->y + b->height + gap) -
                                        std::max(a->y, b->y);
                if (overlapX <= 0.0 || overlapY <= 0.0) continue;

                moved = true;
                const bool alongX = overlapX < overlapY;
                const double push = (alongX ? overlapX : overlapY) / 2.0 + 0.5;
                const double sign = alongX ? (a->x <= b->x ? -1.0 : 1.0)
                                           : (a->y <= b->y ? -1.0 : 1.0);
                if (!a->pinned) { if (alongX) a->x += push * sign; else a->y += push * sign; }
                if (!b->pinned) { if (alongX) b->x -= push * sign; else b->y -= push * sign; }
            }
        }
        if (!moved) break;
    }
    layoutDirty = false;
}

// =============================================================================
// MULTI-EDGE & SELF-RELATION GEOMETRY (phase 3)
// =============================================================================

double UltraCanvasRequirementDiagram::MultiEdgeOffset(
        const RequirementRelation& relation) const {
    // Relations sharing a node pair fan out symmetrically around the centre
    // line: with three of them the offsets are -gap, 0, +gap.
    int total = 0, myIndex = 0;
    for (const auto& r : model.GetRelations()) {
        if (r.kind == RequirementRelationKind::Containment || !r.visible) continue;
        const bool samePair =
            (r.sourceId == relation.sourceId && r.targetId == relation.targetId) ||
            (r.sourceId == relation.targetId && r.targetId == relation.sourceId);
        if (!samePair) continue;
        if (r.id == relation.id) myIndex = total;
        total++;
    }
    if (total <= 1) return 0.0;
    return (static_cast<double>(myIndex) - (static_cast<double>(total) - 1.0) / 2.0) *
           style.multiEdgeGap;
}

void UltraCanvasRequirementDiagram::BuildSelfLoopPath(const RequirementNode& node,
                                                       int loopIndex,
                                                       std::vector<Point2Dd>& outPath) const {
    // A rounded rectangle hanging off the top-right corner, growing outward
    // for each additional loop on the same box.
    const double size = style.selfLoopSize + loopIndex * style.multiEdgeGap;
    const double startX = node.x + node.width * 0.62;
    const double endX = node.x + node.width * 0.88;
    const double top = node.y;

    outPath.clear();
    outPath.push_back(Point2Dd(startX, top));
    outPath.push_back(Point2Dd(startX, top - size));
    outPath.push_back(Point2Dd(endX, top - size));
    outPath.push_back(Point2Dd(endX, top));
}

} // namespace UltraCanvas
