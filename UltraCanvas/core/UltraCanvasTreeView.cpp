// core/UltraCanvasTreeView.cpp
// Hierarchical tree view with icons and text for each row
#include "UltraCanvasTreeView.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

namespace UltraCanvas {
    TreeNode::TreeNode(const TreeNodeData &nodeData, TreeNode *parentNode)
            : data(nodeData), parent(parentNode) {
        state = TreeNodeState::Leaf;
        level = parent ? parent->level + 1 : 0;
        selected = false;
        hovered = false;
    }

    TreeNode *TreeNode::AddChild(const TreeNodeData &childData) {
        auto child = std::make_unique<TreeNode>(childData, this);
        TreeNode* childPtr = child.get();
        children.push_back(std::move(child));

        // Update state if this was a leaf node
        if (state == TreeNodeState::Leaf) {
            state = TreeNodeState::Collapsed;
        }

        return childPtr;
    }

    void TreeNode::RemoveChild(const std::string &nodeId) {
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

    TreeNode *TreeNode::FindChild(const std::string &nodeId) {
        for (auto& child : children) {
            if (child->data.nodeId == nodeId) {
                return child.get();
            }
        }
        return nullptr;
    }

    TreeNode *TreeNode::FindDescendant(const std::string &nodeId) {
        if (data.nodeId == nodeId) {
            return this;
        }

        for (auto& child : children) {
            TreeNode* found = child->FindDescendant(nodeId);
            if (found) return found;
        }

        return nullptr;
    }

    void TreeNode::Expand() {
        if (state == TreeNodeState::Collapsed) {
            state = TreeNodeState::Expanded;
        }
    }

    void TreeNode::Collapse() {
        if (state == TreeNodeState::Expanded) {
            state = TreeNodeState::Collapsed;
        }
    }

    void TreeNode::Toggle() {
        if (state == TreeNodeState::Collapsed) {
            Expand();
        } else if (state == TreeNodeState::Expanded) {
            Collapse();
        }
    }

    bool TreeNode::IsVisible() const {
        if (!data.visible) return false;
        if (!parent) return true; // Root is always visible if data.visible is true

        return parent->IsVisible() && parent->IsExpanded();
    }

    int TreeNode::GetVisibleChildCount() const {
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

    std::vector<TreeNode *> TreeNode::GetVisibleChildren() {
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


    /* UltraCanvasTreeView */

    UltraCanvasTreeView::UltraCanvasTreeView(const std::string &identifier, long id, int x, int y, int w, int h) :
            UltraCanvasElement(identifier, id, x,y,w,h)
    {

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
        textPadding = 8;
        showRootLines = true;
        showExpandButtons = true;

        // Color defaults
        backgroundColor = Colors::White;      // White
        selectionColor = Colors::Selection;       // Blue selection
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

    TreeNode *UltraCanvasTreeView::SetRootNode(const TreeNodeData &rootData) {
        rootNode = std::make_unique<TreeNode>(rootData);
        UpdateScrollbars();
        return rootNode.get();
    }

    TreeNode *UltraCanvasTreeView::AddNode(const std::string &parentId, const TreeNodeData &nodeData) {
        if (!rootNode) {
            return SetRootNode(nodeData);
        }

        TreeNode* parent = rootNode->FindDescendant(parentId);
        if (parent) {
            TreeNode* newNode = parent->AddChild(nodeData);
            UpdateScrollbars();
        }

        return nullptr;
    }

    void UltraCanvasTreeView::RemoveNode(const std::string &nodeId) {
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

    TreeNode *UltraCanvasTreeView::FindNode(const std::string &nodeId) {
        return rootNode ? rootNode->FindDescendant(nodeId) : nullptr;
    }

    void UltraCanvasTreeView::SelectNode(TreeNode *node, bool addToSelection) {
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
            RequestRedraw();
        }
    }

    void UltraCanvasTreeView::DeselectNode(TreeNode *node) {
        auto it = std::find(selectedNodes.begin(), selectedNodes.end(), node);
        if (it != selectedNodes.end()) {
            selectedNodes.erase(it);
            node->selected = false;
        }
        RequestRedraw();
    }

    void UltraCanvasTreeView::ClearSelection() {
        for (TreeNode* node : selectedNodes) {
            node->selected = false;
        }
        selectedNodes.clear();
        RequestRedraw();
    }

    void UltraCanvasTreeView::ExpandNode(TreeNode *node) {
        if (node && node->HasChildren()) {
            node->Expand();
            UpdateScrollbars();

            if (onNodeExpanded) {
                onNodeExpanded(node);
            }
        }
    }

    void UltraCanvasTreeView::CollapseNode(TreeNode *node) {
        if (node && node->HasChildren()) {
            node->Collapse();
            UpdateScrollbars();

            if (onNodeCollapsed) {
                onNodeCollapsed(node);
            }
        }
    }

    void UltraCanvasTreeView::ExpandAll() {
        if (rootNode) {
            ExpandNodeRecursive(rootNode.get());
            UpdateScrollbars();
        }
    }

    void UltraCanvasTreeView::CollapseAll() {
        if (rootNode) {
            CollapseNodeRecursive(rootNode.get());
            UpdateScrollbars();
        }
    }

    void UltraCanvasTreeView::SetSelectionMode(TreeSelectionMode mode) {
        selectionMode = mode;
        if (mode == TreeSelectionMode::NoSelection) {
            ClearSelection();
            RequestRedraw();
        }
    }

    void UltraCanvasTreeView::ScrollTo(TreeNode *node) {
        if (!node) return;

        int nodeY = GetNodeDisplayY(node);
        if (nodeY < scrollOffsetY) {
            scrollOffsetY = nodeY;
        } else if (nodeY >= scrollOffsetY + GetHeight() - rowHeight) {
            scrollOffsetY = nodeY - GetHeight() + rowHeight;
        }

        ClampScrollOffset();
        RequestRedraw();
    }

    void UltraCanvasTreeView::ScrollBy(int deltaY) {
        scrollOffsetY += deltaY;
        ClampScrollOffset();
        RequestRedraw();
    }

    bool UltraCanvasTreeView::OnEvent(const UCEvent &event) {
        if (!IsActive() || !IsVisible()) return false;

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

    void UltraCanvasTreeView::Render() {
        if (!IsVisible()) return;
        auto ctx = GetRenderContext();

        // Draw background / border
        ctx->DrawFilledRectangle(GetBounds(), backgroundColor, Colors::Gray);

        if (rootNode) {
            int currentY = GetY() - scrollOffsetY;
            RenderNode(ctx, rootNode.get(), currentY, 0);
        }

        // Draw scrollbar if needed
        if (hasVerticalScrollbar) {
            RenderVerticalScrollbar(ctx);
        }
    }

    void UltraCanvasTreeView::UpdateScrollbars() {
        if (!rootNode) {
            maxScrollY = 0;
            hasVerticalScrollbar = false;
            return;
        }

        int totalHeight = GetTotalVisibleHeight();
        maxScrollY = std::max(0, totalHeight - GetHeight());
        hasVerticalScrollbar = maxScrollY > 0;

        ClampScrollOffset();
        RequestRedraw();
    }

    void UltraCanvasTreeView::ClampScrollOffset() {
        scrollOffsetY = std::max(0, std::min(scrollOffsetY, maxScrollY));
    }

    int UltraCanvasTreeView::GetTotalVisibleHeight() {
        if (!rootNode) return 0;
        return (1 + rootNode->GetVisibleChildCount()) * rowHeight;
    }

    int UltraCanvasTreeView::GetNodeDisplayY(TreeNode *node) {
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

    TreeNode *UltraCanvasTreeView::GetNodeAtY(int y) {
        if (!rootNode) return nullptr;

        int relativeY = y - GetY() + scrollOffsetY;
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

    void UltraCanvasTreeView::RenderNode(IRenderContext *ctx, TreeNode *node, int &currentY, int level) {
        if (!node || !node->data.visible) return;

        // Skip if outside visible area
        if (currentY + rowHeight < GetY() || currentY > GetY() + GetHeight()) {
            currentY += rowHeight;
            if (node->IsExpanded()) {
                for (auto& child : node->children) {
                    RenderNode(ctx, child.get(), currentY, level + 1);
                }
            }
            return;
        }

        int nodeX = GetX() + level * indentSize;
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
            ctx->DrawFilledRectangle(Rect2Di(GetX() + 1, nodeY, GetWidth() - 2, rowHeight), bgColor);
        }

        // Draw connecting lines
        if (lineStyle != TreeLineStyle::NoLine && level > 0) {
            // Draw horizontal line to parent
            // Implementation would draw line from parent to current node
        }

        // Draw expand/collapse button
        if (showExpandButtons && node->HasChildren()) {
            int buttonX = nodeX + 6;
            int buttonY = nodeY + (rowHeight - 12) / 2;

            // Draw button background
            ctx->DrawFilledRectangle(Rect2Di(buttonX, buttonY, 12, 12), Colors::LightGray, Colors::Gray);

            // Draw +/- symbol
            ctx->DrawFilledRectangle(Rect2Di(buttonX + 3, buttonY + 5, 6, 2), Colors::Black);
            if (!node->IsExpanded()) {
                ctx->DrawFilledRectangle(Rect2Di(buttonX + 5, buttonY + 3, 2, 6), Colors::Black);
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
        ctx->DrawTextWithBackground(node->data.text.c_str(), Point2Df(textX, nodeY + 2), nodeTextColor);

        // Draw right icon
        if (node->data.rightIcon.visible && !node->data.rightIcon.iconPath.empty()) {
            int rightIconX = GetX() + GetWidth() - node->data.rightIcon.width - textPadding;
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
                RenderNode(ctx, child.get(), currentY, level + 1);
            }
        }
    }

    void UltraCanvasTreeView::RenderVerticalScrollbar(IRenderContext *ctx) {
        int scrollbarX = GetX() + GetWidth() - scrollbarWidth;

        // Scrollbar background
        ctx->DrawFilledRectangle(Rect2Di(scrollbarX, GetY(), scrollbarWidth, GetHeight()), Colors::LightGray);

        // Scrollbar thumb
        if (maxScrollY > 0) {
            int thumbHeight = std::max(20, (GetHeight() * GetHeight()) / (GetHeight() + maxScrollY));
            int thumbY = GetY() + (scrollOffsetY * (GetHeight() - thumbHeight)) / maxScrollY;

            ctx->DrawFilledRectangle(Rect2Di(scrollbarX + 2, thumbY, scrollbarWidth - 4, thumbHeight), Colors::LightGray);
        }
    }

    void UltraCanvasTreeView::ExpandNodeRecursive(TreeNode *node) {
        if (node && node->HasChildren()) {
            node->Expand();
            for (auto& child : node->children) {
                ExpandNodeRecursive(child.get());
            }
        }
    }

    void UltraCanvasTreeView::CollapseNodeRecursive(TreeNode *node) {
        if (node && node->HasChildren()) {
            node->Collapse();
            for (auto& child : node->children) {
                CollapseNodeRecursive(child.get());
            }
        }
    }

    void UltraCanvasTreeView::HandleMouseDown(const UCEvent &event) {
        if (!Contains(event.x, event.y)) return;

        lastMousePos = Point2Di(event.x, event.y);

        // Check if clicking on scrollbar
        if (hasVerticalScrollbar && event.x >= GetX() + GetWidth() - scrollbarWidth) {
            isDragging = true;
            return;
        }

        TreeNode* clickedNode = GetNodeAtY(event.y);
        if (clickedNode) {
            int nodeX = GetX() + clickedNode->level * indentSize;

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

    void UltraCanvasTreeView::HandleMouseMove(const UCEvent &event) {
        if (isDragging && hasVerticalScrollbar) {
            // Handle scrollbar dragging
            float deltaY = event.y - lastMousePos.y;
            float scrollRatio = deltaY / (GetHeight() - 20); // 20 = min thumb height
            scrollOffsetY += (int)(scrollRatio * maxScrollY);
            ClampScrollOffset();
            lastMousePos = Point2Di(event.x, event.y);
            return;
        }

        // Update hover state
        TreeNode* newHovered = GetNodeAtY(event.y);
        if (newHovered != hoveredNode) {
            if (hoveredNode) hoveredNode->hovered = false;
            hoveredNode = newHovered;
            if (hoveredNode) hoveredNode->hovered = true;
            RequestRedraw();
        }
    }

    void UltraCanvasTreeView::HandleMouseUp(const UCEvent &event) {
        isDragging = false;

        // Handle right-click context menu
        if (event.x > 0) { // Assuming right-click has positive x
            TreeNode* rightClickedNode = GetNodeAtY(event.y);
            if (rightClickedNode && onNodeRightClicked) {
                onNodeRightClicked(rightClickedNode);
            }
        }
    }

    void UltraCanvasTreeView::HandleMouseDoubleClick(const UCEvent &event) {
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

    void UltraCanvasTreeView::HandleMouseWheel(const UCEvent &event) {
        int scrollAmount = event.wheelDelta * rowHeight * 3; // Scroll 3 rows per wheel notch
        ScrollBy(-scrollAmount);
    }

    void UltraCanvasTreeView::HandleKeyDown(const UCEvent &event) {
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

    void UltraCanvasTreeView::NavigateUp() {
        if (!focusedNode) return;

        TreeNode* prevNode = GetPreviousVisibleNode(focusedNode);
        if (prevNode) {
            SelectNode(prevNode);
            focusedNode = prevNode;
            ScrollTo(focusedNode);
        }
    }

    void UltraCanvasTreeView::NavigateDown() {
        if (!focusedNode) return;

        TreeNode* nextNode = GetNextVisibleNode(focusedNode);
        if (nextNode) {
            SelectNode(nextNode);
            focusedNode = nextNode;
            ScrollTo(focusedNode);
        }
    }

    TreeNode *UltraCanvasTreeView::GetPreviousVisibleNode(TreeNode *current) {
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

    TreeNode *UltraCanvasTreeView::GetLastVisibleNode() {
        if (!rootNode) return nullptr;

        std::vector<TreeNode*> visibleNodes;
        BuildVisibleNodeList(rootNode.get(), visibleNodes);

        return visibleNodes.empty() ? nullptr : visibleNodes.back();
    }

    TreeNode *UltraCanvasTreeView::GetNextVisibleNode(TreeNode *current) {
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

    void UltraCanvasTreeView::BuildVisibleNodeList(TreeNode *node, std::vector<TreeNode *> &list) {
        if (!node || !node->data.visible) return;

        list.push_back(node);

        if (node->IsExpanded()) {
            for (auto& child : node->children) {
                BuildVisibleNodeList(child.get(), list);
            }
        }
    }
}