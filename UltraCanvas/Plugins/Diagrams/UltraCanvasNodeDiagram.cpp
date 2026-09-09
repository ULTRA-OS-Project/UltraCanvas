// Plugins/Diagrams/UltraCanvasNodeDiagram.cpp
// Interactive Node Diagram component implementation
// Version: 2.0.7
// Last Modified: 2026-05-09
// Author: UltraCanvas Framework
//
// See header for full changelog (2.0.6 + 2.0.5 + 2.0.4 + 2.0.3 + 2.0.2 + 2.0.1 patches + 2.0.0 mayor).

#include "Plugins/Diagrams/UltraCanvasNodeDiagram.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <random>
#include <cstdio>

namespace UltraCanvas {

// =============================================================================
// CONSTANTS
// =============================================================================

static constexpr double NODE_DIAGRAM_PI = 3.14159265358979323846f;
static constexpr double HANDLE_HIT_TOLERANCE = 4.0f;  // Extra pixels around handle for easier targeting
static constexpr double LINK_HIT_TOLERANCE = 8.0f;    // World pixels distance to link line

// =============================================================================
// CONSTRUCTOR
// =============================================================================

UltraCanvasNodeDiagram::UltraCanvasNodeDiagram(const std::string& id,
                                                int x, int y, int width, int height)
    : UltraCanvasUIElement(id, x, y, width, height) {
    viewport.SetViewportSize(width, height);
    // Repaint each eased step of a wheel zoom, so it glides instead of
    // stepping (see UltraCanvasDiagramViewport::ZoomAtPoint).
    viewport.SetRepaintCallback([this] { RequestRedraw(); });
    ApplyDefaultTheme();
}

// =============================================================================
// NODE MANAGEMENT - SIMPLE API
// =============================================================================

void UltraCanvasNodeDiagram::AddNode(const std::string& id, const std::string& label, double x, double y) {
    // 2.0.1: Default size 60 (was 40). 40px-diameter circles cropped labels
    // longer than ~5 chars at the default 11pt bold font. 60 fits "Charlie".
    AddNode(id, label, x, y, 60.0f);
}

void UltraCanvasNodeDiagram::AddNode(const std::string& id, const std::string& label, double x, double y, double size) {
    AddNode(id, label, x, y, size, size);
}

void UltraCanvasNodeDiagram::AddNode(const std::string& id, const std::string& label,
                                      double x, double y, double width, double height) {
    if (nodes.find(id) != nodes.end()) return;
    
    NodeDiagramNode node(id, label);
    node.x = x;
    node.y = y;
    node.width = width;
    node.height = height;
    
    // Apply theme-based default colors
    switch (theme) {
        case NodeDiagramTheme::Professional:
            node.fillColor = Color(80, 140, 200, 255);
            node.borderColor = Color(40, 80, 120, 255);
            break;
        case NodeDiagramTheme::Colorful:
            {
                size_t colorIndex = nodes.size() % 8;
                Color colors[] = {
                    Color(220, 80, 80, 255),
                    Color(80, 200, 80, 255),
                    Color(80, 80, 220, 255),
                    Color(220, 220, 80, 255),
                    Color(220, 80, 220, 255),
                    Color(80, 220, 220, 255),
                    Color(255, 140, 0, 255),
                    Color(140, 80, 255, 255)
                };
                node.fillColor = colors[colorIndex];
                node.borderColor = Color(60, 60, 60, 255);
            }
            break;
        case NodeDiagramTheme::Minimal:
            node.fillColor = Color(255, 255, 255, 255);
            node.borderColor = Color(0, 0, 0, 255);
            node.textColor = Color(0, 0, 0, 255);
            break;
        case NodeDiagramTheme::Dark:
            node.fillColor = Color(50, 50, 60, 255);
            node.borderColor = Color(100, 100, 120, 255);
            node.textColor = Color(220, 220, 220, 255);
            break;
        default:
            break;
    }
    
    // 2.2.0: remember the authored size so a later switch back to
    // NodeSizeMode::Fixed can restore it exactly.
    node.baseWidth  = width;
    node.baseHeight = height;

    nodes[id] = node;
    nodeOrder.push_back(id);
    InvalidateDegreeCache();
    RequestRedraw();
}

// NEW in 2.0.0: Verbose API
void UltraCanvasNodeDiagram::AddNode(const NodeDiagramNode& node) {
    if (node.id.empty() || nodes.find(node.id) != nodes.end()) return;
    nodes[node.id] = node;
    // 2.2.0: capture the authored size when the caller did not set it.
    NodeDiagramNode& stored = nodes[node.id];
    if (stored.baseWidth  <= 0.0) stored.baseWidth  = stored.width;
    if (stored.baseHeight <= 0.0) stored.baseHeight = stored.height;
    nodeOrder.push_back(node.id);
    InvalidateDegreeCache();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::RemoveNode(const std::string& id) {
    auto it = nodes.find(id);
    if (it == nodes.end()) return;
    
    // Remove dependent links
    links.erase(
        std::remove_if(links.begin(), links.end(),
            [&id](const NodeDiagramLink& link) {
                return link.sourceNodeId == id || link.targetNodeId == id;
            }),
        links.end()
    );
    
    nodes.erase(it);
    nodeOrder.erase(std::remove(nodeOrder.begin(), nodeOrder.end(), id), nodeOrder.end());

    selectedNodes.erase(id);
    if (selectedNodeId == id) selectedNodeId.clear();

    // 2.2.0: drop the node from any cluster it belonged to, so the group box
    // stops reserving space for a node that no longer exists.
    for (auto& group : groups) {
        group.nodeIds.erase(std::remove(group.nodeIds.begin(), group.nodeIds.end(), id),
                            group.nodeIds.end());
    }
    InvalidateDegreeCache();

    RequestRedraw();
}

void UltraCanvasNodeDiagram::UpdateNodePosition(const std::string& id, double x, double y) {
    auto* node = GetNode(id);
    if (node) {
        node->x = x;
        node->y = y;
        RequestRedraw();
    }
}

void UltraCanvasNodeDiagram::UpdateNodeLabel(const std::string& id, const std::string& label) {
    auto* node = GetNode(id);
    if (node) {
        node->label = label;
        RequestRedraw();
    }
}

void UltraCanvasNodeDiagram::SetNodeColor(const std::string& id, const Color& fillColor, const Color& borderColor) {
    auto* node = GetNode(id);
    if (node) {
        node->fillColor = fillColor;
        node->borderColor = borderColor;
        RequestRedraw();
    }
}

void UltraCanvasNodeDiagram::SetNodeShape(const std::string& id, NodeShape shape) {
    auto* node = GetNode(id);
    if (node) {
        node->shape = shape;
        RequestRedraw();
    }
}

void UltraCanvasNodeDiagram::PinNode(const std::string& id, bool pinned) {
    auto* node = GetNode(id);
    if (node) node->isPinned = pinned;
}

NodeDiagramNode* UltraCanvasNodeDiagram::GetNode(const std::string& id) {
    auto it = nodes.find(id);
    return (it != nodes.end()) ? &it->second : nullptr;
}

const NodeDiagramNode* UltraCanvasNodeDiagram::GetNode(const std::string& id) const {
    auto it = nodes.find(id);
    return (it != nodes.end()) ? &it->second : nullptr;
}

std::vector<std::string> UltraCanvasNodeDiagram::GetAllNodeIds() const {
    return std::vector<std::string>(nodeOrder.begin(), nodeOrder.end());
}

// NEW in 2.0.0: Handle management
void UltraCanvasNodeDiagram::AddHandle(const std::string& nodeId, const NodeHandle& handle) {
    auto* node = GetNode(nodeId);
    if (node) {
        node->handles.push_back(handle);
        RequestRedraw();
    }
}

void UltraCanvasNodeDiagram::AddDefaultHandles(const std::string& nodeId) {
    auto* node = GetNode(nodeId);
    if (!node) return;
    node->handles.clear();
    node->handles.push_back(NodeHandle("target", HandleType::Target, HandlePosition::Left));
    node->handles.push_back(NodeHandle("source", HandleType::Source, HandlePosition::Right));
    RequestRedraw();
}

void UltraCanvasNodeDiagram::RemoveHandle(const std::string& nodeId, const std::string& handleId) {
    auto* node = GetNode(nodeId);
    if (!node) return;
    node->handles.erase(
        std::remove_if(node->handles.begin(), node->handles.end(),
            [&handleId](const NodeHandle& h) { return h.id == handleId; }),
        node->handles.end()
    );
    RequestRedraw();
}

NodeHandle* UltraCanvasNodeDiagram::GetHandle(const std::string& nodeId, const std::string& handleId) {
    auto* node = GetNode(nodeId);
    if (!node) return nullptr;
    for (auto& h : node->handles) {
        if (h.id == handleId) return &h;
    }
    return nullptr;
}

// =============================================================================
// LINK MANAGEMENT - SIMPLE API
// =============================================================================

void UltraCanvasNodeDiagram::AddLink(const std::string& id, const std::string& sourceId, const std::string& targetId) {
    if (nodes.find(sourceId) == nodes.end() || nodes.find(targetId) == nodes.end()) {
        return;
    }
    NodeDiagramLink link(id, sourceId, targetId);
    link.style = defaultLinkStyle;
    links.push_back(link);
    InvalidateDegreeCache();   // 2.2.0
    RequestRedraw();
}

void UltraCanvasNodeDiagram::AddLink(const std::string& id, const std::string& sourceId,
                                     const std::string& targetId, const Color& lineColor) {
    AddLink(id, sourceId, targetId);
    SetLinkColor(id, lineColor);
}

// NEW in 2.0.0: Verbose API
void UltraCanvasNodeDiagram::AddLink(const NodeDiagramLink& link) {
    if (link.id.empty() ||
        nodes.find(link.sourceNodeId) == nodes.end() ||
        nodes.find(link.targetNodeId) == nodes.end()) {
        return;
    }
    // Check duplicate id
    for (const auto& existing : links) {
        if (existing.id == link.id) return;
    }
    links.push_back(link);
    InvalidateDegreeCache();   // 2.2.0
    RequestRedraw();
}

void UltraCanvasNodeDiagram::RemoveLink(const std::string& id) {
    links.erase(
        std::remove_if(links.begin(), links.end(),
            [&id](const NodeDiagramLink& link) { return link.id == id; }),
        links.end()
    );
    selectedLinks.erase(id);
    if (selectedLinkId == id) selectedLinkId.clear();
    InvalidateDegreeCache();   // 2.2.0
    RequestRedraw();
}

void UltraCanvasNodeDiagram::RemoveLink(const std::string& sourceId, const std::string& targetId) {
    links.erase(
        std::remove_if(links.begin(), links.end(),
            [&sourceId, &targetId](const NodeDiagramLink& link) {
                return (link.sourceNodeId == sourceId && link.targetNodeId == targetId);
            }),
        links.end()
    );
    InvalidateDegreeCache();   // 2.2.0
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetLinkColor(const std::string& id, const Color& color) {
    for (auto& link : links) {
        if (link.id == id) {
            link.lineColor = color;
            RequestRedraw();
            break;
        }
    }
}

void UltraCanvasNodeDiagram::SetLinkWidth(const std::string& id, double width) {
    for (auto& link : links) {
        if (link.id == id) {
            link.lineWidth = width;
            RequestRedraw();
            break;
        }
    }
}

void UltraCanvasNodeDiagram::SetLinkLabel(const std::string& id, const std::string& label) {
    for (auto& link : links) {
        if (link.id == id) {
            link.label = label;
            RequestRedraw();
            break;
        }
    }
}

void UltraCanvasNodeDiagram::SetLinkStyle(const std::string& id, LinkStyle s) {
    for (auto& link : links) {
        if (link.id == id) {
            link.style = s;
            RequestRedraw();
            break;
        }
    }
}

NodeDiagramLink* UltraCanvasNodeDiagram::GetLink(const std::string& id) {
    for (auto& link : links) {
        if (link.id == id) return &link;
    }
    return nullptr;
}

std::vector<std::string> UltraCanvasNodeDiagram::GetAllLinkIds() const {
    std::vector<std::string> ids;
    ids.reserve(links.size());
    for (const auto& link : links) {
        ids.push_back(link.id);
    }
    return ids;
}

// =============================================================================
// LAYOUT
// =============================================================================

void UltraCanvasNodeDiagram::SetLayout(NodeDiagramLayout newLayout) {
    layout = newLayout;
}

void UltraCanvasNodeDiagram::RunLayout() {
    // 2.2.0: size the nodes before laying them out - the anti-overlap pass
    // separates nodes by their bounding-box radii, so it needs the final sizes
    // or a freshly grown hub will still be clipping its neighbours.
    if (sizing.mode != NodeSizeMode::Fixed) {
        ApplyNodeSizing();
    }

    switch (layout) {
        case NodeDiagramLayout::ForceDirected:
            RunForceDirectedLayout(style.iterations);
            break;
        case NodeDiagramLayout::Circular:
            ApplyCircularLayout();
            break;
        case NodeDiagramLayout::Grid:
            ApplyGridLayout();
            break;
        case NodeDiagramLayout::Hierarchical:
            if (!nodes.empty()) {
                ApplyHierarchicalLayout(nodes.begin()->first);
            }
            break;
        default:
            break;
    }
    // 2.0.1: Auto-fit so the user never sees a stranded cluster in the corner.
    // FitView is safe even with bounds==0 (early returns) and respects user pan
    // afterwards via SetPanOffset.
    if (autoFitOnLayout && !nodes.empty()) {
        FitView();
    }
    RequestRedraw();
}

void UltraCanvasNodeDiagram::RunForceDirectedLayout(int iterations) {
    if (nodes.empty()) return;
    
    // 2.0.1: Bounds may not yet be set when the user calls RunLayout() right
    // after AddNode (parent layout pass hasn't run). Use a sane fallback so the
    // simulation always has a finite area to converge in.
    double width  = static_cast<double>(finalBounds.width);
    double height = static_cast<double>(finalBounds.height);
    if (width  < 100.0f) width  = 800.0f;
    if (height < 100.0f) height = 600.0f;
    double centerX = width  / 2.0f;
    double centerY = height / 2.0f;
    
    // 2.0.4: DETERMINISTIC circular initialization. The previous version used
    // rand() (never seeded -> first call always gave the same arrangement, but
    // the algorithm sometimes settled into a local minimum with overlapping
    // nodes). Placing nodes evenly on a ring gives the repulsion forces a
    // symmetric starting point so they fan out reliably.
    {
        size_t n = nodes.size();
        if (n == 0) return;
        double initRadius = std::min(width, height) * 0.30f;
        if (initRadius < 50.0f) initRadius = 50.0f;
        const double TWO_PI = 6.28318530717958647692f;
        size_t idx = 0;
        for (auto& pair : nodes) {
            if (!pair.second.isPinned) {
                double angle = (static_cast<double>(idx) / static_cast<double>(n)) * TWO_PI;
                pair.second.x = centerX + initRadius * std::cos(angle);
                pair.second.y = centerY + initRadius * std::sin(angle);
            }
            idx++;
        }
    }
    
    // 2.0.4: 2.5x more iterations + simulated annealing temperature factor
    // that limits how far a node can move per iteration. Starts at 1.0 (full
    // step) and cools to 0.05 (small adjustments) so the layout settles
    // instead of oscillating.
    int iters = std::max(iterations, 250);
    
    // 2.0.4: Center-pulling force (very weak) keeps disconnected components
    // from drifting off-screen.
    const double kCenterPull = 0.005f;
    
    for (int iter = 0; iter < iters; ++iter) {
        // Cool from 1.0 down to 0.05 over the run
        double t = static_cast<double>(iter) / static_cast<double>(iters);
        double temperature = 1.0f - 0.95f * t;
        
        // Reset velocities
        for (auto& pair : nodes) {
            pair.second.vx = 0;
            pair.second.vy = 0;
        }
        
        // Spring force from links
        for (auto& link : links) {
            auto* source = GetNode(link.sourceNodeId);
            auto* target = GetNode(link.targetNodeId);
            if (!source || !target) continue;
            
            double dx = target->x - source->x;
            double dy = target->y - source->y;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 0.1f) dist = 0.1f;
            
            double force = (dist - style.linkDistance) * style.linkStrength;
            double fx = (dx / dist) * force;
            double fy = (dy / dist) * force;
            
            if (!source->isPinned) {
                source->vx += fx;
                source->vy += fy;
            }
            if (!target->isPinned) {
                target->vx -= fx;
                target->vy -= fy;
            }
        }
        
        // Repulsion (Coulomb-like) between all node pairs
        for (auto it1 = nodes.begin(); it1 != nodes.end(); ++it1) {
            if (it1->second.isPinned) continue;
            
            for (auto it2 = nodes.begin(); it2 != nodes.end(); ++it2) {
                if (it1 == it2) continue;
                
                double dx = it2->second.x - it1->second.x;
                double dy = it2->second.y - it1->second.y;
                double distSq = dx * dx + dy * dy;
                if (distSq < 1.0f) distSq = 1.0f;
                
                double force = style.chargeStrength / distSq;
                double invDist = 1.0f / std::sqrt(distSq);
                it1->second.vx += dx * invDist * force;
                it1->second.vy += dy * invDist * force;
            }
        }
        
        // 2.0.4: Center-pulling force - prevents drift
        for (auto& pair : nodes) {
            if (pair.second.isPinned) continue;
            double ncx = pair.second.x + pair.second.width  * 0.5f;
            double ncy = pair.second.y + pair.second.height * 0.5f;
            pair.second.vx += (centerX - ncx) * kCenterPull;
            pair.second.vy += (centerY - ncy) * kCenterPull;
        }

        // 2.2.0: GROUP COHESION.
        //
        // Pull every member of a cluster toward that cluster's centroid. Without
        // it, repulsion scatters group members across the canvas and the cluster
        // boxes grow until they all overlap - the boxes only read as clusters if
        // the layout keeps their members together in the first place.
        if (style.groupCohesion > 0.0 && !groups.empty()) {
            for (const auto& group : groups) {
                if (group.nodeIds.size() < 2) continue;

                double sumX = 0.0, sumY = 0.0;
                int count = 0;
                for (const auto& nodeId : group.nodeIds) {
                    auto it = nodes.find(nodeId);
                    if (it == nodes.end()) continue;
                    sumX += it->second.x + it->second.width  * 0.5f;
                    sumY += it->second.y + it->second.height * 0.5f;
                    ++count;
                }
                if (count < 2) continue;

                double gcx = sumX / static_cast<double>(count);
                double gcy = sumY / static_cast<double>(count);

                for (const auto& nodeId : group.nodeIds) {
                    auto it = nodes.find(nodeId);
                    if (it == nodes.end() || it->second.isPinned) continue;
                    double ncx = it->second.x + it->second.width  * 0.5f;
                    double ncy = it->second.y + it->second.height * 0.5f;
                    it->second.vx += (gcx - ncx) * style.groupCohesion;
                    it->second.vy += (gcy - ncy) * style.groupCohesion;
                }
            }
        }


        // Apply velocities with annealing temperature
        for (auto& pair : nodes) {
            if (pair.second.isPinned) continue;
            
            // Cap per-step movement: heavily limited at high temperature loses
            // the swing, so we cap by absolute value rather than scaling.
            double maxStep = 30.0f * temperature + 2.0f;
            double vx = std::max(-maxStep, std::min(maxStep, pair.second.vx));
            double vy = std::max(-maxStep, std::min(maxStep, pair.second.vy));
            
            pair.second.x += vx;
            pair.second.y += vy;
            
            pair.second.x = std::max(30.0, std::min(width - 30.0, pair.second.x));
            pair.second.y = std::max(30.0, std::min(height - 30.0, pair.second.y));
        }
    }
    
    // 2.0.6: ANTI-OVERLAP POST-PROCESS PASS.
    //
    // Force-directed simulations with discrete timesteps can leave residual
    // node overlaps even when the energy minimum has been found - the
    // repulsive force is continuous and never quite enough to stop two nodes
    // from clipping when they're already very close. This pass runs after the
    // main simulation and physically separates any pair of overlapping nodes
    // by their bounding-box radii plus a small margin. Iterates a few times
    // because pushing A-B apart may create a new B-C overlap.
    {
        const int kSeparationIterations = 8;
        const double kMargin = 6.0;  // Extra gap between separated nodes
        for (int sepIter = 0; sepIter < kSeparationIterations; ++sepIter) {
            bool anyOverlap = false;
            for (auto it1 = nodes.begin(); it1 != nodes.end(); ++it1) {
                auto it2 = it1;
                for (++it2; it2 != nodes.end(); ++it2) {
                    auto& a = it1->second;
                    auto& b = it2->second;
                    
                    // Use the larger half-dimension as the effective radius
                    // (covers both circle and rectangular shapes well enough).
                    double ar = std::max(a.width, a.height) * 0.5f;
                    double br = std::max(b.width, b.height) * 0.5f;
                    double minDist = ar + br + kMargin;
                    
                    double acx = a.x + a.width  * 0.5f;
                    double acy = a.y + a.height * 0.5f;
                    double bcx = b.x + b.width  * 0.5f;
                    double bcy = b.y + b.height * 0.5f;
                    double dx = bcx - acx;
                    double dy = bcy - acy;
                    double dist = std::sqrt(dx * dx + dy * dy);
                    
                    if (dist < minDist) {
                        anyOverlap = true;
                        // Compute push direction (handle the dist==0 edge case
                        // by picking an arbitrary unit vector).
                        double pushX, pushY;
                        if (dist < 0.001f) {
                            pushX = 1.0;
                            pushY = 0.0;
                        } else {
                            pushX = dx / dist;
                            pushY = dy / dist;
                        }
                        // Total push needed to separate them, split half each
                        double push = (minDist - dist) * 0.5f;
                        // Skip pinned nodes - all push goes to the other one
                        bool aPinned = a.isPinned;
                        bool bPinned = b.isPinned;
                        if (aPinned && bPinned) continue;  // Can't move either
                        double aWeight = aPinned ? 0.0 : (bPinned ? 2.0 : 1.0);
                        double bWeight = bPinned ? 0.0 : (aPinned ? 2.0 : 1.0);
                        a.x -= pushX * push * aWeight;
                        a.y -= pushY * push * aWeight;
                        b.x += pushX * push * bWeight;
                        b.y += pushY * push * bWeight;
                    }
                }
            }
            // Re-clamp to bounds after pushing
            for (auto& pair : nodes) {
                if (pair.second.isPinned) continue;
                pair.second.x = std::max(30.0, std::min(width - 30.0, pair.second.x));
                pair.second.y = std::max(30.0, std::min(height - 30.0, pair.second.y));
            }
            if (!anyOverlap) break;  // Stable
        }
    }
}

void UltraCanvasNodeDiagram::ApplyCircularLayout() {
    if (nodes.empty()) return;
    
    double centerX = finalBounds.width / 2.0;
    double centerY = finalBounds.height / 2.0;
    double radius = std::min(finalBounds.width, finalBounds.height) / 2.5f;
    
    size_t count = nodes.size();
    double angleStep = 2.0 * NODE_DIAGRAM_PI / count;
    
    size_t idx = 0;
    for (auto& pair : nodes) {
        double angle = idx * angleStep;
        pair.second.x = centerX + radius * std::cos(angle) - pair.second.width / 2.0;
        pair.second.y = centerY + radius * std::sin(angle) - pair.second.height / 2.0;
        idx++;
    }
    
    RequestRedraw();
}

void UltraCanvasNodeDiagram::ApplyGridLayout() {
    if (nodes.empty()) return;
    
    size_t count = nodes.size();
    size_t cols = static_cast<size_t>(std::sqrt(count));
    if (cols == 0) cols = 1;
    size_t rows = (count + cols - 1) / cols;
    
    double cellWidth = finalBounds.width / static_cast<double>(cols);
    double cellHeight = finalBounds.height / static_cast<double>(rows);
    
    size_t idx = 0;
    for (auto& pair : nodes) {
        size_t row = idx / cols;
        size_t col = idx % cols;
        
        pair.second.x = static_cast<double>(col) * cellWidth + (cellWidth - pair.second.width) / 2.0;
        pair.second.y = static_cast<double>(row) * cellHeight + (cellHeight - pair.second.height) / 2.0;
        
        idx++;
    }
    
    RequestRedraw();
}

void UltraCanvasNodeDiagram::ApplyHierarchicalLayout(const std::string& rootId) {
    if (nodes.empty()) return;
    
    // BFS to assign levels
    std::map<std::string, int> levels;
    std::vector<std::string> queue;
    queue.push_back(rootId);
    levels[rootId] = 0;
    
    while (!queue.empty()) {
        std::string current = queue.front();
        queue.erase(queue.begin());
        
        int currentLevel = levels[current];
        
        for (const auto& link : links) {
            if (link.sourceNodeId == current) {
                const std::string& targetId = link.targetNodeId;
                if (levels.find(targetId) == levels.end()) {
                    levels[targetId] = currentLevel + 1;
                    queue.push_back(targetId);
                }
            }
        }
    }
    
    // Group by level
    std::map<int, std::vector<std::string>> nodesByLevel;
    for (const auto& pair : levels) {
        nodesByLevel[pair.second].push_back(pair.first);
    }
    
    // Orphan nodes -> level 0
    for (const auto& pair : nodes) {
        if (levels.find(pair.first) == levels.end()) {
            nodesByLevel[0].push_back(pair.first);
        }
    }
    
    double levelSpacing = 150.0;
    double startY = 50.0;
    
    for (const auto& levelPair : nodesByLevel) {
        int level = levelPair.first;
        const auto& levelNodes = levelPair.second;
        
        double y = startY + level * levelSpacing;
        
        for (size_t i = 0; i < levelNodes.size(); ++i) {
            double x = 50.0 + i * 100.0;
            nodes[levelNodes[i]].x = x;
            nodes[levelNodes[i]].y = y;
        }
    }
    
    RequestRedraw();
}

// =============================================================================
// DATA-DRIVEN NODE SIZING (NEW in 2.2.0)
// =============================================================================
//
// Degree counts are cached because a sizing pass would otherwise be O(nodes *
// links). The cache is invalidated by any link or node mutation and rebuilt
// lazily on first read, so bulk-loading a graph costs one rebuild rather than
// one per AddLink call.
// =============================================================================

void UltraCanvasNodeDiagram::RebuildDegreeCache() const {
    degreeIn.clear();
    degreeOut.clear();
    for (const auto& link : links) {
        // Only count links whose endpoints actually exist - a dangling link
        // would otherwise inflate a neighbour's degree.
        if (nodes.find(link.sourceNodeId) == nodes.end()) continue;
        if (nodes.find(link.targetNodeId) == nodes.end()) continue;
        degreeOut[link.sourceNodeId]++;
        degreeIn[link.targetNodeId]++;
    }
    degreeCacheDirty = false;
}

void UltraCanvasNodeDiagram::InvalidateDegreeCache() {
    degreeCacheDirty = true;
    // Only re-run sizing when it would actually change something.
    if (sizing.mode == NodeSizeMode::ByDegree) {
        ApplyNodeSizing();
    }
}

int UltraCanvasNodeDiagram::GetNodeDegree(const std::string& id) const {
    return GetNodeDegree(id, sizing.degreeMode);
}

int UltraCanvasNodeDiagram::GetNodeDegree(const std::string& id, NodeDegreeMode mode) const {
    if (nodes.find(id) == nodes.end()) return 0;
    if (degreeCacheDirty) RebuildDegreeCache();

    auto lookup = [](const std::map<std::string, int>& m, const std::string& key) {
        auto it = m.find(key);
        return it == m.end() ? 0 : it->second;
    };

    switch (mode) {
        case NodeDegreeMode::Incoming: return lookup(degreeIn, id);
        case NodeDegreeMode::Outgoing: return lookup(degreeOut, id);
        case NodeDegreeMode::Total:
        default:                        return lookup(degreeIn, id) + lookup(degreeOut, id);
    }
}

void UltraCanvasNodeDiagram::ApplyNodeSizing() {
    if (nodes.empty()) return;

    if (degreeCacheDirty && sizing.mode == NodeSizeMode::ByDegree) {
        RebuildDegreeCache();
    }

    for (auto& pair : nodes) {
        NodeDiagramNode& node = pair.second;

        // Remember the authored size the first time we touch this node, so
        // returning to Fixed mode restores it exactly.
        if (node.baseWidth <= 0.0)  node.baseWidth  = node.width;
        if (node.baseHeight <= 0.0) node.baseHeight = node.height;

        if (sizing.mode == NodeSizeMode::Fixed) {
            node.width  = node.baseWidth;
            node.height = node.baseHeight;
            continue;
        }

        double quantity = (sizing.mode == NodeSizeMode::ByDegree)
                            ? static_cast<double>(GetNodeDegree(node.id))
                            : node.value;
        if (quantity < 0.0) quantity = 0.0;

        // sqrt transfer: node AREA tracks the quantity, which is the mapping
        // the eye actually reads for a filled mark.
        double scaled = sizing.baseSize * std::sqrt(quantity);
        double target = std::max(sizing.minSize, std::min(sizing.maxSize, scaled));

        // Keep the node centred on the same point while it grows or shrinks -
        // resizing from the top-left corner would make hubs visibly drift.
        double oldCenterX = node.x + node.width  * 0.5;
        double oldCenterY = node.y + node.height * 0.5;

        node.width = target;
        if (sizing.keepAspect) {
            node.height = target;
        } else if (node.baseHeight > 0.0) {
            node.height = node.baseHeight;
        }

        node.x = oldCenterX - node.width  * 0.5;
        node.y = oldCenterY - node.height * 0.5;
    }

    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetNodeSizeMode(NodeSizeMode mode) {
    if (sizing.mode == mode) return;
    sizing.mode = mode;
    ApplyNodeSizing();
}

void UltraCanvasNodeDiagram::SetNodeSizing(const NodeDiagramSizing& newSizing) {
    sizing = newSizing;
    if (sizing.minSize > sizing.maxSize) std::swap(sizing.minSize, sizing.maxSize);
    ApplyNodeSizing();
}

void UltraCanvasNodeDiagram::SetNodeSizeRange(double baseSize, double minSize, double maxSize) {
    sizing.baseSize = baseSize;
    sizing.minSize  = std::min(minSize, maxSize);
    sizing.maxSize  = std::max(minSize, maxSize);
    ApplyNodeSizing();
}

void UltraCanvasNodeDiagram::SetNodeValue(const std::string& id, double value) {
    auto it = nodes.find(id);
    if (it == nodes.end()) return;
    it->second.value = value;
    if (sizing.mode == NodeSizeMode::ByValue) {
        ApplyNodeSizing();
    }
}

double UltraCanvasNodeDiagram::GetNodeValue(const std::string& id) const {
    auto it = nodes.find(id);
    return it == nodes.end() ? 0.0 : it->second.value;
}

// =============================================================================
// CLUSTER CONTAINERS / GROUPS (NEW in 2.2.0)
// =============================================================================

void UltraCanvasNodeDiagram::AddGroup(const NodeDiagramGroup& group) {
    if (group.id.empty()) return;
    for (auto& existing : groups) {
        if (existing.id == group.id) {
            existing = group;   // Replace in place, keeping draw order
            RequestRedraw();
            return;
        }
    }
    groups.push_back(group);
    RequestRedraw();
}

void UltraCanvasNodeDiagram::AddGroup(const std::string& id, const std::string& label,
                                       const std::vector<std::string>& nodeIds,
                                       const Color& borderColor,
                                       const Color& fillColor) {
    NodeDiagramGroup group(id, label);
    group.nodeIds = nodeIds;
    group.borderColor = borderColor;
    group.fillColor = fillColor;
    group.labelColor = borderColor;
    AddGroup(group);
}

void UltraCanvasNodeDiagram::RemoveGroup(const std::string& id) {
    groups.erase(std::remove_if(groups.begin(), groups.end(),
                    [&id](const NodeDiagramGroup& g) { return g.id == id; }),
                 groups.end());
    RequestRedraw();
}

void UltraCanvasNodeDiagram::ClearGroups() {
    groups.clear();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::AddNodeToGroup(const std::string& groupId, const std::string& nodeId) {
    NodeDiagramGroup* group = GetGroup(groupId);
    if (!group) return;
    if (std::find(group->nodeIds.begin(), group->nodeIds.end(), nodeId) != group->nodeIds.end()) {
        return;
    }
    group->nodeIds.push_back(nodeId);
    RequestRedraw();
}

void UltraCanvasNodeDiagram::RemoveNodeFromGroup(const std::string& groupId, const std::string& nodeId) {
    NodeDiagramGroup* group = GetGroup(groupId);
    if (!group) return;
    group->nodeIds.erase(std::remove(group->nodeIds.begin(), group->nodeIds.end(), nodeId),
                         group->nodeIds.end());
    RequestRedraw();
}

NodeDiagramGroup* UltraCanvasNodeDiagram::GetGroup(const std::string& id) {
    for (auto& group : groups) {
        if (group.id == id) return &group;
    }
    return nullptr;
}

const NodeDiagramGroup* UltraCanvasNodeDiagram::GetGroup(const std::string& id) const {
    for (const auto& group : groups) {
        if (group.id == id) return &group;
    }
    return nullptr;
}

std::vector<std::string> UltraCanvasNodeDiagram::GetAllGroupIds() const {
    std::vector<std::string> ids;
    ids.reserve(groups.size());
    for (const auto& group : groups) ids.push_back(group.id);
    return ids;
}

std::string UltraCanvasNodeDiagram::GetNodeGroupId(const std::string& nodeId) const {
    for (const auto& group : groups) {
        if (std::find(group.nodeIds.begin(), group.nodeIds.end(), nodeId) != group.nodeIds.end()) {
            return group.id;
        }
    }
    return std::string();
}

Rect2Dd UltraCanvasNodeDiagram::ComputeGroupBounds(const NodeDiagramGroup& group) const {
    bool any = false;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;

    for (const auto& nodeId : group.nodeIds) {
        auto it = nodes.find(nodeId);
        if (it == nodes.end()) continue;   // Tolerate stale ids
        const NodeDiagramNode& node = it->second;
        if (!any) {
            minX = node.x;
            minY = node.y;
            maxX = node.x + node.width;
            maxY = node.y + node.height;
            any = true;
        } else {
            minX = std::min(minX, node.x);
            minY = std::min(minY, node.y);
            maxX = std::max(maxX, node.x + node.width);
            maxY = std::max(maxY, node.y + node.height);
        }
    }

    if (!any) return Rect2Dd(0.0, 0.0, 0.0, 0.0);

    return Rect2Dd(minX - group.padding,
                   minY - group.padding,
                   (maxX - minX) + group.padding * 2.0,
                   (maxY - minY) + group.padding * 2.0);
}

Rect2Dd UltraCanvasNodeDiagram::GetGroupBounds(const std::string& id) const {
    const NodeDiagramGroup* group = GetGroup(id);
    if (!group) return Rect2Dd(0.0, 0.0, 0.0, 0.0);
    return ComputeGroupBounds(*group);
}

void UltraCanvasNodeDiagram::SetGroupsVisible(bool visible) {
    groupsVisible = visible;
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetGroupCohesion(double strength) {
    style.groupCohesion = std::max(0.0, strength);
}

// =============================================================================
// COLOR LEGEND (NEW in 2.2.0)
// =============================================================================

void UltraCanvasNodeDiagram::SetLegendVisible(bool visible) {
    legend.visible = visible;
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetLegendPosition(NodeDiagramPanelPosition pos) {
    legend.position = pos;
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetLegendConfig(const NodeDiagramLegendConfig& cfg) {
    legend = cfg;
    RequestRedraw();
}

void UltraCanvasNodeDiagram::AddLegendEntry(const std::string& label, const Color& color) {
    legend.entries.emplace_back(label, color);
    RequestRedraw();
}

void UltraCanvasNodeDiagram::ClearLegendEntries() {
    legend.entries.clear();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::BuildLegendFromGroups() {
    legend.entries.clear();
    for (const auto& group : groups) {
        if (group.label.empty()) continue;
        legend.entries.emplace_back(group.label, group.borderColor);
    }
    RequestRedraw();
}

// =============================================================================
// LABEL MEASUREMENT (NEW in 2.0.4)
// =============================================================================
//
// Conservative heuristic - the real text would be measured against the actual
// font metrics during Render() but those are not available here. Tuned for
// Arial Bold (the default node label font); other fonts may need a per-glyph
// fudge factor but this is good enough for sizing a node so its label fits.
// =============================================================================

void UltraCanvasNodeDiagram::MeasureLabel(const std::string& label, double fontSize,
                                           int& outWidth, int& outHeight) const {
    if (label.empty()) {
        outWidth = 0;
        outHeight = static_cast<int>(fontSize * 1.3f);
        return;
    }
    // UTF-8 conscious char count: only count bytes that aren't continuation
    // bytes (0x80-0xBF). Approximation - good enough since we're estimating.
    int chars = 0;
    for (unsigned char c : label) {
        if ((c & 0xC0) != 0x80) chars++;
    }
    // 2.0.5: factor 0.58->0.70. The smaller value worked for thin labels but
    // bold text with capital letters ("Superficial") overflowed; 0.70 is closer
    // to the upper-bound average glyph width and avoids visible cropping.
    double charWidth = fontSize * 0.70f;
    outWidth  = static_cast<int>(std::ceil(chars * charWidth));
    outHeight = static_cast<int>(std::ceil(fontSize * 1.30f));
}

void UltraCanvasNodeDiagram::SuggestNodeSizeForLabel(const std::string& label,
                                                     double fontSize,
                                                     double minSize,
                                                     double& outWidth,
                                                     double& outHeight) const {
    int textW = 0, textH = 0;
    MeasureLabel(label, fontSize, textW, textH);
    
    // 2.0.5: padding 24->32. Circle nodes lose ~14% of their horizontal interior
    // to curvature near the top/bottom; the extra padding ensures the label
    // doesn't kiss the border on either side.
    const double kHorizontalPadding = 32.0;
    const double kVerticalPadding   = 16.0;
    
    double w = static_cast<double>(textW) + kHorizontalPadding;
    double h = static_cast<double>(textH) + kVerticalPadding;
    
    // For circular-ish use, the node should be at least roughly square AND big
    // enough to contain the text - use the larger dimension as both width and
    // height for circles. Caller can override for non-circle shapes.
    if (w < minSize) w = minSize;
    if (h < minSize) h = minSize;
    
    outWidth  = w;
    outHeight = h;
}

// =============================================================================
// STYLING & THEME
// =============================================================================

void UltraCanvasNodeDiagram::SetTheme(NodeDiagramTheme newTheme) {
    theme = newTheme;
    ApplyTheme(theme);
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetBackgroundColor(const Color& color) {
    style.backgroundColor = color;
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetGridVisible(bool visible, double spacing) {
    style.showGrid = visible;
    style.gridSpacing = spacing;
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetFontFamily(const std::string& fontFamily) {
    style.fontFamily = fontFamily;
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetFontSize(double size) {
    style.baseFontSize = size;
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetLinkDistance(double distance) {
    style.linkDistance = distance;
}

void UltraCanvasNodeDiagram::ApplyTheme(NodeDiagramTheme t) {
    switch (t) {
        case NodeDiagramTheme::Default:      ApplyDefaultTheme(); break;
        case NodeDiagramTheme::Professional: ApplyProfessionalTheme(); break;
        case NodeDiagramTheme::Colorful:     ApplyColorfulTheme(); break;
        default:                              ApplyDefaultTheme(); break;
    }
}

void UltraCanvasNodeDiagram::ApplyDefaultTheme() {
    style.fontFamily = "Arial";
    style.baseFontSize = 11.0;
    style.backgroundColor = Color(248, 248, 248, 255);
    // 2.0.1: Was (230,230,230) which is barely visible on (248,248,248).
    style.gridColor = Color(215, 215, 215, 255);
    style.showGrid = false;
    style.selectionColor = Color(255, 100, 0, 255);
    style.linkDistance = 100.0;
    style.linkStrength = 0.1f;
    style.chargeStrength = -300.0;
    style.iterations = 100;
}

void UltraCanvasNodeDiagram::ApplyProfessionalTheme() {
    style.fontFamily = "Arial";
    style.baseFontSize = 11.0;
    style.backgroundColor = Color(245, 247, 250, 255);
    style.gridColor = Color(220, 225, 230, 255);
    style.showGrid = true;
    style.gridSpacing = 25.0;
    style.selectionColor = Color(0, 123, 255, 255);
    style.linkDistance = 120.0;
    style.linkStrength = 0.15f;
    style.chargeStrength = -400.0;
}

void UltraCanvasNodeDiagram::ApplyColorfulTheme() {
    style.fontFamily = "Arial";
    style.baseFontSize = 11.0;
    style.backgroundColor = Color(255, 255, 255, 255);
    // 2.0.1: Was (235,235,235) which is barely visible on white.
    style.gridColor = Color(218, 218, 218, 255);
    style.showGrid = false;
    style.selectionColor = Color(255, 100, 0, 255);
    // 2.0.5: linkDistance 80->130, chargeStrength -250->-500.
    // The old values were tuned for 40px nodes; with the 60px+ nodes that 2.0.1
    // made the default, the layout converged with overlapping circles.
    style.linkDistance = 130.0;
    style.linkStrength = 0.08f;
    style.chargeStrength = -500.0;
}


// =============================================================================
// SELECTION
// =============================================================================

void UltraCanvasNodeDiagram::SelectNode(const std::string& id, bool addToSelection) {
    auto it = nodes.find(id);
    if (it == nodes.end()) return;
    
    if (!addToSelection) {
        DeselectAll();
    }
    
    selectedNodes.insert(id);
    it->second.isSelected = true;
    selectedNodeId = id;  // Last selected
    
    // Bring to front in z-order
    auto orderIt = std::find(nodeOrder.begin(), nodeOrder.end(), id);
    if (orderIt != nodeOrder.end()) {
        nodeOrder.erase(orderIt);
        nodeOrder.push_back(id);
    }
    
    if (onNodeClick) onNodeClick(id);
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SelectLink(const std::string& id, bool addToSelection) {
    if (!addToSelection) {
        DeselectAll();
    }
    
    bool found = false;
    for (auto& link : links) {
        if (link.id == id) {
            link.isSelected = true;
            found = true;
            break;
        }
    }
    if (!found) return;
    
    selectedLinks.insert(id);
    selectedLinkId = id;
    
    if (onLinkClick) onLinkClick(id);
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SelectAll() {
    selectedNodes.clear();
    selectedLinks.clear();
    
    for (auto& pair : nodes) {
        if (!pair.second.selectable) continue;
        pair.second.isSelected = true;
        selectedNodes.insert(pair.first);
    }
    for (auto& link : links) {
        if (!link.selectable) continue;
        link.isSelected = true;
        selectedLinks.insert(link.id);
    }
    
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::DeselectAll() {
    for (auto& pair : nodes) {
        pair.second.isSelected = false;
    }
    for (auto& link : links) {
        link.isSelected = false;
    }
    selectedNodes.clear();
    selectedLinks.clear();
    selectedNodeId.clear();
    selectedLinkId.clear();
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::DeleteSelected() {
    // Snapshot ids first because RemoveNode modifies the sets
    std::vector<std::string> nodesToDelete(selectedNodes.begin(), selectedNodes.end());
    std::vector<std::string> linksToDelete(selectedLinks.begin(), selectedLinks.end());
    
    for (const auto& nid : nodesToDelete) {
        auto* node = GetNode(nid);
        if (node && node->deletable) {
            RemoveNode(nid);
        }
    }
    for (const auto& lid : linksToDelete) {
        // Skip if the link was already removed by node deletion
        bool stillExists = false;
        for (const auto& l : links) {
            if (l.id == lid) {
                if (l.deletable) stillExists = true;
                break;
            }
        }
        if (stillExists) {
            RemoveLink(lid);
        }
    }
    
    NotifySelectionChange();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::Clear() {
    nodes.clear();
    nodeOrder.clear();
    links.clear();
    groups.clear();          // 2.2.0
    degreeIn.clear();        // 2.2.0
    degreeOut.clear();
    degreeCacheDirty = false;
    DeselectAll();
    RequestRedraw();
}

std::vector<std::string> UltraCanvasNodeDiagram::GetSelectedNodeIds() const {
    return std::vector<std::string>(selectedNodes.begin(), selectedNodes.end());
}

std::vector<std::string> UltraCanvasNodeDiagram::GetSelectedLinkIds() const {
    return std::vector<std::string>(selectedLinks.begin(), selectedLinks.end());
}

bool UltraCanvasNodeDiagram::IsNodeSelected(const std::string& id) const {
    return selectedNodes.count(id) > 0;
}

bool UltraCanvasNodeDiagram::IsLinkSelected(const std::string& id) const {
    return selectedLinks.count(id) > 0;
}

void UltraCanvasNodeDiagram::NotifySelectionChange() {
    if (onSelectionChange) {
        onSelectionChange(GetSelectedNodeIds(), GetSelectedLinkIds());
    }
}

// =============================================================================
// VIEWPORT
// =============================================================================

// =============================================================================
// VIEWPORT - forwards onto the shared UltraCanvasDiagramViewport (2.1.0).
// The element keeps its public API; the mechanics live in the shared component.
// =============================================================================

void UltraCanvasNodeDiagram::SyncViewportSize() {
    viewport.SetViewportSize(finalBounds.width, finalBounds.height);
}

DiagramContentBounds UltraCanvasNodeDiagram::ComputeContentBounds() const {
    DiagramContentBounds bounds;
    for (const auto& pair : nodes) {
        const auto& n = pair.second;
        bounds.Include(n.x, n.y);
        bounds.Include(n.x + n.width, n.y + n.height);
    }
    // 2.2.0: cluster boxes extend past their members by the group padding, so
    // FitView and the minimap must account for them or the boxes get clipped.
    if (groupsVisible) {
        for (const auto& group : groups) {
            if (!group.visible) continue;
            Rect2Dd box = ComputeGroupBounds(group);
            if (box.width <= 0.0 || box.height <= 0.0) continue;
            bounds.Include(box.x, box.y);
            bounds.Include(box.x + box.width, box.y + box.height);
        }
    }
    return bounds;
}

void UltraCanvasNodeDiagram::SetZoomLevel(double zoom) {
    viewport.SetZoomLevel(zoom);
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetPanOffset(double x, double y) {
    viewport.SetPanOffset(x, y);
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::ZoomIn(double factor) {
    SetZoomLevel(viewport.GetZoomLevel() * factor);
}

void UltraCanvasNodeDiagram::ZoomOut(double factor) {
    SetZoomLevel(viewport.GetZoomLevel() / factor);
}

void UltraCanvasNodeDiagram::FitView(double padding) {
    if (nodes.empty()) return;
    SyncViewportSize();
    if (!viewport.FitView(ComputeContentBounds(), padding)) return;
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::CenterOn(double worldX, double worldY) {
    SyncViewportSize();
    viewport.CenterOn(worldX, worldY);
    NotifyViewportChange();
    RequestRedraw();
}

void UltraCanvasNodeDiagram::ClampZoom() {
    viewport.SetZoomLevel(viewport.GetZoomLevel());  // Re-applies the clamp
}

void UltraCanvasNodeDiagram::NotifyViewportChange() {
    if (onViewportChange) {
        Point2Dd pan = viewport.GetPanOffset();
        onViewportChange(viewport.GetZoomLevel(), pan.x, pan.y);
    }
}

double UltraCanvasNodeDiagram::GetZoomLevel() const {
    return viewport.GetZoomLevel();
}

Point2Dd UltraCanvasNodeDiagram::GetPanOffset() const {
    return viewport.GetPanOffset();
}

void UltraCanvasNodeDiagram::SetMinZoom(double minZ) {
    viewport.SetZoomRange(minZ, viewport.GetMaxZoom());
}

void UltraCanvasNodeDiagram::SetMaxZoom(double maxZ) {
    viewport.SetZoomRange(viewport.GetMinZoom(), maxZ);
}

// =============================================================================
// SNAP-TO-GRID
// =============================================================================

void UltraCanvasNodeDiagram::SetSnapToGrid(bool enabled) {
    viewport.SetSnapToGrid(enabled);
}

void UltraCanvasNodeDiagram::SetSnapGrid(double snapX, double snapY) {
    viewport.SetSnapGrid(snapX, snapY);
}

bool UltraCanvasNodeDiagram::IsSnapToGridEnabled() const {
    return viewport.IsSnapToGridEnabled();
}

Point2Dd UltraCanvasNodeDiagram::SnapPoint(const Point2Dd& p) const {
    return viewport.SnapPoint(p);
}

// =============================================================================
// MINIMAP CONFIG
// =============================================================================

void UltraCanvasNodeDiagram::SetMinimapVisible(bool visible) {
    viewport.SetMinimapVisible(visible);
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetMinimapPosition(NodeDiagramPanelPosition pos) {
    viewport.SetMinimapPosition(pos);
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetMinimapConfig(const NodeDiagramMinimapConfig& cfg) {
    viewport.SetMinimapConfig(cfg);
    RequestRedraw();
}

NodeDiagramMinimapConfig UltraCanvasNodeDiagram::GetMinimapConfig() const {
    return viewport.MinimapConfig();
}

// =============================================================================
// CONTROLS CONFIG
// =============================================================================

void UltraCanvasNodeDiagram::SetControlsVisible(bool visible) {
    viewport.SetControlsVisible(visible);
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetControlsPosition(NodeDiagramPanelPosition pos) {
    viewport.SetControlsPosition(pos);
    RequestRedraw();
}

void UltraCanvasNodeDiagram::SetControlsConfig(const NodeDiagramControlsConfig& cfg) {
    viewport.SetControlsConfig(cfg);
    RequestRedraw();
}

NodeDiagramControlsConfig UltraCanvasNodeDiagram::GetControlsConfig() const {
    return viewport.ControlsConfig();
}

// =============================================================================
// COORDINATE TRANSFORMS
// =============================================================================

Point2Dd UltraCanvasNodeDiagram::ScreenToWorld(const Point2Di& screenPos) const {
    return viewport.ScreenToWorld(screenPos);
}

Point2Di UltraCanvasNodeDiagram::WorldToScreen(const Point2Dd& worldPos) const {
    return viewport.WorldToScreen(worldPos);
}

double UltraCanvasNodeDiagram::CalculateDistance(const Point2Dd& a, const Point2Dd& b) const {
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

// =============================================================================
// HANDLE GEOMETRY
// =============================================================================

Point2Dd UltraCanvasNodeDiagram::GetHandleWorldPosition(const NodeDiagramNode& node,
                                                         const NodeHandle& handle) const {
    Point2Dd p;
    switch (handle.position) {
        case HandlePosition::Top:
            p.x = node.x + node.width / 2.0 + handle.offsetX;
            p.y = node.y + handle.offsetY;
            break;
        case HandlePosition::Right:
            p.x = node.x + node.width + handle.offsetX;
            p.y = node.y + node.height / 2.0 + handle.offsetY;
            break;
        case HandlePosition::Bottom:
            p.x = node.x + node.width / 2.0 + handle.offsetX;
            p.y = node.y + node.height + handle.offsetY;
            break;
        case HandlePosition::Left:
        default:
            p.x = node.x + handle.offsetX;
            p.y = node.y + node.height / 2.0 + handle.offsetY;
            break;
    }
    return p;
}

HandlePosition UltraCanvasNodeDiagram::InferHandleSide(const NodeDiagramNode& fromNode,
                                                       const NodeDiagramNode& toNode) const {
    // Pick the face of fromNode that points towards toNode (dominant axis)
    double fcx = fromNode.x + fromNode.width  * 0.5f;
    double fcy = fromNode.y + fromNode.height * 0.5f;
    double tcx = toNode.x   + toNode.width    * 0.5f;
    double tcy = toNode.y   + toNode.height   * 0.5f;
    double dx = tcx - fcx;
    double dy = tcy - fcy;
    if (std::abs(dx) >= std::abs(dy)) {
        return (dx >= 0.0) ? HandlePosition::Right : HandlePosition::Left;
    } else {
        return (dy >= 0.0) ? HandlePosition::Bottom : HandlePosition::Top;
    }
}

// =============================================================================
// HIT TESTING - PROPER PER-SHAPE
// =============================================================================
//
// Iteration order: nodes in REVERSE z-order so the topmost is hit first.
// We test in WORLD coordinates, so PointInNode receives a world point.
// =============================================================================

bool UltraCanvasNodeDiagram::PointInNode(const NodeDiagramNode& node, const Point2Dd& worldPos) const {
    // First a quick AABB reject for everything except Cloud which extends slightly
    double halfW = node.width  * 0.5f;
    double halfH = node.height * 0.5f;
    double cx = node.x + halfW;
    double cy = node.y + halfH;
    double dx = worldPos.x - cx;
    double dy = worldPos.y - cy;
    
    switch (node.shape) {
        case NodeShape::Circle: {
            double r = std::min(halfW, halfH);
            return (dx * dx + dy * dy) <= (r * r);
        }
        case NodeShape::Square:
        case NodeShape::RoundedSquare:
        case NodeShape::Rectangle: {
            return std::abs(dx) <= halfW && std::abs(dy) <= halfH;
        }
        case NodeShape::Diamond: {
            // |dx|/halfW + |dy|/halfH <= 1
            if (halfW <= 0 || halfH <= 0) return false;
            return (std::abs(dx) / halfW + std::abs(dy) / halfH) <= 1.0;
        }
        case NodeShape::Hexagon: {
            // Approximate as inscribed ellipse: tight enough for hit testing
            if (halfW <= 0 || halfH <= 0) return false;
            double nx = dx / halfW;
            double ny = dy / halfH;
            return (nx * nx + ny * ny) <= 1.0;
        }
        case NodeShape::Triangle: {
            // Apex at top center, base at bottom edge.
            // Outside vertical band -> miss.
            if (dy < -halfH || dy > halfH) return false;
            // Width at this y: 0 at top apex, full width at base.
            double t = (dy + halfH) / node.height;  // 0 at apex, 1 at base
            double halfWAtY = halfW * t;
            return std::abs(dx) <= halfWAtY;
        }
        case NodeShape::Star: {
            // Loose: bounding circle
            double r = std::min(halfW, halfH);
            return (dx * dx + dy * dy) <= (r * r);
        }
        case NodeShape::Cloud: {
            // Loose: bounding ellipse
            if (halfW <= 0 || halfH <= 0) return false;
            double nx = dx / halfW;
            double ny = dy / halfH;
            return (nx * nx + ny * ny) <= 1.0;
        }
    }
    return false;
}

std::string UltraCanvasNodeDiagram::FindNodeAt(const Point2Di& screenPos) {
    Point2Dd worldPos = ScreenToWorld(screenPos);
    
    // Iterate in reverse z-order (topmost first)
    for (auto it = nodeOrder.rbegin(); it != nodeOrder.rend(); ++it) {
        auto nodeIt = nodes.find(*it);
        if (nodeIt == nodes.end()) continue;
        if (PointInNode(nodeIt->second, worldPos)) {
            return nodeIt->first;
        }
    }
    return "";
}

std::string UltraCanvasNodeDiagram::FindLinkAt(const Point2Di& screenPos) {
    Point2Dd worldPos = ScreenToWorld(screenPos);
    double toleranceWorld = LINK_HIT_TOLERANCE / viewport.GetZoomLevel();
    
    for (const auto& link : links) {
        const auto* sourceNode = GetNode(link.sourceNodeId);
        const auto* targetNode = GetNode(link.targetNodeId);
        if (!sourceNode || !targetNode) continue;
        
        // Hit test against the straight line between node centers.
        // For Bezier/Step links this is approximate but visually close enough.
        double sx = sourceNode->x + sourceNode->width / 2.0;
        double sy = sourceNode->y + sourceNode->height / 2.0;
        double tx = targetNode->x + targetNode->width / 2.0;
        double ty = targetNode->y + targetNode->height / 2.0;
        
        double dx = tx - sx;
        double dy = ty - sy;
        double lengthSq = dx * dx + dy * dy;
        if (lengthSq <= 0.0) continue;
        
        double t = ((worldPos.x - sx) * dx + (worldPos.y - sy) * dy) / lengthSq;
        t = std::clamp(t, 0.0, 1.0);
        
        double projX = sx + t * dx;
        double projY = sy + t * dy;
        
        double distX = worldPos.x - projX;
        double distY = worldPos.y - projY;
        double dist = std::sqrt(distX * distX + distY * distY);
        
        if (dist <= toleranceWorld) return link.id;
    }
    return "";
}

std::pair<std::string, std::string> UltraCanvasNodeDiagram::FindHandleAt(const Point2Di& screenPos) {
    Point2Dd worldPos = ScreenToWorld(screenPos);
    
    // Iterate in reverse z-order so handles of the topmost node win
    for (auto it = nodeOrder.rbegin(); it != nodeOrder.rend(); ++it) {
        auto nodeIt = nodes.find(*it);
        if (nodeIt == nodes.end()) continue;
        const auto& node = nodeIt->second;
        
        for (const auto& handle : node.handles) {
            if (!handle.connectable) continue;
            Point2Dd hp = GetHandleWorldPosition(node, handle);
            double dx = worldPos.x - hp.x;
            double dy = worldPos.y - hp.y;
            double r = handle.radius + HANDLE_HIT_TOLERANCE / viewport.GetZoomLevel();
            if ((dx * dx + dy * dy) <= (r * r)) {
                return { node.id, handle.id };
            }
        }
    }
    return { "", "" };
}

bool UltraCanvasNodeDiagram::PointInMinimap(const Point2Di& screenPos) const {
    return viewport.PointInMinimap(screenPos);
}

int UltraCanvasNodeDiagram::FindControlButtonAt(const Point2Di& screenPos) {
    return viewport.FindControlButtonAt(screenPos);
}


static constexpr double NODE_DIAGRAM_PI_R3 = 3.14159265358979323846f;

// =============================================================================
// MAIN RENDER
// =============================================================================

void UltraCanvasNodeDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx || !IsVisible()) return;

    // Keep the shared viewport's drawable area in step with the element size,
    // so overlay placement and FitView stay correct after a resize.
    SyncViewportSize();
    
    // Background (in screen space, no transform)
    ctx->SetFillPaint(style.backgroundColor);
    ctx->FillRectangle(GetLocalBounds());
    
    // Apply viewport transform: translate then scale.
    // After this, all draw calls accept WORLD coordinates.
    ctx->PushState();
    Point2Dd pan = viewport.GetPanOffset();
    ctx->Translate(pan.x, pan.y);
    ctx->Scale(viewport.GetZoomLevel(), viewport.GetZoomLevel());
    
    if (style.showGrid) {
        RenderGrid(ctx);
    }

    // 2.2.0: cluster containers sit behind the links so edges crossing between
    // clusters stay readable on top of the boxes.
    if (groupsVisible && !groups.empty()) {
        RenderGroups(ctx);
    }

    RenderLinks(ctx);
    RenderNodes(ctx);

    // 2.2.0: group titles go LAST. Drawn with the boxes they end up underneath
    // whichever node happens to sit in that corner, and a cluster title is
    // exactly the label you cannot afford to lose.
    if (groupsVisible && !groups.empty()) {
        RenderGroupTitles(ctx);
    }
    
    // In-progress drag-to-connect line stays in WORLD space
    if (isConnecting) {
        RenderConnectionLine(ctx);
    }
    
    // Selection box also in WORLD space
    if (isSelectingBox) {
        RenderSelectionBox(ctx);
    }
    
    ctx->PopState();
    
    // Overlays in SCREEN space (after PopState)
    if (viewport.IsMinimapVisible()) {
        RenderMinimap(ctx);
    }
    if (viewport.AreControlsVisible()) {
        RenderControls(ctx);
    }
    if (legend.visible && !legend.entries.empty()) {
        RenderLegend(ctx);   // 2.2.0
    }
}

// =============================================================================
// CLUSTER CONTAINERS (NEW in 2.2.0)
// =============================================================================
//
// Rendered in WORLD space, before the links, so a box never covers the edges
// that cross it. Bounds are recomputed every frame rather than cached: node
// positions change on drag and on every layout run, and re-deriving a box from
// its members is a handful of comparisons.
// =============================================================================

void UltraCanvasNodeDiagram::RenderGroups(IRenderContext* ctx) {
    for (const auto& group : groups) {
        if (!group.visible) continue;

        Rect2Dd box = ComputeGroupBounds(group);
        if (box.width <= 0.0 || box.height <= 0.0) continue;

        // Fill first (usually a very low alpha wash, or fully transparent)
        if (group.fillColor.a > 0) {
            ctx->SetFillPaint(group.fillColor);
            if (group.cornerRadius > 0.0) {
                ctx->FillRoundedRectangle(box, group.cornerRadius);
            } else {
                ctx->FillRectangle(box);
            }
        }

        // Border. Stroke width is divided by the zoom so the box outline stays
        // visually constant instead of thickening as the user zooms in.
        if (group.borderWidth > 0.0 && group.borderColor.a > 0) {
            double zoom = viewport.GetZoomLevel();
            if (zoom < 0.001) zoom = 1.0;

            ctx->SetStrokePaint(group.borderColor);
            ctx->SetStrokeWidth(group.borderWidth / zoom);

            if (group.dashed) {
                ctx->SetLineDash(UCDashPattern({group.dashLength / zoom,
                                                 group.dashGap / zoom}, 0.0));
            }

            if (group.cornerRadius > 0.0) {
                ctx->DrawRoundedRectangle(box, group.cornerRadius);
            } else {
                ctx->DrawRectangle(box);
            }

            if (group.dashed) {
                ctx->SetLineDash(UCDashPattern());   // Restore solid
            }
        }
    }
}

void UltraCanvasNodeDiagram::RenderGroupTitles(IRenderContext* ctx) {
    for (const auto& group : groups) {
        if (!group.visible) continue;
        if (group.labelPosition == GroupLabelPosition::NoLabel) continue;
        if (group.label.empty()) continue;

        Rect2Dd box = ComputeGroupBounds(group);
        if (box.width <= 0.0 || box.height <= 0.0) continue;

        RenderGroupLabel(ctx, group, box);
    }
}

void UltraCanvasNodeDiagram::RenderGroupLabel(IRenderContext* ctx,
                                               const NodeDiagramGroup& group,
                                               const Rect2Dd& box) {
    // Titles are drawn inside the world transform (so they track their box) but
    // sized in SCREEN pixels: dividing by the zoom keeps a cluster title legible
    // and proportionate at every zoom level, the same trick the box border uses.
    double zoom = viewport.GetZoomLevel();
    if (zoom < 0.001) zoom = 1.0;

    ctx->SetTextPaint(group.labelColor);
    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(group.labelFontSize / zoom);

    Size2Di dims = ctx->GetTextLineDimensions(group.label);
    double textW = static_cast<double>(dims.width);
    double textH = static_cast<double>(dims.height);

    double margin = group.labelMargin / zoom;
    double x = box.x + margin;
    double y = box.y + margin;

    switch (group.labelPosition) {
        case GroupLabelPosition::TopLeft:
            break;   // Already correct
        case GroupLabelPosition::TopCenter:
            x = box.x + (box.width - textW) * 0.5;
            break;
        case GroupLabelPosition::TopRight:
            x = box.x + box.width - textW - margin;
            break;
        case GroupLabelPosition::BottomLeft:
            y = box.y + box.height - textH - margin;
            break;
        case GroupLabelPosition::BottomCenter:
            x = box.x + (box.width - textW) * 0.5;
            y = box.y + box.height - textH - margin;
            break;
        case GroupLabelPosition::NoLabel:
        default:
            return;
    }

    ctx->DrawText(group.label, {x, y});
}

// =============================================================================
// COLOR LEGEND OVERLAY (NEW in 2.2.0)
// =============================================================================
//
// Screen space, like the minimap and controls: a legend that panned away with
// the diagram would be useless. Panel size is measured from the actual text so
// long category names are never clipped.
// =============================================================================

Rect2Dd UltraCanvasNodeDiagram::ComputeLegendPanel(IRenderContext* ctx) const {
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(legend.fontSize);

    double rowHeight = std::max(legend.swatchHeight,
                                 static_cast<double>(ctx->GetTextLineHeight("Ag")));
    double widest = 0.0;
    for (const auto& entry : legend.entries) {
        double w = static_cast<double>(ctx->GetTextLineWidth(entry.label));
        widest = std::max(widest, w);
    }

    double titleHeight = 0.0;
    if (!legend.title.empty()) {
        ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
        ctx->SetFontSize(legend.titleFontSize);
        titleHeight = static_cast<double>(ctx->GetTextLineHeight(legend.title)) + legend.titleGap;
        widest = std::max(widest, static_cast<double>(ctx->GetTextLineWidth(legend.title))
                                   - legend.swatchWidth - legend.swatchGap);
    }

    size_t rows = legend.entries.size();
    double contentW = legend.swatchWidth + legend.swatchGap + widest;
    double contentH = titleHeight
                    + static_cast<double>(rows) * rowHeight
                    + static_cast<double>(rows > 0 ? rows - 1 : 0) * legend.rowGap;

    double panelW = contentW + legend.innerPadding * 2.0;
    double panelH = contentH + legend.innerPadding * 2.0;

    double x = legend.padding;
    double y = legend.padding;
    switch (legend.position) {
        case DiagramPanelPosition::TopLeft:
            break;
        case DiagramPanelPosition::TopRight:
            x = finalBounds.width - panelW - legend.padding;
            break;
        case DiagramPanelPosition::BottomLeft:
            y = finalBounds.height - panelH - legend.padding;
            break;
        case DiagramPanelPosition::BottomRight:
            x = finalBounds.width  - panelW - legend.padding;
            y = finalBounds.height - panelH - legend.padding;
            break;
    }

    return Rect2Dd(x, y, panelW, panelH);
}

void UltraCanvasNodeDiagram::RenderLegend(IRenderContext* ctx) {
    Rect2Dd panel = ComputeLegendPanel(ctx);

    if (legend.showBackground) {
        ctx->SetFillPaint(legend.backgroundColor);
        ctx->FillRoundedRectangle(panel, 4.0);
        ctx->SetStrokePaint(legend.borderColor);
        ctx->SetStrokeWidth(1.0);
        ctx->DrawRoundedRectangle(panel, 4.0);
    }

    double x = panel.x + legend.innerPadding;
    double y = panel.y + legend.innerPadding;

    if (!legend.title.empty()) {
        ctx->SetTextPaint(legend.textColor);
        ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
        ctx->SetFontSize(legend.titleFontSize);
        double titleH = static_cast<double>(ctx->GetTextLineHeight(legend.title));
        ctx->DrawText(legend.title, {x, y});
        y += titleH + legend.titleGap;
    }

    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(legend.fontSize);
    double rowHeight = std::max(legend.swatchHeight,
                                 static_cast<double>(ctx->GetTextLineHeight("Ag")));

    for (const auto& entry : legend.entries) {
        double swatchY = y + (rowHeight - legend.swatchHeight) * 0.5;

        ctx->SetFillPaint(entry.color);
        ctx->FillRectangle(Rect2Dd(x, swatchY, legend.swatchWidth, legend.swatchHeight));
        if (legend.showSwatchBorder) {
            ctx->SetStrokePaint(legend.swatchBorderColor);
            ctx->SetStrokeWidth(0.5);
            ctx->DrawRectangle(Rect2Dd(x, swatchY, legend.swatchWidth, legend.swatchHeight));
        }

        ctx->SetTextPaint(legend.textColor);
        double textH = static_cast<double>(ctx->GetTextLineHeight(entry.label));
        ctx->DrawText(entry.label,
                      {x + legend.swatchWidth + legend.swatchGap,
                       y + (rowHeight - textH) * 0.5});

        y += rowHeight + legend.rowGap;
    }
}

// =============================================================================
// GRID
// =============================================================================
//
// Grid is rendered in WORLD space. To make it cover the visible viewport at
// any zoom/pan, we compute the world rectangle that maps to the screen bounds
// and step through grid lines starting from a multiple of gridSpacing.
// =============================================================================

void UltraCanvasNodeDiagram::RenderGrid(IRenderContext* ctx) {
    ctx->SetStrokePaint(style.gridColor);
    ctx->SetStrokeWidth(1.0 / viewport.GetZoomLevel());  // Keep line visually constant at 1px
    
    double spacing = style.gridSpacing;
    if (spacing < 1.0) return;
    
    Point2Dd topLeft     = ScreenToWorld({0,0});
    Point2Dd bottomRight = ScreenToWorld(Point2Di(finalBounds.width,
                                                   finalBounds.height));
    
    double startX = std::floor(topLeft.x / spacing) * spacing;
    double endX   = std::ceil (bottomRight.x / spacing) * spacing;
    double startY = std::floor(topLeft.y / spacing) * spacing;
    double endY   = std::ceil (bottomRight.y / spacing) * spacing;
    
    for (double x = startX; x <= endX; x += spacing) {
        ctx->DrawLine({x, topLeft.y}, {x, bottomRight.y});
    }
    for (double y = startY; y <= endY; y += spacing) {
        ctx->DrawLine({topLeft.x, y}, {bottomRight.x, y});
    }
}

// =============================================================================
// LINK PATH BUILDERS
// =============================================================================
//
// All paths are returned as polylines for hit-testing and uniform rendering.
// Bezier paths use cubic De Casteljau sampled at 24 steps.
// =============================================================================

void UltraCanvasNodeDiagram::BuildLinkBezier(const Point2Dd& src, const Point2Dd& tgt,
                                              HandlePosition srcSide, HandlePosition tgtSide,
                                              std::vector<Point2Dd>& outPath) const {
    // Control point offset: tangent perpendicular to face, length proportional
    // to distance for organic curvature.
    double dx = tgt.x - src.x;
    double dy = tgt.y - src.y;
    double dist = std::sqrt(dx * dx + dy * dy);
    double offset = std::max(40.0, dist * 0.4f);
    
    auto offsetForSide = [offset](HandlePosition side) {
        switch (side) {
            case HandlePosition::Right:  return Point2Dd( offset,  0.0);
            case HandlePosition::Left:   return Point2Dd(-offset,  0.0);
            case HandlePosition::Bottom: return Point2Dd( 0.0,    offset);
            case HandlePosition::Top:    return Point2Dd( 0.0,   -offset);
        }
        return Point2Dd(offset, 0.0);
    };
    
    Point2Dd cp1Off = offsetForSide(srcSide);
    Point2Dd cp2Off = offsetForSide(tgtSide);
    
    Point2Dd cp1(src.x + cp1Off.x, src.y + cp1Off.y);
    Point2Dd cp2(tgt.x + cp2Off.x, tgt.y + cp2Off.y);
    
    const int segments = 24;
    outPath.clear();
    outPath.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        double t = static_cast<double>(i) / segments;
        double omt = 1.0 - t;
        double omt2 = omt * omt;
        double omt3 = omt2 * omt;
        double t2 = t * t;
        double t3 = t2 * t;
        double bx = omt3 * src.x + 3.0 * omt2 * t * cp1.x + 3.0 * omt * t2 * cp2.x + t3 * tgt.x;
        double by = omt3 * src.y + 3.0 * omt2 * t * cp1.y + 3.0 * omt * t2 * cp2.y + t3 * tgt.y;
        outPath.emplace_back(bx, by);
    }
}

void UltraCanvasNodeDiagram::BuildLinkStep(const Point2Dd& src, const Point2Dd& tgt,
                                            HandlePosition srcSide, HandlePosition tgtSide,
                                            bool smooth, std::vector<Point2Dd>& outPath) const {
    // smooth=false -> sharp orthogonal; smooth=true -> rounded corners
    // (for now both produce the same polyline — corner rounding can be added later).
    (void)smooth;
    
    outPath.clear();
    outPath.push_back(src);
    
    bool srcHorizontal = (srcSide == HandlePosition::Left || srcSide == HandlePosition::Right);
    bool tgtHorizontal = (tgtSide == HandlePosition::Left || tgtSide == HandlePosition::Right);
    
    if (srcHorizontal && tgtHorizontal) {
        // H-V-H pattern
        double midX = (src.x + tgt.x) * 0.5f;
        outPath.emplace_back(midX, src.y);
        outPath.emplace_back(midX, tgt.y);
    }
    else if (!srcHorizontal && !tgtHorizontal) {
        // V-H-V pattern
        double midY = (src.y + tgt.y) * 0.5f;
        outPath.emplace_back(src.x, midY);
        outPath.emplace_back(tgt.x, midY);
    }
    else if (srcHorizontal && !tgtHorizontal) {
        // H-V: leave horizontally, then vertical to target
        outPath.emplace_back(tgt.x, src.y);
    }
    else {
        // V-H: leave vertically, then horizontal to target
        outPath.emplace_back(src.x, tgt.y);
    }
    
    outPath.push_back(tgt);
}

// =============================================================================
// LINKS
// =============================================================================

void UltraCanvasNodeDiagram::RenderLinks(IRenderContext* ctx) {
    for (const auto& link : links) {
        RenderLink(ctx, link);
    }
}

void UltraCanvasNodeDiagram::RenderLink(IRenderContext* ctx, const NodeDiagramLink& link) {
    const auto* sourceNode = GetNode(link.sourceNodeId);
    const auto* targetNode = GetNode(link.targetNodeId);
    if (!sourceNode || !targetNode) return;
    
    // Determine endpoints. If specific handles are referenced and they exist,
    // anchor on those; otherwise use node centers.
    Point2Dd srcPoint, tgtPoint;
    HandlePosition srcSide = HandlePosition::Right;
    HandlePosition tgtSide = HandlePosition::Left;
    
    bool srcHandleResolved = false;
    if (!link.sourceHandleId.empty()) {
        for (const auto& h : sourceNode->handles) {
            if (h.id == link.sourceHandleId) {
                srcPoint = GetHandleWorldPosition(*sourceNode, h);
                srcSide = h.position;
                srcHandleResolved = true;
                break;
            }
        }
    }
    if (!srcHandleResolved) {
        srcPoint.x = sourceNode->x + sourceNode->width  / 2.0;
        srcPoint.y = sourceNode->y + sourceNode->height / 2.0;
        srcSide = InferHandleSide(*sourceNode, *targetNode);
    }
    
    bool tgtHandleResolved = false;
    if (!link.targetHandleId.empty()) {
        for (const auto& h : targetNode->handles) {
            if (h.id == link.targetHandleId) {
                tgtPoint = GetHandleWorldPosition(*targetNode, h);
                tgtSide = h.position;
                tgtHandleResolved = true;
                break;
            }
        }
    }
    if (!tgtHandleResolved) {
        tgtPoint.x = targetNode->x + targetNode->width  / 2.0;
        tgtPoint.y = targetNode->y + targetNode->height / 2.0;
        tgtSide = InferHandleSide(*targetNode, *sourceNode);
    }
    
    // Style resolution: selection > hover > base
    Color linkColor = link.lineColor;
    double linkWidth = link.lineWidth;
    if (link.isSelected) {
        linkColor = style.selectionColor;
        linkWidth = link.lineWidth + 1.5f;
    } else if (hoveredLinkId == link.id) {
        linkColor = Color(
            std::min(255, linkColor.r + 30),
            std::min(255, linkColor.g + 30),
            std::min(255, linkColor.b + 30),
            linkColor.a
        );
    }
    
    // Build path according to style
    std::vector<Point2Dd> path;
    switch (link.style) {
        case LinkStyle::Bezier:
            BuildLinkBezier(srcPoint, tgtPoint, srcSide, tgtSide, path);
            break;
        case LinkStyle::SmoothStep:
            BuildLinkStep(srcPoint, tgtPoint, srcSide, tgtSide, true, path);
            break;
        case LinkStyle::Step:
            BuildLinkStep(srcPoint, tgtPoint, srcSide, tgtSide, false, path);
            break;
        case LinkStyle::Straight:
        default:
            path = { srcPoint, tgtPoint };
            break;
    }
    
    // Draw the polyline
    ctx->SetStrokePaint(linkColor);
    ctx->SetStrokeWidth(linkWidth);
    for (size_t i = 1; i < path.size(); ++i) {
        ctx->DrawLine(path[i - 1], path[i]);
    }
    
    // Arrow head: use last segment's local direction (lesson from BlockDiagram v2.3)
    if (link.directed && path.size() >= 2) {
        const Point2Dd& tip = path.back();
        const Point2Dd& prev = path[path.size() - 2];
        double ddx = tip.x - prev.x;
        double ddy = tip.y - prev.y;
        double dlen = std::sqrt(ddx * ddx + ddy * ddy);
        if (dlen > 0.001f) {
            ddx /= dlen;
            ddy /= dlen;
        } else {
            ddx = 1.0; ddy = 0.0;
        }
        // Pull tip back to node edge to avoid overshoot — small nudge
        double nudge = 6.0;
        double arrowTipX = tip.x - ddx * nudge;
        double arrowTipY = tip.y - ddy * nudge;
        RenderArrowHead(ctx, arrowTipX, arrowTipY, ddx, ddy, linkColor, 10.0);
    }
    
    if (!link.label.empty()) {
        RenderLinkLabel(ctx, link);
    }
}

void UltraCanvasNodeDiagram::RenderLinkLabel(IRenderContext* ctx, const NodeDiagramLink& link) {
    if (link.label.empty()) return;
    
    const auto* sourceNode = GetNode(link.sourceNodeId);
    const auto* targetNode = GetNode(link.targetNodeId);
    if (!sourceNode || !targetNode) return;
    
    double sx = sourceNode->x + sourceNode->width / 2.0;
    double sy = sourceNode->y + sourceNode->height / 2.0;
    double tx = targetNode->x + targetNode->width / 2.0;
    double ty = targetNode->y + targetNode->height / 2.0;
    
    double midX = (sx + tx) / 2.0;
    double midY = (sy + ty) / 2.0;
    
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.baseFontSize - 1.0);
    
    Size2Di textDim = ctx->GetTextLineDimensions(link.label);
    int textWidth = textDim.width;
    int textHeight = textDim.height;

    double padding = 3.0;
    ctx->SetFillPaint(Color(255, 255, 255, 220));
    ctx->FillRoundedRectangle(
        Rect2Dd(
            midX - textWidth / 2.0 - padding,
            midY - textHeight / 2.0 - padding,
            textWidth + padding * 2,
            textHeight + padding * 2),
        3.0
    );
    
    ctx->SetTextPaint(Color(60, 60, 60, 255));
    // 2.0.6: Y is the TOP of text (Cairo+Pango), not baseline. Same canonical
    // pattern as RenderNodeLabel: top = midY - textHeight/2.
    ctx->DrawText(link.label,
                  {midX - textWidth  / 2.0,
                  midY - textHeight / 2.0});
}

void UltraCanvasNodeDiagram::RenderArrowHead(IRenderContext* ctx, double tipX, double tipY,
                                              double dirX, double dirY, const Color& color, double size) {
    // Two thick stroke lines forming a solid V at the tip.
    // Equivalent to a filled arrow at line widths >= 1.6, works without FillPolygon
    // (lesson from BlockDiagram v2.3).
    const double arrowAngle = 0.45f;
    double cosA = std::cos(arrowAngle);
    double sinA = std::sin(arrowAngle);
    
    // Rotate -dirX,-dirY by +/- arrowAngle to get the two rear points
    double backX = -dirX;
    double backY = -dirY;
    
    double ax1 = tipX + size * (backX * cosA - backY * sinA);
    double ay1 = tipY + size * (backX * sinA + backY * cosA);
    double ax2 = tipX + size * (backX * cosA + backY * sinA);
    double ay2 = tipY + size * (-backX * sinA + backY * cosA);
    
    double headStroke = 2.5f;
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(headStroke);
    ctx->DrawLine({ax1, ay1}, {tipX, tipY});
    ctx->DrawLine({ax2, ay2}, {tipX, tipY});
}

// =============================================================================
// NODES
// =============================================================================

void UltraCanvasNodeDiagram::RenderNodes(IRenderContext* ctx) {
    // Render in z-order (back to front)
    for (const auto& nodeId : nodeOrder) {
        auto it = nodes.find(nodeId);
        if (it == nodes.end()) continue;
        const NodeDiagramNode& node = it->second;
        
        RenderNodeShape(ctx, node);
        RenderNodeLabel(ctx, node);
        if (!node.handles.empty()) {
            RenderNodeHandles(ctx, node);
        }
    }
}

void UltraCanvasNodeDiagram::RenderNodeShape(IRenderContext* ctx, const NodeDiagramNode& node) {
    Color fillColor = node.fillColor;
    Color borderColor = node.borderColor;
    double borderWidth = node.borderWidth;
    
    // Hover: lighten fill
    if (!hoveredNodeId.empty() && hoveredNodeId == node.id && !node.isSelected) {
        fillColor = Color(
            std::min(255, fillColor.r + 20),
            std::min(255, fillColor.g + 20),
            std::min(255, fillColor.b + 20),
            fillColor.a
        );
        borderWidth = node.borderWidth + 1.0;
    }
    
    if (node.isSelected) {
        borderColor = style.selectionColor;
        borderWidth = style.selectionWidth;
    }
    
    ctx->SetFillPaint(fillColor);
    ctx->SetStrokePaint(borderColor);
    ctx->SetStrokeWidth(borderWidth);
    
    double centerX = node.x + node.width / 2.0;
    double centerY = node.y + node.height / 2.0;
    double radius = std::min(node.width, node.height) / 2.0;
    
    switch (node.shape) {
        case NodeShape::Circle:
            ctx->FillCircle({centerX, centerY}, radius);
            ctx->DrawCircle({centerX, centerY}, radius);
            break;
        case NodeShape::RoundedSquare: {
            double r = std::min(node.width, node.height) * 0.15f;
            ctx->FillRoundedRectangle(Rect2Dd(node.x, node.y, node.width, node.height), r);
            ctx->DrawRoundedRectangle(Rect2Dd(node.x, node.y, node.width, node.height), r);
            break;
        }
        case NodeShape::Square:
        case NodeShape::Rectangle:
            ctx->FillRectangle(Rect2Dd(node.x, node.y, node.width, node.height));
            ctx->DrawRectangle(Rect2Dd(node.x, node.y, node.width, node.height));
            break;
        case NodeShape::Diamond: {
            std::vector<Point2Dd> pts = {
                { centerX, node.y },
                { node.x + node.width, centerY },
                { centerX, node.y + node.height },
                { node.x, centerY }
            };
            ctx->FillLinePath(pts);
            ctx->DrawLinePath(pts, true);
            break;
        }
        case NodeShape::Hexagon: {
            double rx = node.width / 2.0;
            double ry = node.height / 2.0;
            std::vector<Point2Dd> pts;
            pts.reserve(6);
            for (int i = 0; i < 6; ++i) {
                double angle = i * 60.0 * NODE_DIAGRAM_PI_R3 / 180.0;
                pts.emplace_back(centerX + rx * std::cos(angle),
                                  centerY + ry * std::sin(angle));
            }
            ctx->FillLinePath(pts);
            ctx->DrawLinePath(pts, true);
            break;
        }
        case NodeShape::Triangle: {
            std::vector<Point2Dd> pts = {
                { centerX, node.y },
                { node.x + node.width, node.y + node.height },
                { node.x, node.y + node.height }
            };
            ctx->FillLinePath(pts);
            ctx->DrawLinePath(pts, true);
            break;
        }
        case NodeShape::Star: {
            double outerR = radius;
            double innerR = radius * 0.4f;
            std::vector<Point2Dd> pts;
            pts.reserve(10);
            for (int i = 0; i < 10; ++i) {
                double angle = i * 36.0 * NODE_DIAGRAM_PI_R3 / 180.0 - NODE_DIAGRAM_PI_R3 / 2.0;
                double r = (i % 2 == 0) ? outerR : innerR;
                pts.emplace_back(centerX + r * std::cos(angle),
                                  centerY + r * std::sin(angle));
            }
            ctx->FillLinePath(pts);
            ctx->DrawLinePath(pts, true);
            break;
        }
        case NodeShape::Cloud: {
            // Three overlapping ellipses. Border is approximate.
            ctx->FillEllipse(Rect2Dd(node.x + node.width * 0.25f, centerY,
                                      node.width * 0.2f, node.height * 0.3f));
            ctx->FillEllipse(Rect2Dd(centerX, node.y + node.height * 0.3f,
                                      node.width * 0.25f, node.height * 0.35f));
            ctx->FillEllipse(Rect2Dd(node.x + node.width * 0.75f, centerY,
                                      node.width * 0.2f, node.height * 0.3f));
            ctx->DrawEllipse(Rect2Dd(node.x + node.width * 0.25f, centerY,
                                      node.width * 0.2f, node.height * 0.3f));
            ctx->DrawEllipse(Rect2Dd(centerX, node.y + node.height * 0.3f,
                                      node.width * 0.25f, node.height * 0.35f));
            ctx->DrawEllipse(Rect2Dd(node.x + node.width * 0.75f, centerY,
                                      node.width * 0.2f, node.height * 0.3f));
            break;
        }
        default:
            ctx->FillCircle({centerX, centerY}, radius);
            ctx->DrawCircle({centerX, centerY}, radius);
            break;
    }
}

void UltraCanvasNodeDiagram::RenderNodeLabel(IRenderContext* ctx, const NodeDiagramNode& node) {
    if (node.label.empty()) return;
    
    ctx->SetTextPaint(node.textColor);
    ctx->SetFontFace(style.fontFamily, FontWeight::Bold, FontSlant::Normal);
    ctx->SetFontSize(node.fontSize > 0 ? node.fontSize : style.baseFontSize);
    
    Size2Di nodeTextDim = ctx->GetTextLineDimensions(node.label);
    int textWidth = nodeTextDim.width;
    int textHeight = nodeTextDim.height;
    
    // 2.0.6: Cairo+Pango DrawText (cairo_move_to + pango_cairo_show_layout)
    // treats Y as the TOP of the text bounding box, not the baseline. The old
    // formula (centerY + textHeight/3.0) assumed baseline anchoring and
    // pushed the label below center. Use the canonical framework pattern
    // (same as UltraCanvasButton, FlowChart): y = nodeY + (nodeH - textH)/2.
    double textX = node.x + (node.width  - static_cast<double>(textWidth))  / 2.0;
    double textY = node.y + (node.height - static_cast<double>(textHeight)) / 2.0;
    
    // 2.0.5: keep label inside node bounds when text is wider than the node.
    // Anchor the leading edge to a small inset; trailing edge may overflow if
    // the label is genuinely too long.
    const double kInset = 4.0;
    if (textX < node.x + kInset) {
        textX = node.x + kInset;
    }
    if (textY < node.y + kInset) {
        textY = node.y + kInset;
    }
    
    ctx->DrawText(node.label, {textX, textY});
}

void UltraCanvasNodeDiagram::RenderNodeHandles(IRenderContext* ctx, const NodeDiagramNode& node) {
    for (const auto& handle : node.handles) {
        Point2Dd hp = GetHandleWorldPosition(node, handle);
        
        // Determine color
        Color hcolor = handle.color;
        bool isHovered = (hoveredHandleNodeId == node.id && hoveredHandleId == handle.id);
        if (isHovered) {
            hcolor = handle.hoverColor;
        } else if (handle.currentConnections > 0) {
            hcolor = handle.connectedColor;
        }
        
        // Outer ring (white outline for visibility)
        ctx->SetFillPaint(Color(255, 255, 255, 255));
        ctx->FillCircle({hp.x, hp.y}, handle.radius + 1.0);

        // Inner colored disk
        ctx->SetFillPaint(hcolor);
        ctx->FillCircle({hp.x, hp.y}, handle.radius);

        // Border
        ctx->SetStrokePaint(Color(60, 60, 60, 255));
        ctx->SetStrokeWidth(1.0);
        ctx->DrawCircle({hp.x, hp.y}, handle.radius);
    }
}

// =============================================================================
// OVERLAYS
// =============================================================================

void UltraCanvasNodeDiagram::RenderConnectionLine(IRenderContext* ctx) {
    auto* node = GetNode(connectionSourceNode);
    if (!node) return;
    
    Point2Dd from;
    HandlePosition fromSide = HandlePosition::Right;
    bool found = false;
    for (const auto& h : node->handles) {
        if (h.id == connectionSourceHandle) {
            from = GetHandleWorldPosition(*node, h);
            fromSide = h.position;
            found = true;
            break;
        }
    }
    if (!found) {
        from.x = node->x + node->width  / 2.0;
        from.y = node->y + node->height / 2.0;
    }
    
    // Build a bezier preview
    std::vector<Point2Dd> path;
    HandlePosition guessTo;
    switch (fromSide) {
        case HandlePosition::Right:  guessTo = HandlePosition::Left;   break;
        case HandlePosition::Left:   guessTo = HandlePosition::Right;  break;
        case HandlePosition::Top:    guessTo = HandlePosition::Bottom; break;
        case HandlePosition::Bottom: guessTo = HandlePosition::Top;    break;
    }
    BuildLinkBezier(from, connectionEndPoint, fromSide, guessTo, path);
    
    ctx->SetStrokePaint(style.connectionLineColor);
    ctx->SetStrokeWidth(style.connectionLineWidth);
    for (size_t i = 1; i < path.size(); ++i) {
        ctx->DrawLine(path[i-1], path[i]);
    }
}

void UltraCanvasNodeDiagram::RenderSelectionBox(IRenderContext* ctx) {
    double x = std::min(selectionBoxStart.x, selectionBoxEnd.x);
    double y = std::min(selectionBoxStart.y, selectionBoxEnd.y);
    double w = std::abs(selectionBoxEnd.x - selectionBoxStart.x);
    double h = std::abs(selectionBoxEnd.y - selectionBoxStart.y);
    if (w < 1 || h < 1) return;
    
    ctx->SetFillPaint(style.selectionBoxFill);
    ctx->FillRectangle(Rect2Dd(x, y, w, h));
    ctx->SetStrokePaint(style.selectionBoxStroke);
    ctx->SetStrokeWidth(1.0 / viewport.GetZoomLevel());
    ctx->DrawRectangle(Rect2Dd(x, y, w, h));
}

void UltraCanvasNodeDiagram::RenderMinimap(IRenderContext* ctx) {
    viewport.RenderMinimap(ctx, ComputeContentBounds(),
        [&](const DiagramMinimapProjection& projection) {
            for (const auto& pair : nodes) {
                const auto& n = pair.second;
                Point2Dd topLeft = projection.WorldToMinimap(n.x, n.y);
                double w = std::max(1.0, n.width * projection.scale);
                double h = std::max(1.0, n.height * projection.scale);
                ctx->FillRectangle(Rect2Dd(topLeft.x, topLeft.y, w, h));
            }
        });
}

void UltraCanvasNodeDiagram::RenderControls(IRenderContext* ctx) {
    viewport.RenderControls(ctx, hoveredControlButton, !isInteractive);
}


// =============================================================================
// EVENT DISPATCH
// =============================================================================

bool UltraCanvasNodeDiagram::OnEvent(const UCEvent& event) {
    if (!IsVisible() || IsDisabled()) return false;
    
    // Track multi-select modifier (shift) at all times so click handlers know.
    isMultiSelectKeyHeld = event.shift;
    
    switch (event.type) {
        case UCEventType::MouseDown:        return HandleMouseDown(event);
        case UCEventType::MouseUp:          return HandleMouseUp(event);
        case UCEventType::MouseMove:        return HandleMouseMove(event);
        case UCEventType::MouseWheel:       return HandleMouseWheel(event);
        case UCEventType::MouseDoubleClick: {  // NEW in 2.0.3
            if (!Contains(Point2Di(event.pointer.x, event.pointer.y))) return false;
            std::string nid = FindNodeAt(Point2Di(event.pointer.x, event.pointer.y));
            if (!nid.empty() && onNodeDoubleClick) {
                onNodeDoubleClick(nid);
                return true;
            }
            return false;
        }
        case UCEventType::KeyDown:          return HandleKeyDown(event);
        default:                             break;
    }
    return UltraCanvasUIElement::OnEvent(event);
}

// =============================================================================
// MOUSE DOWN
// =============================================================================

bool UltraCanvasNodeDiagram::HandleMouseDown(const UCEvent& event) {
    if (!Contains(Point2Di(event.pointer.x, event.pointer.y))) return false;
    if (!isInteractive && event.button != UCMouseButton::Middle) {
        // Locked: only allow panning via middle button
        return false;
    }
    
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    lastMousePos = mousePos;
    dragStartPos = mousePos;
    
    // -------- 1. Controls overlay buttons --------
    int controlIdx = FindControlButtonAt(mousePos);
    if (controlIdx >= 0) {
        // Button roles come from the shared viewport, so the index->action
        // mapping stays correct whichever button groups are enabled.
        SyncViewportSize();
        DiagramControlButton role =
            viewport.ApplyControlButton(controlIdx, ComputeContentBounds());
        if (role == DiagramControlButton::ToggleLock) {
            isInteractive = !isInteractive;   // Lock state is the element's, not the viewport's
        } else if (role != DiagramControlButton::NoAction) {
            NotifyViewportChange();
        }
        RequestRedraw();
        return true;
    }
    
    // -------- 1b. (NEW in 2.0.3) Right-click on canvas: dispatch callback --------
    // Right-click is only meaningful on empty canvas (no node, no link, no
    // handle, no overlay). If a handler is hooked up, dispatch the world
    // coordinates so the app can show a context menu / create a node / etc.
    if (event.button == UCMouseButton::Right) {
        // Skip if click landed on a node, link, handle, or minimap - those are
        // not "canvas" empty space. Hit-test in priority order.
        if (PointInMinimap(mousePos)) return true;  // Reserved for future minimap context menu
        auto [hNode, hHandle] = FindHandleAt(mousePos);
        if (!hNode.empty()) return true;            // Right-click on handle: ignored for now
        if (!FindNodeAt(mousePos).empty()) return true;
        if (!FindLinkAt(mousePos).empty()) return true;
        // Empty canvas: dispatch
        if (onCanvasRightClick) {
            Point2Dd world = ScreenToWorld(mousePos);
            onCanvasRightClick(world.x, world.y);
        }
        return true;
    }
    
    // -------- 2. Minimap --------
    if (PointInMinimap(mousePos) && viewport.MinimapConfig().pannable) {
        // Centre on the clicked world coordinate straight away, then keep
        // following the pointer for the rest of the drag.
        isDraggingMinimap = true;
        viewport.HandleMinimapDrag(mousePos, ComputeContentBounds());
        NotifyViewportChange();
        RequestRedraw();
        return true;
    }
    
    // -------- 3. Connection handles (start drag-to-connect) --------
    if (nodesConnectable && event.button == UCMouseButton::Left) {
        auto [hNodeId, hHandleId] = FindHandleAt(mousePos);
        if (!hNodeId.empty()) {
            auto* node = GetNode(hNodeId);
            if (node) {
                for (const auto& h : node->handles) {
                    if (h.id == hHandleId && h.connectable) {
                        isConnecting = true;
                        connectionSourceNode = hNodeId;
                        connectionSourceHandle = hHandleId;
                        connectionSourceType = h.type;
                        connectionEndPoint = ScreenToWorld(mousePos);
                        RequestRedraw();
                        return true;
                    }
                }
            }
        }
    }
    
    // -------- 4. Middle-click pan (always works when interactive) --------
    if (event.button == UCMouseButton::Middle) {
        isDraggingViewport = true;
        SetMouseCursor(UCMouseCursor::SizeAll);
        return true;
    }
    
    // -------- 5. Left-click on node --------
    if (event.button == UCMouseButton::Left) {
        std::string clickedNodeId = FindNodeAt(mousePos);
        if (!clickedNodeId.empty()) {
            auto* node = GetNode(clickedNodeId);
            if (!node) return false;
            
            if (node->selectable) {
                if (isMultiSelectKeyHeld) {
                    // Toggle individual selection
                    if (IsNodeSelected(clickedNodeId)) {
                        selectedNodes.erase(clickedNodeId);
                        node->isSelected = false;
                        if (selectedNodeId == clickedNodeId) selectedNodeId.clear();
                        NotifySelectionChange();
                        RequestRedraw();
                    } else {
                        SelectNode(clickedNodeId, true);
                    }
                } else {
                    if (!IsNodeSelected(clickedNodeId)) {
                        SelectNode(clickedNodeId, false);
                    }
                }
            }
            
            // Begin drag (single or multi)
            if (node->draggable) {
                isDraggingNode = true;
                dragStartPositions.clear();
                for (const auto& sid : selectedNodes) {
                    if (auto* sn = GetNode(sid)) {
                        dragStartPositions[sid] = Point2Dd(sn->x, sn->y);
                        sn->isDragging = true;
                    }
                }
                // Always include the clicked node even if selection logic skipped it
                if (dragStartPositions.find(clickedNodeId) == dragStartPositions.end()) {
                    dragStartPositions[clickedNodeId] = Point2Dd(node->x, node->y);
                    node->isDragging = true;
                }
                SetMouseCursor(UCMouseCursor::SizeAll);
            }
            return true;
        }
        
        // -------- 6. Left-click on link --------
        std::string clickedLinkId = FindLinkAt(mousePos);
        if (!clickedLinkId.empty()) {
            SelectLink(clickedLinkId, isMultiSelectKeyHeld);
            return true;
        }
        
        // -------- 7. Empty-area click --------
        if (!isMultiSelectKeyHeld) {
            DeselectAll();
        }
        
        // Shift+drag in empty area = selection box
        // Plain drag in empty area = pan (if panOnDrag enabled)
        if (isMultiSelectKeyHeld) {
            isSelectingBox = true;
            Point2Dd worldPos = ScreenToWorld(mousePos);
            selectionBoxStart = worldPos;
            selectionBoxEnd = worldPos;
        } else if (panOnDrag) {
            isDraggingViewport = true;
            SetMouseCursor(UCMouseCursor::SizeAll);
        }
        return true;
    }
    
    return false;
}

// =============================================================================
// MOUSE UP
// =============================================================================

bool UltraCanvasNodeDiagram::HandleMouseUp(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    
    // Drag-to-connect completion
    if (isConnecting) {
        bool linkCreated = false;
        auto [tgtNodeId, tgtHandleId] = FindHandleAt(mousePos);
        if (!tgtNodeId.empty() && tgtNodeId != connectionSourceNode) {
            auto* tgtNode = GetNode(tgtNodeId);
            if (tgtNode) {
                for (const auto& h : tgtNode->handles) {
                    if (h.id == tgtHandleId && h.connectable) {
                        // Compose the link. If we started from a Target handle,
                        // swap so the link points the natural direction.
                        NodeDiagramLink newLink;
                        if (connectionSourceType == HandleType::Source) {
                            newLink.sourceNodeId = connectionSourceNode;
                            newLink.sourceHandleId = connectionSourceHandle;
                            newLink.targetNodeId = tgtNodeId;
                            newLink.targetHandleId = tgtHandleId;
                        } else {
                            newLink.sourceNodeId = tgtNodeId;
                            newLink.sourceHandleId = tgtHandleId;
                            newLink.targetNodeId = connectionSourceNode;
                            newLink.targetHandleId = connectionSourceHandle;
                        }
                        // Auto-generate id
                        std::ostringstream idStream;
                        idStream << "link_" << newLink.sourceNodeId << "_"
                                 << newLink.targetNodeId << "_" << links.size();
                        newLink.id = idStream.str();
                        newLink.style = defaultLinkStyle;
                        
                        // Avoid exact duplicate (same src+tgt)
                        bool dup = false;
                        for (const auto& l : links) {
                            if (l.sourceNodeId == newLink.sourceNodeId &&
                                l.targetNodeId == newLink.targetNodeId &&
                                l.sourceHandleId == newLink.sourceHandleId &&
                                l.targetHandleId == newLink.targetHandleId) {
                                dup = true; break;
                            }
                        }
                        if (!dup) {
                            AddLink(newLink);
                            linkCreated = true;
                            if (onLinkCreated) onLinkCreated(newLink);
                        }
                        break;
                    }
                }
            }
        }
        isConnecting = false;
        connectionSourceNode.clear();
        connectionSourceHandle.clear();
        SetMouseCursor(UCMouseCursor::Default);
        RequestRedraw();
        return true;
    }
    
    // Selection box completion
    if (isSelectingBox) {
        double minX = std::min(selectionBoxStart.x, selectionBoxEnd.x);
        double maxX = std::max(selectionBoxStart.x, selectionBoxEnd.x);
        double minY = std::min(selectionBoxStart.y, selectionBoxEnd.y);
        double maxY = std::max(selectionBoxStart.y, selectionBoxEnd.y);
        
        // Select nodes whose center falls inside the box
        for (auto& pair : nodes) {
            auto& n = pair.second;
            if (!n.selectable) continue;
            double ncx = n.x + n.width  * 0.5f;
            double ncy = n.y + n.height * 0.5f;
            if (ncx >= minX && ncx <= maxX && ncy >= minY && ncy <= maxY) {
                if (selectedNodes.find(pair.first) == selectedNodes.end()) {
                    selectedNodes.insert(pair.first);
                    n.isSelected = true;
                }
            }
        }
        isSelectingBox = false;
        NotifySelectionChange();
        RequestRedraw();
        return true;
    }
    
    // Node drag end
    if (isDraggingNode) {
        for (auto& sid : selectedNodes) {
            if (auto* sn = GetNode(sid)) sn->isDragging = false;
        }
        for (const auto& kv : dragStartPositions) {
            if (auto* sn = GetNode(kv.first)) sn->isDragging = false;
        }
        isDraggingNode = false;
        dragStartPositions.clear();
        SetMouseCursor(UCMouseCursor::Default);
        return true;
    }
    
    // Viewport drag end
    if (isDraggingViewport) {
        isDraggingViewport = false;
        SetMouseCursor(UCMouseCursor::Default);
        return true;
    }
    
    // Minimap drag end
    if (isDraggingMinimap) {
        isDraggingMinimap = false;
        return true;
    }
    
    return false;
}

// =============================================================================
// MOUSE MOVE
// =============================================================================

bool UltraCanvasNodeDiagram::HandleMouseMove(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    Point2Di delta(mousePos.x - lastMousePos.x, mousePos.y - lastMousePos.y);
    
    // ----- Active drag operations -----
    if (isConnecting) {
        connectionEndPoint = ScreenToWorld(mousePos);
        RequestRedraw();
        lastMousePos = mousePos;
        return true;
    }
    
    if (isDraggingNode) {
        // Compute total cursor delta in world space relative to drag start
        Point2Dd startWorld = ScreenToWorld(dragStartPos);
        Point2Dd currentWorld = ScreenToWorld(mousePos);
        double worldDx = currentWorld.x - startWorld.x;
        double worldDy = currentWorld.y - startWorld.y;
        
        // Apply movement to every dragged node from its captured start position
        for (const auto& kv : dragStartPositions) {
            auto* n = GetNode(kv.first);
            if (!n || !n->draggable) continue;
            Point2Dd newPos(kv.second.x + worldDx, kv.second.y + worldDy);
            newPos = SnapPoint(newPos);
            n->x = newPos.x;
            n->y = newPos.y;
            if (onNodeDrag) onNodeDrag(n->id, n->x, n->y);
        }
        RequestRedraw();
        lastMousePos = mousePos;
        return true;
    }
    
    if (isDraggingViewport) {
        viewport.PanBy(delta.x, delta.y);
        NotifyViewportChange();
        RequestRedraw();
        lastMousePos = mousePos;
        return true;
    }
    
    if (isSelectingBox) {
        selectionBoxEnd = ScreenToWorld(mousePos);
        RequestRedraw();
        lastMousePos = mousePos;
        return true;
    }
    
    if (isDraggingMinimap) {
        // Delegate to the shared viewport, which inverts the same projection
        // RenderMinimap uses, so click position and pan stay consistent.
        viewport.HandleMinimapDrag(mousePos, ComputeContentBounds());
        NotifyViewportChange();
        RequestRedraw();
        lastMousePos = mousePos;
        return true;
    }
    
    // ----- Hover detection -----
    std::string prevHoveredNode = hoveredNodeId;
    std::string prevHoveredLink = hoveredLinkId;
    std::string prevHoveredHandleNode = hoveredHandleNodeId;
    std::string prevHoveredHandle = hoveredHandleId;
    int prevHoveredCtrl = hoveredControlButton;
    
    hoveredControlButton = FindControlButtonAt(mousePos);
    
    auto [hNodeId, hHandleId] = FindHandleAt(mousePos);
    hoveredHandleNodeId = hNodeId;
    hoveredHandleId = hHandleId;
    
    if (hNodeId.empty()) {
        hoveredNodeId = FindNodeAt(mousePos);
    } else {
        hoveredNodeId.clear();
    }
    
    if (hoveredNodeId.empty() && hNodeId.empty()) {
        hoveredLinkId = FindLinkAt(mousePos);
    } else {
        hoveredLinkId.clear();
    }
    
    // Hover callback
    if (prevHoveredNode != hoveredNodeId && !hoveredNodeId.empty() && onNodeHover) {
        onNodeHover(hoveredNodeId);
    }
    
    // Cursor feedback
    if (hoveredControlButton >= 0) {
        SetMouseCursor(UCMouseCursor::Hand);
    } else if (!hoveredHandleId.empty()) {
        SetMouseCursor(UCMouseCursor::Cross);
    } else if (!hoveredNodeId.empty()) {
        auto* node = GetNode(hoveredNodeId);
        if (node && node->draggable && isInteractive) {
            SetMouseCursor(UCMouseCursor::SizeAll);
        } else {
            SetMouseCursor(UCMouseCursor::Default);
        }
    } else {
        SetMouseCursor(UCMouseCursor::Default);
    }
    
    if (prevHoveredNode != hoveredNodeId ||
        prevHoveredLink != hoveredLinkId ||
        prevHoveredHandleNode != hoveredHandleNodeId ||
        prevHoveredHandle != hoveredHandleId ||
        prevHoveredCtrl != hoveredControlButton) {
        RequestRedraw();
    }
    
    lastMousePos = mousePos;
    return false;
}

// =============================================================================
// MOUSE WHEEL - zoom anchored at cursor (FIXED in 2.0.0)
// =============================================================================

bool UltraCanvasNodeDiagram::HandleMouseWheel(const UCEvent& event) {
    if (!Contains(Point2Di(event.pointer.x, event.pointer.y))) return false;
    if (!isInteractive || !zoomOnScroll) return false;
    
    double zoomFactor = (event.wheelDelta > 0) ? 1.1 : (1.0 / 1.1);
    SyncViewportSize();
    if (!viewport.ZoomAtPoint(Point2Di(event.pointer.x, event.pointer.y), zoomFactor)) {
        return true;  // Already at the zoom limit; event is still consumed
    }
    
    NotifyViewportChange();
    RequestRedraw();
    return true;
}

// =============================================================================
// KEYBOARD - Delete, Ctrl+A, Escape
// =============================================================================

bool UltraCanvasNodeDiagram::HandleKeyDown(const UCEvent& event) {
    if (!isInteractive) return false;
    
    if (event.virtualKey == UCKeys::Delete || event.virtualKey == UCKeys::Backspace) {
        if (!selectedNodes.empty() || !selectedLinks.empty()) {
            DeleteSelected();
            return true;
        }
    }
    
    // Ctrl+A: select all
    if ((event.ctrl || event.meta) && event.virtualKey == UCKeys::A) {
        SelectAll();
        return true;
    }
    
    if (event.virtualKey == UCKeys::Escape) {
        if (isConnecting) {
            isConnecting = false;
            connectionSourceNode.clear();
            connectionSourceHandle.clear();
            RequestRedraw();
            return true;
        }
        if (isSelectingBox) {
            isSelectingBox = false;
            RequestRedraw();
            return true;
        }
        if (!selectedNodes.empty() || !selectedLinks.empty()) {
            DeselectAll();
            return true;
        }
    }
    
    return false;
}

// =============================================================================
// JSON SERIALIZATION
// =============================================================================
//
// Compact, hand-rolled JSON. Self-contained, no external dependencies.
// Format mirrors a simplified ReactFlow document: nodes, links, viewport.
// =============================================================================

namespace {

std::string EscapeJsonString(const std::string& s) {
    std::ostringstream out;
    out << '"';
    for (char c : s) {
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b";  break;
            case '\f': out << "\\f";  break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c) << std::dec;
                } else {
                    out << c;
                }
        }
    }
    out << '"';
    return out.str();
}

std::string ColorToJsonString(const Color& c) {
    std::ostringstream s;
    s << "[" << static_cast<int>(c.r) << "," << static_cast<int>(c.g)
      << "," << static_cast<int>(c.b) << "," << static_cast<int>(c.a) << "]";
    return s.str();
}

const char* ShapeToString(NodeShape s) {
    switch (s) {
        case NodeShape::Circle: return "circle";
        case NodeShape::Square: return "square";
        case NodeShape::Rectangle: return "rectangle";
        case NodeShape::RoundedSquare: return "roundedSquare";
        case NodeShape::Diamond: return "diamond";
        case NodeShape::Hexagon: return "hexagon";
        case NodeShape::Triangle: return "triangle";
        case NodeShape::Star: return "star";
        case NodeShape::Cloud: return "cloud";
    }
    return "circle";
}

NodeShape ShapeFromString(const std::string& s) {
    if (s == "square") return NodeShape::Square;
    if (s == "rectangle") return NodeShape::Rectangle;
    if (s == "roundedSquare") return NodeShape::RoundedSquare;
    if (s == "diamond") return NodeShape::Diamond;
    if (s == "hexagon") return NodeShape::Hexagon;
    if (s == "triangle") return NodeShape::Triangle;
    if (s == "star") return NodeShape::Star;
    if (s == "cloud") return NodeShape::Cloud;
    return NodeShape::Circle;
}

const char* LinkStyleToString(LinkStyle s) {
    switch (s) {
        case LinkStyle::Bezier:     return "bezier";
        case LinkStyle::SmoothStep: return "smoothStep";
        case LinkStyle::Step:       return "step";
        case LinkStyle::Straight:   return "straight";
    }
    return "straight";
}

LinkStyle LinkStyleFromString(const std::string& s) {
    if (s == "bezier") return LinkStyle::Bezier;
    if (s == "smoothStep") return LinkStyle::SmoothStep;
    if (s == "step") return LinkStyle::Step;
    return LinkStyle::Straight;
}

// ---- 2.2.0 enums ----

const char* NodeSizeModeToString(NodeSizeMode m) {
    switch (m) {
        case NodeSizeMode::ByDegree: return "byDegree";
        case NodeSizeMode::ByValue:  return "byValue";
        case NodeSizeMode::Fixed:    return "fixed";
    }
    return "fixed";
}

NodeSizeMode NodeSizeModeFromString(const std::string& s) {
    if (s == "byDegree") return NodeSizeMode::ByDegree;
    if (s == "byValue")  return NodeSizeMode::ByValue;
    return NodeSizeMode::Fixed;
}

const char* NodeDegreeModeToString(NodeDegreeMode m) {
    switch (m) {
        case NodeDegreeMode::Incoming: return "incoming";
        case NodeDegreeMode::Outgoing: return "outgoing";
        case NodeDegreeMode::Total:    return "total";
    }
    return "total";
}

NodeDegreeMode NodeDegreeModeFromString(const std::string& s) {
    if (s == "incoming") return NodeDegreeMode::Incoming;
    if (s == "outgoing") return NodeDegreeMode::Outgoing;
    return NodeDegreeMode::Total;
}

const char* GroupLabelPositionToString(GroupLabelPosition p) {
    switch (p) {
        case GroupLabelPosition::TopCenter:    return "topCenter";
        case GroupLabelPosition::TopRight:     return "topRight";
        case GroupLabelPosition::BottomLeft:   return "bottomLeft";
        case GroupLabelPosition::BottomCenter: return "bottomCenter";
        case GroupLabelPosition::NoLabel:      return "none";
        case GroupLabelPosition::TopLeft:      return "topLeft";
    }
    return "topLeft";
}

GroupLabelPosition GroupLabelPositionFromString(const std::string& s) {
    if (s == "topCenter")    return GroupLabelPosition::TopCenter;
    if (s == "topRight")     return GroupLabelPosition::TopRight;
    if (s == "bottomLeft")   return GroupLabelPosition::BottomLeft;
    if (s == "bottomCenter") return GroupLabelPosition::BottomCenter;
    if (s == "none")         return GroupLabelPosition::NoLabel;
    return GroupLabelPosition::TopLeft;
}

// Tiny JSON helpers operating on std::string. Robust enough for the
// well-formed JSON produced by ToJson(); not a general-purpose parser.

std::string ExtractStringValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t kp = json.find(searchKey);
    if (kp == std::string::npos) return "";
    size_t cp = json.find(':', kp);
    if (cp == std::string::npos) return "";
    size_t qs = json.find('"', cp + 1);
    if (qs == std::string::npos) return "";
    // Find the closing quote, honoring backslash escapes
    size_t qe = qs + 1;
    while (qe < json.size()) {
        if (json[qe] == '"' && json[qe - 1] != '\\') break;
        qe++;
    }
    if (qe >= json.size()) return "";
    return json.substr(qs + 1, qe - qs - 1);
}

double ExtractNumberValue(const std::string& json, const std::string& key, double defaultVal = 0.0) {
    std::string searchKey = "\"" + key + "\"";
    size_t kp = json.find(searchKey);
    if (kp == std::string::npos) return defaultVal;
    size_t cp = json.find(':', kp);
    if (cp == std::string::npos) return defaultVal;
    size_t vs = cp + 1;
    while (vs < json.size() && (json[vs] == ' ' || json[vs] == '\t')) vs++;
    size_t ve = vs;
    while (ve < json.size() &&
           (std::isdigit(static_cast<unsigned char>(json[ve])) ||
            json[ve] == '.' || json[ve] == '-' || json[ve] == '+' ||
            json[ve] == 'e' || json[ve] == 'E')) {
        ve++;
    }
    if (ve == vs) return defaultVal;
    try { return std::stof(json.substr(vs, ve - vs)); }
    catch (...) { return defaultVal; }
}

bool ExtractBoolValue(const std::string& json, const std::string& key, bool defaultVal = false) {
    std::string searchKey = "\"" + key + "\"";
    size_t kp = json.find(searchKey);
    if (kp == std::string::npos) return defaultVal;
    size_t cp = json.find(':', kp);
    if (cp == std::string::npos) return defaultVal;
    size_t vs = cp + 1;
    while (vs < json.size() && (json[vs] == ' ' || json[vs] == '\t')) vs++;
    if (vs + 4 <= json.size() && json.substr(vs, 4) == "true") return true;
    if (vs + 5 <= json.size() && json.substr(vs, 5) == "false") return false;
    return defaultVal;
}

Color ExtractColorValue(const std::string& json, const std::string& key, const Color& fallback) {
    std::string searchKey = "\"" + key + "\"";
    size_t kp = json.find(searchKey);
    if (kp == std::string::npos) return fallback;
    size_t cp = json.find(':', kp);
    if (cp == std::string::npos) return fallback;
    size_t bs = json.find('[', cp);
    if (bs == std::string::npos) return fallback;
    size_t be = json.find(']', bs);
    if (be == std::string::npos) return fallback;
    
    std::string contents = json.substr(bs + 1, be - bs - 1);
    std::vector<int> nums;
    std::stringstream ss(contents);
    std::string token;
    while (std::getline(ss, token, ',')) {
        try { nums.push_back(std::stoi(token)); }
        catch (...) {}
    }
    if (nums.size() >= 4) return Color(nums[0], nums[1], nums[2], nums[3]);
    if (nums.size() == 3) return Color(nums[0], nums[1], nums[2], 255);
    return fallback;
}

// Extract array of objects (each being a balanced { ... })
std::vector<std::string> ExtractObjectArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string searchKey = "\"" + key + "\"";
    size_t kp = json.find(searchKey);
    if (kp == std::string::npos) return result;
    size_t cp = json.find(':', kp);
    if (cp == std::string::npos) return result;
    size_t bs = json.find('[', cp);
    if (bs == std::string::npos) return result;
    
    int bracketDepth = 1;
    size_t pos = bs + 1;
    int braceDepth = 0;
    size_t objStart = std::string::npos;
    
    while (pos < json.size() && bracketDepth > 0) {
        char c = json[pos];
        if (c == '{') {
            if (braceDepth == 0) objStart = pos;
            braceDepth++;
        } else if (c == '}') {
            braceDepth--;
            if (braceDepth == 0 && objStart != std::string::npos) {
                result.push_back(json.substr(objStart, pos - objStart + 1));
                objStart = std::string::npos;
            }
        } else if (c == '[' && braceDepth == 0) {
            bracketDepth++;
        } else if (c == ']' && braceDepth == 0) {
            bracketDepth--;
        }
        pos++;
    }
    return result;
}

// 2.2.0: pull a flat array of strings, e.g. "nodeIds": ["a", "b", "c"].
std::vector<std::string> ExtractStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string searchKey = "\"" + key + "\"";
    size_t kp = json.find(searchKey);
    if (kp == std::string::npos) return result;
    size_t cp = json.find(':', kp);
    if (cp == std::string::npos) return result;
    size_t bs = json.find('[', cp);
    if (bs == std::string::npos) return result;
    size_t be = json.find(']', bs);
    if (be == std::string::npos) return result;

    size_t pos = bs + 1;
    while (pos < be) {
        size_t qs = json.find('"', pos);
        if (qs == std::string::npos || qs > be) break;
        size_t qe = qs + 1;
        while (qe < be) {
            if (json[qe] == '"' && json[qe - 1] != '\\') break;
            qe++;
        }
        if (qe >= be) break;
        result.push_back(json.substr(qs + 1, qe - qs - 1));
        pos = qe + 1;
    }
    return result;
}

} // anonymous namespace

std::string UltraCanvasNodeDiagram::ToJson() const {
    std::ostringstream out;
    out << "{\n";
    
    // Viewport
    out << "  \"viewport\": {\n";
    out << "    \"zoom\": " << viewport.GetZoomLevel() << ",\n";
    out << "    \"panX\": " << viewport.GetPanOffset().x << ",\n";
    out << "    \"panY\": " << viewport.GetPanOffset().y << "\n";
    out << "  },\n";
    
    // Nodes
    out << "  \"nodes\": [\n";
    bool first = true;
    for (const auto& nid : nodeOrder) {
        auto it = nodes.find(nid);
        if (it == nodes.end()) continue;
        const auto& n = it->second;
        if (!first) out << ",\n";
        first = false;
        out << "    {\n";
        out << "      \"id\": " << EscapeJsonString(n.id) << ",\n";
        out << "      \"label\": " << EscapeJsonString(n.label) << ",\n";
        out << "      \"shape\": \"" << ShapeToString(n.shape) << "\",\n";
        out << "      \"x\": " << n.x << ",\n";
        out << "      \"y\": " << n.y << ",\n";
        out << "      \"width\": " << n.width << ",\n";
        out << "      \"height\": " << n.height << ",\n";
        out << "      \"fillColor\": " << ColorToJsonString(n.fillColor) << ",\n";
        out << "      \"borderColor\": " << ColorToJsonString(n.borderColor) << ",\n";
        out << "      \"textColor\": " << ColorToJsonString(n.textColor) << ",\n";
        out << "      \"borderWidth\": " << n.borderWidth << ",\n";
        out << "      \"fontSize\": " << n.fontSize << ",\n";
        out << "      \"value\": " << n.value << ",\n";
        out << "      \"isPinned\": " << (n.isPinned ? "true" : "false") << ",\n";
        out << "      \"draggable\": " << (n.draggable ? "true" : "false") << ",\n";
        out << "      \"selectable\": " << (n.selectable ? "true" : "false") << ",\n";
        out << "      \"deletable\": " << (n.deletable ? "true" : "false");
        if (!n.handles.empty()) {
            out << ",\n      \"handles\": [\n";
            for (size_t i = 0; i < n.handles.size(); ++i) {
                const auto& h = n.handles[i];
                if (i > 0) out << ",\n";
                out << "        { \"id\": " << EscapeJsonString(h.id)
                    << ", \"type\": \"" << (h.type == HandleType::Source ? "source" : "target") << "\""
                    << ", \"position\": \"";
                switch (h.position) {
                    case HandlePosition::Top:    out << "top"; break;
                    case HandlePosition::Right:  out << "right"; break;
                    case HandlePosition::Bottom: out << "bottom"; break;
                    case HandlePosition::Left:   out << "left"; break;
                }
                out << "\" }";
            }
            out << "\n      ]";
        }
        out << "\n    }";
    }
    out << "\n  ],\n";
    
    // Links
    out << "  \"links\": [\n";
    first = true;
    for (const auto& l : links) {
        if (!first) out << ",\n";
        first = false;
        out << "    {\n";
        out << "      \"id\": " << EscapeJsonString(l.id) << ",\n";
        out << "      \"source\": " << EscapeJsonString(l.sourceNodeId) << ",\n";
        out << "      \"target\": " << EscapeJsonString(l.targetNodeId) << ",\n";
        if (!l.sourceHandleId.empty()) {
            out << "      \"sourceHandle\": " << EscapeJsonString(l.sourceHandleId) << ",\n";
        }
        if (!l.targetHandleId.empty()) {
            out << "      \"targetHandle\": " << EscapeJsonString(l.targetHandleId) << ",\n";
        }
        out << "      \"style\": \"" << LinkStyleToString(l.style) << "\",\n";
        out << "      \"lineColor\": " << ColorToJsonString(l.lineColor) << ",\n";
        out << "      \"lineWidth\": " << l.lineWidth << ",\n";
        out << "      \"directed\": " << (l.directed ? "true" : "false");
        if (!l.label.empty()) {
            out << ",\n      \"label\": " << EscapeJsonString(l.label);
        }
        out << "\n    }";
    }
    out << "\n  ],\n";

    // Sizing (2.2.0)
    out << "  \"sizing\": {\n";
    out << "    \"mode\": \"" << NodeSizeModeToString(sizing.mode) << "\",\n";
    out << "    \"degreeMode\": \"" << NodeDegreeModeToString(sizing.degreeMode) << "\",\n";
    out << "    \"baseSize\": " << sizing.baseSize << ",\n";
    out << "    \"minSize\": " << sizing.minSize << ",\n";
    out << "    \"maxSize\": " << sizing.maxSize << ",\n";
    out << "    \"keepAspect\": " << (sizing.keepAspect ? "true" : "false") << "\n";
    out << "  },\n";

    // Groups (2.2.0)
    out << "  \"groups\": [\n";
    first = true;
    for (const auto& g : groups) {
        if (!first) out << ",\n";
        first = false;
        out << "    {\n";
        out << "      \"id\": " << EscapeJsonString(g.id) << ",\n";
        out << "      \"label\": " << EscapeJsonString(g.label) << ",\n";
        out << "      \"fillColor\": " << ColorToJsonString(g.fillColor) << ",\n";
        out << "      \"borderColor\": " << ColorToJsonString(g.borderColor) << ",\n";
        out << "      \"labelColor\": " << ColorToJsonString(g.labelColor) << ",\n";
        out << "      \"borderWidth\": " << g.borderWidth << ",\n";
        out << "      \"dashed\": " << (g.dashed ? "true" : "false") << ",\n";
        out << "      \"padding\": " << g.padding << ",\n";
        out << "      \"cornerRadius\": " << g.cornerRadius << ",\n";
        out << "      \"labelFontSize\": " << g.labelFontSize << ",\n";
        out << "      \"labelPosition\": \"" << GroupLabelPositionToString(g.labelPosition) << "\",\n";
        out << "      \"visible\": " << (g.visible ? "true" : "false") << ",\n";
        out << "      \"nodeIds\": [";
        for (size_t i = 0; i < g.nodeIds.size(); ++i) {
            if (i > 0) out << ", ";
            out << EscapeJsonString(g.nodeIds[i]);
        }
        out << "]\n";
        out << "    }";
    }
    out << "\n  ]\n";
    out << "}\n";
    return out.str();
}

bool UltraCanvasNodeDiagram::FromJson(const std::string& json) {
    if (json.empty()) return false;
    
    Clear();
    
    // Viewport
    {
        size_t vpStart = json.find("\"viewport\"");
        if (vpStart != std::string::npos) {
            size_t braceStart = json.find('{', vpStart);
            size_t braceEnd = json.find('}', braceStart);
            if (braceStart != std::string::npos && braceEnd != std::string::npos) {
                std::string vpJson = json.substr(braceStart, braceEnd - braceStart + 1);
                viewport.SetZoomLevel(ExtractNumberValue(vpJson, "zoom", 1.0));
                viewport.SetPanOffset(ExtractNumberValue(vpJson, "panX", 0.0),
                                      ExtractNumberValue(vpJson, "panY", 0.0));
                ClampZoom();
            }
        }
    }
    
    // Nodes
    auto nodeObjects = ExtractObjectArray(json, "nodes");
    for (const auto& nodeJson : nodeObjects) {
        NodeDiagramNode n;
        n.id = ExtractStringValue(nodeJson, "id");
        if (n.id.empty()) continue;
        n.label = ExtractStringValue(nodeJson, "label");
        n.shape = ShapeFromString(ExtractStringValue(nodeJson, "shape"));
        n.x = ExtractNumberValue(nodeJson, "x");
        n.y = ExtractNumberValue(nodeJson, "y");
        n.width  = ExtractNumberValue(nodeJson, "width", 40.0);
        n.height = ExtractNumberValue(nodeJson, "height", 40.0);
        n.fillColor   = ExtractColorValue(nodeJson, "fillColor",   n.fillColor);
        n.borderColor = ExtractColorValue(nodeJson, "borderColor", n.borderColor);
        n.textColor   = ExtractColorValue(nodeJson, "textColor",   n.textColor);
        n.borderWidth = ExtractNumberValue(nodeJson, "borderWidth", 2.0);
        n.fontSize    = ExtractNumberValue(nodeJson, "fontSize", 10.0);
        n.value       = ExtractNumberValue(nodeJson, "value", 1.0);   // 2.2.0
        n.isPinned    = ExtractBoolValue(nodeJson, "isPinned", false);
        n.draggable   = ExtractBoolValue(nodeJson, "draggable", true);
        n.selectable  = ExtractBoolValue(nodeJson, "selectable", true);
        n.deletable   = ExtractBoolValue(nodeJson, "deletable", true);
        
        // Handles
        auto handleObjects = ExtractObjectArray(nodeJson, "handles");
        for (const auto& hJson : handleObjects) {
            NodeHandle h;
            h.id = ExtractStringValue(hJson, "id");
            if (h.id.empty()) continue;
            std::string typeStr = ExtractStringValue(hJson, "type");
            h.type = (typeStr == "target") ? HandleType::Target : HandleType::Source;
            std::string posStr = ExtractStringValue(hJson, "position");
            if (posStr == "top")    h.position = HandlePosition::Top;
            else if (posStr == "bottom") h.position = HandlePosition::Bottom;
            else if (posStr == "left")   h.position = HandlePosition::Left;
            else                          h.position = HandlePosition::Right;
            n.handles.push_back(h);
        }
        AddNode(n);
    }
    
    // Links
    auto linkObjects = ExtractObjectArray(json, "links");
    for (const auto& linkJson : linkObjects) {
        NodeDiagramLink l;
        l.id = ExtractStringValue(linkJson, "id");
        if (l.id.empty()) continue;
        l.sourceNodeId = ExtractStringValue(linkJson, "source");
        l.targetNodeId = ExtractStringValue(linkJson, "target");
        l.sourceHandleId = ExtractStringValue(linkJson, "sourceHandle");
        l.targetHandleId = ExtractStringValue(linkJson, "targetHandle");
        l.style = LinkStyleFromString(ExtractStringValue(linkJson, "style"));
        l.lineColor = ExtractColorValue(linkJson, "lineColor", l.lineColor);
        l.lineWidth = ExtractNumberValue(linkJson, "lineWidth", 2.0);
        l.directed  = ExtractBoolValue(linkJson, "directed", true);
        l.label     = ExtractStringValue(linkJson, "label");
        AddLink(l);
    }

    // Groups (2.2.0) - loaded before sizing so a degree-driven sizing pass sees
    // the finished graph exactly once.
    {
        size_t groupsKey = json.find("\"groups\"");
        if (groupsKey != std::string::npos) {
            auto groupObjects = ExtractObjectArray(json, "groups");
            for (const auto& groupJson : groupObjects) {
                NodeDiagramGroup g;
                g.id = ExtractStringValue(groupJson, "id");
                if (g.id.empty()) continue;
                g.label         = ExtractStringValue(groupJson, "label");
                g.fillColor     = ExtractColorValue(groupJson, "fillColor",   g.fillColor);
                g.borderColor   = ExtractColorValue(groupJson, "borderColor", g.borderColor);
                g.labelColor    = ExtractColorValue(groupJson, "labelColor",  g.labelColor);
                g.borderWidth   = ExtractNumberValue(groupJson, "borderWidth", 1.0);
                g.dashed        = ExtractBoolValue(groupJson, "dashed", false);
                g.padding       = ExtractNumberValue(groupJson, "padding", 24.0);
                g.cornerRadius  = ExtractNumberValue(groupJson, "cornerRadius", 0.0);
                g.labelFontSize = ExtractNumberValue(groupJson, "labelFontSize", 12.0);
                g.labelPosition = GroupLabelPositionFromString(
                                      ExtractStringValue(groupJson, "labelPosition"));
                g.visible       = ExtractBoolValue(groupJson, "visible", true);
                g.nodeIds       = ExtractStringArray(groupJson, "nodeIds");
                AddGroup(g);
            }
        }
    }

    // Sizing (2.2.0)
    {
        size_t sizingStart = json.find("\"sizing\"");
        if (sizingStart != std::string::npos) {
            size_t braceStart = json.find('{', sizingStart);
            size_t braceEnd = json.find('}', braceStart);
            if (braceStart != std::string::npos && braceEnd != std::string::npos) {
                std::string sizingJson = json.substr(braceStart, braceEnd - braceStart + 1);
                NodeDiagramSizing s;
                s.mode       = NodeSizeModeFromString(ExtractStringValue(sizingJson, "mode"));
                s.degreeMode = NodeDegreeModeFromString(ExtractStringValue(sizingJson, "degreeMode"));
                s.baseSize   = ExtractNumberValue(sizingJson, "baseSize", s.baseSize);
                s.minSize    = ExtractNumberValue(sizingJson, "minSize", s.minSize);
                s.maxSize    = ExtractNumberValue(sizingJson, "maxSize", s.maxSize);
                s.keepAspect = ExtractBoolValue(sizingJson, "keepAspect", true);
                SetNodeSizing(s);
            }
        }
    }

    NotifyViewportChange();
    RequestRedraw();
    return true;
}


} // namespace UltraCanvas