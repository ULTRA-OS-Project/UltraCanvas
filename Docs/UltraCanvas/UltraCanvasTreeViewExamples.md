# UltraCanvasTreeView Documentation

## Overview

The `UltraCanvasTreeView` is a hierarchical tree view control component that provides a powerful and flexible way to display tree-structured data with support for icons, custom styling, selection modes, and interactive features. It is part of the UltraCanvas cross-platform UI framework.

**Version:** 1.0.0  
**Files:** 
- Header: `include/UltraCanvasTreeView.h`
- Implementation: `core/UltraCanvasTreeView.cpp`

## Features

### Core Capabilities
- **Hierarchical Data Display**: Multi-level tree structure with parent-child relationships
- **Dual Icon Support**: Left and right icons for each node
- **Selection Modes**: Single, multiple, or no selection
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
    Color textColor = Colors::Black;       // Text color
    Color backgroundColor = Colors::Transparent; // Background color
    std::string tooltip;                   // Tooltip text
    void* userData = nullptr;              // Custom user data

    // Optional columns — read by column tree views (UltraCanvasColumnsTreeView);
    // ignored by the base tree, so setting them is always safe.
    std::string typeText;                  // Type column (e.g. "int", "*ptr")
    std::string valueText;                 // Value column (e.g. "45", "2x67")
    Color typeColor = Colors::Transparent; // Type column text override
    bool isGroupHeader = false;            // Full-width section-header bar
    uint64_t accessSequence = 0;           // Sort key for TreeSortMode::LastAccess
};
```

### Columnar Layout: `UltraCanvasColumnsTreeView`

The base `UltraCanvasTreeView` always renders one text run per row (`data.text`). For
IDE-style debugger "Variables" / "Watch" panels, the framework provides the subclass
`UltraCanvasColumnsTreeView` (in `UltraCanvasColumnsTreeView.h`), which adds a columnar
row layout. It renders each row in one of two modes, switchable at runtime with
`SetDisplayMode()`:

- **Columns** (default) — three aligned columns drawn from `data.text` (Name),
  `data.typeText` (Type) and `data.valueText` (Value), with an accent-filled Type
  column. Nodes with `data.isGroupHeader = true` render as full-width section bars.
- **Classic** — delegates to the base single-text layout (`data.text` per row).

Hierarchy and expand/collapse work in both modes.

```cpp
enum class TreeDisplayMode {
    Classic = 0,      // single-text rows (delegates to the base tree)
    Columns = 1       // aligned Name / Type / Value columns (default)
};
```

Column geometry and colours are controlled by `TreeColumnStyle`
(`SetColumnStyle()` / `GetColumnStyle()`):

```cpp
struct TreeColumnStyle {
    int   typeColumnWidth      = 64;   // fixed Type column width (px)
    int   valueColumnWidth     = 0;    // 0 => Value takes remaining width
    int   columnGap            = 8;    // gap between columns (px)
    int   typeColumnPadding    = 4;    // padding around the Type accent fill
    Color typeColumnBackground = Color(255, 190, 130);  // orange accent
    Color typeTextColor        = Color(40, 40, 40);
    Color valueTextColor       = Color(40, 40, 40);
    Color groupHeaderBackground = Colors::Black;
    Color groupHeaderTextColor  = Colors::White;
    bool  showColumnSeparators  = false;
    Color columnSeparatorColor  = Color(210, 210, 210);
};
```

This layout is intended for IDE-style debugger "Variables" / "Watch" panels — an
example is in `Apps/DemoApp/UltraCanvasTreeViewExamples.cpp`.

### Sorting Modes

`SetSortMode(TreeSortMode, bool ascending)` sorts the whole tree and remembers the
choice. `Alphabetic` compares `data.text` case-insensitively; `LastAccess` compares
`data.accessSequence` (stamp it when a variable is read/written, then sort
descending to float the most recently touched entries to the top). Node-level and
alphabetical helpers (`SortNodeChildren`, `SortAllNodes`, `SetAutoSortChildren`)
remain available.

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
                   int x, int y, int w, int h)
```

