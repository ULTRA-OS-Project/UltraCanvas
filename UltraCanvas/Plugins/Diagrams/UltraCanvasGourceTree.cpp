// Plugins/Diagrams/UltraCanvasGourceTree.cpp
// Gource-style radial tree diagram for storage/filesystem visualization
// Version: 1.0.1
// Last Modified: 2026-05-09
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasGourceTree.h"
#include "UltraCanvasTooltipManager.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <queue>
#include <limits>

namespace UltraCanvas {

// ===== CONSTANTS =====
constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;

// ===== CONSTRUCTOR =====

UltraCanvasGourceTree::UltraCanvasGourceTree(const std::string& id, 
                                               float x, float y, float w, float h)
    : UltraCanvasUIElement(id, x, y, w, h) {
    
    // Initialize default style
    ApplyThemeColors(GourceTheme::Default);
    
    // Set initial center point
    style.centerX = w / 2.0f;
    style.centerY = h / 2.0f;
}

// ===== NODE MANAGEMENT =====

void UltraCanvasGourceTree::SetRootNode(const std::string& id, const std::string& name,
                                         const std::string& fullPath) {
    // Clear existing nodes if any
    nodes.clear();
    links.clear();
    rootNodeId = id;
    
    GourceNode rootNode;
    rootNode.id = id;
    rootNode.name = name;
    rootNode.fullPath = fullPath.empty() ? name : fullPath;
    rootNode.type = GourceNodeType::Directory;
    rootNode.depth = 0;
    rootNode.isExpanded = true;
    rootNode.nodeColor = style.rootNodeColor;
    rootNode.nodeRadius = style.rootNodeRadius;
    
    nodes[id] = rootNode;
    needsLayout = true;
}

void UltraCanvasGourceTree::AddNode(const std::string& parentId, const std::string& nodeId,
                                     const std::string& name, GourceNodeType type,
                                     const GourceFileInfo& fileInfo) {
    // Verify parent exists
    auto parentIt = nodes.find(parentId);
    if (parentIt == nodes.end()) {
        return; // Parent not found
    }
    
    // Check if node already exists
    if (nodes.find(nodeId) != nodes.end()) {
        return; // Node already exists
    }
    
    GourceNode node;
    node.id = nodeId;
    node.name = name;
    node.parentId = parentId;
    node.type = type;
    node.fileInfo = fileInfo;
    node.depth = parentIt->second.depth + 1;
    node.isExpanded = (type == GourceNodeType::Directory);
    
    // Build full path
    if (!parentIt->second.fullPath.empty()) {
        node.fullPath = parentIt->second.fullPath + "/" + name;
    } else {
        node.fullPath = name;
    }
    
    // Set color based on type/extension
    if (type == GourceNodeType::File) {
        std::string ext = GetExtension(name);
        node.nodeColor = extensionColors.GetColor(ext);
        node.nodeRadius = style.fileNodeRadius;
        node.fileInfo.extension = ext;
    } else {
        node.nodeColor = style.dirNodeColor;
        node.nodeRadius = style.dirNodeRadius;
    }
    
    // Check depth limit
    if (maxDepth >= 0 && node.depth > maxDepth && type == GourceNodeType::Directory) {
        node.type = GourceNodeType::CollapsedDirectory;
        node.isExpanded = false;
    }
    
    // Update date range for highlighting
    if (autoDateRange) {
        if (fileInfo.lastAccessTime > 0) {
            if (oldestDate == 0 || fileInfo.lastAccessTime < oldestDate) {
                oldestDate = fileInfo.lastAccessTime;
            }
            if (fileInfo.lastAccessTime > newestDate) {
                newestDate = fileInfo.lastAccessTime;
            }
        }
        if (fileInfo.creationTime > 0) {
            if (oldestDate == 0 || fileInfo.creationTime < oldestDate) {
                oldestDate = fileInfo.creationTime;
            }
            if (fileInfo.creationTime > newestDate) {
                newestDate = fileInfo.creationTime;
            }
        }
    }
    
    // Add to parent's children
    parentIt->second.childIds.push_back(nodeId);
    parentIt->second.childCount++;
    
    // Update ancestor descendant counts
    std::string ancestorId = parentId;
    while (!ancestorId.empty()) {
        auto ancestorIt = nodes.find(ancestorId);
        if (ancestorIt != nodes.end()) {
            ancestorIt->second.descendantCount++;
            ancestorId = ancestorIt->second.parentId;
        } else {
            break;
        }
    }
    
    nodes[nodeId] = node;
    needsLayout = true;
    
    // Trigger animation in animated mode
    if (layoutMode == GourceLayoutMode::Animated) {
        isAnimating = true;
        physicsIterations = 0;
    }
}

void UltraCanvasGourceTree::AddFile(const std::string& parentId, const std::string& filePath,
                                     const std::string& fileName, const GourceFileInfo& fileInfo) {
    AddNode(parentId, filePath, fileName, GourceNodeType::File, fileInfo);
}

void UltraCanvasGourceTree::AddDirectory(const std::string& parentId, const std::string& dirPath,
                                          const std::string& dirName) {
    GourceFileInfo info;
    AddNode(parentId, dirPath, dirName, GourceNodeType::Directory, info);
}

void UltraCanvasGourceTree::RemoveNode(const std::string& nodeId) {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return;
    
    // Collect all descendant IDs
    std::vector<std::string> toRemove;
    CollectDescendantIds(nodeId, toRemove);
    toRemove.push_back(nodeId);
    
    // Remove from parent's children
    if (!nodeIt->second.parentId.empty()) {
        auto parentIt = nodes.find(nodeIt->second.parentId);
        if (parentIt != nodes.end()) {
            auto& children = parentIt->second.childIds;
            children.erase(std::remove(children.begin(), children.end(), nodeId), children.end());
            parentIt->second.childCount--;
        }
    }
    
    // Update ancestor descendant counts
    std::string ancestorId = nodeIt->second.parentId;
    int removedCount = static_cast<int>(toRemove.size());
    while (!ancestorId.empty()) {
        auto ancestorIt = nodes.find(ancestorId);
        if (ancestorIt != nodes.end()) {
            ancestorIt->second.descendantCount -= removedCount;
            ancestorId = ancestorIt->second.parentId;
        } else {
            break;
        }
    }
    
    // Remove all nodes
    for (const auto& id : toRemove) {
        nodes.erase(id);
        
        // Remove from selection
        selectedNodeIds.erase(
            std::remove(selectedNodeIds.begin(), selectedNodeIds.end(), id),
            selectedNodeIds.end()
        );
    }
    
    needsLayout = true;
}

void UltraCanvasGourceTree::ClearAll() {
    nodes.clear();
    links.clear();
    rootNodeId.clear();
    selectedNodeIds.clear();
    hoveredNodeId.clear();
    oldestDate = 0;
    newestDate = 0;
    needsLayout = true;
}

GourceNode* UltraCanvasGourceTree::GetNode(const std::string& nodeId) {
    auto it = nodes.find(nodeId);
    return (it != nodes.end()) ? &it->second : nullptr;
}

const GourceNode* UltraCanvasGourceTree::GetNode(const std::string& nodeId) const {
    auto it = nodes.find(nodeId);
    return (it != nodes.end()) ? &it->second : nullptr;
}

GourceNode* UltraCanvasGourceTree::GetRootNode() {
    return GetNode(rootNodeId);
}

// ===== NODE STATE =====

void UltraCanvasGourceTree::ExpandNode(const std::string& nodeId) {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return;
    
    if (nodeIt->second.type == GourceNodeType::Directory ||
        nodeIt->second.type == GourceNodeType::CollapsedDirectory) {
        
        nodeIt->second.isExpanded = true;
        if (nodeIt->second.type == GourceNodeType::CollapsedDirectory) {
            nodeIt->second.type = GourceNodeType::Directory;
        }
        
        needsLayout = true;
        
        if (onNodeExpand) {
            onNodeExpand(nodeId);
        }
        
        RequestRedraw();
    }
}

void UltraCanvasGourceTree::CollapseNode(const std::string& nodeId) {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return;
    
    if (nodeIt->second.type == GourceNodeType::Directory) {
        nodeIt->second.isExpanded = false;
        needsLayout = true;
        
        if (onNodeCollapse) {
            onNodeCollapse(nodeId);
        }
        
        RequestRedraw();
    }
}

void UltraCanvasGourceTree::ToggleNode(const std::string& nodeId) {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return;
    
    if (nodeIt->second.isExpanded) {
        CollapseNode(nodeId);
    } else {
        ExpandNode(nodeId);
    }
}

void UltraCanvasGourceTree::ExpandAll() {
    for (auto& [id, node] : nodes) {
        if (node.type == GourceNodeType::Directory ||
            node.type == GourceNodeType::CollapsedDirectory) {
            node.isExpanded = true;
            if (node.type == GourceNodeType::CollapsedDirectory) {
                node.type = GourceNodeType::Directory;
            }
        }
    }
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGourceTree::CollapseAll() {
    for (auto& [id, node] : nodes) {
        if (node.type == GourceNodeType::Directory && id != rootNodeId) {
            node.isExpanded = false;
        }
    }
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGourceTree::ExpandToDepth(int depth) {
    for (auto& [id, node] : nodes) {
        if (node.type == GourceNodeType::Directory ||
            node.type == GourceNodeType::CollapsedDirectory) {
            node.isExpanded = (node.depth < depth);
        }
    }
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGourceTree::SelectNode(const std::string& nodeId) {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return;
    
    // Deselect others
    for (auto& [id, node] : nodes) {
        node.isSelected = false;
    }
    selectedNodeIds.clear();
    
    nodeIt->second.isSelected = true;
    selectedNodeIds.push_back(nodeId);
    
    if (onNodeSelect) {
        onNodeSelect(nodeId);
    }
    
    RequestRedraw();
}

void UltraCanvasGourceTree::DeselectNode(const std::string& nodeId) {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt != nodes.end()) {
        nodeIt->second.isSelected = false;
    }
    selectedNodeIds.erase(
        std::remove(selectedNodeIds.begin(), selectedNodeIds.end(), nodeId),
        selectedNodeIds.end()
    );
    RequestRedraw();
}

void UltraCanvasGourceTree::ClearSelection() {
    for (auto& [id, node] : nodes) {
        node.isSelected = false;
    }
    selectedNodeIds.clear();
    RequestRedraw();
}

std::vector<std::string> UltraCanvasGourceTree::GetSelectedNodes() const {
    return selectedNodeIds;
}

// ===== LAYOUT & DISPLAY CONFIGURATION =====

void UltraCanvasGourceTree::SetLayoutMode(GourceLayoutMode mode) {
    if (layoutMode != mode) {
        layoutMode = mode;
        needsLayout = true;
        if (mode == GourceLayoutMode::Animated) {
            isAnimating = true;
            physicsIterations = 0;
        }
    }
}

void UltraCanvasGourceTree::SetMaxDepth(int depth) {
    if (maxDepth != depth) {
        maxDepth = depth;
        
        // Update collapsed state for nodes beyond depth
        for (auto& [id, node] : nodes) {
            if (node.type == GourceNodeType::Directory ||
                node.type == GourceNodeType::CollapsedDirectory) {
                if (maxDepth >= 0 && node.depth >= maxDepth) {
                    node.type = GourceNodeType::CollapsedDirectory;
                    node.isExpanded = false;
                } else {
                    if (node.type == GourceNodeType::CollapsedDirectory) {
                        node.type = GourceNodeType::Directory;
                    }
                }
            }
        }
        
        needsLayout = true;
        RequestRedraw();
    }
}

void UltraCanvasGourceTree::SetHighlightMode(GourceHighlightMode mode) {
    if (highlightMode != mode) {
        highlightMode = mode;
        UpdateNodeOpacity();
        RequestRedraw();
    }
}

void UltraCanvasGourceTree::SetHighlightDateRange(time_t oldest, time_t newest) {
    oldestDate = oldest;
    newestDate = newest;
    autoDateRange = false;
    UpdateNodeOpacity();
    RequestRedraw();
}

void UltraCanvasGourceTree::AutoCalculateDateRange() {
    autoDateRange = true;
    oldestDate = 0;
    newestDate = 0;
    
    for (const auto& [id, node] : nodes) {
        if (node.fileInfo.lastAccessTime > 0) {
            if (oldestDate == 0 || node.fileInfo.lastAccessTime < oldestDate) {
                oldestDate = node.fileInfo.lastAccessTime;
            }
            if (node.fileInfo.lastAccessTime > newestDate) {
                newestDate = node.fileInfo.lastAccessTime;
            }
        }
        if (node.fileInfo.creationTime > 0) {
            if (oldestDate == 0 || node.fileInfo.creationTime < oldestDate) {
                oldestDate = node.fileInfo.creationTime;
            }
            if (node.fileInfo.creationTime > newestDate) {
                newestDate = node.fileInfo.creationTime;
            }
        }
    }
    
    UpdateNodeOpacity();
    RequestRedraw();
}

void UltraCanvasGourceTree::SetShowFileSizeArea(bool show) {
    style.showFileSizeArea = show;
    RequestRedraw();
}

void UltraCanvasGourceTree::SetFileSizeScale(float scale) {
    style.fileSizeScaleFactor = scale;
    ComputeFileSizeRadii();
    RequestRedraw();
}

void UltraCanvasGourceTree::SetTheme(GourceTheme theme) {
    if (currentTheme != theme) {
        currentTheme = theme;
        ApplyThemeColors(theme);
        RequestRedraw();
    }
}

void UltraCanvasGourceTree::SetStyle(const GourceStyle& newStyle) {
    style = newStyle;
    needsLayout = true;
    RequestRedraw();
}

void UltraCanvasGourceTree::SetExtensionColor(const std::string& ext, const Color& color) {
    extensionColors.SetColor(ext, color);
    
    // Update existing nodes with this extension
    for (auto& [id, node] : nodes) {
        if (node.type == GourceNodeType::File && node.fileInfo.extension == ext) {
            node.nodeColor = color;
        }
    }
    
    RequestRedraw();
}

// ===== VIEW CONTROL =====

void UltraCanvasGourceTree::SetZoom(float zoom) {
    zoomLevel = std::clamp(zoom, minZoom, maxZoom);
    RequestRedraw();
}

void UltraCanvasGourceTree::ZoomIn() {
    SetZoom(zoomLevel * 1.2f);
}

void UltraCanvasGourceTree::ZoomOut() {
    SetZoom(zoomLevel / 1.2f);
}

void UltraCanvasGourceTree::ZoomToFit() {
    if (nodes.empty()) return;
    
    // Find bounds of all visible nodes
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    
    for (const auto& [id, node] : nodes) {
        if (!node.isVisible) continue;
        minX = std::min(minX, node.x - node.nodeRadius);
        maxX = std::max(maxX, node.x + node.nodeRadius);
        minY = std::min(minY, node.y - node.nodeRadius);
        maxY = std::max(maxY, node.y + node.nodeRadius);
    }
    
    if (minX >= maxX || minY >= maxY) return;
    
    Rect2Di bounds = GetContentRect();
    float nodeWidth = maxX - minX;
    float nodeHeight = maxY - minY;
    
    float zoomX = (bounds.width - 80) / nodeWidth;
    float zoomY = (bounds.height - 80) / nodeHeight;
    
    zoomLevel = std::min(zoomX, zoomY);
    zoomLevel = std::clamp(zoomLevel, minZoom, maxZoom);
    
    // Center the view
    float centerNodeX = (minX + maxX) / 2.0f;
    float centerNodeY = (minY + maxY) / 2.0f;
    
    panX = bounds.width / 2.0f - centerNodeX * zoomLevel;
    panY = bounds.height / 2.0f - centerNodeY * zoomLevel;
    
    RequestRedraw();
}

void UltraCanvasGourceTree::ResetView() {
    zoomLevel = 1.0f;
    panX = 0.0f;
    panY = 0.0f;
    RequestRedraw();
}

void UltraCanvasGourceTree::SetPan(float x, float y) {
    panX = x;
    panY = y;
    RequestRedraw();
}

void UltraCanvasGourceTree::CenterOnNode(const std::string& nodeId) {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return;
    
    Rect2Di bounds = GetContentRect();
    panX = bounds.width / 2.0f - nodeIt->second.x * zoomLevel;
    panY = bounds.height / 2.0f - nodeIt->second.y * zoomLevel;
    
    RequestRedraw();
}

// ===== LAYOUT COMPUTATION =====

void UltraCanvasGourceTree::PerformLayout() {
    if (nodes.empty() || rootNodeId.empty()) return;
    
    // Update center point
    Rect2Di bounds = GetContentRect();
    style.centerX = bounds.width / 2.0f;
    style.centerY = bounds.height / 2.0f;
    
    // Update visibility based on expanded state
    UpdateVisibility();
    
    // Compute radial layout (initial positions)
    ComputeRadialLayout();
    
    // Compute file size radii
    ComputeFileSizeRadii();
    
    // Update opacity based on highlight mode
    UpdateNodeOpacity();
    
    // Rebuild link list
    RebuildLinks();
    
    // CRITICAL: Always initialize node positions to target positions first
    // This ensures nodes are visible immediately at correct positions,
    // not stuck at origin (0,0) waiting for animation to move them
    for (auto& [id, node] : nodes) {
        node.x = node.targetX;
        node.y = node.targetY;
    }
    
    // Start animation if in hybrid/animated mode (for force-directed relaxation)
    if (layoutMode == GourceLayoutMode::Hybrid || layoutMode == GourceLayoutMode::Animated) {
        isAnimating = true;
        physicsIterations = 0;
    } else {
        isAnimating = false;
    }
    
    needsLayout = false;
    
    if (layoutMode == GourceLayoutMode::Static && onLayoutComplete) {
        onLayoutComplete();
    }
}

void UltraCanvasGourceTree::ComputeRadialLayout() {
    auto rootIt = nodes.find(rootNodeId);
    if (rootIt == nodes.end()) return;
    
    // Position root at center
    rootIt->second.targetX = style.centerX;
    rootIt->second.targetY = style.centerY;
    rootIt->second.angle = 0;
    rootIt->second.radius = 0;
    
    // Compute angles for children recursively
    ComputeNodeAngles(rootNodeId, 0.0f, TWO_PI);
    
    // Compute radii based on depth
    ComputeNodeRadii();
    
    // Convert polar to Cartesian coordinates
    for (auto& [id, node] : nodes) {
        if (id == rootNodeId) continue;
        
        node.targetX = style.centerX + node.radius * std::cos(node.angle);
        node.targetY = style.centerY + node.radius * std::sin(node.angle);
    }
}

void UltraCanvasGourceTree::ComputeNodeAngles(const std::string& nodeId, 
                                               float startAngle, float endAngle) {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return;
    
    const auto& childIds = nodeIt->second.childIds;
    if (childIds.empty()) return;
    
    // Count visible children
    int visibleChildren = 0;
    for (const auto& childId : childIds) {
        auto childIt = nodes.find(childId);
        if (childIt != nodes.end() && childIt->second.isVisible) {
            visibleChildren++;
        }
    }
    
    if (visibleChildren == 0) return;
    
    float totalAngle = endAngle - startAngle;
    float anglePerChild = totalAngle / visibleChildren;
    float currentAngle = startAngle + anglePerChild / 2.0f;
    
    for (const auto& childId : childIds) {
        auto childIt = nodes.find(childId);
        if (childIt == nodes.end() || !childIt->second.isVisible) continue;
        
        childIt->second.angle = currentAngle;
        
        // Calculate angle range for this child's descendants
        int descendants = CountVisibleDescendants(childId);
        float childAngleRange = (descendants > 0) 
            ? anglePerChild * (1 + descendants) / visibleChildren 
            : anglePerChild;
        
        // Recursively compute angles for children
        if (childIt->second.isExpanded && !childIt->second.childIds.empty()) {
            float childStart = currentAngle - childAngleRange / 2.0f;
            float childEnd = currentAngle + childAngleRange / 2.0f;
            ComputeNodeAngles(childId, childStart, childEnd);
        }
        
        currentAngle += anglePerChild;
    }
}

void UltraCanvasGourceTree::ComputeNodeRadii() {
    for (auto& [id, node] : nodes) {
        node.radius = node.depth * style.ringSpacing;
    }
}

void UltraCanvasGourceTree::ApplyForceDirectedRelaxation(float deltaTime) {
    if (!isAnimating) return;
    
    // Compute forces
    ComputeRepulsionForces();
    ComputeAttractionForces();
    
    // Update positions
    UpdateNodePositions(deltaTime);
    
    physicsIterations++;
    
    // Check for convergence
    bool converged = true;
    for (const auto& [id, node] : nodes) {
        float dx = node.targetX - node.x;
        float dy = node.targetY - node.y;
        if (std::abs(dx) > 0.5f || std::abs(dy) > 0.5f) {
            converged = false;
            break;
        }
    }
    
    if (converged || physicsIterations >= maxPhysicsIterations) {
        isAnimating = false;
        if (onLayoutComplete) {
            onLayoutComplete();
        }
    }
}

void UltraCanvasGourceTree::ComputeRepulsionForces() {
    // Repulsion between nodes at same depth
    std::map<int, std::vector<std::string>> nodesByDepth;
    for (const auto& [id, node] : nodes) {
        if (node.isVisible) {
            nodesByDepth[node.depth].push_back(id);
        }
    }
    
    float repulsionStrength = style.physicsStrength * 1000.0f;
    
    for (auto& [depth, nodeIds] : nodesByDepth) {
        for (size_t i = 0; i < nodeIds.size(); i++) {
            for (size_t j = i + 1; j < nodeIds.size(); j++) {
                auto& nodeA = nodes[nodeIds[i]];
                auto& nodeB = nodes[nodeIds[j]];
                
                float dx = nodeB.x - nodeA.x;
                float dy = nodeB.y - nodeA.y;
                float distSq = dx * dx + dy * dy;
                float minDist = style.minNodeSpacing;
                
                if (distSq < minDist * minDist && distSq > 0.001f) {
                    float dist = std::sqrt(distSq);
                    float force = repulsionStrength / distSq;
                    
                    float fx = (dx / dist) * force;
                    float fy = (dy / dist) * force;
                    
                    nodeA.velocityX -= fx;
                    nodeA.velocityY -= fy;
                    nodeB.velocityX += fx;
                    nodeB.velocityY += fy;
                }
            }
        }
    }
}

void UltraCanvasGourceTree::ComputeAttractionForces() {
    float attractionStrength = style.physicsStrength * 0.1f;
    
    for (const auto& link : links) {
        auto sourceIt = nodes.find(link.sourceId);
        auto targetIt = nodes.find(link.targetId);
        
        if (sourceIt == nodes.end() || targetIt == nodes.end()) continue;
        if (!sourceIt->second.isVisible || !targetIt->second.isVisible) continue;
        
        auto& source = sourceIt->second;
        auto& target = targetIt->second;
        
        float dx = target.x - source.x;
        float dy = target.y - source.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        
        if (dist > 0.001f) {
            // Attract towards target position
            float targetDist = std::abs(target.depth - source.depth) * style.ringSpacing;
            float force = (dist - targetDist) * attractionStrength;
            
            float fx = (dx / dist) * force;
            float fy = (dy / dist) * force;
            
            // Only move child nodes, keep parents stable
            target.velocityX -= fx;
            target.velocityY -= fy;
        }
    }
}

void UltraCanvasGourceTree::UpdateNodePositions(float deltaTime) {
    for (auto& [id, node] : nodes) {
        if (id == rootNodeId) {
            // Keep root at center
            node.x = node.targetX;
            node.y = node.targetY;
            continue;
        }
        
        // Apply velocity
        node.x += node.velocityX * deltaTime;
        node.y += node.velocityY * deltaTime;
        
        // Lerp towards target
        node.x += (node.targetX - node.x) * style.animationSpeed;
        node.y += (node.targetY - node.y) * style.animationSpeed;
        
        // Apply damping
        node.velocityX *= style.dampingFactor;
        node.velocityY *= style.dampingFactor;
    }
}

void UltraCanvasGourceTree::UpdateVisibility() {
    // First, mark all as invisible
    for (auto& [id, node] : nodes) {
        node.isVisible = false;
    }
    
    // BFS to mark visible nodes based on expansion state
    if (rootNodeId.empty()) return;
    
    std::queue<std::string> queue;
    queue.push(rootNodeId);
    
    while (!queue.empty()) {
        std::string currentId = queue.front();
        queue.pop();
        
        auto nodeIt = nodes.find(currentId);
        if (nodeIt == nodes.end()) continue;
        
        nodeIt->second.isVisible = true;
        
        // Only process children if expanded
        if (nodeIt->second.isExpanded) {
            for (const auto& childId : nodeIt->second.childIds) {
                queue.push(childId);
            }
        }
    }
}

void UltraCanvasGourceTree::UpdateNodeOpacity() {
    if (highlightMode == GourceHighlightMode::NoneHighlight) {
        for (auto& [id, node] : nodes) {
            node.opacity = 1.0f;
        }
        return;
    }
    
    time_t dateRange = newestDate - oldestDate;
    if (dateRange <= 0) dateRange = 1;
    
    for (auto& [id, node] : nodes) {
        node.opacity = CalculateOpacity(node);
    }
}

void UltraCanvasGourceTree::ComputeFileSizeRadii() {
    if (!style.showFileSizeArea) return;
    
    for (auto& [id, node] : nodes) {
        if (node.type == GourceNodeType::File && node.fileInfo.fileSize > 0) {
            float radius = std::sqrt(static_cast<float>(node.fileInfo.fileSize) * style.fileSizeScaleFactor);
            node.fileSizeRadius = std::clamp(radius, style.minFileSizeRadius, style.maxFileSizeRadius);
        } else {
            node.fileSizeRadius = 0.0f;
        }
    }
}

void UltraCanvasGourceTree::RebuildLinks() {
    links.clear();
    
    for (const auto& [id, node] : nodes) {
        if (!node.parentId.empty() && node.isVisible) {
            auto parentIt = nodes.find(node.parentId);
            if (parentIt != nodes.end() && parentIt->second.isVisible) {
                links.emplace_back(node.parentId, id);
            }
        }
    }
}

void UltraCanvasGourceTree::UpdateAnimation(float deltaTime) {
    if (needsLayout) {
        PerformLayout();
    }
    
    if (isAnimating) {
        ApplyForceDirectedRelaxation(deltaTime);
        RequestRedraw();
    }
}

bool UltraCanvasGourceTree::IsAnimationComplete() const {
    return !isAnimating;
}

// ===== HELPER METHODS =====

void UltraCanvasGourceTree::CollectDescendantIds(const std::string& nodeId, 
                                                  std::vector<std::string>& ids) const {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return;
    
    for (const auto& childId : nodeIt->second.childIds) {
        ids.push_back(childId);
        CollectDescendantIds(childId, ids);
    }
}

int UltraCanvasGourceTree::CountVisibleDescendants(const std::string& nodeId) const {
    auto nodeIt = nodes.find(nodeId);
    if (nodeIt == nodes.end()) return 0;
    
    if (!nodeIt->second.isExpanded) return 0;
    
    int count = 0;
    for (const auto& childId : nodeIt->second.childIds) {
        auto childIt = nodes.find(childId);
        if (childIt != nodes.end() && childIt->second.isVisible) {
            count++;
            count += CountVisibleDescendants(childId);
        }
    }
    return count;
}

Point2Dd UltraCanvasGourceTree::ScreenToWorld(const Point2Di& screenPos) const {
    Rect2Di bounds = GetBounds();
    float worldX = (screenPos.x - panX) / zoomLevel;
    float worldY = (screenPos.y - panY) / zoomLevel;
    return Point2Dd(worldX, worldY);
}

Point2Di UltraCanvasGourceTree::WorldToScreen(const Point2Dd& worldPos) const {
    int screenX = static_cast<int>(worldPos.x * zoomLevel + panX);
    int screenY = static_cast<int>(worldPos.y * zoomLevel + panY);
    return Point2Di(screenX, screenY);
}

std::string UltraCanvasGourceTree::FindNodeAtPosition(const Point2Dd& worldPos) const {
    // Search in reverse order (top nodes first)
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        const auto& node = it->second;
        if (!node.isVisible) continue;
        
        float dx = worldPos.x - node.x;
        float dy = worldPos.y - node.y;
        float distSq = dx * dx + dy * dy;
        
        float hitRadius = node.nodeRadius + 3.0f; // Add some padding
        if (node.type == GourceNodeType::CollapsedDirectory) {
            hitRadius = style.collapsedBoxSize / 2.0f + 3.0f;
        }
        
        if (distSq <= hitRadius * hitRadius) {
            return it->first;
        }
    }
    return "";
}

Color UltraCanvasGourceTree::GetNodeColor(const GourceNode& node) const {
    Color baseColor = node.nodeColor;
    
    if (node.isSelected) {
        return style.selectionColor;
    }
    if (node.isHovered) {
        return style.hoverColor;
    }
    
    return baseColor.WithAlpha(static_cast<uint8_t>(node.opacity * 255));
}

float UltraCanvasGourceTree::CalculateOpacity(const GourceNode& node) const {
    if (highlightMode == GourceHighlightMode::NoneHighlight) {
        return 1.0f;
    }
    
    time_t timestamp = 0;
    switch (highlightMode) {
        case GourceHighlightMode::LastAccess:
        case GourceHighlightMode::InvertLastAccess:
            timestamp = node.fileInfo.lastAccessTime;
            break;
        case GourceHighlightMode::CreationDate:
        case GourceHighlightMode::InvertCreationDate:
            timestamp = node.fileInfo.creationTime;
            break;
        default:
            return 1.0f;
    }
    
    if (timestamp == 0 || oldestDate == 0 || newestDate == 0) {
        return 0.5f; // Default for missing data
    }
    
    time_t dateRange = newestDate - oldestDate;
    if (dateRange <= 0) return 1.0f;
    
    float normalizedTime = static_cast<float>(timestamp - oldestDate) / dateRange;
    normalizedTime = std::clamp(normalizedTime, 0.0f, 1.0f);
    
    // Map to opacity (0.2 to 1.0 range)
    float opacity;
    switch (highlightMode) {
        case GourceHighlightMode::LastAccess:
        case GourceHighlightMode::CreationDate:
            // Recent = opaque
            opacity = 0.2f + normalizedTime * 0.8f;
            break;
        case GourceHighlightMode::InvertLastAccess:
        case GourceHighlightMode::InvertCreationDate:
            // Old = opaque
            opacity = 1.0f - normalizedTime * 0.8f;
            break;
        default:
            opacity = 1.0f;
    }
    
    return opacity;
}

std::string UltraCanvasGourceTree::GetExtension(const std::string& filename) const {
    size_t dotPos = filename.rfind('.');
    if (dotPos != std::string::npos && dotPos < filename.length() - 1) {
        return filename.substr(dotPos + 1);
    }
    return "";
}

std::string UltraCanvasGourceTree::FormatFileSize(int64_t bytes) const {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    
    std::ostringstream oss;
    if (unitIndex == 0) {
        oss << bytes << " B";
    } else {
        oss << std::fixed << std::setprecision(1) << size << " " << units[unitIndex];
    }
    return oss.str();
}

std::string UltraCanvasGourceTree::FormatTimestamp(time_t timestamp) const {
    if (timestamp == 0) return "Unknown";
    
    std::tm* tm = std::localtime(&timestamp);
    if (!tm) return "Invalid";
    
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d %H:%M");
    return oss.str();
}

void UltraCanvasGourceTree::ApplyThemeColors(GourceTheme theme) {
    switch (theme) {
        case GourceTheme::Dark:
            style.backgroundColor = Color(20, 20, 30);
            style.fileNodeColor = Color(100, 180, 255);
            style.dirNodeColor = Color(255, 180, 100);
            style.linkColor = Color(60, 60, 80, 150);
            style.labelColor = Colors::White;
            style.labelBackground = Color(0, 0, 0, 200);
            style.rootNodeColor = Color(255, 215, 0);
            break;
            
        case GourceTheme::Light:
            style.backgroundColor = Color(245, 245, 250);
            style.fileNodeColor = Color(50, 120, 200);
            style.dirNodeColor = Color(200, 120, 50);
            style.linkColor = Color(150, 150, 170, 150);
            style.labelColor = Colors::Black;
            style.labelBackground = Color(255, 255, 255, 200);
            style.rootNodeColor = Color(200, 160, 0);
            break;
            
        case GourceTheme::Colorful:
            style.backgroundColor = Color(30, 30, 40);
            style.fileNodeColor = Color(100, 200, 255);
            style.dirNodeColor = Color(255, 150, 100);
            style.linkColor = Color(100, 80, 120, 150);
            style.labelColor = Colors::White;
            style.rootNodeColor = Color(255, 100, 150);
            break;
            
        case GourceTheme::Monochrome:
            style.backgroundColor = Color(30, 30, 30);
            style.fileNodeColor = Color(200, 200, 200);
            style.dirNodeColor = Color(255, 255, 255);
            style.linkColor = Color(100, 100, 100, 150);
            style.labelColor = Color(200, 200, 200);
            style.rootNodeColor = Colors::White;
            break;
            
        case GourceTheme::Default:
        case GourceTheme::Custom:
        default:
            style.backgroundColor = Color(20, 20, 30);
            style.fileNodeColor = Colors::LightBlue;
            style.dirNodeColor = Color(255, 165, 0);
            style.linkColor = Color(80, 80, 100, 150);
            style.labelColor = Colors::White;
            style.labelBackground = Color(0, 0, 0, 180);
            style.rootNodeColor = Color(255, 215, 0);
            break;
    }
}

void UltraCanvasGourceTree::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    if (!ctx || !IsVisible()) return;
    
    // Perform layout if needed
    if (needsLayout) {
        PerformLayout();
    }
    
    Rect2Di bounds = GetLocalBounds();

//    // Save state and set clip region (element-local coords)
//    ctx->PushState();
//    ctx->ClipRect(Rect2Dd(finalBounds.x, finalBounds.y, finalBounds.width, finalBounds.height));

    // Draw background
    DrawBackground(ctx);
    
    // Apply view transformation
    ctx->Translate(panX, panY);
    ctx->Scale(zoomLevel, zoomLevel);
    
    // Draw links first (behind nodes)
    if (style.showLinks) {
        DrawLinks(ctx);
    }
    
    // Draw nodes
    DrawNodes(ctx);
    
//    // Restore transformation
//    ctx->PopState();
    
    // Draw tooltip (in screen space)
    if (!hoveredNodeId.empty()) {
        auto nodeIt = nodes.find(hoveredNodeId);
        if (nodeIt != nodes.end()) {
            DrawTooltip(ctx, nodeIt->second);
        }
    }
}

void UltraCanvasGourceTree::DrawBackground(IRenderContext* ctx) {
    Rect2Di bounds = GetLocalBounds();
    
    // Fill background
    ctx->SetFillPaint(style.backgroundColor);
    ctx->ClearPath();
    ctx->Rect(0, 0, finalBounds.width, finalBounds.height);
    ctx->FillPathPreserve();
    
    // Draw depth rings (optional grid)
    if (style.showGrid && !nodes.empty()) {
        ctx->SetStrokePaint(style.gridColor);
        ctx->SetStrokeWidth(0.5f);
        
        // Find max depth
        int maxNodeDepth = 0;
        for (const auto& [id, node] : nodes) {
            if (node.isVisible) {
                maxNodeDepth = std::max(maxNodeDepth, node.depth);
            }
        }
        
        // Draw concentric circles
        float centerX = finalBounds.x + panX + style.centerX * zoomLevel;
        float centerY = finalBounds.y + panY + style.centerY * zoomLevel;
        
        for (int d = 1; d <= maxNodeDepth; d++) {
            float radius = d * style.ringSpacing * zoomLevel;
            ctx->ClearPath();
            ctx->Arc(centerX, centerY, radius, 0, 2 * 3.14159265f);
            ctx->StrokePathPreserve();
        }
    }
}

void UltraCanvasGourceTree::DrawLinks(IRenderContext* ctx) {
    for (const auto& link : links) {
        auto sourceIt = nodes.find(link.sourceId);
        auto targetIt = nodes.find(link.targetId);
        
        if (sourceIt == nodes.end() || targetIt == nodes.end()) continue;
        if (!sourceIt->second.isVisible || !targetIt->second.isVisible) continue;
        
        Color linkColor = style.linkColor;
        
        // Highlight links connected to hovered/selected nodes
        if (link.sourceId == hoveredNodeId || link.targetId == hoveredNodeId) {
            linkColor = style.hoverColor.WithAlpha(180);
        } else if (sourceIt->second.isSelected || targetIt->second.isSelected) {
            linkColor = style.selectionColor.WithAlpha(180);
        }
        
        if (style.curvedLinks) {
            DrawCurvedLink(ctx, sourceIt->second, targetIt->second, linkColor);
        } else {
            DrawStraightLink(ctx, sourceIt->second, targetIt->second, linkColor);
        }
    }
}

void UltraCanvasGourceTree::DrawCurvedLink(IRenderContext* ctx, const GourceNode& source,
                                            const GourceNode& target, const Color& color) {
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.linkWidth);
    
