// include/UltraCanvasTreeView.h
// Hierarchical tree view with icons and text for each row
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

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
    Color textColor = Colors::Black;     // Text color (ARGB)
    Color backgroundColor = Colors::Transparent; // Background color (transparent by default)
    std::string tooltip;          // Tooltip text
    void* userData = nullptr;     // Custom user data
    
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
    
    // Colors
    Color backgroundColor;       // Tree background color
    Color selectionColor;       // Selected row background
    Color hoverColor;           // Hovered row background
    Color lineColor;            // Connecting line color
    Color textColor;            // Default text color
    
    // Scrolling
    int scrollOffsetY;             // Vertical scroll offset
    int maxScrollY;                // Maximum scroll value
    bool hasVerticalScrollbar;     // Show vertical scrollbar
    int scrollbarWidth;            // Width of scrollbar
    
    // Interaction state
    bool isDragging;               // Currently dragging scrollbar
    TreeNode* draggedNode;         // Node being dragged (for drag & drop)
    Point2Di lastMousePos;          // Last mouse position
    
public:
    // ===== EVENTS AND CALLBACKS =====
    std::function<void(TreeNode*)> onNodeSelected;
    std::function<void(TreeNode*)> onNodeDoubleClicked;
    std::function<void(TreeNode*)> onNodeExpanded;
    std::function<void(TreeNode*)> onNodeCollapsed;
    std::function<void(TreeNode*, TreeNode*)> onNodeDragDrop; // dragged, target
    std::function<void(TreeNode*)> onNodeRightClicked;
    
    // ===== CONSTRUCTOR =====
    UltraCanvasTreeView(const std::string& identifier, long id, 
                       int x, int y, int w, int h);
    
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
    
    // ===== COLOR PROPERTIES =====
    void SetBackgroundColor(const Color &color) { backgroundColor = color; }
    void SetSelectionColor(const Color &color) { selectionColor = color; }
    void SetHoverColor(const Color &color) { hoverColor = color; }
    void SetLineColor(const Color &color) { lineColor = color; }
    void SetTextColor(const Color &color) { textColor = color; }
    
    // ===== SCROLLING =====
    void ScrollTo(TreeNode* node);
    void ScrollBy(int deltaY);
    
    // ===== EVENT HANDLING =====
    bool OnEvent(const UCEvent& event) override;
    
    // ===== RENDERING =====
    void Render() override;
    
private:

    // ===== HELPER METHODS =====
    void UpdateScrollbars();
    
    void ClampScrollOffset();
    
    int GetTotalVisibleHeight();
    
    int GetNodeDisplayY(TreeNode* node);
    
    TreeNode* GetNodeAtY(int y);
    
    void RenderNode(IRenderContext *ctx, TreeNode* node, int& currentY, int level);
    void RenderVerticalScrollbar(IRenderContext* ctx);
    
    void ExpandNodeRecursive(TreeNode* node);
    void CollapseNodeRecursive(TreeNode* node);
    
    // ===== EVENT HANDLERS =====
    void HandleMouseDown(const UCEvent& event);
    void HandleMouseMove(const UCEvent& event);
    void HandleMouseUp(const UCEvent& event);
    void HandleMouseDoubleClick(const UCEvent& event);
    void HandleMouseWheel(const UCEvent& event);
    void HandleKeyDown(const UCEvent& event);
    
    void NavigateUp();
    void NavigateDown();
    
    TreeNode* GetPreviousVisibleNode(TreeNode* current);
    TreeNode* GetNextVisibleNode(TreeNode* current);
    TreeNode* GetLastVisibleNode();
    
    void BuildVisibleNodeList(TreeNode* node, std::vector<TreeNode*>& list);
};

// ===== FACTORY FUNCTIONS =====
//std::shared_ptr<UltraCanvasTreeView> CreateTreeView(
//    const std::string& identifier, long id, long x, long y, long w, long h) {
//    return std::make_shared<UltraCanvasTreeView>(identifier, id, x, y, w, h);
//}

// ===== CONVENIENCE BUILDER CLASS =====
class TreeViewBuilder {
private:
    std::shared_ptr<UltraCanvasTreeView> treeView;
    
public:
    TreeViewBuilder(const std::string& identifier, long id, long x, long y, long w, long h) {
        treeView = std::make_shared<UltraCanvasTreeView>(identifier, id, x, y, w, h);
        //treeView = CreateTreeView(identifier, id, x, y, w, h);
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
