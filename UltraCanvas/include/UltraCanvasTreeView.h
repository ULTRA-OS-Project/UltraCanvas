// UltraCanvasTreeView.h
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
    
    TreeNode(const TreeNodeData& nodeData, TreeNode* parentNode = nullptr) 
        : data(nodeData), parent(parentNode) {
        state = TreeNodeState::Leaf;
        level = parent ? parent->level + 1 : 0;
        selected = false;
        hovered = false;
    }
    
    // ===== CHILD MANAGEMENT =====
    TreeNode* AddChild(const TreeNodeData& childData) {
        auto child = std::make_unique<TreeNode>(childData, this);
        TreeNode* childPtr = child.get();
        children.push_back(std::move(child));
        
        // Update state if this was a leaf node
        if (state == TreeNodeState::Leaf) {
            state = TreeNodeState::Collapsed;
        }
        
        return childPtr;
    }
    
    void RemoveChild(const std::string& nodeId) {
        children.erase(
            std::remove_if(children.begin(), children.end(),
                [&nodeId](const std::unique_ptr<TreeNode>& child) {
                    return child->data.nodeId == nodeId;
                }),
            children.end()
        );
        
        // Update state if no more children
        if (children.empty()) {
            state = TreeNodeState::Leaf;
        }
    }
    
    TreeNode* FindChild(const std::string& nodeId) {
        for (auto& child : children) {
            if (child->data.nodeId == nodeId) {
                return child.get();
            }
        }
        return nullptr;
    }
    
    TreeNode* FindDescendant(const std::string& nodeId) {
        if (data.nodeId == nodeId) {
            return this;
        }
        
        for (auto& child : children) {
            TreeNode* found = child->FindDescendant(nodeId);
            if (found) return found;
        }
        
        return nullptr;
    }
    
    // ===== STATE MANAGEMENT =====
    void Expand() {
        if (state == TreeNodeState::Collapsed) {
            state = TreeNodeState::Expanded;
        }
    }
    
    void Collapse() {
        if (state == TreeNodeState::Expanded) {
            state = TreeNodeState::Collapsed;
        }
    }
    
    void Toggle() {
        if (state == TreeNodeState::Collapsed) {
            Expand();
        } else if (state == TreeNodeState::Expanded) {
            Collapse();
        }
    }
    
    bool HasChildren() const {
        return !children.empty();
    }
    
    bool IsExpanded() const {
        return state == TreeNodeState::Expanded;
    }
    
    bool IsVisible() const {
        if (!data.visible) return false;
        if (!parent) return true; // Root is always visible if data.visible is true
        
        return parent->IsVisible() && parent->IsExpanded();
    }
    
    // ===== UTILITY METHODS =====
    int GetVisibleChildCount() const {
        if (state != TreeNodeState::Expanded) return 0;
        
        int count = 0;
        for (const auto& child : children) {
            if (child->data.visible) {
                count++;
                count += child->GetVisibleChildCount();
            }
        }
        return count;
    }
    
    std::vector<TreeNode*> GetVisibleChildren() {
        std::vector<TreeNode*> visible;
        if (state == TreeNodeState::Expanded) {
            for (auto& child : children) {
                if (child->data.visible) {
                    visible.push_back(child.get());
                    auto childVisible = child->GetVisibleChildren();
                    visible.insert(visible.end(), childVisible.begin(), childVisible.end());
                }
            }
        }
        return visible;
    }
};