    float x0 = source.x;
    float y0 = source.y;
    float x1 = target.x;
    float y1 = target.y;
    
    // Calculate control points for bezier curve
    float midX = (x0 + x1) / 2.0f;
    float midY = (y0 + y1) / 2.0f;
    
    // Curve towards center
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = std::sqrt(dx * dx + dy * dy);
    
    // Perpendicular offset towards center
    float nx = -dy / dist;
    float ny = dx / dist;
    
    // Determine if we should curve towards or away from center
    float centerDx = style.centerX - midX;
    float centerDy = style.centerY - midY;
    float dotProduct = nx * centerDx + ny * centerDy;
    float curveFactor = (dotProduct > 0 ? 1.0f : -1.0f) * style.linkCurvature * dist * 0.3f;
    
    float ctrlX = midX + nx * curveFactor;
    float ctrlY = midY + ny * curveFactor;
    
    ctx->ClearPath();
    ctx->MoveTo(x0, y0);
    ctx->QuadraticCurveTo(ctrlX, ctrlY, x1, y1);
    ctx->StrokePathPreserve();
}

void UltraCanvasGourceTree::DrawStraightLink(IRenderContext* ctx, const GourceNode& source,
                                              const GourceNode& target, const Color& color) {
    ctx->SetStrokePaint(color);
    ctx->SetStrokeWidth(style.linkWidth);
    
    ctx->ClearPath();
    ctx->MoveTo(source.x, source.y);
    ctx->LineTo(target.x, target.y);
    ctx->StrokePathPreserve();
}

