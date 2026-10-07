# UltraCanvasTreeView Documentation

<!-- doc-check: void ShowPage(const std::string& pageId); void OpenItem(TreeNode* node); void LoadChildrenFromDisk(TreeNode* node); void ShowContextMenu(TreeNode* node, const UCEvent& event); std::shared_ptr<UltraCanvasSplitPane> splitPane; -->

## Overview

The `UltraCanvasTreeView` is a hierarchical tree view control component that provides a powerful and flexible way to display tree-structured data with support for icons, custom styling, selection modes, and interactive features. It is part of the UltraCanvas cross-platform UI framework.

**Version:** 1.2.1  
**Last Modified:** 2026-10-07  
**Files:** 
- Header: `include/UltraCanvasTreeView.h` (columns variant: `include/UltraCanvasColumnsTreeView.h`)
- Implementation: `core/UltraCanvasTreeView.cpp`, `core/UltraCanvasColumnsTreeView.cpp`

## Features

### Core Capabilities
- **Hierarchical Data Display**: Multi-level tree structure with parent-child relationships
- **Dual Icon Support**: Left and right icons for each node
- **Selection Modes**: Single, multiple, or no selection
- **Check Flags**: Optional tri-state checkbox per row, with subtree propagation
- **Connecting Lines**: Dotted or solid parent/child connectors, root level included
- **Visual Customization**: Colors, fonts, line styles, and spacing
- **Scrolling**: Automatic scrollbar when content exceeds viewport
- **Scroll-to-Top Button**: Floating "move to the top" affordance for long trees
- **Keyboard Navigation**: Full keyboard support with arrow keys
- **Mouse Interaction**: Click, double-click, drag & drop support
- **Dynamic Updates**: Add, remove, expand, collapse nodes at runtime
- **Event System**: Comprehensive event callbacks for user interactions

## Class Architecture

### Main Classes

#### `UltraCanvasTreeView`
The main tree view component class that extends `UltraCanvasUIElement`.

#### `TreeNode`
Represents individual nodes in the tree hierarchy.

#### `TreeNodeData`
Data structure containing node information (text, icons, styling).

#### `TreeViewBuilder`
Convenience builder class for fluent configuration.

## Data Structures

### Enumerations

```cpp
enum class TreeNodeState {
    Collapsed = 0,    // Node is collapsed
    Expanded = 1,     // Node is expanded
    Leaf = 2          // Node has no children
};

enum class TreeSelectionMode {
    NoSelection = 0,  // No selection allowed
    Single = 1,       // Single node selection
    Multiple = 2      // Multiple node selection
};

enum class TreeLineStyle {
    NoLine = 0,       // No connecting lines
    Dotted = 1,       // Dotted lines
    Solid = 2         // Solid lines
};

enum class TreeCheckState {
    Unchecked = 0,
    Checked = 1,
    Mixed = 2         // Part of the subtree is checked (see Check Flags)
};

enum class TreeSortMode {
    NoSort = 0,       // Preserve insertion order
    Alphabetic = 1,   // By display name (data.text), case-insensitive
    LastAccess = 2    // By data.accessSequence (most-recent first when descending)
};
```

### TreeNodeIcon Structure

```cpp
struct TreeNodeIcon {
    std::string iconPath;  // Path to icon file
    int width = 16;        // Icon width
    int height = 16;       // Icon height
    bool visible = true;   // Visibility flag

    TreeNodeIcon() = default;
    TreeNodeIcon(const std::string& path, int w = 16, int h = 16);
};
```

### TreeNodeData Structure

```cpp
struct TreeNodeData {
    std::string nodeId;                    // Unique identifier
    std::string text;                      // Display text
    TreeNodeIcon leftIcon;                 // Left-side icon
    TreeNodeIcon rightIcon;                // Right-side icon
    bool enabled = true;                   // Interaction enabled
    bool visible = true;                   // Visibility flag
    bool showFirstChildOnExpand = true;    // false: opt out of "jump to first entry"
    Color textColor = Colors::Black;       // Text color
    Color backgroundColor = Colors::Transparent; // Background color
    std::string tooltip;                   // Tooltip text
    void* userData = nullptr;              // Custom user data

    // Check flag — drawn only while the tree runs with SetShowCheckboxes(true).
    TreeCheckState checkState = TreeCheckState::Unchecked;
    bool showCheckbox = true;              // false: keep the slot, draw no box

    // Optional columns — read by column tree views (UltraCanvasColumnsTreeView);
    // ignored by the base tree, so setting them is always safe.
    std::map<std::string, TreeCellData> cells; // per-column values, keyed by TreeViewColumn::id
    bool isGroupHeader = false;            // Full-width section-header bar

    void SetCell(const std::string& colId, std::string text, Color color = Colors::Transparent);
    const TreeCellData* GetCell(const std::string& colId) const;

    uint64_t accessSequence = 0;           // Sort key for TreeSortMode::LastAccess

    TreeNodeData() = default;
    TreeNodeData(const std::string& id, const std::string& displayText);
};
```