// ===== TREE VIEW CLASS =====
class UltraCanvasTreeView : public UltraCanvasElement {
private:
    // ===== STANDARD ULTRACANVAS PROPERTIES =====
    std::string Identifier;          // [text] Element identifier
    long IdentifierID;              // [longint] Unique ID
    long x_pos;                     // [longint] X position
    long y_pos;                     // [longint] Y position  
    long width_size;                // [longint] Width
    long height_size;               // [longint] Height
    bool Active;                    // [boolean] Is element active
    bool Visible;                   // [boolean] Is element visible
    int MousePointer;               // [int] Mouse cursor type when hovering
    int MouseControls;              // [int] Mouse interaction type
    long ParentObject;              // [longint] Reference to parent
    long z_index;                   // [longint] Depth/layer order
    std::string Script;             // [text] Property description text
    std::vector<uint8_t> Cache;     // [image] Compressed bitmap cache
    
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
    Point2D lastMousePos;          // Last mouse position
    
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
                       long x, long y, long w, long h) {
        // Standard properties initialization
        Identifier = identifier;
        IdentifierID = id;
        x_pos = x;
        y_pos = y;
        width_size = w;
        height_size = h;
        Active = true;
        Visible = true;
        MousePointer = 0; // Standard cursor
        MouseControls = 2; // Input controls
        ParentObject = -1;
        z_index = 0;
        Script = "TreeView: " + identifier;
        
        // Tree view specific initialization
        rootNode = nullptr;
        selectionMode = TreeSelectionMode::Single;
        lineStyle = TreeLineStyle::Dotted;
        hoveredNode = nullptr;
        focusedNode = nullptr;
        
        // Visual defaults
        rowHeight = 20;
        indentSize = 16;
        iconSpacing = 4;
        textPadding = 2;
        showRootLines = true;
        showExpandButtons = true;
        
        // Color defaults
        backgroundColor = Colors::White;      // White
        selectionColor = Colors::Blue;       // Blue selection
        hoverColor = Color(0xE5,0xF3,0xFF);          // Light blue hover
        lineColor = Color(0x80,0x80,0x80);           // Gray lines
        textColor = Colors::Black;           // Black text
        
        // Scrolling defaults
        scrollOffsetY = 0;
        maxScrollY = 0;
        hasVerticalScrollbar = false;
        scrollbarWidth = 16;
        
        // Interaction state
        isDragging = false;
        draggedNode = nullptr;
    }
    
    // ===== TREE STRUCTURE MANAGEMENT =====
    TreeNode* SetRootNode(const TreeNodeData& rootData) {
        rootNode = std::make_unique<TreeNode>(rootData);
        UpdateScrollbars();
        return rootNode.get();
    }
    
    TreeNode* GetRootNode() const {
        return rootNode.get();
    }
    
    TreeNode* AddNode(const std::string& parentId, const TreeNodeData& nodeData) {
        if (!rootNode) {
            return SetRootNode(nodeData);
        }
        
        TreeNode* parent = rootNode->FindDescendant(parentId);
        if (parent) {
            TreeNode* newNode = parent->AddChild(nodeData);
            UpdateScrollbars();
            return newNode;
        }
        
        return nullptr;
    }
    
    void RemoveNode(const std::string& nodeId) {
        if (!rootNode) return;
        
        TreeNode* node = rootNode->FindDescendant(nodeId);
        if (node && node->parent) {
            node->parent->RemoveChild(nodeId);
            
            // Clear selection if removed node was selected
            selectedNodes.erase(
                std::remove(selectedNodes.begin(), selectedNodes.end(), node),
                selectedNodes.end()
            );
            
            if (hoveredNode == node) hoveredNode = nullptr;
            if (focusedNode == node) focusedNode = nullptr;
            
            UpdateScrollbars();
        }
    }
    
    TreeNode* FindNode(const std::string& nodeId) {
        return rootNode ? rootNode->FindDescendant(nodeId) : nullptr;
    }
    
    // ===== SELECTION MANAGEMENT =====
    void SelectNode(TreeNode* node, bool addToSelection = false) {
        if (!node || !node->data.enabled) return;
        
        if (selectionMode == TreeSelectionMode::NoSelection) return;
        
        if (selectionMode == TreeSelectionMode::Single || !addToSelection) {
            ClearSelection();
        }
        
        if (std::find(selectedNodes.begin(), selectedNodes.end(), node) == selectedNodes.end()) {
            selectedNodes.push_back(node);
            node->selected = true;
            
            if (onNodeSelected) {
                onNodeSelected(node);
            }
        }
    }
    
    void DeselectNode(TreeNode* node) {
        auto it = std::find(selectedNodes.begin(), selectedNodes.end(), node);
        if (it != selectedNodes.end()) {
            selectedNodes.erase(it);
            node->selected = false;
        }
    }
    
    void ClearSelection() {
        for (TreeNode* node : selectedNodes) {
            node->selected = false;
        }
        selectedNodes.clear();
    }
    
    const std::vector<TreeNode*>& GetSelectedNodes() const {
        return selectedNodes;
    }
    
    TreeNode* GetFirstSelectedNode() const {
        return selectedNodes.empty() ? nullptr : selectedNodes[0];
    }
    
    // ===== EXPANSION MANAGEMENT =====
    void ExpandNode(TreeNode* node) {
        if (node && node->HasChildren()) {
            node->Expand();
            UpdateScrollbars();
            
            if (onNodeExpanded) {
                onNodeExpanded(node);
            }
        }
    }
    
    void CollapseNode(TreeNode* node) {
        if (node && node->HasChildren()) {
            node->Collapse();
            UpdateScrollbars();
            
            if (onNodeCollapsed) {
                onNodeCollapsed(node);
            }
        }
    }
    
    void ExpandAll() {
        if (rootNode) {
            ExpandNodeRecursive(rootNode.get());
            UpdateScrollbars();
        }
    }
    
    void CollapseAll() {
        if (rootNode) {
            CollapseNodeRecursive(rootNode.get());
            UpdateScrollbars();
        }
    }
    
    // ===== VISUAL PROPERTIES =====
    void SetRowHeight(int height) { rowHeight = height; UpdateScrollbars(); }
    int GetRowHeight() const { return rowHeight; }
    
    void SetIndentSize(int size) { indentSize = size; }
    int GetIndentSize() const { return indentSize; }
    
    void SetSelectionMode(TreeSelectionMode mode) { 
        selectionMode = mode;
        if (mode == TreeSelectionMode::NoSelection) {
            ClearSelection();
        }
    }
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
    void ctx->SetTextColor(const Color &color) { textColor = color; }
    
    // ===== SCROLLING =====
    void ScrollTo(TreeNode* node) {
        if (!node) return;
        
        int nodeY = GetNodeDisplayY(node);
        if (nodeY < scrollOffsetY) {
            scrollOffsetY = nodeY;
        } else if (nodeY >= scrollOffsetY + height_size - rowHeight) {
            scrollOffsetY = nodeY - height_size + rowHeight;
        }
        
        ClampScrollOffset();
    }
    
    void ScrollBy(int deltaY) {
        scrollOffsetY += deltaY;
        ClampScrollOffset();
    }
    
    // ===== EVENT HANDLING =====
    bool OnEvent(const UCEvent& event) override {
        if (!Active || !Visible) return false;
        
        switch (event.type) {
            case UCEventType::MouseDown:
                HandleMouseDown(event);
                break;
            case UCEventType::MouseMove:
                HandleMouseMove(event);
                break;
            case UCEventType::MouseUp:
                HandleMouseUp(event);
                break;
            case UCEventType::MouseDoubleClick:
                HandleMouseDoubleClick(event);
                break;
            case UCEventType::MouseWheel:
                HandleMouseWheel(event);
                break;
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
            default:
                break;
        }
        return false;
    }
    
    // ===== RENDERING =====
    void Render() override {
        if (!Visible) return;
        
        // Draw background
        UltraCanvas::DrawFilledRect(Rect2D(x_pos, y_pos, width_size, height_size), backgroundColor);
        
        // Draw border
        UltraCanvas::DrawFilledRect(Rect2D(x_pos, y_pos, width_size, 1), Colors::Gray);
        UltraCanvas::DrawFilledRect(Rect2D(x_pos, y_pos + height_size - 1, width_size, 1), Colors::Gray);
        UltraCanvas::DrawFilledRect(Rect2D(x_pos, y_pos, 1, height_size), Colors::Gray);
        UltraCanvas::DrawFilledRect(Rect2D(x_pos + width_size - 1, y_pos, 1, height_size), Colors::Gray);
        
        if (rootNode) {
            int currentY = y_pos - scrollOffsetY;
            RenderNode(rootNode.get(), currentY, 0);
        }
        
        // Draw scrollbar if needed
        if (hasVerticalScrollbar) {
            RenderVerticalScrollbar();
        }
    }
    