void UltraCanvasGourceTree::DrawNodes(IRenderContext* ctx) {
    // Draw in order: files first, then directories, then root
    // This ensures directories appear on top of files
    
    // First pass: file size areas (behind everything)
    if (style.showFileSizeArea) {
        for (const auto& [id, node] : nodes) {
            if (node.isVisible && node.fileSizeRadius > 0) {
                DrawFileSizeArea(ctx, node);
            }
        }
    }
    
    // Second pass: files
    for (const auto& [id, node] : nodes) {
        if (node.isVisible && node.type == GourceNodeType::File) {
            DrawNode(ctx, node);
        }
    }
    
    // Third pass: directories and collapsed directories
    for (const auto& [id, node] : nodes) {
        if (node.isVisible && 
            (node.type == GourceNodeType::Directory || 
             node.type == GourceNodeType::CollapsedDirectory)) {
            DrawNode(ctx, node);
        }
    }
}

void UltraCanvasGourceTree::DrawNode(IRenderContext* ctx, const GourceNode& node) {
    Color nodeColor = GetNodeColor(node);
    
    // Draw the node shape
    DrawNodeShape(ctx, node);
    
    // Draw label if appropriate
    if (style.showLabels || (style.showLabelsOnHover && node.isHovered)) {
        DrawNodeLabel(ctx, node);
    }
}