A column cell is a text plus an optional colour:

```cpp
struct TreeCellData {
    std::string text;                          // cell text
    Color       textColor = Colors::Transparent; // Transparent => the column's default colour
};
```

### Columnar Layout: `UltraCanvasColumnsTreeView`

The base `UltraCanvasTreeView` always renders one text run per row (`data.text`). For
IDE-style debugger "Variables" / "Watch" panels, the framework provides the subclass
`UltraCanvasColumnsTreeView` (in `UltraCanvasColumnsTreeView.h`), which adds a columnar
row layout. It takes the same constructors as the base tree and renders each row in
one of two modes, switchable at runtime with `SetDisplayMode()`:

- **Columns** (default) — the aligned columns defined with `SetColumns()`. The tree
  column (`isTreeColumn = true`) holds the indent, expander and icon and draws
  `data.text`; every other column draws `data.cells[column.id]` (set with
  `data.SetCell()`). Nodes with `data.isGroupHeader = true` render as full-width
  section bars.
- **Classic** — delegates to the base single-text layout (`data.text` per row).

Hierarchy and expand/collapse work in both modes.

```cpp
enum class TreeDisplayMode {
    Classic = 0,      // single-text rows (delegates to the base tree)
    Columns = 1       // aligned, user-defined columns (default)
};

struct TreeViewColumn {
    std::string   id;                        // key into TreeNodeData::cells
    std::string   title;                     // header label (drawn only when a header is enabled)
    int           width      = 0;            // > 0 => fixed px; <= 0 => flexible (shares remaining space)
    int           minWidth   = 0;            // floor for flexible columns / resize
    float         flexWeight = 1.0f;         // relative share among flexible columns
    TextAlignment alignment  = TextAlignment::Left;
    Color         textColor  = Color(40, 40, 40);         // default cell text colour
    Color         accentBackground = Colors::Transparent; // optional fill behind the cell
    int           accentPadding    = 0;      // px added around the accent fill
    bool          isTreeColumn = false;      // hosts indent/expander/icon; draws node->data.text
};
```

```cpp
void SetDisplayMode(TreeDisplayMode mode);
TreeDisplayMode GetDisplayMode() const;

void SetColumns(std::vector<TreeViewColumn> cols);
const std::vector<TreeViewColumn>& GetColumns() const;
int  TreeColumnIndex() const;              // first isTreeColumn, else 0

void SetShowColumnHeader(bool show);       // header band with the titles; off by default
bool GetShowColumnHeader() const;

void SetColumnsResizable(bool on);         // drag column boundaries; on by default
bool GetColumnsResizable() const;

void SetColumnStyle(const TreeColumnStyle& style);
const TreeColumnStyle& GetColumnStyle() const;
```

When `SetColumns()` is never called, a default Name / Type / Value set is installed
with the ids `"name"` (tree column), `"type"` (128 px, orange accent) and `"value"`.

Global geometry and colours that are not per column live in `TreeColumnStyle`:

```cpp
struct TreeColumnStyle {
    int   columnGap             = 8;      // horizontal gap between columns (px)
    bool  showColumnSeparators  = false;  // thin vertical rules between columns
    Color columnSeparatorColor  = Color(210, 210, 210);
    Color groupHeaderBackground = Colors::Black;   // full-width section-header bar
    Color groupHeaderTextColor  = Colors::White;   // section-header text

    // Header band (drawn only when SetShowColumnHeader(true)).
    int   headerHeight          = 22;
    Color headerBackground      = Color(60, 60, 60);
    Color headerTextColor       = Color(220, 220, 220);
    Color headerBorderColor     = Color(90, 90, 90);
};
```

```cpp
auto varsTree = std::make_shared<UltraCanvasColumnsTreeView>("VarsTree", 300, 330);
varsTree->SetColumns({
    { "name",  "Name",  0,   0,  2.0f, TextAlignment::Left, Color(40, 40, 40), Colors::Transparent,  0, true  },
    { "type",  "Type",  128, 0,  1.0f, TextAlignment::Left, Color(40, 40, 40), Color(255, 190, 130), 4, false },
    { "value", "Value", 0,   48, 1.0f, TextAlignment::Left, Color(40, 40, 40), Colors::Transparent,  0, false },
});
varsTree->SetShowColumnHeader(true);

TreeNodeData section("grp_loop", "Loop");
section.isGroupHeader = true;
varsTree->SetRootNode(section);

TreeNodeData var("v_width", "width");
var.SetCell("type", "int");
var.SetCell("value", "257");
varsTree->AddNode("grp_loop", var);
varsTree->ExpandAll();
```