private:

    // ===== HELPER METHODS =====
    void UpdateScrollbars() {
        if (!rootNode) {
            maxScrollY = 0;
            hasVerticalScrollbar = false;
            return;
        }
        
        int totalHeight = GetTotalVisibleHeight();
        maxScrollY = std::max((long int)0, totalHeight - height_size);
        hasVerticalScrollbar = maxScrollY > 0;
        
        ClampScrollOffset();
    }
    
    void ClampScrollOffset() {
        scrollOffsetY = std::max(0, std::min(scrollOffsetY, maxScrollY));
    }
    
    int GetTotalVisibleHeight() {
        if (!rootNode) return 0;
        return (1 + rootNode->GetVisibleChildCount()) * rowHeight;
    }
    
    int GetNodeDisplayY(TreeNode* node) {
        if (!rootNode || !node) return 0;
        
        int y = 0;
        std::function<bool(TreeNode*, int&)> findNodeY = [&](TreeNode* current, int& currentY) -> bool {
            if (current == node) {
                y = currentY;
                return true;
            }
            
            currentY += rowHeight;
            
            if (current->IsExpanded()) {
                for (auto& child : current->children) {
                    if (child->data.visible && findNodeY(child.get(), currentY)) {
                        return true;
                    }
                }
            }
            
            return false;
        };
        
        int currentY = 0;
        findNodeY(rootNode.get(), currentY);
        return y;
    }
    
    TreeNode* GetNodeAtY(int y) {
        if (!rootNode) return nullptr;
        
        int relativeY = y - y_pos + scrollOffsetY;
        int nodeIndex = relativeY / rowHeight;
        
        if (nodeIndex < 0) return nullptr;
        
        int currentIndex = 0;
        std::function<TreeNode*(TreeNode*)> findNode = [&](TreeNode* current) -> TreeNode* {
            if (currentIndex == nodeIndex) {
                return current;
            }
            
            currentIndex++;
            
            if (current->IsExpanded()) {
                for (auto& child : current->children) {
                    if (child->data.visible) {
                        TreeNode* found = findNode(child.get());
                        if (found) return found;
                    }
                }
            }
            
            return nullptr;
        };
        
        return findNode(rootNode.get());
    }
    
    void RenderNode(TreeNode* node, int& currentY, int level) {
        if (!node || !node->data.visible) return;
        
        // Skip if outside visible area
        if (currentY + rowHeight < y_pos || currentY > y_pos + height_size) {
            currentY += rowHeight;
            if (node->IsExpanded()) {
                for (auto& child : node->children) {
                    RenderNode(child.get(), currentY, level + 1);
                }
            }
            return;
        }
        
        int nodeX = x_pos + level * indentSize;
        int nodeY = currentY;
        
        // Draw node background
        Color bgColor = backgroundColor;
        if (node->selected) {
            bgColor = selectionColor;
        } else if (node->hovered) {
            bgColor = hoverColor;
        } else if (node->data.backgroundColor != Colors::Transparent) {
            bgColor = node->data.backgroundColor;
        }
        
        if (bgColor != backgroundColor) {
            UltraCanvas::DrawFilledRect(Rect2D(x_pos + 1, nodeY, width_size - 2, rowHeight), bgColor);
        }
        
        // Draw connecting lines
        if (lineStyle != TreeLineStyle::NoLine && level > 0) {
            // Draw horizontal line to parent
            // Implementation would draw line from parent to current node
        }
        
        // Draw expand/collapse button
        if (showExpandButtons && node->HasChildren()) {
            int buttonX = nodeX;
            int buttonY = nodeY + (rowHeight - 12) / 2;
            
            // Draw button background
            UltraCanvas::DrawFilledRect(Rect2D(buttonX, buttonY, 12, 12), Colors::LightGray);
            UltraCanvas::DrawFilledRect(Rect2D(buttonX, buttonY, 12, 1), Colors::Gray);
            UltraCanvas::DrawFilledRect(Rect2D(buttonX, buttonY + 11, 12, 1), Colors::Gray);
            UltraCanvas::DrawFilledRect(Rect2D(buttonX, buttonY, 1, 12), Colors::Gray);
            UltraCanvas::DrawFilledRect(Rect2D(buttonX + 11, buttonY, 1, 12), Colors::Gray);
            
            // Draw +/- symbol
            UltraCanvas::DrawFilledRect(Rect2D(buttonX + 3, buttonY + 5, 6, 2), Colors::Black);
            if (!node->IsExpanded()) {
                UltraCanvas::DrawFilledRect(Rect2D(buttonX + 5, buttonY + 3, 2, 6), Colors::Black);
            }
        }
        
        // Calculate text position
        int textX = nodeX + (showExpandButtons && node->HasChildren() ? 16 : 0) + textPadding;
        
        // Draw left icon
        if (node->data.leftIcon.visible && !node->data.leftIcon.iconPath.empty()) {
            ctx->DrawImage(node->data.leftIcon.iconPath.c_str(),
                textX, nodeY + (rowHeight - node->data.leftIcon.height) / 2,
                node->data.leftIcon.width, node->data.leftIcon.height);
            textX += node->data.leftIcon.width + iconSpacing;
        }
        
        // Draw text
        Color nodeTextColor = node->data.textColor != Colors::Black ? node->data.textColor : textColor;
        DrawTextWithBackground(node->data.text.c_str(), Point2D(textX, nodeY + rowHeight / 2 + 4), nodeTextColor);
        
        // Draw right icon
        if (node->data.rightIcon.visible && !node->data.rightIcon.iconPath.empty()) {
            int rightIconX = x_pos + width_size - node->data.rightIcon.width - textPadding;
            if (hasVerticalScrollbar) {
                rightIconX -= scrollbarWidth;
            }
            
            ctx->DrawImage(node->data.rightIcon.iconPath.c_str(),
                rightIconX, nodeY + (rowHeight - node->data.rightIcon.height) / 2,
                node->data.rightIcon.width, node->data.rightIcon.height);
        }
        
        currentY += rowHeight;
        
        // Render children if expanded
        if (node->IsExpanded()) {
            for (auto& child : node->children) {
                RenderNode(child.get(), currentY, level + 1);
            }
        }
    }
    
    void RenderVerticalScrollbar() {
        int scrollbarX = x_pos + width_size - scrollbarWidth;
        
        // Scrollbar background
        UltraCanvas::DrawFilledRect(Rect2D(scrollbarX, y_pos, scrollbarWidth, height_size), Colors::LightGray);
        
        // Scrollbar thumb
        if (maxScrollY > 0) {
            int thumbHeight = std::max((long int)20, (height_size * height_size) / (height_size + maxScrollY));
            int thumbY = y_pos + (scrollOffsetY * (height_size - thumbHeight)) / maxScrollY;
            
            UltraCanvas::DrawFilledRect(Rect2D(scrollbarX + 2, thumbY, scrollbarWidth - 4, thumbHeight), Colors::LightGray);
        }
    }
    
    void ExpandNodeRecursive(TreeNode* node) {
        if (node && node->HasChildren()) {
            node->Expand();
            for (auto& child : node->children) {
                ExpandNodeRecursive(child.get());
            }
        }
    }
    
    void CollapseNodeRecursive(TreeNode* node) {
        if (node && node->HasChildren()) {
            node->Collapse();
            for (auto& child : node->children) {
                CollapseNodeRecursive(child.get());
            }
        }
    }
    
    // ===== EVENT HANDLERS =====
    void HandleMouseDown(const UCEvent& event) {
        if (!Contains(event.x, event.y)) return;
        
        lastMousePos = Point2D(event.x, event.y);
        
        // Check if clicking on scrollbar
        if (hasVerticalScrollbar && event.x >= x_pos + width_size - scrollbarWidth) {
            isDragging = true;
            return;
        }
        
        TreeNode* clickedNode = GetNodeAtY(event.y);
        if (clickedNode) {
            int nodeX = x_pos + clickedNode->level * indentSize;
            
            // Check if clicking on expand/collapse button
            if (showExpandButtons && clickedNode->HasChildren() && 
                event.x >= nodeX && event.x <= nodeX + 12) {
                clickedNode->Toggle();
                UpdateScrollbars();
                
                if (clickedNode->IsExpanded() && onNodeExpanded) {
                    onNodeExpanded(clickedNode);
                } else if (!clickedNode->IsExpanded() && onNodeCollapsed) {
                    onNodeCollapsed(clickedNode);
                }
                return;
            }
            
            // Regular node selection
            bool addToSelection = (event.ctrl && selectionMode == TreeSelectionMode::Multiple);
            SelectNode(clickedNode, addToSelection);
            focusedNode = clickedNode;
        } else {
            ClearSelection();
            focusedNode = nullptr;
        }
    }
    
    void HandleMouseMove(const UCEvent& event) {
        if (isDragging && hasVerticalScrollbar) {
            // Handle scrollbar dragging
            float deltaY = event.y - lastMousePos.y;
            float scrollRatio = deltaY / (height_size - 20); // 20 = min thumb height
            scrollOffsetY += (int)(scrollRatio * maxScrollY);
            ClampScrollOffset();
            lastMousePos = Point2D(event.x, event.y);
            return;
        }
        
        // Update hover state
        TreeNode* newHovered = GetNodeAtY(event.y);
        if (newHovered != hoveredNode) {
            if (hoveredNode) hoveredNode->hovered = false;
            hoveredNode = newHovered;
            if (hoveredNode) hoveredNode->hovered = true;
        }
    }
    
    void HandleMouseUp(const UCEvent& event) {
        isDragging = false;
        
        // Handle right-click context menu
        if (event.x > 0) { // Assuming right-click has positive x
            TreeNode* rightClickedNode = GetNodeAtY(event.y);
            if (rightClickedNode && onNodeRightClicked) {
                onNodeRightClicked(rightClickedNode);
            }
        }
    }
    
    void HandleMouseDoubleClick(const UCEvent& event) {
        TreeNode* doubleClickedNode = GetNodeAtY(event.y);
        if (doubleClickedNode) {
            if (doubleClickedNode->HasChildren()) {
                doubleClickedNode->Toggle();
                UpdateScrollbars();
            }
            
            if (onNodeDoubleClicked) {
                onNodeDoubleClicked(doubleClickedNode);
            }
        }
    }
    
    void HandleMouseWheel(const UCEvent& event) {
        int scrollAmount = event.wheelDelta * rowHeight * 3; // Scroll 3 rows per wheel notch
        ScrollBy(-scrollAmount);
    }
    
    void HandleKeyDown(const UCEvent& event) {
        if (!focusedNode) return;
        
        switch (event.nativeKeyCode) {
            case 38: // Up arrow
                NavigateUp();
                break;
            case 40: // Down arrow
                NavigateDown();
                break;
            case 37: // Left arrow
                if (focusedNode->IsExpanded()) {
                    CollapseNode(focusedNode);
                } else if (focusedNode->parent) {
                    SelectNode(focusedNode->parent);
                    focusedNode = focusedNode->parent;
                }
                break;
            case 39: // Right arrow
                if (focusedNode->HasChildren()) {
                    if (!focusedNode->IsExpanded()) {
                        ExpandNode(focusedNode);
                    } else {
                        NavigateDown();
                    }
                }
                break;
            case 13: // Enter
                if (focusedNode->HasChildren()) {
                    focusedNode->Toggle();
                    UpdateScrollbars();
                }
                if (onNodeDoubleClicked) {
                    onNodeDoubleClicked(focusedNode);
                }
                break;
            case 32: // Space
                SelectNode(focusedNode, event.ctrl && selectionMode == TreeSelectionMode::Multiple);
                break;
            case 36: // Home
                if (rootNode) {
                    SelectNode(rootNode.get());
                    focusedNode = rootNode.get();
                    ScrollTo(focusedNode);
                }
                break;
            case 35: // End
                {
                    TreeNode* lastVisible = GetLastVisibleNode();
                    if (lastVisible) {
                        SelectNode(lastVisible);
                        focusedNode = lastVisible;
                        ScrollTo(focusedNode);
                    }
                }
                break;
        }
    }
    
    void NavigateUp() {
        if (!focusedNode) return;
        
        TreeNode* prevNode = GetPreviousVisibleNode(focusedNode);
        if (prevNode) {
            SelectNode(prevNode);
            focusedNode = prevNode;
            ScrollTo(focusedNode);
        }
    }
    
    void NavigateDown() {
        if (!focusedNode) return;
        
        TreeNode* nextNode = GetNextVisibleNode(focusedNode);
        if (nextNode) {
            SelectNode(nextNode);
            focusedNode = nextNode;
            ScrollTo(focusedNode);
        }
    }
    
    TreeNode* GetPreviousVisibleNode(TreeNode* current) {
        if (!current || !rootNode) return nullptr;
        
        // Build list of all visible nodes
        std::vector<TreeNode*> visibleNodes;
        BuildVisibleNodeList(rootNode.get(), visibleNodes);
        
        // Find current node and return previous
        for (size_t i = 1; i < visibleNodes.size(); i++) {
            if (visibleNodes[i] == current) {
                return visibleNodes[i - 1];
            }
        }
        
        return nullptr;
    }
    
    TreeNode* GetNextVisibleNode(TreeNode* current) {
        if (!current || !rootNode) return nullptr;
        
        // Build list of all visible nodes
        std::vector<TreeNode*> visibleNodes;
        BuildVisibleNodeList(rootNode.get(), visibleNodes);
        
        // Find current node and return next
        for (size_t i = 0; i < visibleNodes.size() - 1; i++) {
            if (visibleNodes[i] == current) {
                return visibleNodes[i + 1];
            }
        }
        
        return nullptr;
    }
    
    TreeNode* GetLastVisibleNode() {
        if (!rootNode) return nullptr;
        
        std::vector<TreeNode*> visibleNodes;
        BuildVisibleNodeList(rootNode.get(), visibleNodes);
        
        return visibleNodes.empty() ? nullptr : visibleNodes.back();
    }
    
    void BuildVisibleNodeList(TreeNode* node, std::vector<TreeNode*>& list) {
        if (!node || !node->data.visible) return;
        
        list.push_back(node);
        
        if (node->IsExpanded()) {
            for (auto& child : node->children) {
                BuildVisibleNodeList(child.get(), list);
            }
        }
    }
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
        treeView->ctx->SetTextColor(text);
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