void UltraCanvasGourceTree::DrawFileSizeArea(IRenderContext* ctx, const GourceNode& node) {
    if (node.fileSizeRadius <= 0) return;
    
    Color areaColor = style.fileSizeAreaColor;
    areaColor = areaColor.WithAlpha(static_cast<uint8_t>(node.opacity * areaColor.a * 0.5f));
    
    ctx->SetFillPaint(areaColor);
    ctx->ClearPath();
    ctx->Arc(node.x, node.y, node.fileSizeRadius, 0, 2 * 3.14159265f);
    ctx->FillPathPreserve();
}

void UltraCanvasGourceTree::DrawNodeShape(IRenderContext* ctx, const GourceNode& node) {
    Color fillColor = GetNodeColor(node);
    Color strokeColor = style.fileNodeStroke;
    float strokeWidth = style.fileNodeStrokeWidth;
    
    if (node.type == GourceNodeType::CollapsedDirectory) {
        // Draw as a box
        strokeColor = style.collapsedBoxStroke;
        strokeWidth = style.collapsedBoxStrokeWidth;
        
        float halfSize = style.collapsedBoxSize / 2.0f;
        float x = node.x - halfSize;
        float y = node.y - halfSize;
        
        ctx->SetFillPaint(style.collapsedBoxColor.WithAlpha(
            static_cast<uint8_t>(node.opacity * 255)));
        ctx->ClearPath();
        ctx->RoundedRect(x, y, style.collapsedBoxSize, style.collapsedBoxSize, 
                       style.collapsedBoxCornerRadius);
        ctx->FillPathPreserve();
        
        // Draw stroke
        if (strokeWidth > 0) {
            ctx->SetStrokePaint(strokeColor);
            ctx->SetStrokeWidth(strokeWidth);
            ctx->StrokePathPreserve();
        }
        
        // Draw expand indicator (plus sign)
        ctx->SetStrokePaint(Colors::White);
        ctx->SetStrokeWidth(1.5f);
        ctx->ClearPath();
        ctx->MoveTo(node.x - halfSize * 0.5f, node.y);
        ctx->LineTo(node.x + halfSize * 0.5f, node.y);
        ctx->MoveTo(node.x, node.y - halfSize * 0.5f);
        ctx->LineTo(node.x, node.y + halfSize * 0.5f);
        ctx->StrokePathPreserve();
        
    } else if (node.type == GourceNodeType::Directory) {
        // Draw as circle with different style
        strokeColor = style.dirNodeStroke;
        strokeWidth = style.dirNodeStrokeWidth;
        
        ctx->SetFillPaint(fillColor);
        ctx->ClearPath();
        ctx->Arc(node.x, node.y, node.nodeRadius, 0, 2 * 3.14159265f);
        ctx->FillPathPreserve();
        
        if (strokeWidth > 0) {
            ctx->SetStrokePaint(strokeColor);
            ctx->SetStrokeWidth(strokeWidth);
            ctx->StrokePathPreserve();
        }
        
        // Draw collapse indicator if expanded and has children
        if (node.isExpanded && !node.childIds.empty()) {
            ctx->SetStrokePaint(Colors::White);
            ctx->SetStrokeWidth(1.0f);
            ctx->ClearPath();
            float lineLen = node.nodeRadius * 0.5f;
            ctx->MoveTo(node.x - lineLen, node.y);
            ctx->LineTo(node.x + lineLen, node.y);
            ctx->StrokePathPreserve();
        }
        
    } else {
        // Regular file - draw as circle
        ctx->SetFillPaint(fillColor);
        ctx->ClearPath();
        ctx->Arc(node.x, node.y, node.nodeRadius, 0, 2 * 3.14159265f);
        ctx->FillPathPreserve();
        
        if (strokeWidth > 0) {
            ctx->SetStrokePaint(strokeColor);
            ctx->SetStrokeWidth(strokeWidth);
            ctx->StrokePathPreserve();
        }
    }
    
    // Draw selection/hover glow
    if (node.isSelected || node.isHovered) {
        Color glowColor = node.isSelected ? style.selectionColor : style.hoverColor;
        ctx->SetStrokePaint(glowColor.WithAlpha(150));
        ctx->SetStrokeWidth(style.hoverGlowRadius);
        
        float radius = node.nodeRadius;
        if (node.type == GourceNodeType::CollapsedDirectory) {
            radius = style.collapsedBoxSize / 2.0f;
        }
        
        ctx->ClearPath();
        ctx->Arc(node.x, node.y, radius + style.hoverGlowRadius, 0, 2 * 3.14159265f);
        ctx->StrokePathPreserve();
    }
}

