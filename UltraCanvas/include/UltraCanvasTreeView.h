// include/UltraCanvasTreeView.h
// Hierarchical tree view with icons and text for each row
// Last Modified: 2026-06-04
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasScrollbar.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <cstdint>

namespace UltraCanvas {

// ===== TREE VIEW ENUMS AND STRUCTURES =====

enum class TreeNodeState {
    Collapsed = 0,
    Expanded = 1,
    Leaf = 2        // No children, no expand/collapse button
};

enum class TreeSelectionMode {
    NoSelection = 0,        // No selection allowed
    Single = 1,     // Only one node can be selected
    Multiple = 2   // Multiple nodes can be selected
};

enum class TreeLineStyle {
    NoLine = 0,       // No connecting lines
    Dotted = 1,     // Dotted connecting lines
    Solid = 2       // Solid connecting lines
};

// ===== TREE DISPLAY MODE =====
// How each row is laid out. Classic renders a single line of text (data.text) and
// is the historical/default behaviour. Modern renders three aligned columns
// (Name / Type / Value) with an accent-filled Type column, matching an IDE-style
// debugger "Variables" panel. Both modes keep the tree hierarchy and expand/collapse.
enum class TreeDisplayMode {
    Classic = 0,   // single-text rows (default, back-compatible)
    Modern  = 1    // aligned Name / Type / Value columns
};

// ===== TREE SORT MODE =====
// Ordering applied to a node's children. LastAccess orders by TreeNodeData::accessSequence
// (largest first when ascending=false), which callers stamp when a value is touched.
// NB: value is NoSort (not "None") deliberately — <X11/Xlib.h>, pulled in by the
// Linux backend, #defines `None`, which would mangle an enumerator named None.
enum class TreeSortMode {
    NoSort = 0,      // preserve insertion order
    Alphabetic = 1,  // by display name (data.text), case-insensitive
    LastAccess = 2   // by data.accessSequence
};

// ===== MODERN-MODE COLUMN STYLE =====
// Geometry and colours used only when TreeDisplayMode::Modern is active.
struct TreeColumnStyle {
    int   typeColumnWidth      = 64;   // fixed width of the Type column (px)
    int   valueColumnWidth     = 0;    // 0 => Value column takes the remaining width to the right
    int   columnGap            = 8;    // horizontal gap between columns (px)
    int   typeColumnPadding    = 4;    // padding around the Type accent fill (px)
    Color typeColumnBackground = Color(255, 190, 130);  // orange accent behind the Type column
    Color typeTextColor        = Color(40, 40, 40);     // Type column text
    Color valueTextColor       = Color(40, 40, 40);     // Value column text
    Color groupHeaderBackground = Colors::Black;        // full-width section-header bar
    Color groupHeaderTextColor  = Colors::White;        // section-header text
    bool  showColumnSeparators  = false;                // thin vertical rules between columns
    Color columnSeparatorColor  = Color(210, 210, 210);
};

struct TreeNodeIcon {
    std::string iconPath;
    int width = 16;
    int height = 16;
    bool visible = true;
    
    TreeNodeIcon() = default;
    TreeNodeIcon(const std::string& path, int w = 16, int h = 16) 
        : iconPath(path), width(w), height(h) {}
};

struct TreeNodeData {
    std::string nodeId;           // Unique identifier for the node
    std::string text;             // Display text
    TreeNodeIcon leftIcon;        // Optional icon on left side
    TreeNodeIcon rightIcon;       // Optional icon on right side
    bool enabled = true;          // Can be interacted with
    bool visible = true;          // Should be displayed
    bool showFirstChildOnExpand = true; // Per-node override of the tree's "jump to
                                        // first entry" behaviour: when false, selecting
                                        // /expanding this node does not jump to its first
                                        // child, so the node itself stays selected.
    Color textColor = Colors::Black;     // Text color (ARGB)
    Color backgroundColor = Colors::Transparent; // Background color (transparent by default)
    std::string tooltip;          // Tooltip text
    void* userData = nullptr;     // Custom user data

    // ----- Modern display mode (TreeDisplayMode::Modern) -----
    // In Classic mode `text` holds the whole row. In Modern mode `text` is the
    // Name column and these supply the Type and Value columns. They are ignored
    // in Classic mode, so setting them is always safe.
    std::string typeText;         // Type column (e.g. "int", "*ptr", "fp", "str")
    std::string valueText;        // Value column (e.g. "45", "2x67", "up")
    Color typeColor = Colors::Transparent; // Type column text override (Transparent => use style default)
    bool  isGroupHeader = false;  // Render this row as a full-width section-header bar (Line/Loop/...)
    // Ordering key for TreeSortMode::LastAccess. Callers stamp a monotonically
    // increasing value each time the variable is read/written so the most recently
    // accessed entries can float to the top.
    uint64_t accessSequence = 0;