This layout is intended for IDE-style debugger "Variables" / "Watch" panels — the
full example is in `Apps/DemoApp/UltraCanvasTreeViewExamples.cpp`.

### Sorting Modes

`SetSortMode(TreeSortMode, bool ascending)` sorts the whole tree and remembers the
choice. `Alphabetic` compares `data.text` case-insensitively; `LastAccess` compares
`data.accessSequence` (stamp it when a variable is read/written, then sort
descending to float the most recently touched entries to the top). Node-level and
alphabetical helpers remain available:

```cpp
void SetSortMode(TreeSortMode mode, bool ascending = true);
TreeSortMode GetSortMode() const;
bool GetSortAscending() const;

void SetAutoSortChildren(bool enable, bool ascending = true);  // keep children sorted on insert
bool GetAutoSortChildren() const;

void SortNodeChildren(const std::string& nodeId, bool recursive = false, bool ascending = true);
void SortNodeChildren(TreeNode* node, bool recursive = false, bool ascending = true);
void SortAllNodes(bool ascending = true);   // whole tree, from the root, recursively
```

```cpp
// IDE debugger Variables panel in columns, sorted by most-recent access
auto tree = std::make_shared<UltraCanvasColumnsTreeView>("vars");
tree->SetDisplayMode(TreeDisplayMode::Columns);   // Columns is the default
tree->SetSortMode(TreeSortMode::LastAccess, /*ascending=*/false);
```

## API Reference

### Constructor

```cpp
UltraCanvasTreeView(const std::string& identifier,
                    float x, float y, float w, float h);

UltraCanvasTreeView(const std::string& identifier,
                    float w, float h);

UltraCanvasTreeView(const std::string& identifier = "TreeView");
```

Creates a new tree view control.

**Parameters:**
- `identifier`: Unique string identifier for the control
- `x`, `y`: Position coordinates (omit them when a layout places the tree)
- `w`, `h`: Width and height dimensions

### Tree Structure Management

#### `SetRootNode`
```cpp
TreeNode* SetRootNode(const TreeNodeData& rootData);
TreeNode* GetRootNode() const;
```
Sets (or returns) the root node of the tree.

#### `SetRootVisible`
```cpp
void SetRootVisible(bool visible);   // default: true
bool IsRootVisible() const;
```
Hides the root row and draws its children as the top level, turning the tree
into a **forest** — several independent sections side by side instead of one
node with everything under it. The root still owns the nodes (`SetRootNode` /
`AddNode` are unchanged) and is kept expanded while it is hidden; it is simply
never drawn, hit-tested or counted as a row, so indentation, scrolling and
clicks all behave as if the children were the top of the tree.

```cpp
tree->SetRootVisible(false);
tree->SetRootNode(TreeNodeData("root", ""));   // never seen
tree->AddNode("root", TreeNodeData("pinned", "Pinned"));
tree->AddNode("root", TreeNodeData("computer", "Computer"));
```

A section can be taken out of the tree without removing its nodes by clearing
`node->data.visible` — the UltraFiler's folder tree hides its whole "Pinned"
section that way while nothing is pinned.

#### `AddNode`
```cpp
TreeNode* AddNode(const std::string& parentId, const TreeNodeData& nodeData);
```
Adds a new node as a child of the specified parent.

#### `RemoveNode`
```cpp
void RemoveNode(const std::string& nodeId);
```
Removes a node and all its children from the tree.

#### `FindNode`
```cpp
TreeNode* FindNode(const std::string& nodeId);
```
Finds a node by its ID.

### Selection Management

#### `SelectNode`
```cpp
void SelectNode(TreeNode* node, bool addToSelection = false);
```
Selects a node, optionally adding to existing selection.

#### `DeselectNode` / `ClearSelection`
```cpp
void DeselectNode(TreeNode* node);
void ClearSelection();
```
Removes one node from the selection, or clears all selected nodes.

#### `GetSelectedNodes`
```cpp
const std::vector<TreeNode*>& GetSelectedNodes() const;
TreeNode* GetFirstSelectedNode() const;   // nullptr when nothing is selected
```
Returns all currently selected nodes, or the first of them.

### Expansion Management

