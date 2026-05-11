// include/Plugins/Diagrams/UltraCanvasGourceTree.h
// Gource-style radial tree diagram for storage/filesystem visualization
// Version: 1.0.1
// Last Modified: 2026-05-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasCommonTypes.h"
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <memory>
#include <functional>
#include <cmath>
#include <ctime>
#include <chrono>

namespace UltraCanvas {

// ===== ENUMERATIONS =====

enum class GourceLayoutMode {
    Static,         // Instant radial tree layout
    Animated,       // Force-directed with animation during data addition
    Hybrid          // Radial initial + force relaxation
};

enum class GourceHighlightMode {
    NoneHighlight,          // No date-based highlighting (renamed from None to avoid X11 macro collision)
    LastAccess,             // Recent access = opaque, old = transparent
    CreationDate,           // Recent creation = opaque, old = transparent
    InvertLastAccess,       // Old access = opaque (show unused files)
    InvertCreationDate      // Old creation = opaque
};

enum class GourceNodeType {
    File,
    Directory,
    CollapsedDirectory      // Directory beyond depth limit
};

enum class GourceTheme {
    Default,
    Dark,
    Light,
    Colorful,
    Monochrome,
    Custom
};

// ===== DATA STRUCTURES =====

struct GourceFileInfo {
    int64_t fileSize = 0;           // Size in bytes
    time_t creationTime = 0;        // File creation timestamp
    time_t lastAccessTime = 0;      // Last access timestamp
    time_t modificationTime = 0;    // Last modification timestamp
    std::string extension;          // File extension for coloring
    std::string mimeType;           // Optional MIME type
    
    GourceFileInfo() = default;
};

struct GourceNode {
    // Identity
    std::string id;                 // Unique node identifier
    std::string name;               // Display name (filename/dirname)
    std::string fullPath;           // Full filesystem path
    std::string parentId;           // Parent node ID (empty for root)
    
    // Type and state
    GourceNodeType type = GourceNodeType::File;
    bool isExpanded = true;         // For directories: expanded or collapsed
    bool isVisible = true;          // Whether node should be rendered
    bool isHovered = false;
    bool isSelected = false;
    
    // File information
    GourceFileInfo fileInfo;
    
    // Tree structure
    int depth = 0;                  // Depth in tree (0 = root)
    int childCount = 0;             // Number of direct children
    int descendantCount = 0;        // Total descendants (for sizing)
    std::vector<std::string> childIds;
    
    // Layout - computed positions
    float x = 0.0f;                  // Current X position
    float y = 0.0f;                  // Current Y position
    double targetX = 0.0f;           // Target X (for animation)
    double targetY = 0.0f;           // Target Y (for animation)
    double angle = 0.0f;             // Angle from center (radians)
    double radius = 0.0f;            // Distance from center
    
    // Physics (for force-directed mode)
    float velocityX = 0.0f;
    float velocityY = 0.0f;

    // Appearance
    float nodeRadius = 5.0f;         // Visual node radius
    float fileSizeRadius = 0.0f;     // Transparent area radius (file size)
    Color nodeColor = Colors::Blue;
    float opacity = 1.0f;            // Based on highlight mode
    
    GourceNode() = default;
    GourceNode(const std::string& nodeId, const std::string& nodeName, 
               const std::string& path, GourceNodeType nodeType)
        : id(nodeId), name(nodeName), fullPath(path), type(nodeType) {}
};

struct GourceLink {
    std::string sourceId;
    std::string targetId;
    Color color = Color(128, 128, 128, 100);
    double width = 1.0f;
    
    GourceLink() = default;
    GourceLink(const std::string& src, const std::string& tgt)
        : sourceId(src), targetId(tgt) {}
};

// ===== STYLE CONFIGURATION =====

struct GourceStyle {
    // Background
    Color backgroundColor = Color(20, 20, 30);
    bool showGrid = false;
    Color gridColor = Color(50, 50, 60);
    
    // Nodes - Files
    double fileNodeRadius = 4.0f;
    Color fileNodeColor = Colors::LightBlue;
    Color fileNodeStroke = Colors::White;
    double fileNodeStrokeWidth = 0.5f;
    
