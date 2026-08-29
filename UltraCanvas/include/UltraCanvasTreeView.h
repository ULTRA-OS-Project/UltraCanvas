// include/UltraCanvasTreeView.h
// Hierarchical tree view with icons and text for each row
// Last Modified: 2026-06-04
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasUIElement.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include "UltraCanvasScrollbar.h"
#include "UltraCanvasSmoothScroll.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <map>
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

// ===== SCROLL-TO-TOP BUTTON STYLE =====
// Look and thresholds of the floating "move to the top" button that the tree
// draws over the bottom-right corner of its content area once the rows overflow
// the visible area and the view has been scrolled down.
// See UltraCanvasTreeView::SetShowScrollToTopButton().
struct TreeScrollToTopStyle {
    int   size            = 24;    // button width/height in px
    int   margin          = 8;     // gap between the button and the content edges
    int   minHiddenRows   = 3;     // show only when MORE than this many rows are out of view
    int   keepClearRows   = 3;     // rows at the end of the tree the button must never cover
    float cornerRadius    = 12.0f; // 0 => square; >= size/2 => fully rounded
    Color background      = Color(0x3C, 0x3C, 0x3C, 0xC0);  // translucent so rows stay readable
    Color hoverBackground = Color(0x1E, 0x1E, 0x1E, 0xF0);
    Color borderColor     = Color(0xFF, 0xFF, 0xFF, 0x60);
    Color arrowColor      = Colors::White;
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

// ===== COLUMN CELL DATA =====
// One cell value for a column tree view (see UltraCanvasColumnsTreeView). The
// tree/name column reads its text from TreeNodeData::text; every other column
// reads its cell from TreeNodeData::cells, keyed by the column's id.
struct TreeCellData {
    std::string text;                          // cell text
    Color       textColor = Colors::Transparent; // Transparent => use the column's default color
    TreeCellData() = default;
    TreeCellData(std::string t, Color c = Colors::Transparent)
        : text(std::move(t)), textColor(c) {}
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

    // ----- Optional columns (used by column tree views, see UltraCanvasColumnsTreeView) -----
    // The base tree renders a single line from `text`. A column tree view treats
    // `text` as the tree/name column and reads `cells` (keyed by column id) for
    // every other column. `cells` is ignored by the base tree, so setting it is
    // always safe.
    std::map<std::string, TreeCellData> cells; // per-column values, keyed by TreeViewColumn::id
    bool  isGroupHeader = false;  // Render this row as a full-width section-header bar (Line/Loop/...)

    // Set/get a column cell by column id. The tree/name column uses `text`, not `cells`.
    void SetCell(const std::string& colId, std::string text, Color color = Colors::Transparent) {
        cells[colId] = TreeCellData(std::move(text), color);
    }
    const TreeCellData* GetCell(const std::string& colId) const {
        auto it = cells.find(colId);
        return it == cells.end() ? nullptr : &it->second;
    }
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
    // Remembers that the node was expanded when its last child was removed,
    // so AddChild can restore the expanded state instead of collapsing.
    // Lazy-loading callers swap a placeholder child for the real children on
    // expansion; without this the node would silently snap shut.
    bool wasExpandedBeforeEmptied = false;
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
    bool rootVisible = true;      // false: the root's children are the top level
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
    float fontSize = 12.0f;        // Row label font size
    bool showRootLines;            // Show lines for root level
    bool showExpandButtons;        // Show +/- buttons
    bool showFirstChildOnExpand;   // auto open first child on expand node
    bool autoExpandSelectedNode;  // auto expand selected node
    bool autoSortChildren = false; // keep children sorted alphabetically on insert
    bool autoSortAscending = true; // direction used by auto-sort

    // Active sort (applied by SetSortMode / re-applied by SortAllNodes)
    TreeSortMode sortMode = TreeSortMode::NoSort;
    bool sortAscending = true;

    // Colors
//    Color backgroundColor;       // Tree background color
    Color selectionColor;       // Selected row background
    Color hoverColor;           // Hovered row background
    Color lineColor;            // Connecting line color
    Color textColor;            // Default text color
    Color expandButtonColor = Color(0xE0, 0xE0, 0xE0); // +/- node icon background
    Color dropTargetColor = Color(0x33, 0x99, 0xFF, 0x66); // drag-drop target row

    // The node currently highlighted as a drag-and-drop target, if any.
    TreeNode* dropTargetNode = nullptr;