#### `ExpandNode` / `CollapseNode` / `ToggleNode`
```cpp
void ExpandNode(TreeNode* node);
void CollapseNode(TreeNode* node);
void ToggleNode(TreeNode* node);
```
Expands or collapses a specific node; `ToggleNode` flips between the two.
All three fire `onNodeExpanded` / `onNodeCollapsed`, and every built-in
gesture that toggles a node — the expand button, a double-click, the Enter
key — goes through them. Hosts that lazily load children in
`onNodeExpanded` (see below) depend on that: `TreeNode::Expand()` /
`Collapse()` / `Toggle()` flip the state **without** notifying, so calling
those directly on a lazily-loaded tree leaves an expanded node showing only
its placeholder child.

#### `ExpandAll` / `CollapseAll`
```cpp
void ExpandAll();
void CollapseAll();
```
Expands or collapses all nodes in the tree.

#### Jump to first entry
```cpp
void SetShowFirstChildOnExpand(bool show);   // default false
bool GetShowFirstChildOnExpand() const;
void SetAutoExpandSelectedNode(bool expand); // default false
bool GetAutoExpandSelectedNode() const;
```
With "jump to first entry" on, expanding a parent - by its button, a double
click or Enter - selects its first child, and so does a single click on a
parent that is already open; the arrow keys step over open parents, from the
first child of one heading straight to the last child of the one before. It
suits a tree whose headings have no content
of their own, such as a settings tree where a heading shows its first sub
page: the heading is then never the row left selected. A node opts out
through `TreeNodeData::showFirstChildOnExpand = false`, keeping the
selection on itself (an overview page, say). With `SetAutoExpandSelectedNode`
on as well, selecting a collapsed parent opens it first, so the jump happens
from a click on a closed heading too.

```cpp
tree->SetShowFirstChildOnExpand(true);
tree->SetAutoExpandSelectedNode(true);
tree->ExpandAll();
tree->onNodeSelected = [](TreeNode* node) { ShowPage(node->data.nodeId); };
```

### Visual Properties

#### Row Height
```cpp
void SetRowHeight(int height);
int GetRowHeight() const;
```
Sets/gets the height of each row in pixels.

#### Indentation
```cpp
void SetIndentSize(int size);
int GetIndentSize() const;
```
Sets/gets the indentation size per level (default 16).

Every row reserves the same 16px slot for the expand/collapse button, whether
or not the node has children, so the icon and the label of a childless node
line up with those of its expandable siblings instead of sliding one button
width to the left. `SetShowExpandButtons(false)` drops the slot from all rows.

#### Font Size
```cpp
void SetFontSize(float size);
float GetFontSize() const;
```
Sets/gets the font size used for the row labels (default 12). Also used by
`UltraCanvasColumnsTreeView` for its cell text, column headers and group
headers.

#### Selection Mode
```cpp
void SetSelectionMode(TreeSelectionMode mode);
TreeSelectionMode GetSelectionMode() const;
```
Sets/gets the selection mode.

#### Line Style
```cpp
void SetLineStyle(TreeLineStyle style);
TreeLineStyle GetLineStyle() const;
```
Sets/gets the connecting line style (default `TreeLineStyle::Dotted`; use
`TreeLineStyle::NoLine` for a tree without connectors).

The connectors are drawn the way a file manager draws them: a vertical line
descends from the centre of a parent's expand button through its children, a
horizontal stub joins each child's row to it, and the line stops at the last
child. Deeper levels keep the trunks of the ancestors that still have rows to
come below. The stub ends at the child's expand button, or reaches its icon
when the child has none. Lines are drawn over the row background, so they stay
visible on the selected row, and take `SetLineColor` (default gray).

#### Root Lines
```cpp
void SetShowRootLines(bool show);
bool GetShowRootLines() const;
```
Connects the **top-level** rows to each other as well: a trunk down the left
margin with a stub into every top-level row, exactly as the levels below are
drawn. The rows move one indent right to make room for it, so the margin is
only taken while that trunk is actually drawn. Default: on.

It does nothing under `TreeLineStyle::NoLine`, and nothing on a tree whose root
is visible — there the root row already is the trunk every other row hangs
from. It is a forest (`SetRootVisible(false)`, several top-level rows) that has
something to connect.

### Check Flags (Checkable Nodes)

A tree can draw a check flag on every row, between the expand button and the
icon. The flags are independent of the row selection — selecting, expanding and
multi-selection all keep working as before — so one tree can answer both "which
row am I looking at" and "which rows did I tick".