    TreeNodeData() = default;
    TreeNodeData(const std::string& id, const std::string& displayText) 
        : nodeId(id), text(displayText) {}
};

// ===== TREE NODE CLASS =====
class TreeNode {
public:
    TreeNodeData data;
    TreeNodeState state;
    int level;                    // Depth in tree (0 = root level)
    bool selected;
    bool hovered;
    
    TreeNode* parent;
    std::vector<std::unique_ptr<TreeNode>> children;
    
    TreeNode(const TreeNodeData& nodeData, TreeNode* parentNode = nullptr);
    
    // ===== CHILD MANAGEMENT =====
    TreeNode* AddChild(const TreeNodeData& childData);
    
    void RemoveChild(const std::string& nodeId);
    
    TreeNode* FindChild(const std::string& nodeId);

    TreeNode* FindDescendant(const std::string& nodeId);

    TreeNode* FirstChild() { return children.empty() ? nullptr : children[0].get(); };

    // Sort direct children alphabetically (case-insensitive) by data.text.
    // recursive=true also sorts every descendant level. ascending=false reverses.
    void SortChildNodes(bool recursive = false, bool ascending = true);

    // Sort direct children by the given mode (Alphabetic by data.text, or LastAccess
    // by data.accessSequence). TreeSortMode::NoSort is a no-op. recursive=true also sorts
    // every descendant level. ascending=false reverses the order.
    void SortChildNodes(TreeSortMode mode, bool recursive = false, bool ascending = true);

    // ===== STATE MANAGEMENT =====
    void Expand();
    
    void Collapse();
    
    void Toggle();

    bool HasChildren() const { return !children.empty(); }
    
    bool IsExpanded() const { return state == TreeNodeState::Expanded; }
    
    bool IsVisible() const;
    
    // ===== UTILITY METHODS =====
    int GetVisibleChildCount() const;
    
    std::vector<TreeNode*> GetVisibleChildren();
};

// ===== TREE VIEW CLASS =====
class UltraCanvasTreeView : public UltraCanvasUIElement {
private:
    // ===== TREE VIEW SPECIFIC PROPERTIES =====
    std::unique_ptr<TreeNode> rootNode;
    TreeSelectionMode selectionMode;
    TreeLineStyle lineStyle;
    std::vector<TreeNode*> selectedNodes;
    TreeNode* hoveredNode;
    TreeNode* focusedNode;
    
    // Visual properties
    int rowHeight;                  // Height of each row in pixels
    int indentSize;                 // Indentation per level
    int iconSpacing;               // Space between icon and text
    int textPadding;               // Padding around text
    bool showRootLines;            // Show lines for root level
    bool showExpandButtons;        // Show +/- buttons
    bool showFirstChildOnExpand;   // auto open first child on expand node
    bool autoExpandSelectedNode;  // auto expand selected node
    bool autoSortChildren = false; // keep children sorted alphabetically on insert
    bool autoSortAscending = true; // direction used by auto-sort

    // Display mode + Modern-mode column layout
    TreeDisplayMode displayMode = TreeDisplayMode::Classic;
    TreeColumnStyle columnStyle;

    // Active sort (applied by SetSortMode / re-applied by SortAllNodes)
    TreeSortMode sortMode = TreeSortMode::NoSort;
    bool sortAscending = true;

    // Colors
//    Color backgroundColor;       // Tree background color
    Color selectionColor;       // Selected row background
    Color hoverColor;           // Hovered row background
    Color lineColor;            // Connecting line color
    Color textColor;            // Default text color

    ScrollbarStyle scrollbarStyle = GetDefaultScrollbarStyleOr(ScrollbarStyle::Default());

    // Scrolling (using unified scrollbar)
    std::shared_ptr<UltraCanvasScrollbar> verticalScrollbar;

    // Scrolling
    int scrollOffsetY;             // Vertical scroll offset
    int maxScrollY;                // Maximum scroll value

    // Interaction state
//    bool isDragging;               // Currently dragging scrollbar
//    TreeNode* draggedNode;         // Node being dragged (for drag & drop)
//    Point2Di lastMousePos;          // Last mouse position
    
public:
    // ===== EVENTS AND CALLBACKS =====
    std::function<void(TreeNode*)> onNodeSelected;
    std::function<void(TreeNode*)> onNodeDoubleClicked;
    std::function<void(TreeNode*)> onNodeExpanded;
    std::function<void(TreeNode*)> onNodeCollapsed;
    std::function<void(TreeNode*, TreeNode*)> onNodeDragDrop; // dragged, target
    std::function<void(TreeNode*)> onNodeRightClicked;
    