    ScrollbarStyle scrollbarStyle = GetDefaultScrollbarStyleOr(ScrollbarStyle::Default());

    // Scrolling (using unified scrollbar)
    std::shared_ptr<UltraCanvasScrollbar> verticalScrollbar;

    // Scrolling
    int scrollOffsetY;             // Vertical scroll offset
    // Wheel scrolling normally goes through verticalScrollbar, which animates
    // itself. This drives the same glide for the direct ScrollBy() path used
    // when no scrollbar is up (see UltraCanvasSmoothScroll.h); everything that
    // positions the tree exactly — the scrollbar's own callback, revealing a
    // node, a rebuilt tree returning to the top — cancels it first.
    UltraCanvasSmoothScroll scrollAnim;
    int maxScrollY;                // Maximum scroll value

    // Floating "move to the top" button (bottom-right of the content area)
    bool showScrollToTopButton = true;   // feature is on by default
    bool scrollToTopHovered = false;     // pointer currently over the button
    TreeScrollToTopStyle scrollToTopStyle;

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
    // Files dragged in from elsewhere (the file list, another app) and dropped
    // on a node. `onFilesDragAccept` decides whether a node is a valid drop
    // target - only accepted nodes get the drop highlight. `onFilesDroppedOnNode`
    // performs the drop and returns whether it handled it.
    std::function<bool(TreeNode* target)> onFilesDragAccept;
    std::function<bool(TreeNode* target,
                       const std::vector<std::string>& files)> onFilesDroppedOnNode;
    // Right mouse button released over a node. The event is passed along so a
    // handler can open a context menu at the pointer (event.pointerWindow).
    // A right press never changes the selection — the menu acts on the node
    // it was opened over, while the selected node stays where it was.
    std::function<void(TreeNode*, const UCEvent&)> onNodeRightClicked;
    
    // ===== CONSTRUCTOR =====
    UltraCanvasTreeView(const std::string& identifier, 
                       float x, float y, float w, float h);

    UltraCanvasTreeView(const std::string& identifier,
                        float w, float h);

    UltraCanvasTreeView(const std::string& identifier = "TreeView");

    // ===== TREE STRUCTURE MANAGEMENT =====
    TreeNode* SetRootNode(const TreeNodeData& rootData);
    
    TreeNode* GetRootNode() const { return rootNode.get(); }

    // Hide the root row and draw its children as the top level, so the tree
    // shows a forest rather than one node with everything under it. The root
    // still owns the nodes (SetRootNode / AddNode are unchanged) and is kept
    // expanded while it is hidden — it is simply never drawn, hit-tested or
    // counted as a row. Default: the root is visible.
    void SetRootVisible(bool visible);
    bool IsRootVisible() const { return rootVisible; }
    
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
    // Expand a collapsed node, collapse an expanded one - through ExpandNode /
    // CollapseNode, so onNodeExpanded / onNodeCollapsed always fire. Every
    // user gesture that toggles a node must go through here (or those two):
    // a bare TreeNode::Toggle() flips the state without notifying, and a
    // lazily-loaded tree then shows an expanded node with only its "..."
    // placeholder, because the host never got the callback that loads the
    // real children.
    void ToggleNode(TreeNode* node);
    void ExpandAll();
    void CollapseAll();

    // ===== VISUAL PROPERTIES =====
    void SetRowHeight(int height) { rowHeight = height; UpdateScrollbars(); }
    int GetRowHeight() const { return rowHeight; }
    
    void SetIndentSize(int size) { indentSize = size; }
    int GetIndentSize() const { return indentSize; }

    void SetFontSize(float size) { fontSize = size; }
    float GetFontSize() const { return fontSize; }
    
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
    void SetExpandButtonColor(const Color &color) { expandButtonColor = color; }

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
    // Jump back to the first row (animated when the scrollbar has smooth scrolling on).
    void ScrollToTop();

    // ===== SCROLL-TO-TOP BUTTON =====
    // A floating "move to the top" button drawn over the bottom-right corner of
    // the content area. It appears once the view is scrolled down AND more than
    // TreeScrollToTopStyle::minHiddenRows rows are outside the visible area, and
    // slides up as the view nears the end of the list so it never covers the last
    // TreeScrollToTopStyle::keepClearRows rows. Enabled by default.
    void SetShowScrollToTopButton(bool show);
    bool GetShowScrollToTopButton() const { return showScrollToTopButton; }