```cpp
void SetShowCheckboxes(bool show);      // off by default
bool GetShowCheckboxes() const;

void SetCheckPropagation(bool enable);  // on by default
bool GetCheckPropagation() const;

void SetNodeChecked(TreeNode* node, bool checked);
void SetNodeChecked(const std::string& nodeId, bool checked);
void SetNodeCheckState(TreeNode* node, TreeCheckState state);
void ToggleNodeCheck(TreeNode* node);
TreeCheckState GetNodeCheckState(TreeNode* node) const;
bool IsNodeChecked(TreeNode* node) const;
std::vector<TreeNode*> GetCheckedNodes() const;
void SetAllChecked(bool checked);

void SetCheckboxColors(const Color& background, const Color& border, const Color& check);
std::function<void(TreeNode*, TreeCheckState)> onNodeCheckChanged;
```

`TreeCheckState` is `Unchecked`, `Checked` or `Mixed`. Mixed is what a parent
shows while only part of its subtree is flagged — a filled square rather than a
tick, so "some" never reads as "all"; clicking it completes the subtree.

With **propagation** on (the default) a flag carries down the subtree and every
ancestor follows as Checked or Mixed. With it off, each row carries its own flag
and nothing else moves.

The flag is toggled by a click on the box — which leaves the selection where it
was — or by the space bar on the focused row. `onNodeCheckChanged` fires once
per row whose state actually moved, propagated parents and children included,
so a "3 of 12 flagged" caption can be kept up to date from it. An individual row
can drop its box with `TreeNodeData::showCheckbox = false` (a section header, a
row that must not be picked); the row keeps the slot, so everything stays
aligned.

```cpp
auto backupTree = std::make_shared<UltraCanvasTreeView>("Backup", 20, 20, 300, 240);
backupTree->SetShowCheckboxes(true);
backupTree->SetRootVisible(false);
backupTree->SetRootNode(TreeNodeData("root", ""));
backupTree->AddNode("root", TreeNodeData("docs", "Documents"));
backupTree->AddNode("docs", TreeNodeData("invoice", "Invoice.odt"));
backupTree->AddNode("docs", TreeNodeData("report", "Report.pdf"));
backupTree->ExpandAll();
backupTree->SetNodeChecked("invoice", true);   // "Documents" turns Mixed

backupTree->onNodeCheckChanged = [backupTree](TreeNode*, TreeCheckState) {
    debugOutput << backupTree->GetCheckedNodes().size() << " rows flagged" << std::endl;
};
```

### Scroll-to-Top Button

When a tree grows past its viewport, getting back to the root can take a lot of
wheel turns. The tree therefore draws a small floating **"move to the top"**
button over the bottom-right corner of its content area once the view has been
scrolled down:

```cpp
void SetShowScrollToTopButton(bool show);
bool GetShowScrollToTopButton() const;

void SetScrollToTopButtonStyle(const TreeScrollToTopStyle& style);
const TreeScrollToTopStyle& GetScrollToTopButtonStyle() const;

bool    IsScrollToTopButtonActive() const;   // on screen right now?
Rect2Di GetScrollToTopButtonRect() const;   // element-local rect (empty when inactive)

void ScrollToTop();                          // what the button does; callable directly
```

The other scrolling calls bring a node into view or move the view by pixels:

```cpp
void ScrollTo(TreeNode* node);
void ScrollBy(int deltaY);
```

Behaviour:

- **Enabled by default.** `SetShowScrollToTopButton(false)` turns it off, e.g.
  for short trees inside dense dialogs where any overlay is a distraction.
- **Appears only for genuinely long trees**: it stays hidden until *more than*
  `TreeScrollToTopStyle::minHiddenRows` (3 by default) rows sit outside the
  visible area, so a tree that overflows by a row or two never grows a button.
- **Appears only once the user has scrolled down**: while the first row is on
  screen there is nothing to go back to, so the button stays hidden.
- **Dodges the end of the list**: as the view approaches the bottom, the button
  slides up so it never covers the last `TreeScrollToTopStyle::keepClearRows`
  (3 by default) rows. Away from the bottom it rests in the corner.
- **Never overlaps the vertical scrollbar** — it is placed to the left of it.
- Clicking it jumps back to the first row, animated when the scrollbar has
  smooth scrolling enabled. The click is consumed by the button, so the row
  underneath is neither selected nor expanded.

`UltraCanvasColumnsTreeView` inherits the button, and it is placed below the
optional column header band.