void UltraCanvasGourceTree::DrawNodeLabel(IRenderContext* ctx, const GourceNode& node) {
    // Only show labels on hover or if always enabled
    if (!style.showLabels && !node.isHovered) return;
    
    std::string label = node.name;
    if (label.length() > static_cast<size_t>(style.maxLabelLength)) {
        label = label.substr(0, style.maxLabelLength - 3) + "...";
    }
    
    ctx->SetFontFace(style.fontFamily, FontWeight::Normal, FontSlant::Normal);
    ctx->SetFontSize(style.fontSize);
    
    float textWidth = ctx->GetTextLineWidth(label);
    float textHeight = style.fontSize;
    
    // Position label below node
    float labelX = node.x - textWidth / 2.0f;
    float labelY = node.y + node.nodeRadius + style.labelPadding + textHeight;
    
    if (node.type == GourceNodeType::CollapsedDirectory) {
        labelY = node.y + style.collapsedBoxSize / 2.0f + style.labelPadding + textHeight;
    }
    
    // Draw background
    float bgPadding = 2.0f;
    ctx->SetFillPaint(style.labelBackground);
    ctx->ClearPath();
    ctx->RoundedRect(labelX - bgPadding, labelY - textHeight - bgPadding,
                   textWidth + bgPadding * 2, textHeight + bgPadding * 2, 2.0f);
    ctx->FillPathPreserve();
    
    // Draw text
    ctx->SetFillPaint(style.labelColor.WithAlpha(static_cast<uint8_t>(node.opacity * 255)));
    ctx->FillText(label, labelX, labelY);
}