    // Nodes - Directories
    double dirNodeRadius = 6.0f;
    Color dirNodeColor = Color(255, 165, 0);
    Color dirNodeStroke = Colors::White;
    double dirNodeStrokeWidth = 1.0f;
    
    // Nodes - Collapsed directories (boxes)
    double collapsedBoxSize = 12.0f;
    Color collapsedBoxColor = Color(100, 100, 120);
    Color collapsedBoxStroke = Colors::White;
    double collapsedBoxStrokeWidth = 1.0f;
    double collapsedBoxCornerRadius = 2.0f;
    
    // File size visualization
    bool showFileSizeArea = true;
    Color fileSizeAreaColor = Color(100, 150, 255, 40);
    double fileSizeScaleFactor = 0.00001f;   // Scale bytes to radius
    float maxFileSizeRadius = 50.0f;
    float minFileSizeRadius = 2.0f;
    
    // Links/Edges
    Color linkColor = Color(80, 80, 100, 150);
    double linkWidth = 1.0f;
    bool showLinks = true;
    bool curvedLinks = true;
    double linkCurvature = 0.3f;
    
    // Labels
    bool showLabels = true;
    bool showLabelsOnHover = true;
    std::string fontFamily = "Sans";
    double fontSize = 10.0f;
    Color labelColor = Colors::White;
    Color labelBackground = Color(0, 0, 0, 180);
    double labelPadding = 3.0f;
    int maxLabelLength = 20;
    
    // Selection and hover
    Color hoverColor = Colors::Yellow;
    Color selectionColor = Colors::Cyan;
    double hoverGlowRadius = 3.0f;
    
    // Root node
    double rootNodeRadius = 10.0f;
    Color rootNodeColor = Color(255, 215, 0);
    
    // Animation
    double animationSpeed = 0.15f;       // Lerp factor per frame
    double physicsStrength = 0.5f;
    double dampingFactor = 0.85f;
    
    // Layout
    double ringSpacing = 60.0f;          // Distance between depth rings
    double minNodeSpacing = 15.0f;       // Minimum space between nodes
    double centerX = 0.0f;               // Center point (computed)
    double centerY = 0.0f;
    
    // Tooltip
    Color tooltipBackground = Color(40, 40, 50, 230);
    Color tooltipBorder = Color(100, 100, 120);
    Color tooltipText = Colors::White;
    double tooltipPadding = 8.0f;
    double tooltipFontSize = 11.0f;
    
    GourceStyle() = default;
};

// ===== EXTENSION COLOR MAPPING =====

struct ExtensionColorMap {
    std::unordered_map<std::string, Color> colors;
    
    ExtensionColorMap() {
        // Default extension colors
        colors["cpp"] = Color(0, 150, 255);
        colors["c"] = Color(0, 100, 200);
        colors["h"] = Color(100, 100, 255);
        colors["hpp"] = Color(100, 150, 255);
        colors["py"] = Color(50, 200, 50);
        colors["js"] = Color(255, 220, 50);
        colors["ts"] = Color(50, 120, 200);
        colors["html"] = Color(255, 100, 50);
        colors["css"] = Color(50, 150, 200);
        colors["json"] = Color(200, 200, 100);
        colors["xml"] = Color(200, 150, 100);
        colors["md"] = Color(150, 150, 150);
        colors["txt"] = Color(200, 200, 200);
        colors["png"] = Color(255, 100, 150);
        colors["jpg"] = Color(255, 120, 120);
        colors["gif"] = Color(255, 150, 100);
        colors["svg"] = Color(255, 180, 50);
        colors["pdf"] = Color(255, 50, 50);
        colors["zip"] = Color(150, 100, 50);
        colors["exe"] = Color(100, 255, 100);
        colors["dll"] = Color(100, 200, 100);
        colors["so"] = Color(100, 200, 150);
    }
    
    Color GetColor(const std::string& ext) const {
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
        auto it = colors.find(lowerExt);
        return (it != colors.end()) ? it->second : Color(180, 180, 180);
    }
    
    void SetColor(const std::string& ext, const Color& color) {
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
        colors[lowerExt] = color;
    }
};

// ===== MAIN GOURCE TREE CLASS =====

class UltraCanvasGourceTree : public UltraCanvasUIElement {
public:
    // ===== CONSTRUCTOR =====
    UltraCanvasGourceTree(const std::string& id, long x, long y, long w, long h);
    
