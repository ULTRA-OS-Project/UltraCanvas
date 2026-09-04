// core/UltraCanvasTreeView.cpp
// Hierarchical tree view with icons and text for each row
// Last Modified: 2026-07-20
#include "UltraCanvasTreeView.h"
#include "UltraCanvasApplication.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <cctype>

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
        TreeNode *childPtr = child.get();
        children.push_back(std::move(child));

        // Update state if this was a leaf node, restoring the expanded state
        // if the node only became a leaf by having all its children removed
        // while expanded (e.g. a lazy-load placeholder being swapped out).
        if (state == TreeNodeState::Leaf) {
            state = wasExpandedBeforeEmptied ? TreeNodeState::Expanded
                                             : TreeNodeState::Collapsed;
        }
        wasExpandedBeforeEmptied = false;

        return childPtr;
    }

    void TreeNode::RemoveChild(const std::string &nodeId) {
        children.erase(
                std::remove_if(children.begin(), children.end(),
                               [&nodeId](const std::unique_ptr<TreeNode> &child) {
                                   return child->data.nodeId == nodeId;
                               }),
                children.end()
        );

        // Update state if no more children. Remember an expanded state so it
        // survives a transient empty phase (see wasExpandedBeforeEmptied).
        if (children.empty() && state != TreeNodeState::Leaf) {
            wasExpandedBeforeEmptied = (state == TreeNodeState::Expanded);
            state = TreeNodeState::Leaf;
        }
    }

    TreeNode *TreeNode::FindChild(const std::string &nodeId) {
        for (auto &child: children) {
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

        for (auto &child: children) {
            TreeNode *found = child->FindDescendant(nodeId);
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
        for (const auto &child: children) {
            if (child->data.visible) {
                count++;
                count += child->GetVisibleChildCount();
            }
        }
        return count;
    }

    std::vector<TreeNode *> TreeNode::GetVisibleChildren() {
        std::vector<TreeNode *> visible;
        if (state == TreeNodeState::Expanded) {
            for (auto &child: children) {
                if (child->data.visible) {
                    visible.push_back(child.get());
                    auto childVisible = child->GetVisibleChildren();
                    visible.insert(visible.end(), childVisible.begin(), childVisible.end());
                }
            }
        }
        return visible;
    }

    namespace {
        // Case-insensitive lexicographic less-than (ASCII), matching the
        // std::tolower idiom used elsewhere in the framework.
        bool CaseInsensitiveTextLess(const std::string &a, const std::string &b) {
            return std::lexicographical_compare(
                    a.begin(), a.end(), b.begin(), b.end(),
                    [](unsigned char c1, unsigned char c2) {
                        return std::tolower(c1) < std::tolower(c2);
                    });
        }
    }

    void TreeNode::SortChildNodes(bool recursive, bool ascending) {
        std::sort(children.begin(), children.end(),
                  [ascending](const std::unique_ptr<TreeNode> &a,
                              const std::unique_ptr<TreeNode> &b) {
                      return ascending
                             ? CaseInsensitiveTextLess(a->data.text, b->data.text)
                             : CaseInsensitiveTextLess(b->data.text, a->data.text);
                  });
        if (recursive) {
            for (auto &child: children) {
                child->SortChildNodes(true, ascending);
            }
        }
    }

    void TreeNode::SortChildNodes(TreeSortMode mode, bool recursive, bool ascending) {
        if (mode == TreeSortMode::Alphabetic) {
            SortChildNodes(recursive, ascending);
            return;
        }
        if (mode == TreeSortMode::LastAccess) {
            std::sort(children.begin(), children.end(),
                      [ascending](const std::unique_ptr<TreeNode> &a,
                                  const std::unique_ptr<TreeNode> &b) {
                          // Ties fall back to a stable-ish name compare so equal
                          // sequences keep a predictable order.
                          if (a->data.accessSequence != b->data.accessSequence) {
                              return ascending
                                     ? a->data.accessSequence < b->data.accessSequence
                                     : a->data.accessSequence > b->data.accessSequence;
                          }
                          return CaseInsensitiveTextLess(a->data.text, b->data.text);
                      });
            if (recursive) {
                for (auto &child: children) {
                    child->SortChildNodes(mode, true, ascending);
                }
            }
            return;
        }
        // TreeSortMode::NoSort -> leave order untouched.
    }


    /* UltraCanvasTreeView */

    UltraCanvasTreeView::UltraCanvasTreeView(const std::string &identifier, float x, float y, float w, float h) :
            UltraCanvasUIElement(identifier, x, y, w, h) {

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
        showFirstChildOnExpand = false;
        autoExpandSelectedNode = false;

        // Color defaults
        selectionColor = Colors::Selection;       // Blue selection
        hoverColor = Color(0xE5, 0xF3, 0xFF);          // Light blue hover
        lineColor = Color(0x80, 0x80, 0x80);           // Gray lines
        textColor = Colors::Black;           // Black text

        // Scrolling defaults
        scrollAnim.Cancel();
        scrollOffsetY = 0;
        maxScrollY = 0;
        CreateScrollbar();


        // Interaction state
//        isDragging = false;
//        draggedNode = nullptr;

        SetBackgroundColor(Colors::White);
        SetBorders(1, Colors::Gray);
    }

    UltraCanvasTreeView::UltraCanvasTreeView(const std::string &identifier, float w, float h) :
            UltraCanvasUIElement(identifier, w, h) {

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
        showFirstChildOnExpand = false;
        autoExpandSelectedNode = false;

        // Color defaults
        selectionColor = Colors::Selection;       // Blue selection
        hoverColor = Color(0xE5, 0xF3, 0xFF);          // Light blue hover
        lineColor = Color(0x80, 0x80, 0x80);           // Gray lines
        textColor = Colors::Black;           // Black text

        // Scrolling defaults
        scrollAnim.Cancel();
        scrollOffsetY = 0;
        maxScrollY = 0;
        CreateScrollbar();


        // Interaction state
//        isDragging = false;
//        draggedNode = nullptr;

        SetBackgroundColor(Colors::White);
        SetBorders(1, Colors::Gray);
    }

    UltraCanvasTreeView::UltraCanvasTreeView(const std::string &identifier) :
            UltraCanvasUIElement(identifier) {

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
        showFirstChildOnExpand = false;
        autoExpandSelectedNode = false;

        // Color defaults
        selectionColor = Colors::Selection;       // Blue selection
        hoverColor = Color(0xE5, 0xF3, 0xFF);          // Light blue hover
        lineColor = Color(0x80, 0x80, 0x80);           // Gray lines
        textColor = Colors::Black;           // Black text

        // Scrolling defaults
        scrollAnim.Cancel();
        scrollOffsetY = 0;
        maxScrollY = 0;
        CreateScrollbar();


        // Interaction state
//        isDragging = false;
//        draggedNode = nullptr;

        SetBackgroundColor(Colors::White);
        SetBorders(1, Colors::Gray);
    }

    void UltraCanvasTreeView::SetRootVisible(bool visible) {
        if (rootVisible == visible) return;
        rootVisible = visible;
        // A hidden root must stay expanded: its children ARE the top level, and
        // a collapsed root would leave the tree looking empty.
        if (!rootVisible && rootNode) rootNode->Expand();
        UpdateScrollbars();
        RequestRedraw();
    }

    TreeNode *UltraCanvasTreeView::SetRootNode(const TreeNodeData &rootData) {
        // Replacing the root frees the previous node graph. Drop any pointers
        // into the old graph first so selection/hover/focus can't dangle when a
        // caller rebuilds the tree (e.g. the menu-configuration widget).
        selectedNodes.clear();
        hoveredNode = nullptr;
        focusedNode = nullptr;
        rootNode = std::make_unique<TreeNode>(rootData);
        // A hidden root never shows an expander, so it must start expanded or
        // the whole forest below it would be invisible.
        if (!rootVisible) rootNode->Expand();
        UpdateScrollbars();
        return rootNode.get();
    }

    TreeNode *UltraCanvasTreeView::AddNode(const std::string &parentId, const TreeNodeData &nodeData) {
        if (!rootNode) {
            return SetRootNode(nodeData);
        }

        TreeNode *parent = rootNode->FindDescendant(parentId);
        if (parent) {
            TreeNode *newNode = parent->AddChild(nodeData);
            if (autoSortChildren) {
                parent->SortChildNodes(false, autoSortAscending);  // only this parent's row changed
            }
            UpdateScrollbars();
            return newNode;
        }

        return nullptr;
    }

    void UltraCanvasTreeView::RemoveNode(const std::string &nodeId) {
        if (!rootNode) return;

        TreeNode *node = rootNode->FindDescendant(nodeId);
        if (node && node->parent) {
            // RemoveChild destroys the whole subtree, not just this node, so
            // every pointer the view holds into ANY of it has to go - not only
            // the ones aimed at the node named here. Removing a populated node
            // (a drive whose folders had been expanded, an unmounted volume)
            // and then hovering the tree used to dereference a child that no
            // longer existed.
            std::vector<TreeNode *> doomed;
            CollectSubtree(node, doomed);
            const auto inSubtree = [&doomed](const TreeNode *candidate) {
                return std::find(doomed.begin(), doomed.end(), candidate) != doomed.end();
            };

            node->parent->RemoveChild(nodeId);

            selectedNodes.erase(
                    std::remove_if(selectedNodes.begin(), selectedNodes.end(), inSubtree),
                    selectedNodes.end()
            );
            if (inSubtree(hoveredNode)) hoveredNode = nullptr;
            if (inSubtree(focusedNode)) focusedNode = nullptr;

            UpdateScrollbars();
        }
    }

    void UltraCanvasTreeView::CollectSubtree(TreeNode *node,
                                             std::vector<TreeNode *> &out) {
        if (!node) return;
        out.push_back(node);
        for (const std::unique_ptr<TreeNode> &child : node->children)
            CollectSubtree(child.get(), out);
    }

    TreeNode *UltraCanvasTreeView::FindNode(const std::string &nodeId) {
        return rootNode ? rootNode->FindDescendant(nodeId) : nullptr;
    }

    void UltraCanvasTreeView::SetAutoSortChildren(bool enable, bool ascending) {
        autoSortChildren = enable;
        autoSortAscending = ascending;
        if (enable && rootNode) {              // sort existing tree immediately
            rootNode->SortChildNodes(true, ascending);
            UpdateScrollbars();
            RequestRedraw();
        }
    }

    void UltraCanvasTreeView::SortNodeChildren(const std::string &nodeId, bool recursive, bool ascending) {
        SortNodeChildren(FindNode(nodeId), recursive, ascending);
    }

    void UltraCanvasTreeView::SortNodeChildren(TreeNode *node, bool recursive, bool ascending) {
        if (!node) return;
        node->SortChildNodes(recursive, ascending);
        UpdateScrollbars();
        RequestRedraw();
    }

    void UltraCanvasTreeView::SortAllNodes(bool ascending) {
        if (!rootNode) return;
        rootNode->SortChildNodes(true, ascending);
        UpdateScrollbars();
        RequestRedraw();
    }

    void UltraCanvasTreeView::SetSortMode(TreeSortMode mode, bool ascending) {
        sortMode = mode;
        sortAscending = ascending;
        if (rootNode && mode != TreeSortMode::NoSort) {
            rootNode->SortChildNodes(mode, true, ascending);
        }
        UpdateScrollbars();
        RequestRedraw();
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
        }
        RequestRedraw();
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
        for (TreeNode *node: selectedNodes) {
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
        RequestRedraw();
    }

    void UltraCanvasTreeView::CollapseNode(TreeNode *node) {
        if (node && node->HasChildren()) {
            node->Collapse();
            UpdateScrollbars();

            if (onNodeCollapsed) {
                onNodeCollapsed(node);
            }
        }
        RequestRedraw();
    }

    void UltraCanvasTreeView::ToggleNode(TreeNode *node) {
        if (!node) return;
        if (node->IsExpanded()) CollapseNode(node);
        else ExpandNode(node);
    }

    void UltraCanvasTreeView::ExpandAll() {
        if (rootNode) {
            ExpandNodeRecursive(rootNode.get());
            UpdateScrollbars();
            RequestRedraw();
        }
    }

    void UltraCanvasTreeView::CollapseAll() {
        if (rootNode) {
            CollapseNodeRecursive(rootNode.get());
            UpdateScrollbars();
            RequestRedraw();
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
        // Revealing a node positions the tree directly — it is normally the
        // answer to a keyboard move that has already happened.
        scrollAnim.Cancel();

        int nodeY = GetNodeDisplayY(node);
        int viewHeight = GetHeight() - GetHeaderHeight();   // visible rows sit below the header band
        if (nodeY < scrollOffsetY) {
            scrollOffsetY = nodeY;
        } else if (nodeY >= scrollOffsetY + viewHeight - rowHeight) {
            scrollOffsetY = nodeY - viewHeight + rowHeight;
        }

        ClampScrollOffset();
        RequestRedraw();
    }

    void UltraCanvasTreeView::ScrollBy(int deltaY) {
        if (!scrollAnim.IsBound()) {
            scrollAnim.Bind([this] { return static_cast<double>(scrollOffsetY); },
                            [this](double v) {
                                scrollOffsetY = static_cast<int>(std::lround(v));
                                ClampScrollOffset();
                                RequestRedraw();
                            });
        }
        // Glide rather than jump, matching what the scrollbar does on the path
        // where one is visible (see UltraCanvasSmoothScroll.h).
        scrollAnim.AnimateBy(deltaY, 0, std::max(0, maxScrollY));
    }

    void UltraCanvasTreeView::ScrollToTop() {
        if (verticalScrollbar && verticalScrollbar->IsVisible()) {
            // Route through the scrollbar so the jump is animated when smooth
            // scrolling is on; onScrollChange writes scrollOffsetY back to us.
            verticalScrollbar->SmoothScrollTo(0);
        } else {
            scrollOffsetY = 0;
            ClampScrollOffset();
        }
        RequestRedraw();
    }

    void UltraCanvasTreeView::SetShowScrollToTopButton(bool show) {
        if (showScrollToTopButton == show) return;
        showScrollToTopButton = show;
        if (!show) scrollToTopHovered = false;
        RequestRedraw();
    }

    void UltraCanvasTreeView::SetScrollToTopButtonStyle(const TreeScrollToTopStyle &style) {
        scrollToTopStyle = style;
        RequestRedraw();
    }

    bool UltraCanvasTreeView::IsScrollToTopButtonActive() const {
        if (!showScrollToTopButton || !rootNode || rowHeight <= 0) return false;
        if (maxScrollY <= 0) return false;

        // Nothing to go back to while the first row is already on screen, so the
        // button only appears once the user has actually scrolled down.
        if (scrollOffsetY <= 0) return false;

        // Rows that cannot be shown at once. maxScrollY is exactly the pixel
        // amount of content hanging outside the viewport, so a partially hidden
        // row still counts as hidden (round up).
        int hiddenRows = (maxScrollY + rowHeight - 1) / rowHeight;
        return hiddenRows > scrollToTopStyle.minHiddenRows;
    }

    Rect2Di UltraCanvasTreeView::GetScrollToTopButtonRect() const {
        if (!IsScrollToTopButtonActive()) return Rect2Di(0, 0, 0, 0);

        Rect2Di contentRect = GetLocalContentRect();
        const int size = std::max(8, scrollToTopStyle.size);
        const int margin = std::max(0, scrollToTopStyle.margin);
        const int headerHeight = GetHeaderHeight();

        // Keep clear of the vertical scrollbar so the two never overlap.
        int x = contentRect.Right() - GetVerticalScrollbarWidth() - margin - size;
        x = std::max(contentRect.x, x);

        // Resting place: bottom-right corner of the content area.
        int bottom = contentRect.Bottom() - margin;

        // As the view approaches the end of the list, slide the button up so the
        // final keepClearRows rows stay fully readable. totalHeight is recovered
        // from the scroll geometry (maxScrollY == totalHeight - viewHeight).
        int keepClearRows = std::max(0, scrollToTopStyle.keepClearRows);
        if (keepClearRows > 0) {
            int viewHeight = static_cast<int>(GetHeight()) - headerHeight;
            int totalHeight = maxScrollY + viewHeight;
            int tailTop = contentRect.y + headerHeight + totalHeight
                          - keepClearRows * rowHeight - scrollOffsetY;
            bottom = std::min(bottom, tailTop);
        }

        // Never let the dodge push the button off the top of the content area.
        int top = std::max(contentRect.y + headerHeight + margin, bottom - size);
        return Rect2Di(x, top, size, size);
    }

    void UltraCanvasTreeView::RenderScrollToTopButton(IRenderContext *ctx) {
        Rect2Di r = GetScrollToTopButtonRect();
        if (r.width <= 0 || r.height <= 0) return;

        const TreeScrollToTopStyle &s = scrollToTopStyle;
        float radius = std::min(s.cornerRadius, r.width * 0.5f);
        ctx->DrawFilledRectangle(Rect2Dd(r.x, r.y, r.width, r.height),
                                 scrollToTopHovered ? s.hoverBackground : s.background,
                                 1.0f, s.borderColor, radius);

        // Glyph: a short bar ("the top") with an arrow pointing up at it.
        double cx = r.x + r.width * 0.5;
        double halfWidth = r.width * 0.22;
        double barY = r.y + r.height * 0.26;
        double barHeight = std::max(1.0, r.height * 0.09);
        ctx->DrawFilledRectangle(Rect2Dd(cx - halfWidth, barY, halfWidth * 2, barHeight), s.arrowColor);

        double headTop = r.y + r.height * 0.40;
        double headBottom = r.y + r.height * 0.60;
        ctx->PushState();
        ctx->SetFillPaint(s.arrowColor);
        ctx->FillLinePath({Point2Dd(cx, headTop),
                           Point2Dd(cx + halfWidth, headBottom),
                           Point2Dd(cx - halfWidth, headBottom)});
        ctx->PopState();

        double stemWidth = std::max(2.0, r.width * 0.11);
        ctx->DrawFilledRectangle(Rect2Dd(cx - stemWidth * 0.5, headBottom - 1,
                                         stemWidth, r.y + r.height * 0.78 - headBottom + 1),
                                 s.arrowColor);
    }

    bool UltraCanvasTreeView::HandleScrollToTopButtonEvent(const UCEvent &event) {
        bool isPointerEvent = event.type == UCEventType::MouseMove ||
                              event.type == UCEventType::MouseDown ||
                              event.type == UCEventType::MouseUp ||
                              event.type == UCEventType::MouseDoubleClick ||
                              event.type == UCEventType::MouseLeave;
        if (!isPointerEvent) return false;

        if (!IsScrollToTopButtonActive() || event.type == UCEventType::MouseLeave) {
            if (scrollToTopHovered) {
                scrollToTopHovered = false;
                RequestRedraw();
            }
            return false;
        }

        bool inside = GetScrollToTopButtonRect().Contains(event.pointer);

        if (event.type == UCEventType::MouseMove) {
            if (inside != scrollToTopHovered) {
                scrollToTopHovered = inside;
                // The row under the button must not keep its hover highlight
                // while the pointer is really over the button.
                if (inside && hoveredNode) {
                    hoveredNode->hovered = false;
                    hoveredNode = nullptr;
                }
                SetMouseCursor(inside ? UCMouseCursor::Hand : UCMouseCursor::Default);
                RequestRedraw();
            }
            return inside;
        }

        if (!inside) return false;

        // Swallow every button event over the glyph so the row underneath is
        // neither selected nor toggled; MouseDown does the actual scrolling.
        if (event.type == UCEventType::MouseDown) {
            ScrollToTop();
        }
        return true;
    }

    bool UltraCanvasTreeView::OnEvent(const UCEvent &event) {
        if (IsDisabled() || !IsVisible()) return false;

        if (verticalScrollbar->IsVisible()) {
            auto sbB = verticalScrollbar->GetBounds();
            if (sbB.Contains(event.pointer) || verticalScrollbar->IsDragging()) {
                UCEvent localEvent = event;
                localEvent.pointer = event.pointer - sbB.TopLeft();
                if (verticalScrollbar->OnEvent(localEvent)) {
                    return true;
                }
            }
        }

        // The floating scroll-to-top button sits on top of the rows, so it gets
        // first refusal on pointer events inside its rect.
        if (HandleScrollToTopButtonEvent(event)) {
            return true;
        }

        switch (event.type) {
            case UCEventType::MouseDown:
                return HandleMouseDown(event);
                break;
            case UCEventType::MouseMove:
                return HandleMouseMove(event);
                break;
            case UCEventType::MouseUp:
                return HandleMouseUp(event);
                break;
            case UCEventType::MouseDoubleClick:
                return HandleMouseDoubleClick(event);
                break;
            case UCEventType::MouseWheel:
                return HandleMouseWheel(event);
                break;
            case UCEventType::KeyDown:
                HandleKeyDown(event);
                break;
            case UCEventType::DragEnter:
            case UCEventType::DragOver:
                return HandleDragOver(event);
            case UCEventType::DragLeave:
                if (dropTargetNode) { dropTargetNode = nullptr; RequestRedraw(); }
                return false;
            case UCEventType::Drop:
                return HandleDrop(event);
            default:
                break;
        }
        return false;
    }

    bool UltraCanvasTreeView::HandleDragOver(const UCEvent &event) {
        // Highlight the row under the pointer only when a handler accepts it as
        // a drop target, so the user sees where a drop would land.
        TreeNode *target = GetNodeAtY(event.pointer.y);
        TreeNode *accepted =
                (target && onFilesDragAccept && onFilesDragAccept(target))
                ? target : nullptr;
        if (accepted != dropTargetNode) {
            dropTargetNode = accepted;
            RequestRedraw();
        }
        return accepted != nullptr;
    }

    bool UltraCanvasTreeView::HandleDrop(const UCEvent &event) {
        TreeNode *target = GetNodeAtY(event.pointer.y);
        // Clear the highlight before the handler runs: it may rebuild the tree
        // (e.g. adding a pinned node), which would dangle dropTargetNode.
        dropTargetNode = nullptr;
        RequestRedraw();
        if (event.droppedFiles.empty() || !onFilesDroppedOnNode || !target)
            return false;
        return onFilesDroppedOnNode(target, event.droppedFiles);
    }

    void UltraCanvasTreeView::Arrange(const Rect2Df &finalRect, const CSSLayout::LayoutContext &ctx) {
        // The engine has resolved our final bounds (explicit size or parent
        // stretch). Set finalBounds + damage via the base, then recompute the
        // scrollbar against the now-valid width/height.
        UltraCanvasUIElement::Arrange(finalRect, ctx);

        // Fix for the right-side gap: scrollbar visibility used to be computed
        // only from the tree mutators (AddNode/Expand/...), which ran while
        // finalBounds.height was still 0 and so wrongly marked the scrollbar
        // visible. Computing it here, with valid bounds, keeps row width correct.
        UpdateScrollbars();
    }

    void UltraCanvasTreeView::Render(IRenderContext *ctx, const Rect2Df& dirtyRect) {
        // Draw background / border
        UltraCanvasUIElement::Render(ctx, dirtyRect);
        // Build local-space content rect (ctx is translated to element origin)
        Rect2Di contentRect = GetLocalContentRect();
        const int headerHeight = GetHeaderHeight();
        if (rootNode) {
            // Rows start below the (optional) fixed header band and scroll beneath it.
            int currentY = contentRect.y + headerHeight - scrollOffsetY;
            // One flag per drawn level, the top level included (pipes[0]) - see the
            // declaration of RenderNode.
            std::vector<bool> pipes;
            if (rootVisible) {
                pipes.push_back(false);   // the root row has no sibling below it
                RenderNode(ctx, rootNode.get(), currentY, 0, contentRect, pipes);
            } else {
                // Hidden root: its children are the top level, at depth 0.
                RenderChildNodes(ctx, rootNode.get(), currentY, -1, contentRect, pipes);
            }
        }

        // Draw the fixed header band last so rows scrolled up are covered by it.
        if (headerHeight > 0) {
            RenderHeader(ctx, Rect2Di(contentRect.x, contentRect.y, contentRect.width, headerHeight));
        }

        // Draw scrollbar if needed (translate to scrollbar's bounds origin)
        if (verticalScrollbar->IsVisible()) {
            ctx->PushState();
            auto sbB = verticalScrollbar->GetBounds();
            ctx->Translate(sbB.TopLeft());
            verticalScrollbar->Render(ctx, dirtyRect);
            ctx->PopState();
        }

        // Floating "move to the top" button, drawn over everything else.
        RenderScrollToTopButton(ctx);
    }

    void UltraCanvasTreeView::UpdateScrollbars() {
        if (!rootNode) {
            maxScrollY = 0;
            verticalScrollbar->SetVisible(false);
            return;
        }

        int headerHeight = GetHeaderHeight();
        int totalHeight = GetTotalVisibleHeight();
        int viewHeight = GetHeight() - headerHeight;   // rows live below the fixed header band

        maxScrollY = std::max(0, totalHeight - viewHeight);
        bool hasVerticalScrollbar = maxScrollY > 0;

        verticalScrollbar->SetVisible(hasVerticalScrollbar);

        if (hasVerticalScrollbar) {
            // Position scrollbar in element-local space, below the header band.
            int localPaddingX = GetBorderLeftWidth();
            int localPaddingY = GetBorderTopWidth();
            int paddingW = GetWidth() - GetTotalBorderHorizontal();
            int paddingH = GetHeight() - GetTotalBorderVertical();
            int scrollbarWidth = verticalScrollbar->GetStyle().trackSize;
            int sbX = localPaddingX + paddingW - scrollbarWidth;
            int sbY = localPaddingY + headerHeight;
            int sbHeight = paddingH - headerHeight;

            verticalScrollbar->SetPosition(sbX, sbY);
            verticalScrollbar->SetSize(scrollbarWidth, sbHeight);
            verticalScrollbar->SetViewportSize(viewHeight);
            verticalScrollbar->SetContentSize(totalHeight);
        }
        ClampScrollOffset();
    }

    void UltraCanvasTreeView::ClampScrollOffset() {
        scrollOffsetY = std::max(0, std::min(scrollOffsetY, maxScrollY));
        if (verticalScrollbar->IsVisible()) {
            verticalScrollbar->SetScrollPosition(scrollOffsetY);
        }
    }

    int UltraCanvasTreeView::GetTotalVisibleHeight() {
        if (!rootNode) return 0;
        if (rootVisible) {
            return (1 + rootNode->GetVisibleChildCount()) * rowHeight;
        }
        // A hidden root never enters the Expanded state, yet Render() and
        // GetNodeAtY() always walk its children as the top level. Mirror that
        // here instead of calling rootNode->GetVisibleChildCount(), which gates
        // on the (never-expanded) root and would report zero content — leaving
        // maxScrollY at 0 so the scrollbar and wheel scrolling never engaged.
        int count = 0;
        for (const auto &child : rootNode->children) {
            if (child->data.visible) {
                count++;
                count += child->GetVisibleChildCount();
            }
        }
        return count * rowHeight;
    }

    int UltraCanvasTreeView::GetNodeDisplayY(TreeNode *node) {
        if (!rootNode || !node) return 0;

        int y = 0;
        std::function<bool(TreeNode *, int &)> findNodeY = [&](TreeNode *current, int &currentY) -> bool {
            if (current == node) {
                y = currentY;
                return true;
            }

            currentY += rowHeight;

            if (current->IsExpanded()) {
                for (auto &child: current->children) {
                    if (child->data.visible && findNodeY(child.get(), currentY)) {
                        return true;
                    }
                }
            }

            return false;
        };

        int currentY = 0;
        if (rootVisible) {
            findNodeY(rootNode.get(), currentY);
        } else {
            for (auto &child: rootNode->children) {
                if (child->data.visible && findNodeY(child.get(), currentY)) break;
            }
        }
        return y;
    }

    TreeNode *UltraCanvasTreeView::GetNodeAtY(int y) {
        if (!rootNode) return nullptr;

        // y is element-local now; subtract local content offset + header band + add scroll
        int localContentY = GetBorderTopWidth() + GetPaddingTop() + GetHeaderHeight();
        int relativeY = y - localContentY + scrollOffsetY;
        if (relativeY < 0) return nullptr;   // click landed in the fixed header band
        int nodeIndex = relativeY / rowHeight;

        if (nodeIndex < 0) return nullptr;

        int currentIndex = 0;
        std::function<TreeNode *(TreeNode *)> findNode = [&](TreeNode *current) -> TreeNode * {
            if (currentIndex == nodeIndex) {
                return current;
            }

            currentIndex++;

            if (current->IsExpanded()) {
                for (auto &child: current->children) {
                    if (child->data.visible) {
                        TreeNode *found = findNode(child.get());
                        if (found) return found;
                    }
                }
            }

            return nullptr;
        };

        if (rootVisible) return findNode(rootNode.get());
        for (auto &child: rootNode->children) {
            if (!child->data.visible) continue;
            if (TreeNode *found = findNode(child.get())) return found;
        }
        return nullptr;
    }

    int UltraCanvasTreeView::GetTreeLineX(const Rect2Di &contentRect, int level) const {
        return GetRowOriginX(contentRect, level) + kExpanderButtonOffset + kExpanderButtonSize / 2;
    }

    // Both helpers expect the caller to have pushed the render state: they leave
    // the stroke width and the dash pattern behind them.
    void UltraCanvasTreeView::DrawTreeLineV(IRenderContext *ctx, int x, int yFrom, int yTo) {
        if (!ctx || yTo <= yFrom) return;
        // The line sits on a half-pixel so a 1px stroke covers exactly one column
        // of pixels. The dashes are measured from yFrom, and the offset pulls the
        // dots onto even device rows - so the trunks of separate rows, and the
        // horizontal stubs below, all land on one shared dot grid.
        if (lineStyle == TreeLineStyle::Dotted) {
            ctx->SetLineDash(UCDashPattern({1.0, 1.0}, static_cast<double>(yFrom & 1)));
        }
        ctx->SetStrokeWidth(1.0);
        ctx->DrawLine(Point2Dd(x + 0.5, yFrom), Point2Dd(x + 0.5, yTo), lineColor);
    }

    void UltraCanvasTreeView::DrawTreeLineH(IRenderContext *ctx, int y, int xFrom, int xTo) {
        if (!ctx || xTo <= xFrom) return;
        if (lineStyle == TreeLineStyle::Dotted) {
            ctx->SetLineDash(UCDashPattern({1.0, 1.0}, static_cast<double>(xFrom & 1)));
        }
        ctx->SetStrokeWidth(1.0);
        ctx->DrawLine(Point2Dd(xFrom, y + 0.5), Point2Dd(xTo, y + 0.5), lineColor);
    }

    void UltraCanvasTreeView::RenderChildNodes(IRenderContext *ctx, TreeNode *node, int &currentY, int level,
                                               const Rect2Di &contentRect, std::vector<bool> &pipes) {
        // The last visible child ends its parent's connector: everything above it
        // keeps the trunk running down to the next sibling.
        TreeNode *lastVisible = nullptr;
        for (auto &child: node->children) {
            if (child->data.visible) lastVisible = child.get();
        }
        if (!lastVisible) return;

        pipes.push_back(false);
        for (auto &child: node->children) {
            if (!child->data.visible) continue;
            pipes.back() = (child.get() != lastVisible);
            RenderNode(ctx, child.get(), currentY, level + 1, contentRect, pipes);
        }
        pipes.pop_back();
    }

    void UltraCanvasTreeView::RenderNode(IRenderContext *ctx, TreeNode *node, int &currentY, int level,
                                         const Rect2Di &contentRect, std::vector<bool> &pipes) {
        if (!node || !node->data.visible) return;

        const int nodeY = currentY;
        currentY += rowHeight;

        // Rows scrolled out of the viewport cost nothing but their height; their
        // children still have to be walked so the ones below land correctly.
        const bool offscreen = (nodeY + rowHeight < contentRect.y || nodeY > contentRect.Bottom());

        if (!offscreen) {
            const int nodeX = GetRowOriginX(contentRect, level);
            const int sbWidth = verticalScrollbar->IsVisible() ? verticalScrollbar->GetWidth() : 0;
            const int nodeWidth = contentRect.width - sbWidth;

            // Let a subclass fully own the row (e.g. a full-width section-header bar).
            if (RenderNodeFullRow(ctx, node, nodeY, contentRect, nodeWidth)) {
                if (node->IsExpanded()) RenderChildNodes(ctx, node, currentY, level, contentRect, pipes);
                return;
            }

            // Draw node background
            Color bgColor = backgroundColor;
            if (node == dropTargetNode) {
                bgColor = dropTargetColor;
            } else if (node->selected) {
                bgColor = selectionColor;
            } else if (node->hovered) {
                bgColor = hoverColor;
            } else if (node->data.backgroundColor != Colors::Transparent) {
                bgColor = node->data.backgroundColor;
            }

            if (bgColor != backgroundColor) {
                ctx->DrawFilledRectangle(Rect2Di(contentRect.x + 1, nodeY, nodeWidth - 2, rowHeight), bgColor);
            }

            const bool hasExpander = showExpandButtons && node->HasChildren();
            // The expander slot - and the check flag slot behind it - are reserved on
            // every row, so the icon and label of a childless or unflagged node sit at
            // the same x as those of its siblings.
            const int slotEnd = nodeX + (showExpandButtons ? kExpanderSlot : 0);
            int textX = slotEnd + (showCheckboxes ? kCheckboxSlot : 0) + textPadding;

            // Connecting lines: the trunks of the ancestors that continue past this
            // row, plus the elbow that ties this row to its parent's trunk. Drawn
            // after the row background (so they stay visible on a selected row) and
            // before the expander button, which caps the elbow. Column -1 is the
            // root-level trunk, drawn only when there is a gutter to hold it.
            const int firstColumn = GetRootGutter() > 0 ? -1 : 0;
            if (lineStyle != TreeLineStyle::NoLine && level - 1 >= firstColumn) {
                const int rowMidY = nodeY + rowHeight / 2;
                ctx->PushState();   // the dash pattern must not outlive the row
                for (int column = firstColumn; column + 1 < level; ++column) {
                    if (pipes[column + 1]) {
                        DrawTreeLineV(ctx, GetTreeLineX(contentRect, column), nodeY, nodeY + rowHeight);
                    }
                }
                const int trunkX = GetTreeLineX(contentRect, level - 1);
                // A last child stops the trunk at its own row centre; the others let
                // it run on to the sibling below.
                DrawTreeLineV(ctx, trunkX, nodeY, pipes[level] ? nodeY + rowHeight : rowMidY + 1);
                // The stub runs to the expander button, else to the check flag, else
                // all the way to the icon - stopping a hair short of it, so a dotted
                // line never touches a glyph on a row that carries no icon.
                const int stubEnd = hasExpander ? nodeX + kExpanderButtonOffset
                                                : (showCheckboxes ? slotEnd + kCheckboxLead
                                                                  : textX - 2);
                DrawTreeLineH(ctx, rowMidY, trunkX, stubEnd);
                ctx->PopState();
            }

            // Draw expand/collapse button
            if (hasExpander) {
                int buttonX = nodeX + kExpanderButtonOffset;
                int buttonY = nodeY + (rowHeight - kExpanderButtonSize) / 2;

                // Draw button background
                ctx->DrawFilledRectangle(Rect2Di(buttonX, buttonY, kExpanderButtonSize, kExpanderButtonSize),
                                         expandButtonColor, 1.0, Colors::Gray);

                // Draw +/- symbol
                ctx->DrawFilledRectangle(Rect2Di(buttonX + 3, buttonY + 5, 6, 2), Colors::Black);
                if (!node->IsExpanded()) {
                    ctx->DrawFilledRectangle(Rect2Di(buttonX + 5, buttonY + 3, 2, 6), Colors::Black);
                }
            }

            // Draw the check flag
            if (showCheckboxes) {
                RenderCheckbox(ctx, node, GetCheckboxRect(node, nodeX, nodeY));
            }

            // Draw left icon
            if (node->data.leftIcon.visible && !node->data.leftIcon.iconPath.empty()) {
                ctx->DrawImage(node->data.leftIcon.iconPath.c_str(),
                               Rect2Dd(textX, nodeY + (rowHeight - node->data.leftIcon.height) / 2,
                                       node->data.leftIcon.width, node->data.leftIcon.height),
                               ImageFitMode::Contain);
                textX += node->data.leftIcon.width + iconSpacing;
            }

            // Draw the row's label/content (Classic single text run; subclasses draw columns).
            RenderNodeLabel(ctx, node, nodeY, textX, nodeWidth, sbWidth, contentRect);
        }

        // Render children if expanded
        if (node->IsExpanded()) {
            RenderChildNodes(ctx, node, currentY, level, contentRect, pipes);
        }
    }

    // ===== CHECK FLAGS =====

    Rect2Di UltraCanvasTreeView::GetCheckboxRect(TreeNode *node, int rowOriginX, int nodeY) const {
        if (!showCheckboxes || !node || !node->data.showCheckbox) return Rect2Di(0, 0, 0, 0);
        const int boxX = rowOriginX + (showExpandButtons ? kExpanderSlot : 0) + kCheckboxLead;
        return Rect2Di(boxX, nodeY + (rowHeight - kCheckboxSize) / 2, kCheckboxSize, kCheckboxSize);
    }

    void UltraCanvasTreeView::RenderCheckbox(IRenderContext *ctx, TreeNode *node, const Rect2Di &box) {
        if (!ctx || box.width <= 0) return;

        ctx->DrawFilledRectangle(box, checkboxBackgroundColor, 1.0, checkboxBorderColor);

        switch (node->data.checkState) {
            case TreeCheckState::Checked: {
                // A tick drawn as two strokes, inset so it never touches the border.
                ctx->PushState();
                ctx->SetStrokeWidth(2.0);
                const double left = box.x + box.width * 0.24;
                const double mid = box.x + box.width * 0.44;
                const double right = box.x + box.width * 0.78;
                const double midY = box.y + box.height * 0.52;
                const double lowY = box.y + box.height * 0.74;
                const double highY = box.y + box.height * 0.26;
                ctx->DrawLine(Point2Dd(left, midY), Point2Dd(mid, lowY), checkboxCheckColor);
                ctx->DrawLine(Point2Dd(mid, lowY), Point2Dd(right, highY), checkboxCheckColor);
                ctx->PopState();
                break;
            }
            case TreeCheckState::Mixed: {
                // Part of the subtree is checked: a filled square rather than a tick,
                // so "some" never reads as "all" at a glance.
                const int inset = std::max(3, box.width / 4);
                ctx->DrawFilledRectangle(Rect2Di(box.x + inset, box.y + inset,
                                                 box.width - 2 * inset, box.height - 2 * inset),
                                         checkboxCheckColor);
                break;
            }
            case TreeCheckState::Unchecked:
            default:
                break;
        }
    }

    void UltraCanvasTreeView::AssignCheckState(TreeNode *node, TreeCheckState state) {
        if (!node || node->data.checkState == state) return;
        node->data.checkState = state;
        if (onNodeCheckChanged) onNodeCheckChanged(node, state);
    }

    void UltraCanvasTreeView::ApplyCheckStateToSubtree(TreeNode *node, TreeCheckState state) {
        if (!node) return;
        AssignCheckState(node, state);
        for (auto &child: node->children) {
            ApplyCheckStateToSubtree(child.get(), state);
        }
    }

    void UltraCanvasTreeView::RefreshAncestorCheckStates(TreeNode *node) {
        for (TreeNode *parent = node ? node->parent : nullptr; parent; parent = parent->parent) {
            int checked = 0;
            int total = 0;
            bool anyMixed = false;
            for (auto &child: parent->children) {
                total++;
                if (child->data.checkState == TreeCheckState::Checked) checked++;
                else if (child->data.checkState == TreeCheckState::Mixed) anyMixed = true;
            }
            TreeCheckState state = TreeCheckState::Unchecked;
            if (total > 0 && checked == total) state = TreeCheckState::Checked;
            else if (anyMixed || checked > 0) state = TreeCheckState::Mixed;
            AssignCheckState(parent, state);
        }
    }

    void UltraCanvasTreeView::SetNodeCheckState(TreeNode *node, TreeCheckState state) {
        if (!node) return;
        if (checkPropagation && state != TreeCheckState::Mixed) {
            ApplyCheckStateToSubtree(node, state);
        } else {
            AssignCheckState(node, state);
        }
        if (checkPropagation) RefreshAncestorCheckStates(node);
        RequestRedraw();
    }

    void UltraCanvasTreeView::SetNodeChecked(const std::string &nodeId, bool checked) {
        SetNodeChecked(FindNode(nodeId), checked);
    }

    void UltraCanvasTreeView::ToggleNodeCheck(TreeNode *node) {
        if (!node) return;
        // Mixed counts as "not yet checked": the click that follows a partial
        // subtree completes it rather than clearing it.
        SetNodeCheckState(node, node->data.checkState == TreeCheckState::Checked
                                        ? TreeCheckState::Unchecked
                                        : TreeCheckState::Checked);
    }

    std::vector<TreeNode *> UltraCanvasTreeView::GetCheckedNodes() const {
        std::vector<TreeNode *> checked;
        std::function<void(TreeNode *)> collect = [&](TreeNode *node) {
            if (!node) return;
            if (node->data.checkState == TreeCheckState::Checked) checked.push_back(node);
            for (const auto &child: node->children) collect(child.get());
        };
        if (rootNode) {
            if (rootVisible) collect(rootNode.get());
            else for (const auto &child: rootNode->children) collect(child.get());
        }
        return checked;
    }

    void UltraCanvasTreeView::SetAllChecked(bool checked) {
        if (!rootNode) return;
        const TreeCheckState state = checked ? TreeCheckState::Checked : TreeCheckState::Unchecked;
        if (rootVisible) {
            ApplyCheckStateToSubtree(rootNode.get(), state);
        } else {
            // The hidden root is not a row, but it still carries the state its
            // children agree on so a later propagation reads it correctly.
            rootNode->data.checkState = state;
            for (auto &child: rootNode->children) ApplyCheckStateToSubtree(child.get(), state);
        }
        RequestRedraw();
    }

    void UltraCanvasTreeView::RenderNodeLabel(IRenderContext *ctx, TreeNode *node, int nodeY,
                                              int textX, int nodeWidth, int sbWidth,
                                              const Rect2Di &contentRect) {
        // Classic mode: single text run + optional right icon.
        Color nodeTextColor = node->data.textColor != Colors::Black ? node->data.textColor : textColor;
        ctx->SetFontSize(fontSize);
        ctx->SetTextPaint(nodeTextColor);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        auto layout = ctx->GetOrCreateTextLayout(node->data.text, Size2Di(nodeWidth - textX, rowHeight), true);
        if (layout) {
            ctx->DrawTextLayout(*layout, Point2Dd(textX, nodeY));
        }

        // Draw right icon
        if (node->data.rightIcon.visible && !node->data.rightIcon.iconPath.empty()) {
            int rightIconX = contentRect.Right() - node->data.rightIcon.width - textPadding - sbWidth;

            ctx->DrawImage(node->data.rightIcon.iconPath.c_str(),
                           Rect2Dd(rightIconX, nodeY + (rowHeight - node->data.rightIcon.height) / 2,
                                   node->data.rightIcon.width, node->data.rightIcon.height),
                           ImageFitMode::Contain);
        }
    }

    void UltraCanvasTreeView::ExpandNodeRecursive(TreeNode *node) {
        if (node && node->HasChildren()) {
            node->Expand();
            for (auto &child: node->children) {
                ExpandNodeRecursive(child.get());
            }
        }
    }

    void UltraCanvasTreeView::CollapseNodeRecursive(TreeNode *node) {
        if (node && node->HasChildren()) {
            node->Collapse();
            for (auto &child: node->children) {
                CollapseNodeRecursive(child.get());
            }
        }
    }

    bool UltraCanvasTreeView::HandleMouseDown(const UCEvent &event) {
        if (!Contains(event.pointer)) return false;

        // The right button belongs to the context menu (fired on MouseUp) —
        // it must not move the selection to the node under the cursor.
        if (event.button == UCMouseButton::Right) {
            return onNodeRightClicked != nullptr;
        }

//        lastMousePos = Point2Di(event.pointer.x, event.pointer.y);

        // Check if clicking on scrollbar
//        if (hasVerticalScrollbar && event.pointer.x >= GetX() + GetWidth() - scrollbarWidth) {
//            isDragging = true;
//            UltraCanvasApplication::GetInstance()->CaptureMouse(this);
//            return true;
//        }

        TreeNode *clickedNode = GetNodeAtY(event.pointer.y);
        if (clickedNode) {
            // nodeX in element-local space. node->level is absolute depth from
            // the root, but Render() draws a hidden root's children as the top
            // level (display level 0) — so mirror that offset here, otherwise
            // the expand-button hit box lands one indent too far right (over the
            // node icon instead of the "+"). GetRowOriginX adds the root-line
            // gutter, which shifts every row when the root connectors are drawn.
            Rect2Di localContentRect = GetLocalContentRect();
            int displayLevel = clickedNode->level - (rootVisible ? 0 : 1);
            int nodeX = GetRowOriginX(localContentRect, displayLevel);

            // Check if clicking on expand/collapse button
            if (showExpandButtons && clickedNode->HasChildren() &&
                event.pointer.x >= nodeX &&
                event.pointer.x <= nodeX + kExpanderButtonOffset + kExpanderButtonSize) {
                ToggleNode(clickedNode);
                if (clickedNode->IsExpanded() && showFirstChildOnExpand) {
                    ExpandFirstChildNode(clickedNode);
                }
                return true;
            }

            // Check if clicking the row's check flag: it toggles the flag and
            // leaves the selection where it was.
            if (showCheckboxes) {
                Rect2Di box = GetCheckboxRect(clickedNode, nodeX, 0);
                if (box.width > 0 && event.pointer.x >= box.x && event.pointer.x < box.Right()) {
                    ToggleNodeCheck(clickedNode);
                    return true;
                }
            }

            // Regular node selection
            bool addToSelection = (event.ctrl && selectionMode == TreeSelectionMode::Multiple);
            SelectNode(clickedNode, addToSelection);
            focusedNode = clickedNode;
            if (autoExpandSelectedNode && !focusedNode->children.empty() && !focusedNode->IsExpanded()) {
                ExpandNode(focusedNode);
                if (showFirstChildOnExpand) {
                    ExpandFirstChildNode(focusedNode);
                }
            }

            // If a parent node sitting at the bottom of the view was clicked,
            // nudge the scroll down one row to reveal that more entries follow.
            ScrollDownIfLastVisibleParent(clickedNode);
        } else {
            ClearSelection();
            focusedNode = nullptr;
        }
        return true;
    }

    bool UltraCanvasTreeView::HandleMouseMove(const UCEvent &event) {
//        if (isDragging && hasVerticalScrollbar) {
//            // Handle scrollbar dragging
//            float deltaY = event.pointer.y - lastMousePos.y;
//            scrollOffsetY += deltaY;
//            ClampScrollOffset();
//            lastMousePos = Point2Di(event.pointer.x, event.pointer.y);
//            RequestRedraw();
//            return true;
//        }

        // Update hover state
        TreeNode *newHovered = GetNodeAtY(event.pointer.y);
        if (newHovered != hoveredNode) {
            if (hoveredNode) hoveredNode->hovered = false;
            hoveredNode = newHovered;
            if (hoveredNode) hoveredNode->hovered = true;
            RequestRedraw();
            return true;
        }
        return false;
    }

    bool UltraCanvasTreeView::HandleMouseUp(const UCEvent &event) {
//        if (isDragging)  {
//            isDragging = false;
//            return true;
//        }

        // Right-click context menu on the node under the cursor. Only the
        // right button fires it — this used to trigger on every release.
        if (event.button == UCMouseButton::Right) {
            TreeNode *rightClickedNode = GetNodeAtY(event.pointer.y);
            if (rightClickedNode && onNodeRightClicked) {
                onNodeRightClicked(rightClickedNode, event);
                return true;
            }
        }
        return false;
    }

    bool UltraCanvasTreeView::HandleMouseDoubleClick(const UCEvent &event) {
        TreeNode *doubleClickedNode = GetNodeAtY(event.pointer.y);
        if (doubleClickedNode) {
            if (doubleClickedNode->HasChildren()) {
                ToggleNode(doubleClickedNode);
            }

            if (onNodeDoubleClicked) {
                onNodeDoubleClicked(doubleClickedNode);
            }
            if (showFirstChildOnExpand && doubleClickedNode->IsExpanded()) {
                ExpandFirstChildNode(doubleClickedNode);
            }
            RequestRedraw();
            return true;
        }
        return false;
    }

    bool UltraCanvasTreeView::HandleMouseWheel(const UCEvent &event) {
        if (verticalScrollbar->IsVisible()) {
            // Route through the scrollbar so wheel scrolling is smoothly
            // animated; one line step = one row, wheelScrollLines rows/notch.
            verticalScrollbar->GetStyleRef().scrollSpeed = std::max(1, rowHeight);
            return verticalScrollbar->ScrollByWheel(event.wheelDelta);
        }
        int scrollAmount = event.wheelDelta * rowHeight * 3; // Scroll 3 rows per wheel notch
        ScrollBy(-scrollAmount);
        return true;
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
                    ToggleNode(focusedNode);
                }
                if (onNodeDoubleClicked) {
                    onNodeDoubleClicked(focusedNode);
                }
                if (showFirstChildOnExpand && focusedNode->IsExpanded()) {
                    ExpandFirstChildNode(focusedNode);
                }

                break;
            case 32: // Space
                // With check flags on, the space bar is the keyboard equivalent of
                // clicking the box - the row is already focused, so selecting it
                // again would do nothing visible.
                if (showCheckboxes && focusedNode->data.showCheckbox) {
                    ToggleNodeCheck(focusedNode);
                } else {
                    SelectNode(focusedNode, event.ctrl && selectionMode == TreeSelectionMode::Multiple);
                }
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
                TreeNode *lastVisible = GetLastVisibleNode();
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

        TreeNode *prevNode = GetPreviousVisibleNode(focusedNode);
        if (prevNode) {
            SelectNode(prevNode);
            focusedNode = prevNode;
            ScrollTo(focusedNode);
            if (autoExpandSelectedNode && !focusedNode->children.empty() && !focusedNode->IsExpanded()) {
                ExpandNode(focusedNode);
                if (showFirstChildOnExpand) {
                    ExpandFirstChildNode(focusedNode);
                }
            }
        }
    }

    void UltraCanvasTreeView::NavigateDown() {
        if (!focusedNode) return;

        TreeNode *nextNode = GetNextVisibleNode(focusedNode);
        if (nextNode) {
            SelectNode(nextNode);
            focusedNode = nextNode;
            ScrollTo(focusedNode);
            if (autoExpandSelectedNode && !focusedNode->children.empty() && !focusedNode->IsExpanded()) {
                ExpandNode(focusedNode);
                if (showFirstChildOnExpand) {
                    ExpandFirstChildNode(focusedNode);
                }
            }
        }
    }

    TreeNode *UltraCanvasTreeView::GetPreviousVisibleNode(TreeNode *current) {
        if (!current || !rootNode) return nullptr;

        // Build list of all visible nodes
        std::vector<TreeNode *> visibleNodes;
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

        std::vector<TreeNode *> visibleNodes;
        BuildVisibleNodeList(rootNode.get(), visibleNodes);

        return visibleNodes.empty() ? nullptr : visibleNodes.back();
    }

    void UltraCanvasTreeView::ScrollDownIfLastVisibleParent(TreeNode *node) {
        // Only parent nodes hint at hidden content; leaves are ignored so a
        // childless row at the bottom does not move the view.
        if (!node || !node->HasChildren()) return;

        // Nothing below to reveal: already scrolled to the bottom.
        if (scrollOffsetY >= maxScrollY) return;

        int nodeY = GetNodeDisplayY(node);
        int visibleBottom = scrollOffsetY + GetHeight();

        // The node counts as the last visible row when the row directly beneath
        // it would not fully fit inside the current viewport. In that case scroll
        // down exactly one row so the next entry peeks into view.
        if (nodeY + 2 * rowHeight > visibleBottom) {
            ScrollBy(rowHeight);
        }
    }

    TreeNode *UltraCanvasTreeView::GetNextVisibleNode(TreeNode *current) {
        if (!current || !rootNode) return nullptr;

        // Build list of all visible nodes
        std::vector<TreeNode *> visibleNodes;
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
            for (auto &child: node->children) {
                BuildVisibleNodeList(child.get(), list);
            }
        }
    }

    void UltraCanvasTreeView::ExpandFirstChildNode(TreeNode *node) {
        if (!node || !node->HasChildren()) return;
        // Per-node opt-out: nodes that carry their own content (e.g. an overview page)
        // keep the selection on themselves instead of jumping to the first child.
        if (!node->data.showFirstChildOnExpand) return;
        SelectNode(node->FirstChild(), false);
    }

    void UltraCanvasTreeView::SetWindow(UltraCanvasWindowBase *win) {
        UltraCanvasUIElement::SetWindow(win);
        if (verticalScrollbar) {
            verticalScrollbar->SetWindow(win);
        }
    }

    void UltraCanvasTreeView::CreateScrollbar() {
        verticalScrollbar = std::make_shared<UltraCanvasScrollbar>(
                GetIdentifier() + "_vscroll", 0, 0, scrollbarStyle.trackSize, 100,
                ScrollbarOrientation::Vertical);
        verticalScrollbar->onScrollChange = [this](int pos) {
            scrollAnim.Cancel();   // the scrollbar owns the position here
            scrollOffsetY = pos;
            RequestRedraw();
        };
        verticalScrollbar->SetStyle(scrollbarStyle);
        verticalScrollbar->SetVisible(false);
    }

    void UltraCanvasTreeView::SetVerticalScrollbarStyle(const ScrollbarStyle& style) {
        scrollbarStyle = style;
        if (verticalScrollbar) {
            verticalScrollbar->SetStyle(scrollbarStyle);
        }
        // The track size feeds row width (rows shrink by sbWidth) and the
        // scrollbar's own bounds, both resolved in UpdateScrollbars().
        UpdateScrollbars();
        RequestRedraw();
    }
}