void UltraCanvasGourceTree::DrawTooltip(IRenderContext* ctx, const GourceNode& node) {
    // Build tooltip text
    std::ostringstream tooltip;
    tooltip << node.name << "\n";
    tooltip << "Path: " << node.fullPath << "\n";
    
    if (node.type == GourceNodeType::File) {
        tooltip << "Type: File";
        if (!node.fileInfo.extension.empty()) {
            tooltip << " (." << node.fileInfo.extension << ")";
        }
        tooltip << "\n";
        
        if (node.fileInfo.fileSize > 0) {
            tooltip << "Size: " << FormatFileSize(node.fileInfo.fileSize) << "\n";
        }
    } else if (node.type == GourceNodeType::Directory) {
        tooltip << "Type: Directory\n";
        tooltip << "Children: " << node.childCount << "\n";
        tooltip << "Total items: " << node.descendantCount << "\n";
    } else {
        tooltip << "Type: Directory (collapsed)\n";
        tooltip << "Click to expand\n";
    }
    
    if (node.fileInfo.creationTime > 0) {
        tooltip << "Created: " << FormatTimestamp(node.fileInfo.creationTime) << "\n";
    }
    if (node.fileInfo.lastAccessTime > 0) {
        tooltip << "Accessed: " << FormatTimestamp(node.fileInfo.lastAccessTime) << "\n";
    }
    if (node.fileInfo.modificationTime > 0) {
        tooltip << "Modified: " << FormatTimestamp(node.fileInfo.modificationTime) << "\n";
    }
    
    std::string tooltipText = tooltip.str();
    
    // Use tooltip manager
    Point2Di screenPos = WorldToScreen(Point2Dd(node.x, node.y));
    
    // Adjust position to not overlap node
    screenPos.x += static_cast<int>(node.nodeRadius * zoomLevel) + 10;
    screenPos.y -= 20;
    screenPos = MapFromLocal(screenPos);
    UltraCanvasTooltipManager::UpdateAndShowTooltipImmediately(
        GetWindow(), tooltipText, screenPos);
}