    bool AcceptsFocus() const override { return true; }
    
    // ===== NODE MANAGEMENT =====
    
    // Add root node
    void SetRootNode(const std::string& id, const std::string& name, 
                     const std::string& fullPath = "");
    
    // Add child node to parent
    void AddNode(const std::string& parentId, const std::string& nodeId,
                 const std::string& name, GourceNodeType type,
                 const GourceFileInfo& fileInfo = GourceFileInfo());
    
    // Add file node (convenience method)
    void AddFile(const std::string& parentId, const std::string& filePath,
                 const std::string& fileName, const GourceFileInfo& fileInfo);
    
    // Add directory node (convenience method)
    void AddDirectory(const std::string& parentId, const std::string& dirPath,
                      const std::string& dirName);
    
    // Remove node and all descendants
    void RemoveNode(const std::string& nodeId);
    
    // Clear all nodes
    void ClearAll();
    
    // Get node by ID
    GourceNode* GetNode(const std::string& nodeId);
    const GourceNode* GetNode(const std::string& nodeId) const;
    
    // Get root node
    GourceNode* GetRootNode();
    
    // ===== NODE STATE =====
    
    // Expand/collapse directory nodes
    void ExpandNode(const std::string& nodeId);
    void CollapseNode(const std::string& nodeId);
    void ToggleNode(const std::string& nodeId);
    void ExpandAll();
    void CollapseAll();
    void ExpandToDepth(int depth);
    
    // Selection
    void SelectNode(const std::string& nodeId);
    void DeselectNode(const std::string& nodeId);
    void ClearSelection();
    std::vector<std::string> GetSelectedNodes() const;
    
    // ===== LAYOUT & DISPLAY =====
    
    // Layout mode
    void SetLayoutMode(GourceLayoutMode mode);
    GourceLayoutMode GetLayoutMode() const { return layoutMode; }
    
    // Depth limiting
    void SetMaxDepth(int depth);
    int GetMaxDepth() const { return maxDepth; }
    
    // Highlight mode (date-based transparency)
    void SetHighlightMode(GourceHighlightMode mode);
    GourceHighlightMode GetHighlightMode() const { return highlightMode; }
    
    // Date range for highlight calculation
    void SetHighlightDateRange(time_t oldest, time_t newest);
    void AutoCalculateDateRange();
    
    // File size visualization
    void SetShowFileSizeArea(bool show);
    bool GetShowFileSizeArea() const { return style.showFileSizeArea; }
    void SetFileSizeScale(float scale);
    
    // Theme
    void SetTheme(GourceTheme theme);
    GourceTheme GetTheme() const { return currentTheme; }
    
    // Style access
    GourceStyle& GetStyle() { return style; }
    const GourceStyle& GetStyle() const { return style; }
    void SetStyle(const GourceStyle& newStyle);
    
    // Extension colors
    ExtensionColorMap& GetExtensionColors() { return extensionColors; }
    void SetExtensionColor(const std::string& ext, const Color& color);
    
    // ===== VIEW CONTROL =====
    
    // Zoom
    void SetZoom(float zoom);
    double GetZoom() const { return zoomLevel; }
    void ZoomIn();
    void ZoomOut();
    void ZoomToFit();
    void ResetView();
    
    // Pan
    void SetPan(float x, float y);
    Point2Df GetPan() const { return Point2Df(panX, panY); }
    void CenterOnNode(const std::string& nodeId);
    
    // ===== LAYOUT COMPUTATION =====
    
    // Trigger layout recalculation
    void PerformLayout();
    
    // Update animation/physics (call each frame for animated mode)
    void UpdateAnimation(float deltaTime);
    
    // Check if animation is complete
    bool IsAnimationComplete() const;
    
    // ===== RENDERING =====
    void Render(IRenderContext* ctx, const Rect2Di& dirtyRect) override;
    
    // ===== EVENT HANDLING =====
    bool OnEvent(const UCEvent& event) override;
    