Creates a new tree view control.

**Parameters:**
- `identifier`: Unique string identifier for the control
- `id`: Numeric ID for the control
- `x`, `y`: Position coordinates
- `w`, `h`: Width and height dimensions

### Tree Structure Management

#### `SetRootNode`
```cpp
TreeNode* SetRootNode(const TreeNodeData& rootData)
```
Sets the root node of the tree.

#### `SetRootVisible`
```cpp
void SetRootVisible(bool visible)   // default: true
bool IsRootVisible() const
```
Hides the root row and draws its children as the top level, turning the tree
into a **forest** — several independent sections side by side instead of one
node with everything under it. The root still owns the nodes (`SetRootNode` /
`AddNode` are unchanged) and is kept expanded while it is hidden; it is simply
never drawn, hit-tested or counted as a row, so indentation, scrolling and
clicks all behave as if the children were the top of the tree.

```cpp
tree->SetRootVisible(false);
tree->SetRootNode(MakeNode("root", ""));   // never seen
tree->AddNode("root", MakeNode("pinned", "Pinned"));
tree->AddNode("root", MakeNode("computer", "Computer"));
```

A section can be taken out of the tree without removing its nodes by clearing
`node->data.visible` — the UltraFiler's folder tree hides its whole "Pinned"
section that way while nothing is pinned.

#### `AddNode`
```cpp
TreeNode* AddNode(const std::string& parentId, const TreeNodeData& nodeData)
```
Adds a new node as a child of the specified parent.

#### `RemoveNode`
```cpp
void RemoveNode(const std::string& nodeId)
```
Removes a node and all its children from the tree.

#### `FindNode`
```cpp
TreeNode* FindNode(const std::string& nodeId)
```
Finds a node by its ID.

### Selection Management

#### `SelectNode`
```cpp
void SelectNode(TreeNode* node, bool addToSelection = false)
```
Selects a node, optionally adding to existing selection.

#### `ClearSelection`
```cpp
void ClearSelection()
```
Clears all selected nodes.

#### `GetSelectedNodes`
```cpp
const std::vector<TreeNode*>& GetSelectedNodes() const
```
Returns all currently selected nodes.

### Expansion Management

#### `ExpandNode` / `CollapseNode` / `ToggleNode`
```cpp
void ExpandNode(TreeNode* node)
void CollapseNode(TreeNode* node)
void ToggleNode(TreeNode* node)
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
void ExpandAll()
void CollapseAll()
```
Expands or collapses all nodes in the tree.

### Visual Properties

#### Row Height
```cpp
void SetRowHeight(int height)
int GetRowHeight() const
```
Sets/gets the height of each row in pixels.

#### Indentation
```cpp
void SetIndentSize(int size)
int GetIndentSize() const
```
Sets/gets the indentation size per level.

#### Font Size
```cpp
void SetFontSize(float size)
float GetFontSize() const
```
Sets/gets the font size used for the row labels (default 12). Also used by
`UltraCanvasColumnsTreeView` for its cell text, column headers and group
headers.

#### Selection Mode
```cpp
void SetSelectionMode(TreeSelectionMode mode)
TreeSelectionMode GetSelectionMode() const
```
Sets/gets the selection mode.

#### Line Style
```cpp
void SetLineStyle(TreeLineStyle style)
TreeLineStyle GetLineStyle() const
```
Sets/gets the connecting line style.

### Scroll-to-Top Button

When a tree grows past its viewport, getting back to the root can take a lot of
wheel turns. The tree therefore draws a small floating **"move to the top"**
button over the bottom-right corner of its content area once the view has been
scrolled down:

```cpp
void SetShowScrollToTopButton(bool show)
bool GetShowScrollToTopButton() const

void SetScrollToTopButtonStyle(const TreeScrollToTopStyle& style)
const TreeScrollToTopStyle& GetScrollToTopButtonStyle() const

bool    IsScrollToTopButtonActive() const   // on screen right now?
Rect2Di GetScrollToTopButtonRect() const    // element-local rect (empty when inactive)

void ScrollToTop()                          // what the button does; callable directly
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

### Color Properties

```cpp
void SetBackgroundColor(const Color& color)
void SetSelectionColor(const Color& color)
void SetHoverColor(const Color& color)
void SetLineColor(const Color& color)
void SetTextColor(const Color& color)
void SetExpandButtonColor(const Color& color)
Color GetExpandButtonColor() const
```
Sets various color properties for the tree view.

`SetExpandButtonColor` controls the background fill of the `+`/`-` node icon
(default `#E0E0E0`); the icon keeps its gray 1px border and black `+`/`-` glyph.

### Event Callbacks

The tree view provides several event callbacks:

```cpp
std::function<void(TreeNode*)> onNodeSelected;
std::function<void(TreeNode*)> onNodeDoubleClicked;
std::function<void(TreeNode*)> onNodeExpanded;
std::function<void(TreeNode*)> onNodeCollapsed;
std::function<void(TreeNode*, TreeNode*)> onNodeDragDrop;
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
window->AddElement(treeView);
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

// Expand root
computer->Expand();
```

### Event Handling

```cpp
// Handle selection
treeView->onNodeSelected = [](TreeNode* node) {
    std::cerr << "Selected: " << node->data.text << std::endl;
    // Update UI based on selection
};

// Handle double-click
treeView->onNodeDoubleClicked = [](TreeNode* node) {
    if (node->HasChildren()) {
        node->Toggle(); // Toggle expansion
    } else {
        // Open file or perform action
        OpenFile(node->data.userData);
    }
};

// Handle expansion
treeView->onNodeExpanded = [](TreeNode* node) {
    // Lazy load children if needed
    if (node->children.empty()) {
        LoadChildrenFromDisk(node);
    }
};

// Handle right-click (open a context menu at the pointer)
treeView->onNodeRightClicked = [](TreeNode* node, const UCEvent& event) {
    // Show context menu, e.g. menu->OpenMenu(event.pointerWindow, ...)
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
    .SetBackgroundColor(Colors::White)
    .SetSelectionColor(Colors::Blue)
    .SetHoverColor(Color(230, 240, 250))
    .Build();
```

## Keyboard Navigation

The tree view supports comprehensive keyboard navigation:

| Key | Action |
|-----|--------|
| **↑** (Up Arrow) | Navigate to previous visible node |
| **↓** (Down Arrow) | Navigate to next visible node |
| **←** (Left Arrow) | Collapse node or navigate to parent |
| **→** (Right Arrow) | Expand node or navigate to first child |
| **Enter** | Toggle node expansion |
| **Space** | Select/deselect node |
| **Home** | Navigate to first node |
| **End** | Navigate to last visible node |
| **Page Up** | Scroll up one page |
| **Page Down** | Scroll down one page |

## Mouse Interaction

### Click Behaviors
- **Single Click**: Select node
- **Ctrl+Click**: Add to selection (multi-select mode)
- **Double Click**: Toggle expansion or trigger action
- **Right Click**: Context menu

### Expand/Collapse Button
- Clicking the +/- button expands or collapses the node without selecting it

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

### Child Management
```cpp
TreeNode* AddChild(const TreeNodeData& childData)
void RemoveChild(const std::string& nodeId)
TreeNode* FindChild(const std::string& nodeId)
TreeNode* FindDescendant(const std::string& nodeId)
```

### State Management
```cpp
void Expand()
void Collapse()
void Toggle()
bool HasChildren() const
bool IsExpanded() const
bool IsVisible() const
```

### Utility Methods
```cpp
int GetVisibleChildCount() const
std::vector<TreeNode*> GetVisibleChildren()
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

## See Also

- [UltraCanvasUIElement](UltraCanvasUIElement.md) - Base class documentation
- [UltraCanvasContainer](UltraCanvasContainer.md) - Container for tree views
- [UltraCanvasEvent](UltraCanvasEvent.md) - Event system documentation
- [UltraCanvasRenderContext](UltraCanvasRenderContext.md) - Rendering system