// ===== EVENT HANDLING =====

bool UltraCanvasGourceTree::OnEvent(const UCEvent& event) {
    switch (event.type) {
        case UCEventType::MouseMove:
            return HandleMouseMove(event);
            
        case UCEventType::MouseDown:
            return HandleMouseDown(event);
            
        case UCEventType::MouseUp:
            return HandleMouseUp(event);
            
        case UCEventType::MouseWheel:
            return HandleMouseWheel(event);
            
        case UCEventType::MouseDoubleClick:
            return HandleDoubleClick(event);
            
        case UCEventType::KeyDown:
            return HandleKeyDown(event);
            
        case UCEventType::MouseLeave:
            if (!hoveredNodeId.empty()) {
                auto nodeIt = nodes.find(hoveredNodeId);
                if (nodeIt != nodes.end()) {
                    nodeIt->second.isHovered = false;
                }
                hoveredNodeId.clear();
                UltraCanvasTooltipManager::HideTooltip();
                RequestRedraw();
            }
            return true;
            
        default:
            break;
    }
    
    return UltraCanvasUIElement::OnEvent(event);
}

bool UltraCanvasGourceTree::HandleMouseMove(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    
    if (isPanning) {
        // Update pan offset
        panX = panStartOffset.x + (mousePos.x - dragStartPos.x);
        panY = panStartOffset.y + (mousePos.y - dragStartPos.y);
        RequestRedraw();
        return true;
    }
    
    // Convert to world coordinates and find node under cursor
    Point2Dd worldPos = ScreenToWorld(mousePos);
    std::string newHoveredId = FindNodeAtPosition(worldPos);
    
    if (newHoveredId != hoveredNodeId) {
        // Update previous hovered node
        if (!hoveredNodeId.empty()) {
            auto nodeIt = nodes.find(hoveredNodeId);
            if (nodeIt != nodes.end()) {
                nodeIt->second.isHovered = false;
            }
        }
        
        // Update new hovered node
        hoveredNodeId = newHoveredId;
        if (!hoveredNodeId.empty()) {
            auto nodeIt = nodes.find(hoveredNodeId);
            if (nodeIt != nodes.end()) {
                nodeIt->second.isHovered = true;
                
                if (onNodeHover) {
                    onNodeHover(hoveredNodeId);
                }
            }
        } else {
            UltraCanvasTooltipManager::HideTooltip();
        }
        
        RequestRedraw();
    }
    
    return !hoveredNodeId.empty();
}

bool UltraCanvasGourceTree::HandleMouseDown(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    
    // Right click or middle click starts panning
    if (event.button == UCMouseButton::Middle || event.button == UCMouseButton::Right) {
        isPanning = true;
        dragStartPos = mousePos;
        panStartOffset = Point2Dd(panX, panY);
        return true;
    }
    
    // Left click
    if (event.button == UCMouseButton::Left) {
        Point2Dd worldPos = ScreenToWorld(mousePos);
        std::string clickedId = FindNodeAtPosition(worldPos);
        
        if (!clickedId.empty()) {
            SelectNode(clickedId);
            
            if (onNodeClick) {
                onNodeClick(clickedId);
            }
            
            return true;
        } else {
            // Clicked on empty space - clear selection
            ClearSelection();
        }
    }
    
    return false;
}

bool UltraCanvasGourceTree::HandleMouseUp(const UCEvent& event) {
    if (isPanning) {
        isPanning = false;
        return true;
    }
    
    return false;
}