```cpp
struct TreeScrollToTopStyle {
    int   size            = 24;    // button width/height in px
    int   margin          = 8;     // gap between the button and the content edges
    int   minHiddenRows   = 3;     // show only when MORE than this many rows are out of view
    int   keepClearRows   = 3;     // rows at the end of the tree the button must never cover
    float cornerRadius    = 12.0f; // 0 => square; >= size/2 => fully rounded
    Color background      = Color(0x3C, 0x3C, 0x3C, 0xC0);
    Color hoverBackground = Color(0x1E, 0x1E, 0x1E, 0xF0);
    Color borderColor     = Color(0xFF, 0xFF, 0xFF, 0x60);
    Color arrowColor      = Colors::White;
};
```

Example — a larger, lighter button that keeps five rows clear:

```cpp
auto tree = std::make_shared<UltraCanvasTreeView>("FileTree", 20, 50, 300, 400);

TreeScrollToTopStyle style;
style.size            = 32;
style.keepClearRows   = 5;
style.background      = Color(0x20, 0x60, 0xC0, 0xC0);
style.hoverBackground = Color(0x20, 0x60, 0xC0, 0xF0);
tree->SetScrollToTopButtonStyle(style);

// ...or switch the affordance off entirely:
// tree->SetShowScrollToTopButton(false);
```

### Measuring the Tree

```cpp
int GetRequiredWidth(IRenderContext* ctx = nullptr);
```

The width the tree needs to show its widest row in full: indent, expander and
check-flag slots, left icon and label text, plus the tree's right padding and
border, the row's right icon and the vertical scrollbar while one is shown. Only
the rows on show count - the children of a collapsed node do not. Text is
measured with `ctx`, or the window's render context when `ctx` is null; the
call returns 0 while there is neither (the tree is not in a window yet), so try
again once the window is up.

```cpp
// Fit a sidebar to its folder names, with 10 px to spare.
if (int need = tree->GetRequiredWidth(); need > 0)
    splitPane->SetPaneFixedSize(0, need + 10);
```

### Color Properties

```cpp
void SetBackgroundColor(const Color& color);
void SetSelectionColor(const Color& color);
void SetHoverColor(const Color& color);
void SetLineColor(const Color& color);
void SetTextColor(const Color& color);
void SetExpandButtonColor(const Color& color);
void SetCheckboxColors(const Color& background, const Color& border, const Color& check);
```
Sets various color properties for the tree view. `SetBackgroundColor` is
inherited from `UltraCanvasUIElement`.

`SetExpandButtonColor` controls the background fill of the `+`/`-` node icon
(default `#E0E0E0`); the icon keeps its gray 1px border and black `+`/`-` glyph.
`SetCheckboxColors` restyles the check flags: the box fill, its outline, and the
colour of both the tick and the Mixed square.

### Event Callbacks

The tree view provides several event callbacks:

```cpp
std::function<void(TreeNode*)> onNodeSelected;
std::function<void(TreeNode*)> onNodeDoubleClicked;
std::function<void(TreeNode*)> onNodeExpanded;
std::function<void(TreeNode*)> onNodeCollapsed;
std::function<void(TreeNode*, TreeNode*)> onNodeDragDrop;   // dragged, target
std::function<void(TreeNode*, TreeCheckState)> onNodeCheckChanged;
// Files dragged in from elsewhere and dropped on a node: onFilesDragAccept
// decides whether a node is a valid target (only accepted nodes get the drop
// highlight); onFilesDroppedOnNode performs the drop and returns whether it did.
std::function<bool(TreeNode* target)> onFilesDragAccept;
std::function<bool(TreeNode* target,
                   const std::vector<std::string>& files)> onFilesDroppedOnNode;
// Right mouse button released over a node; the event carries the pointer
// position (event.pointerWindow) for placing a context menu. A right press
// never changes the selection.
std::function<void(TreeNode*, const UCEvent&)> onNodeRightClicked;
```

## Usage Examples

### Basic Tree Creation

```cpp
// Create tree view
auto treeView = std::make_shared<UltraCanvasTreeView>(
    "MyTree", 10, 10, 300, 400);

// Configure appearance
treeView->SetRowHeight(24);
treeView->SetSelectionMode(TreeSelectionMode::Single);
treeView->SetLineStyle(TreeLineStyle::Solid);

// Set root node
TreeNodeData rootData("root", "Root Node");
rootData.leftIcon = TreeNodeIcon("folder.png", 16, 16);
TreeNode* root = treeView->SetRootNode(rootData);

// Add children
TreeNodeData childData("child1", "Child Node 1");
childData.leftIcon = TreeNodeIcon("file.png", 16, 16);
treeView->AddNode("root", childData);

// Add to window
window->AddChild(treeView);
```

### File Explorer Example