    // ===== CONSTRUCTOR =====
    UltraCanvasTreeView(const std::string& identifier, 
                       float x, float y, float w, float h);

    UltraCanvasTreeView(const std::string& identifier,
                        float w, float h);

    UltraCanvasTreeView(const std::string& identifier = "TreeView");

    // ===== TREE STRUCTURE MANAGEMENT =====
    TreeNode* SetRootNode(const TreeNodeData& rootData);
    
    TreeNode* GetRootNode() const { return rootNode.get(); }
    
    TreeNode* AddNode(const std::string& parentId, const TreeNodeData& nodeData);
    void RemoveNode(const std::string& nodeId);
    TreeNode* FindNode(const std::string& nodeId);
    
    // ===== SELECTION MANAGEMENT =====
    void SelectNode(TreeNode* node, bool addToSelection = false);
    void DeselectNode(TreeNode* node);
    void ClearSelection();
    const std::vector<TreeNode*>& GetSelectedNodes() const { return selectedNodes; }
    TreeNode* GetFirstSelectedNode() const { return selectedNodes.empty() ? nullptr : selectedNodes[0]; }
    
    // ===== EXPANSION MANAGEMENT =====
    void ExpandNode(TreeNode* node);
    void CollapseNode(TreeNode* node);
    void ExpandAll();
    void CollapseAll();

    // ===== VISUAL PROPERTIES =====
    void SetRowHeight(int height) { rowHeight = height; UpdateScrollbars(); }
    int GetRowHeight() const { return rowHeight; }
    
    void SetIndentSize(int size) { indentSize = size; }
    int GetIndentSize() const { return indentSize; }
    
    void SetSelectionMode(TreeSelectionMode mode);
    TreeSelectionMode GetSelectionMode() const { return selectionMode; }
    
    void SetLineStyle(TreeLineStyle style) { lineStyle = style; }
    TreeLineStyle GetLineStyle() const { return lineStyle; }
    
    void SetShowExpandButtons(bool show) { showExpandButtons = show; }
    bool GetShowExpandButtons() const { return showExpandButtons; }

    void SetShowFirstChildOnExpand(bool show) { showFirstChildOnExpand = show; }
    bool GetShowFirstChildOnExpand() const { return showFirstChildOnExpand; }

    void SetAutoExpandSelectedNode(bool expand) {
        autoExpandSelectedNode = expand;
    }
    bool GetAutoExpandSelectedNode() const { return autoExpandSelectedNode; }

    // ===== COLOR PROPERTIES =====
    void SetSelectionColor(const Color &color) { selectionColor = color; }
    void SetHoverColor(const Color &color) { hoverColor = color; }
    void SetLineColor(const Color &color) { lineColor = color; }
    void SetTextColor(const Color &color) { textColor = color; }

    // ===== DISPLAY MODE (Classic / Modern columns) =====
    // Switch between the single-text Classic layout and the columnar Modern layout.
    // Safe to toggle at runtime; triggers a redraw. Node data is shared between modes.
    void SetDisplayMode(TreeDisplayMode mode);
    TreeDisplayMode GetDisplayMode() const { return displayMode; }

    // Column geometry/colours used by Modern mode.
    void SetColumnStyle(const TreeColumnStyle& style) { columnStyle = style; RequestRedraw(); }
    const TreeColumnStyle& GetColumnStyle() const { return columnStyle; }

    // ===== SORTING =====
    // Persistent option: keep children alphabetically sorted as nodes are added.
    void SetAutoSortChildren(bool enable, bool ascending = true);
    bool GetAutoSortChildren() const { return autoSortChildren; }

    // Sort the whole tree by the given mode and remember it as the active sort
    // (re-applied by SortAllNodes()). Pass TreeSortMode::NoSort to leave order as-is.
    void SetSortMode(TreeSortMode mode, bool ascending = true);
    TreeSortMode GetSortMode() const { return sortMode; }
    bool GetSortAscending() const { return sortAscending; }

    // On-demand sort of a specified node's children (no-op if not found / null).
    void SortNodeChildren(const std::string& nodeId, bool recursive = false, bool ascending = true);
    void SortNodeChildren(TreeNode* node, bool recursive = false, bool ascending = true);

    // Convenience: sort the entire tree from the root, recursively.
    void SortAllNodes(bool ascending = true);

    // ===== SCROLLING =====
    void ScrollTo(TreeNode* node);
    void ScrollBy(int deltaY);
    
    // ===== EVENT HANDLING =====
    bool OnEvent(const UCEvent& event) override;
    
    // ===== RENDERING =====
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;

