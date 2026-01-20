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
};
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

#### `ExpandNode` / `CollapseNode`
```cpp
void ExpandNode(TreeNode* node)
void CollapseNode(TreeNode* node)
```
Expands or collapses a specific node.

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

### Color Properties

```cpp
void SetBackgroundColor(const Color& color)
void SetSelectionColor(const Color& color)
void SetHoverColor(const Color& color)
void SetLineColor(const Color& color)
void SetTextColor(const Color& color)
```
Sets various color properties for the tree view.

### Event Callbacks

The tree view provides several event callbacks:

```cpp
std::function<void(TreeNode*)> onNodeSelected;
std::function<void(TreeNode*)> onNodeDoubleClicked;
std::function<void(TreeNode*)> onNodeExpanded;
std::function<void(TreeNode*)> onNodeCollapsed;
std::function<void(TreeNode*, TreeNode*)> onNodeDragDrop;
std::function<void(TreeNode*)> onNodeRightClicked;
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
documents.leftIcon = TreeNodeIcon("folder.png");
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
    std::cout << "Selected: " << node->data.text << std::endl;
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

// Handle right-click
treeView->onNodeRightClicked = [](TreeNode* node) {
    // Show context menu
    ShowContextMenu(node);
};
```

### Using TreeViewBuilder

```cpp
auto treeView = TreeViewBuilder("MyTree", 10, 10, 300, 400)
    .SetRowHeight(24)
    .SetIndentSize(20)
    .SetSelectionMode(TreeSelectionMode::Multiple)
    .SetLineStyle(TreeLineStyle::Dotted)
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

## Rendering Details

### Visual Elements
1. **Expand/Collapse Buttons**: +/- indicators for expandable nodes
2. **Connecting Lines**: Optional dotted or solid lines between nodes
3. **Icons**: Left and right icons with configurable sizes
4. **Selection Highlight**: Background color for selected nodes
5. **Hover Highlight**: Background color for hovered nodes
6. **Scrollbar**: Vertical scrollbar when content exceeds viewport

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

## See Also

- [UltraCanvasUIElement](UltraCanvasUIElement.md) - Base class documentation
- [UltraCanvasContainer](UltraCanvasContainer.md) - Container for tree views
- [UltraCanvasEvent](UltraCanvasEvent.md) - Event system documentation
- [UltraCanvasRenderContext](UltraCanvasRenderContext.md) - Rendering system