bool UltraCanvasGourceTree::HandleMouseWheel(const UCEvent& event) {
    float zoomFactor = (event.wheelDelta > 0) ? 1.15f : 0.87f;
    
    // Get mouse position for zoom center
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    Rect2Di bounds = GetBounds();
    
    // Calculate world position under mouse before zoom
    Point2Dd worldBefore = ScreenToWorld(mousePos);
    
    // Apply zoom
    float newZoom = zoomLevel * zoomFactor;
    newZoom = std::clamp(newZoom, minZoom, maxZoom);
    
    if (newZoom != zoomLevel) {
        zoomLevel = newZoom;
        
        // Adjust pan to keep mouse position fixed
        Point2Dd worldAfter = ScreenToWorld(mousePos);
        panX += (worldAfter.x - worldBefore.x) * zoomLevel;
        panY += (worldAfter.y - worldBefore.y) * zoomLevel;
        
        RequestRedraw();
    }
    
    return true;
}

bool UltraCanvasGourceTree::HandleDoubleClick(const UCEvent& event) {
    Point2Di mousePos(event.pointer.x, event.pointer.y);
    Point2Dd worldPos = ScreenToWorld(mousePos);
    std::string clickedId = FindNodeAtPosition(worldPos);
    
    if (!clickedId.empty()) {
        auto nodeIt = nodes.find(clickedId);
        if (nodeIt != nodes.end()) {
            // Toggle expand/collapse for directories
            if (nodeIt->second.type == GourceNodeType::Directory ||
                nodeIt->second.type == GourceNodeType::CollapsedDirectory) {
                ToggleNode(clickedId);
            }
            
            if (onNodeDoubleClick) {
                onNodeDoubleClick(clickedId);
            }
            
            return true;
        }
    }
    
    return false;
}

bool UltraCanvasGourceTree::HandleKeyDown(const UCEvent& event) {
    if (!IsFocused()) return false;
    
    switch (event.virtualKey) {
        case '+':
        case '=':
            ZoomIn();
            return true;
            
        case '-':
            ZoomOut();
            return true;
            
        case '0':
            if (event.ctrl) {
                ResetView();
                return true;
            }
            break;
            
        case 'F':
        case 'f':
            ZoomToFit();
            return true;
            
        case UCKeys::Space:
            if (!selectedNodeIds.empty()) {
                ToggleNode(selectedNodeIds.front());
                return true;
            }
            break;
            
        case UCKeys::Enter:
            if (!selectedNodeIds.empty()) {
                ExpandNode(selectedNodeIds.front());
                return true;
            }
            break;
            
        case UCKeys::Escape:
            ClearSelection();
            return true;
            
        case 'E':
        case 'e':
            if (event.ctrl) {
                ExpandAll();
                return true;
            }
            break;
            
        case 'C':
        case 'c':
            if (event.ctrl && event.shift) {
                CollapseAll();
                return true;
            }
            break;
    }
    
    return false;
}

// ===== DATA EXPORT =====

bool UltraCanvasGourceTree::SaveToSVG(const std::string& filePath) {
    std::ofstream file(filePath);
    if (!file.is_open()) return false;
    
    Rect2Di bounds = GetBounds();
    
    // Find actual content bounds
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    
    for (const auto& [id, node] : nodes) {
        if (!node.isVisible) continue;
        float r = node.nodeRadius + node.fileSizeRadius;
        minX = std::min(minX, node.x - r);
        maxX = std::max(maxX, node.x + r);
        minY = std::min(minY, node.y - r);
        maxY = std::max(maxY, node.y + r);
    }
    
    float padding = 50.0f;
    float width = maxX - minX + padding * 2;
    float height = maxY - minY + padding * 2;
    float offsetX = -minX + padding;
    float offsetY = -minY + padding;
    
    // SVG header
    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
    file << "width=\"" << width << "\" height=\"" << height << "\">\n";
    
    // Background
    file << "<rect width=\"100%\" height=\"100%\" fill=\""
         << "rgb(" << (int)style.backgroundColor.r << ","
         << (int)style.backgroundColor.g << ","
         << (int)style.backgroundColor.b << ")\"/>\n";
    
    // Links
    file << "<g class=\"links\" stroke-linecap=\"round\">\n";
    for (const auto& link : links) {
        auto sourceIt = nodes.find(link.sourceId);
        auto targetIt = nodes.find(link.targetId);
        if (sourceIt == nodes.end() || targetIt == nodes.end()) continue;
        if (!sourceIt->second.isVisible || !targetIt->second.isVisible) continue;
        
        const auto& src = sourceIt->second;
        const auto& tgt = targetIt->second;
        
        file << "<line x1=\"" << (src.x + offsetX) << "\" y1=\"" << (src.y + offsetY)
             << "\" x2=\"" << (tgt.x + offsetX) << "\" y2=\"" << (tgt.y + offsetY)
             << "\" stroke=\"rgb(" << (int)style.linkColor.r << ","
             << (int)style.linkColor.g << "," << (int)style.linkColor.b
             << ")\" stroke-opacity=\"" << (style.linkColor.a / 255.0f)
             << "\" stroke-width=\"" << style.linkWidth << "\"/>\n";
    }
    file << "</g>\n";
    
    // Nodes
    file << "<g class=\"nodes\">\n";
    for (const auto& [id, node] : nodes) {
        if (!node.isVisible) continue;
        
        // File size area
        if (style.showFileSizeArea && node.fileSizeRadius > 0) {
            file << "<circle cx=\"" << (node.x + offsetX) << "\" cy=\"" << (node.y + offsetY)
                 << "\" r=\"" << node.fileSizeRadius
                 << "\" fill=\"rgb(" << (int)style.fileSizeAreaColor.r << ","
                 << (int)style.fileSizeAreaColor.g << "," << (int)style.fileSizeAreaColor.b
                 << ")\" fill-opacity=\"" << (style.fileSizeAreaColor.a / 255.0f * 0.5f)
                 << "\"/>\n";
        }
        
        // Node shape
        if (node.type == GourceNodeType::CollapsedDirectory) {
            float halfSize = style.collapsedBoxSize / 2.0f;
            file << "<rect x=\"" << (node.x + offsetX - halfSize) 
                 << "\" y=\"" << (node.y + offsetY - halfSize)
                 << "\" width=\"" << style.collapsedBoxSize
                 << "\" height=\"" << style.collapsedBoxSize
                 << "\" rx=\"" << style.collapsedBoxCornerRadius
                 << "\" fill=\"rgb(" << (int)style.collapsedBoxColor.r << ","
                 << (int)style.collapsedBoxColor.g << "," << (int)style.collapsedBoxColor.b
                 << ")\" stroke=\"white\" stroke-width=\"1\"/>\n";
        } else {
            file << "<circle cx=\"" << (node.x + offsetX) << "\" cy=\"" << (node.y + offsetY)
                 << "\" r=\"" << node.nodeRadius
                 << "\" fill=\"rgb(" << (int)node.nodeColor.r << ","
                 << (int)node.nodeColor.g << "," << (int)node.nodeColor.b
                 << ")\" fill-opacity=\"" << node.opacity
                 << "\" stroke=\"white\" stroke-width=\"0.5\"/>\n";
        }
        
        // Label
        if (style.showLabels) {
            file << "<text x=\"" << (node.x + offsetX) << "\" y=\"" 
                 << (node.y + offsetY + node.nodeRadius + 12)
                 << "\" text-anchor=\"middle\" font-size=\"" << style.fontSize
                 << "\" fill=\"white\">" << node.name << "</text>\n";
        }
    }
    file << "</g>\n";
    
    file << "</svg>\n";
    file.close();
    
    return true;
}

} // namespace UltraCanvas