```cpp
// Create file explorer tree
auto fileTree = std::make_shared<UltraCanvasTreeView>(
    "FileExplorer", 0, 0, 350, 600);

// Configure for file browsing
fileTree->SetRowHeight(22);
fileTree->SetSelectionMode(TreeSelectionMode::Single);
fileTree->SetShowExpandButtons(true);

// Create directory structure
TreeNodeData computerData("computer", "My Computer");
computerData.leftIcon = TreeNodeIcon("computer.png");
TreeNode* computer = fileTree->SetRootNode(computerData);

// Add drives
TreeNodeData driveC("c_drive", "Local Disk (C:)");
driveC.leftIcon = TreeNodeIcon("drive.png");
fileTree->AddNode("computer", driveC);

// Add folders
TreeNodeData documents("docs", "Documents");
documents.leftIcon = TreeNodeIcon("folder-brown.svg");
fileTree->AddNode("c_drive", documents);

// Add files
TreeNodeData file("file1", "Document.txt");
file.leftIcon = TreeNodeIcon("text.png");
file.rightIcon = TreeNodeIcon("lock.png", 12, 12); // Security indicator
fileTree->AddNode("docs", file);

// Expand root (ExpandNode, unlike TreeNode::Expand(), fires onNodeExpanded)
fileTree->ExpandNode(computer);
```

### Event Handling

```cpp
// Handle selection
treeView->onNodeSelected = [](TreeNode* node) {
    std::cerr << "Selected: " << node->data.text << std::endl;
    // Update UI based on selection
};

// Handle double-click (the tree has already toggled a parent by then)
treeView->onNodeDoubleClicked = [](TreeNode* node) {
    if (!node->HasChildren()) {
        OpenItem(node);   // open the file or perform the row's action
    }
};

// Handle expansion: lazy loading. A folder is added with a single "..."
// placeholder child so it gets an expand button; on first expansion the host
// replaces the placeholder with the real children.
treeView->onNodeExpanded = [](TreeNode* node) {
    if (node->children.size() == 1 && node->children[0]->data.text == "...") {
        LoadChildrenFromDisk(node);
    }
};

// Handle right-click (open a context menu at the pointer)
treeView->onNodeRightClicked = [](TreeNode* node, const UCEvent& event) {
    // Show a context menu at event.pointerWindow
    ShowContextMenu(node, event);
};
```

### Using TreeViewBuilder

```cpp
auto treeView = TreeViewBuilder("MyTree", 10, 10, 300, 400)
    .SetRowHeight(24)
    .SetIndentSize(20)
    .SetSelectionMode(TreeSelectionMode::Multiple)
    .SetLineStyle(TreeLineStyle::Dotted)
    .SetShowScrollToTopButton(true)
    .SetColors(Colors::White,             // background
               Colors::Blue,              // selection
               Color(230, 240, 250),      // hover
               Colors::Black)             // text
    .Build();
```

The builder also offers `SetShowRootLines()`, `SetShowCheckboxes()`,
`SetAutoSortChildren()`, `SetScrollToTopButtonStyle()` and the callback setters
`OnNodeSelected()`, `OnNodeDoubleClicked()`, `OnNodeExpanded()`,
`OnNodeCollapsed()`, `OnNodeRightClicked()` and `OnNodeCheckChanged()`.

## Keyboard Navigation

The tree view supports comprehensive keyboard navigation:

| Key | Action |
|-----|--------|
| **↑** (Up Arrow) | Navigate to previous visible node |
| **↓** (Down Arrow) | Navigate to next visible node |
| **←** (Left Arrow) | Collapse node or navigate to parent |
| **→** (Right Arrow) | Expand node or navigate to first child |
| **Enter** | Toggle node expansion |
| **Space** | Toggle the row's check flag, or select the row when the tree has none |
| **Home** | Navigate to first node |
| **End** | Navigate to last visible node |
| **Page Up** | Scroll up one page |
| **Page Down** | Scroll down one page |

## Mouse Interaction