/*
=== USAGE EXAMPLES ===

// Basic tree view creation
auto treeView = CreateTreeView("FileExplorer", 2001, 10, 10, 300, 400);

// Set up root node
TreeNodeData rootData("root", "My Computer");
rootData.leftIcon = TreeNodeIcon("icons/computer.png", 16, 16);
TreeNode* root = treeView->SetRootNode(rootData);

// Add child nodes with icons
TreeNodeData driveC("drive_c", "Local Disk (C:)");
driveC.leftIcon = TreeNodeIcon("icons/drive.png", 16, 16);
driveC.rightIcon = TreeNodeIcon("icons/lock.png", 12, 12);
treeView->AddNode("root", driveC);

TreeNodeData documents("documents", "Documents");
documents.leftIcon = TreeNodeIcon("icons/folder.png", 16, 16);
treeView->AddNode("drive_c", documents);

TreeNodeData file1("file1", "Important.txt");
file1.leftIcon = TreeNodeIcon("icons/text.png", 16, 16);
file1.rightIcon = TreeNodeIcon("icons/star.png", 12, 12);
treeView->AddNode("documents", file1);

// Configure tree view
treeView->SetRowHeight(22);
treeView->SetSelectionMode(TreeSelectionMode::Single);
treeView->SetLineStyle(TreeLineStyle::Dotted);

// Set up event handlers
treeView->onNodeSelected = [](TreeNode* node) {
    std::cout << "Selected: " << node->data.text << std::endl;
};

treeView->onNodeDoubleClicked = [](TreeNode* node) {
    std::cout << "Double-clicked: " << node->data.text << std::endl;
};

// Using builder pattern
auto advancedTreeView = TreeViewBuilder("AdvancedTree", 2002, 320, 10, 300, 400)
    .SetRowHeight(24)
    .SetIndentSize(20)
    .SetSelectionMode(TreeSelectionMode::Multiple)
    .SetLineStyle(TreeLineStyle::Solid)
    .SetColors(0xFFFFFFFF, 0xFF316AC5, 0xFFE5F3FF, 0xFF000000)
    .OnNodeSelected([](TreeNode* node) {
        std::cout << "Advanced selected: " << node->data.text << std::endl;
    })
    .OnNodeRightClicked([](TreeNode* node) {
        std::cout << "Right-clicked: " << node->data.text << std::endl;
    })
    .Build();

// Add to window
window->AddElement(treeView.get());
window->AddElement(advancedTreeView.get());
*/