    void SetScrollToTopButtonStyle(const TreeScrollToTopStyle& style);
    const TreeScrollToTopStyle& GetScrollToTopButtonStyle() const { return scrollToTopStyle; }

    // True while the button is actually on screen (feature enabled, view scrolled
    // down AND enough rows hidden). Useful for tests and for callers that draw
    // their own overlay.
    bool IsScrollToTopButtonActive() const;

    // Element-local rect the button occupies right now; empty when not active.
    Rect2Di GetScrollToTopButtonRect() const;


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

protected:
    // ===== ROW RENDERING EXTENSION POINTS =====
    // Hooks that let a subclass (e.g. UltraCanvasColumnsTreeView) customise how a
    // row is drawn without re-implementing the whole RenderNode traversal.

    // Draw a row that occupies the entire width (e.g. a section-header bar) instead
    // of the normal background/expander/label. Return true if the row was fully
    // handled, in which case RenderNode skips its own drawing for this node.
    virtual bool RenderNodeFullRow(IRenderContext* ctx, TreeNode* node, int nodeY,
                                   const Rect2Di& contentRect, int rowWidth) { return false; }

    // Draw the row's content to the right of the expander/left-icon. The base draws
    // a single text run + optional right icon (Classic). Subclasses override to draw
    // columns.
    virtual void RenderNodeLabel(IRenderContext* ctx, TreeNode* node, int nodeY,
                                 int textX, int nodeWidth, int sbWidth,
                                 const Rect2Di& contentRect);

    // ===== OPTIONAL FIXED HEADER BAND =====
    // Height (px) of a fixed header band drawn at the top of the content area; the
    // rows scroll beneath it. The base tree has no header, so this returns 0 and
    // every layout/scroll/hit-test calculation below becomes a no-op. A subclass
    // (e.g. UltraCanvasColumnsTreeView) overrides both to draw column titles.
    virtual int  GetHeaderHeight() const { return 0; }
    virtual void RenderHeader(IRenderContext* ctx, const Rect2Di& headerRect) {}

    // Read-only access for subclass renderers.
    int   GetTextPadding() const { return textPadding; }
    Color GetTextColor()   const { return textColor; }
    Color GetExpandButtonColor() const { return expandButtonColor; }
    // Width reserved on the right for the vertical scrollbar (0 when hidden).
    int   GetVerticalScrollbarWidth() const {
        return (verticalScrollbar && verticalScrollbar->IsVisible()) ? verticalScrollbar->GetWidth() : 0;
    }
    // Recompute scrollbar geometry (e.g. after a subclass changes GetHeaderHeight()).
    void  UpdateScrollbars();

private:

    // ===== SCROLLBAR MANAGEMENT =====
    void CreateScrollbar();

    void ClampScrollOffset();

    int GetTotalVisibleHeight();

    // ===== SCROLL-TO-TOP BUTTON =====
    void RenderScrollToTopButton(IRenderContext* ctx);
    // Consume pointer events that land on the button (hover tracking + click).
    // Returns true when the event was handled and must not reach the rows.
    bool HandleScrollToTopButtonEvent(const UCEvent& event);

    int GetNodeDisplayY(TreeNode* node);
    
    TreeNode* GetNodeAtY(int y);
    
    void RenderNode(IRenderContext *ctx, TreeNode* node, int& currentY, int level, const Rect2Di& contentRect);

    void ExpandNodeRecursive(TreeNode* node);
    void CollapseNodeRecursive(TreeNode* node);
    
    // ===== EVENT HANDLERS =====
    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseDoubleClick(const UCEvent& event);
    bool HandleMouseWheel(const UCEvent& event);
    void HandleKeyDown(const UCEvent& event);
    bool HandleDragOver(const UCEvent& event);
    bool HandleDrop(const UCEvent& event);
    
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

    TreeViewBuilder& SetShowScrollToTopButton(bool show) {
        treeView->SetShowScrollToTopButton(show);
        return *this;
    }

    TreeViewBuilder& SetScrollToTopButtonStyle(const TreeScrollToTopStyle& style) {
        treeView->SetScrollToTopButtonStyle(style);
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
    
    TreeViewBuilder& OnNodeRightClicked(std::function<void(TreeNode*, const UCEvent&)> callback) {
        treeView->onNodeRightClicked = callback;
        return *this;
    }
    
    std::shared_ptr<UltraCanvasTreeView> Build() {
        return treeView;
    }
};

} // namespace UltraCanvas