### Click Behaviors
- **Single Click**: Select node. With `SetShowFirstChildOnExpand(true)`, a
  click on a parent that is already open selects its first child instead
  (see [Jump to first entry](#jump-to-first-entry))
- **Ctrl+Click**: Add to selection (multi-select mode)
- **Double Click**: Toggle expansion or trigger action
- **Right Click**: Context menu

### Expand/Collapse Button
- Clicking the +/- button expands or collapses the node without selecting it

### Check Flag
- Clicking the check box toggles the row's flag and leaves the selection alone

### Scrolling
- **Mouse Wheel**: Scroll vertically
- **Scrollbar Drag**: Direct scrolling control
- **Scroll-to-Top Button**: Click the floating arrow in the bottom-right corner
  to jump back to the first row (see [Scroll-to-Top Button](#scroll-to-top-button))

## Rendering Details

### Visual Elements
1. **Expand/Collapse Buttons**: +/- indicators for expandable nodes
2. **Connecting Lines**: Optional dotted or solid lines between nodes
3. **Icons**: Left and right icons with configurable sizes
4. **Selection Highlight**: Background color for selected nodes
5. **Hover Highlight**: Background color for hovered nodes
6. **Scrollbar**: Vertical scrollbar when content exceeds viewport
7. **Scroll-to-Top Button**: Floating "move to the top" arrow over the
   bottom-right corner of long trees that have been scrolled down

### Performance Optimizations
- Only visible nodes are rendered (viewport culling)
- Efficient tree traversal algorithms
- Smart scrollbar updates
- Cached layout calculations

## TreeNode Class Methods

```cpp
class TreeNode {
public:
    TreeNodeData data;
    TreeNodeState state;
    int level;                    // depth in tree (0 = root level)
    bool selected;
    bool hovered;
    TreeNode* parent;
    std::vector<std::unique_ptr<TreeNode>> children;

    // Child management
    TreeNode* AddChild(const TreeNodeData& childData);
    void RemoveChild(const std::string& nodeId);
    TreeNode* FindChild(const std::string& nodeId);
    TreeNode* FindDescendant(const std::string& nodeId);
    TreeNode* FirstChild();

    // Sorting of the direct children (recursive=true: every level below too)
    void SortChildNodes(bool recursive = false, bool ascending = true);   // alphabetic
    void SortChildNodes(TreeSortMode mode, bool recursive = false, bool ascending = true);

    // State management - these do not fire the tree's callbacks; use
    // UltraCanvasTreeView::ExpandNode / CollapseNode / ToggleNode for that
    void Expand();
    void Collapse();
    void Toggle();
    bool HasChildren() const;
    bool IsExpanded() const;
    bool IsVisible() const;

    // Utility methods
    int GetVisibleChildCount() const;
    std::vector<TreeNode*> GetVisibleChildren();
};
```

## Default Values

| Property | Default Value |
|----------|--------------|
| Row Height | 20 pixels |
| Indent Size | 16 pixels |
| Icon Spacing | 4 pixels |
| Text Padding | 8 pixels |
| Selection Mode | Single |
| Line Style | Dotted |
| Show Expand Buttons | true |
| Show Root Lines | true |
| Show Checkboxes | false |
| Check Propagation | true |
| Scrollbar Width | 16 pixels |
| Background Color | White |
| Selection Color | Blue |
| Hover Color | Light Blue (#E5F3FF) |
| Line Color | Gray (#808080) |
| Text Color | Black |
| Show Scroll-to-Top Button | true |
| Scroll-to-Top Button Size | 24 pixels |
| Scroll-to-Top Min Hidden Rows | 3 |
| Scroll-to-Top Keep-Clear Rows | 3 |

## Platform Integration

The UltraCanvasTreeView integrates seamlessly with the UltraCanvas framework:

- **Cross-Platform**: Works on Windows, Linux, macOS through platform abstraction
- **Event System**: Uses UCEvent for unified event handling
- **Rendering**: Uses IRenderContext for platform-independent drawing
- **Layout**: Compatible with UltraCanvas layout system
- **Themes**: Supports framework theming system

## Best Practices

1. **Unique IDs**: Always use unique nodeId values for each node
2. **Lazy Loading**: For large trees, implement lazy loading in onNodeExpanded
3. **Icon Caching**: Pre-load frequently used icons for better performance
4. **Selection Handling**: Clear selection before removing nodes
5. **Memory Management**: Use smart pointers for node management
6. **Event Delegation**: Leverage callbacks for business logic separation

## Known Limitations

- Maximum tree depth depends on available stack size
- Icon loading is synchronous (may block for network resources)
- No built-in drag-and-drop reordering (requires custom implementation)
- Text editing requires external text input component

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2024-12-19 | Initial release with full tree functionality |
| 1.1.0 | 2026-08-08 | Floating scroll-to-top button for long trees |
| 1.2.0 | 2026-10-02 | `GetRequiredWidth` - the width the widest row on show needs |
| 1.2.1 | 2026-10-07 | Docs: columns API (`SetColumns`, `TreeViewColumn`, `data.cells`), real constructors and builder calls |

## See Also

- [UltraCanvasUIElement](UltraCanvasUIElement.md) - Base class documentation
- [UltraCanvasContainer](UltraCanvasContainer.md) - Container for tree views
- [UltraCanvasEvent](UltraCanvasEvent.md) - Event system documentation
- [UltraCanvasRenderContext](UltraCanvasRenderContext.md) - Rendering system