    // ===== LAYOUT (CSS Measure/Arrange) =====
    // TreeView is externally sized (explicit size or parent stretch); it has no
    // intrinsic size, so the base block measure is sufficient. We only hook
    // Arrange to recompute scrollbar geometry against the resolved finalBounds.
    void Arrange(const Rect2Df& finalRect, const CSSLayout::LayoutContext& ctx) override;

// ==== WINDOW PROPAGATION =====
    void SetWindow(UltraCanvasWindowBase* win) override;

private:

    // ===== SCROLLBAR MANAGEMENT =====
    void CreateScrollbar();
    void UpdateScrollbars();
    
    void ClampScrollOffset();
    
    int GetTotalVisibleHeight();
    
    int GetNodeDisplayY(TreeNode* node);
    
    TreeNode* GetNodeAtY(int y);
    
    void RenderNode(IRenderContext *ctx, TreeNode* node, int& currentY, int level, const Rect2Di& contentRect);

    // Modern-mode row painter: draws the Name/Type/Value columns (or a full-width
    // section-header bar when node->data.isGroupHeader). Called from RenderNode.
    void RenderNodeColumns(IRenderContext *ctx, TreeNode* node, int nodeY, int textX,
                           int rowLeft, int rowWidth);

    void ExpandNodeRecursive(TreeNode* node);
    void CollapseNodeRecursive(TreeNode* node);
    
    // ===== EVENT HANDLERS =====
    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseDoubleClick(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    void HandleKeyDown(const UCEvent& event);
    
    void NavigateUp();
    void NavigateDown();
    
    TreeNode* GetPreviousVisibleNode(TreeNode* current);
    TreeNode* GetNextVisibleNode(TreeNode* current);
    TreeNode* GetLastVisibleNode();

    // When a parent node (one that has children) is the bottom-most visible row,
    // nudge the scroll position down by a single row so the user can see that
    // more entries exist below it. Leaf nodes (no children) are left untouched.
    void ScrollDownIfLastVisibleParent(TreeNode* node);

    void ExpandFirstChildNode(TreeNode *node);
    void BuildVisibleNodeList(TreeNode* node, std::vector<TreeNode*>& list);
};

// ===== FACTORY FUNCTIONS =====
//std::shared_ptr<UltraCanvasTreeView> CreateTreeView(
//    const std::string& identifier, float x, float y, float w, float h) {
//    return std::make_shared<UltraCanvasTreeView>(identifier, x, y, w, h);
//}

// ===== CONVENIENCE BUILDER CLASS =====
class TreeViewBuilder {
private:
    std::shared_ptr<UltraCanvasTreeView> treeView;
    
public:
    TreeViewBuilder(const std::string& identifier, float x, float y, float w, float h) {
        treeView = std::make_shared<UltraCanvasTreeView>(identifier, x, y, w, h);
        //treeView = CreateTreeView(identifier, x, y, w, h);
    }
    
    TreeViewBuilder& SetRowHeight(int height) {
        treeView->SetRowHeight(height);
        return *this;
    }
    
    TreeViewBuilder& SetIndentSize(int size) {
        treeView->SetIndentSize(size);
        return *this;
    }
    
    TreeViewBuilder& SetSelectionMode(TreeSelectionMode mode) {
        treeView->SetSelectionMode(mode);
        return *this;
    }
    
    TreeViewBuilder& SetLineStyle(TreeLineStyle style) {
        treeView->SetLineStyle(style);
        return *this;
    }

    TreeViewBuilder& SetAutoSortChildren(bool enable, bool ascending = true) {
        treeView->SetAutoSortChildren(enable, ascending);
        return *this;
    }

    TreeViewBuilder& SetColors(const Color& bg, const Color& selection, const Color& hover, const Color& text) {
        treeView->SetBackgroundColor(bg);
        treeView->SetSelectionColor(selection);
        treeView->SetHoverColor(hover);
        treeView->SetTextColor(text);
        return *this;
    }
    
    TreeViewBuilder& OnNodeSelected(std::function<void(TreeNode*)> callback) {
        treeView->onNodeSelected = callback;
        return *this;
    }
    
    TreeViewBuilder& OnNodeDoubleClicked(std::function<void(TreeNode*)> callback) {
        treeView->onNodeDoubleClicked = callback;
        return *this;
    }
    
    TreeViewBuilder& OnNodeExpanded(std::function<void(TreeNode*)> callback) {
        treeView->onNodeExpanded = callback;
        return *this;
    }
    
    TreeViewBuilder& OnNodeCollapsed(std::function<void(TreeNode*)> callback) {
        treeView->onNodeCollapsed = callback;
        return *this;
    }
    
    TreeViewBuilder& OnNodeRightClicked(std::function<void(TreeNode*)> callback) {
        treeView->onNodeRightClicked = callback;
        return *this;
    }
    
    std::shared_ptr<UltraCanvasTreeView> Build() {
        return treeView;
    }
};

} // namespace UltraCanvas