    // ===== CALLBACKS =====
    std::function<void(const std::string&)> onNodeClick;
    std::function<void(const std::string&)> onNodeDoubleClick;
    std::function<void(const std::string&)> onNodeExpand;
    std::function<void(const std::string&)> onNodeCollapse;
    std::function<void(const std::string&)> onNodeHover;
    std::function<void(const std::string&)> onNodeSelect;
    std::function<void()> onLayoutComplete;
    
    // ===== DATA EXPORT =====
    bool SaveToSVG(const std::string& filePath);
    
private:
    // ===== MEMBER VARIABLES =====
    
    // Node storage
    std::map<std::string, GourceNode> nodes;
    std::vector<GourceLink> links;
    std::string rootNodeId;
    
    // Configuration
    GourceLayoutMode layoutMode = GourceLayoutMode::Hybrid;
    GourceHighlightMode highlightMode = GourceHighlightMode::NoneHighlight;
    GourceTheme currentTheme = GourceTheme::Default;
    int maxDepth = -1;              // -1 = unlimited
    
    // Date range for highlighting
    time_t oldestDate = 0;
    time_t newestDate = 0;
    bool autoDateRange = true;
    
    // View state
    float zoomLevel = 1.0f;
    double panX = 0.0f;
    double panY = 0.0f;
    float minZoom = 0.1f;
    float maxZoom = 5.0f;
    
    // Interaction state
    std::string hoveredNodeId;
    std::vector<std::string> selectedNodeIds;
    bool isDragging = false;
    bool isPanning = false;
    Point2Di dragStartPos;
    Point2Df panStartOffset;
    
    // Animation state
    bool needsLayout = true;
    bool isAnimating = false;
    int physicsIterations = 0;
    int maxPhysicsIterations = 100;
    
    // Style and colors
    GourceStyle style;
    ExtensionColorMap extensionColors;
    
    // ===== LAYOUT METHODS =====
    void ComputeRadialLayout();
    void ComputeNodeAngles(const std::string& nodeId, float startAngle, float endAngle);
    void ComputeNodeRadii();
    void ApplyForceDirectedRelaxation(float deltaTime);
    void ComputeRepulsionForces();
    void ComputeAttractionForces();
    void UpdateNodePositions(float deltaTime);
    void UpdateVisibility();
    void UpdateNodeOpacity();
    void ComputeFileSizeRadii();
    void RebuildLinks();
    
    // ===== DRAWING METHODS =====
    void DrawBackground(IRenderContext* ctx);
    void DrawLinks(IRenderContext* ctx);
    void DrawNodes(IRenderContext* ctx);
    void DrawNode(IRenderContext* ctx, const GourceNode& node);
    void DrawFileSizeArea(IRenderContext* ctx, const GourceNode& node);
    void DrawNodeShape(IRenderContext* ctx, const GourceNode& node);
    void DrawNodeLabel(IRenderContext* ctx, const GourceNode& node);
    void DrawCurvedLink(IRenderContext* ctx, const GourceNode& source, 
                        const GourceNode& target, const Color& color);
    void DrawStraightLink(IRenderContext* ctx, const GourceNode& source,
                          const GourceNode& target, const Color& color);
    void DrawTooltip(IRenderContext* ctx, const GourceNode& node);
    
    // ===== EVENT HANDLERS =====
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    bool HandleDoubleClick(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);
    
    // ===== HELPER METHODS =====
    Point2Df ScreenToWorld(const Point2Di& screenPos) const;
    Point2Di WorldToScreen(const Point2Df& worldPos) const;
    std::string FindNodeAtPosition(const Point2Df& worldPos) const;
    Color GetNodeColor(const GourceNode& node) const;
    float CalculateOpacity(const GourceNode& node) const;
    std::string GetExtension(const std::string& filename) const;
    std::string FormatFileSize(int64_t bytes) const;
    std::string FormatTimestamp(time_t timestamp) const;
    void ApplyThemeColors(GourceTheme theme);
    int CountVisibleDescendants(const std::string& nodeId) const;
    void CollectDescendantIds(const std::string& nodeId, std::vector<std::string>& ids) const;
};

// ===== FACTORY FUNCTION =====

inline std::shared_ptr<UltraCanvasGourceTree> CreateGourceTree(
    const std::string& id, long x, long y, long w, long h
) {
    return std::make_shared<UltraCanvasGourceTree>(id, x, y, w, h);
}

} // namespace UltraCanvas
