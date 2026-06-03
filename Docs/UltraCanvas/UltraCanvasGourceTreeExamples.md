# UltraCanvasGourceTree Documentation

## Overview

**UltraCanvasGourceTree** is a Gource-style radial tree diagram for visualizing filesystems, storage devices, and other hierarchical data. Files are drawn as colored dots around their parent directory, directories form rings out from a root center, and file size is represented by a translucent halo around each file dot. Date-based highlight modes can fade old or recently-accessed files. Three layout modes (Static, Animated, Hybrid) trade off determinism vs. visual smoothness.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasGourceTree.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 1.0.1

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasGourceTree
```

## Header Include

```cpp
#include "Plugins/Diagrams/UltraCanvasGourceTree.h"
```

## Features

- **Three layout modes**: instant radial (Static), force-directed animation (Animated), radial-then-relax (Hybrid)
- **Date-based highlighting**: LastAccess, CreationDate, plus inverse modes for surfacing stale files
- **File-size visualization**: translucent halo around each file scaled to bytes
- **Per-extension coloring**: built-in palette for common extensions, override-able
- **Tree operations**: Expand / Collapse / ExpandAll / CollapseAll / ExpandToDepth
- **Themes**: Default, Dark, Light, Colorful, Monochrome, Custom
- **View control**: zoom in/out, fit, pan, center-on-node
- **SVG export** via `SaveToSVG`
- **Tooltips, selection, hover** callbacks

## Data Structures

### Enumerations

```cpp
enum class GourceLayoutMode {
    Static,    // Instant radial tree layout
    Animated,  // Force-directed with animation during data addition
    Hybrid     // Radial initial + force relaxation
};

enum class GourceHighlightMode {
    NoneHighlight,
    LastAccess,
    CreationDate,
    InvertLastAccess,
    InvertCreationDate
};

enum class GourceNodeType {
    File,
    Directory,
    CollapsedDirectory   // Directory beyond depth limit
};

enum class GourceTheme {
    Default, Dark, Light, Colorful, Monochrome, Custom
};
```

### GourceFileInfo

```cpp
struct GourceFileInfo {
    int64_t     fileSize        = 0;   // Size in bytes
    time_t      creationTime    = 0;
    time_t      lastAccessTime  = 0;
    time_t      modificationTime = 0;
    std::string extension;             // For coloring
    std::string mimeType;
};
```

### GourceNode

```cpp
struct GourceNode {
    // Identity
    std::string id, name, fullPath, parentId;

    // Type and state
    GourceNodeType type = GourceNodeType::File;
    bool isExpanded = true;
    bool isVisible  = true;
    bool isHovered  = false;
    bool isSelected = false;

    // File information
    GourceFileInfo fileInfo;

    // Tree structure
    int depth = 0;
    int childCount = 0;
    int descendantCount = 0;
    std::vector<std::string> childIds;

    // Computed layout
    float  x = 0, y = 0;
    double targetX = 0, targetY = 0;
    double angle  = 0;
    double radius = 0;

    // Physics
    float velocityX = 0, velocityY = 0;

    // Appearance
    float nodeRadius     = 5.0f;
    float fileSizeRadius = 0.0f;
    Color nodeColor      = Colors::Blue;
    float opacity        = 1.0f;
};
```

### GourceLink

```cpp
struct GourceLink {
    std::string sourceId;
    std::string targetId;
    Color  color = Color(128, 128, 128, 100);
    double width = 1.0;
};
```

### GourceStyle (selected fields)

```cpp
struct GourceStyle {
    Color  backgroundColor   = Color(20, 20, 30);
    double fileNodeRadius    = 4.0;
    Color  fileNodeColor     = Colors::LightBlue;
    double dirNodeRadius     = 6.0;
    Color  dirNodeColor      = Color(255, 165, 0);
    bool   showFileSizeArea  = true;
    double fileSizeScaleFactor = 0.00001;
    bool   showLinks  = true;
    bool   curvedLinks = true;
    bool   showLabels  = true;
    double fontSize    = 10.0;
    double ringSpacing = 60.0;
    double rootNodeRadius = 10.0;
    Color  rootNodeColor   = Color(255, 215, 0);
    // ...plus animation, hover, tooltip and label colors
};
```

## Class Reference

### Constructor

```cpp
UltraCanvasGourceTree(const std::string& id,
                      long x, long y, long w, long h);
```

### Factory Function

```cpp
std::shared_ptr<UltraCanvasGourceTree> CreateGourceTree(
        const std::string& id, long x, long y, long w, long h);
```

### Node Management

```cpp
void SetRootNode(const std::string& id, const std::string& name,
                 const std::string& fullPath = "");

void AddNode(const std::string& parentId, const std::string& nodeId,
             const std::string& name, GourceNodeType type,
             const GourceFileInfo& fileInfo = GourceFileInfo());

void AddFile(const std::string& parentId, const std::string& filePath,
             const std::string& fileName, const GourceFileInfo& fileInfo);

void AddDirectory(const std::string& parentId, const std::string& dirPath,
                  const std::string& dirName);

void RemoveNode(const std::string& nodeId);
void ClearAll();

GourceNode*       GetNode(const std::string& nodeId);
GourceNode*       GetRootNode();
```

### Node State

```cpp
void ExpandNode(const std::string& nodeId);
void CollapseNode(const std::string& nodeId);
void ToggleNode(const std::string& nodeId);
void ExpandAll();
void CollapseAll();
void ExpandToDepth(int depth);

void SelectNode(const std::string& nodeId);
void DeselectNode(const std::string& nodeId);
void ClearSelection();
std::vector<std::string> GetSelectedNodes() const;
```

### Layout, Highlight, Theme

```cpp
void SetLayoutMode(GourceLayoutMode mode);
GourceLayoutMode GetLayoutMode() const;

void SetMaxDepth(int depth);              // -1 = unlimited
int  GetMaxDepth() const;

void SetHighlightMode(GourceHighlightMode mode);
void SetHighlightDateRange(time_t oldest, time_t newest);
void AutoCalculateDateRange();

void SetShowFileSizeArea(bool show);
void SetFileSizeScale(float scale);

void        SetTheme(GourceTheme theme);
GourceTheme GetTheme() const;

GourceStyle&        GetStyle();
const GourceStyle&  GetStyle() const;
void                SetStyle(const GourceStyle& newStyle);

ExtensionColorMap& GetExtensionColors();
void               SetExtensionColor(const std::string& ext, const Color& color);
```

### View Control

```cpp
void   SetZoom(float zoom);
double GetZoom() const;
void   ZoomIn();
void   ZoomOut();
void   ZoomToFit();
void   ResetView();

void     SetPan(float x, float y);
Point2Df GetPan() const;
void     CenterOnNode(const std::string& nodeId);
```

### Layout Computation

```cpp
void PerformLayout();
void UpdateAnimation(float deltaTime);    // Per-frame in Animated mode
bool IsAnimationComplete() const;
```

### Data Export

```cpp
bool SaveToSVG(const std::string& filePath);
```

### Callbacks

```cpp
std::function<void(const std::string&)> onNodeClick;
std::function<void(const std::string&)> onNodeDoubleClick;
std::function<void(const std::string&)> onNodeExpand;
std::function<void(const std::string&)> onNodeCollapse;
std::function<void(const std::string&)> onNodeHover;
std::function<void(const std::string&)> onNodeSelect;
std::function<void()>                   onLayoutComplete;
```

## Usage Examples

All examples are drawn from `Apps/DemoApp/UltraCanvasGourceTreeExamples.cpp`.

### Building a project tree

```cpp
void GenerateSampleFileSystem(UltraCanvasGourceTree* tree) {
    tree->SetRootNode("root", "MyProject", "/home/user/MyProject");

    tree->AddDirectory("root", "src", "src");

    GourceFileInfo cppInfo;
    cppInfo.fileSize       = 15000;
    cppInfo.creationTime   = time(nullptr) - 86400 * 30;   // 30 days ago
    cppInfo.lastAccessTime = time(nullptr) - 3600;          // 1 hour ago
    tree->AddFile("src", "src/main.cpp", "main.cpp", cppInfo);

    cppInfo.fileSize       = 8500;
    cppInfo.creationTime   = time(nullptr) - 86400 * 25;
    cppInfo.lastAccessTime = time(nullptr) - 86400 * 2;
    tree->AddFile("src", "src/utils.cpp", "utils.cpp", cppInfo);

    tree->AddDirectory("root", "include", "include");
    tree->AddDirectory("root", "docs", "docs");
    tree->AddDirectory("root", "tests", "tests");
    // ...add files
}
```

### Configuring the project tree visualization

```cpp
auto projectTree = std::make_shared<UltraCanvasGourceTree>(
    "ProjectTree", 10, 90, kDefaultWidth - 50, 540);

GenerateSampleFileSystem(projectTree.get());
projectTree->SetLayoutMode(GourceLayoutMode::Hybrid);
projectTree->SetShowFileSizeArea(true);
projectTree->PerformLayout();
```

### Wiring node callbacks to a status label

```cpp
projectTree->onNodeClick = [statusLabel](const std::string& nodeId) {
    statusLabel->SetText("Selected: " + nodeId);
};
projectTree->onNodeExpand = [statusLabel](const std::string& nodeId) {
    statusLabel->SetText("Expanded: " + nodeId);
};
projectTree->onNodeCollapse = [statusLabel](const std::string& nodeId) {
    statusLabel->SetText("Collapsed: " + nodeId);
};
projectTree->onNodeHover = [statusLabel](const std::string& nodeId) {
    statusLabel->SetText("Hovering: " + nodeId);
};
```

### Theme, layout, and highlight dropdowns

```cpp
themeDropdown->onSelectionChanged =
    [projectTree](int index, const DropdownItem& item) {
        switch (index) {
            case 0: projectTree->SetTheme(GourceTheme::Dark); break;
            case 1: projectTree->SetTheme(GourceTheme::Light); break;
            case 2: projectTree->SetTheme(GourceTheme::Colorful); break;
            case 3: projectTree->SetTheme(GourceTheme::Monochrome); break;
        }
    };

layoutDropdown->onSelectionChanged =
    [projectTree](int index, const DropdownItem& item) {
        switch (index) {
            case 0: projectTree->SetLayoutMode(GourceLayoutMode::Static);   break;
            case 1: projectTree->SetLayoutMode(GourceLayoutMode::Animated); break;
            case 2: projectTree->SetLayoutMode(GourceLayoutMode::Hybrid);   break;
        }
        projectTree->PerformLayout();
    };

highlightDropdown->onSelectionChanged =
    [projectTree](int index, const DropdownItem& item) {
        switch (index) {
            case 0: projectTree->SetHighlightMode(GourceHighlightMode::NoneHighlight);     break;
            case 1: projectTree->SetHighlightMode(GourceHighlightMode::LastAccess);        break;
            case 2: projectTree->SetHighlightMode(GourceHighlightMode::CreationDate);      break;
            case 3: projectTree->SetHighlightMode(GourceHighlightMode::InvertLastAccess);  break;
            case 4: projectTree->SetHighlightMode(GourceHighlightMode::InvertCreationDate); break;
        }
    };
```

### Zoom and expand-all buttons

```cpp
zoomInBtn->onClick    = [projectTree]() { projectTree->ZoomIn();    };
zoomOutBtn->onClick   = [projectTree]() { projectTree->ZoomOut();   };
zoomFitBtn->onClick   = [projectTree]() { projectTree->ZoomToFit(); };
expandAllBtn->onClick = [projectTree]() { projectTree->ExpandAll(); };
collapseAllBtn->onClick = [projectTree]() { projectTree->CollapseAll(); };
```

### Highlighting recent vs. old files

```cpp
showOldBtn->onClick = [storageTree, statusLabel]() {
    storageTree->SetHighlightMode(GourceHighlightMode::InvertLastAccess);
    statusLabel->SetText("Highlighting old/unused files (more opaque = older)");
};

showRecentBtn->onClick = [storageTree, statusLabel]() {
    storageTree->SetHighlightMode(GourceHighlightMode::LastAccess);
    statusLabel->SetText("Highlighting recent files (more opaque = recently accessed)");
};
```

### Max-depth slider with an "unlimited" checkbox

```cpp
depthSlider->onValueChanged =
    [storageTree, depthValueLabel, unlimitedCheck](float value) {
        if (!unlimitedCheck->IsChecked()) {
            int depth = static_cast<int>(value);
            depthValueLabel->SetText(std::to_string(depth));
            storageTree->SetMaxDepth(depth);
        }
    };

unlimitedCheck->onStateChanged =
    [storageTree, depthSlider, depthValueLabel](CheckedState, CheckedState newState) {
        if (newState == CheckedState::Checked) {
            storageTree->SetMaxDepth(-1);            // unlimited
            depthValueLabel->SetText("∞");
        } else {
            int depth = static_cast<int>(depthSlider->GetValue());
            storageTree->SetMaxDepth(depth);
            depthValueLabel->SetText(std::to_string(depth));
        }
    };
```

### Exporting to SVG

```cpp
exportBtn->onClick = [storageTree, statusLabel]() {
    if (storageTree->SaveToSVG("/tmp/storage_tree.svg")) {
        statusLabel->SetText("Exported to /tmp/storage_tree.svg");
    } else {
        statusLabel->SetText("Export failed!");
    }
};
```

### Generating a synthetic large tree for performance benchmarking

```cpp
void GenerateLargeFileSystem(UltraCanvasGourceTree* tree,
                             int depth, int filesPerDir) {
    tree->SetRootNode("root", "LargeProject", "/storage/LargeProject");

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> sizeDist(500, 50000);
    std::uniform_int_distribution<> daysDist(1, 365);
    std::vector<std::string> extensions =
        {"cpp","h","py","js","json","md","txt","xml","css","html"};
    std::uniform_int_distribution<> extDist(0, extensions.size() - 1);

    std::function<void(const std::string&, int)> createLevel =
        [&](const std::string& parentId, int level) {
            if (level >= depth) return;
            int numDirs = (level == 0) ? 5 : 3;
            for (int d = 0; d < numDirs; d++) {
                std::string dirId   = parentId + "_dir" + std::to_string(d);
                std::string dirName = "folder_" + std::to_string(level) +
                                       "_" + std::to_string(d);
                tree->AddDirectory(parentId, dirId, dirName);
                for (int f = 0; f < filesPerDir; f++) {
                    std::string ext = extensions[extDist(gen)];
                    std::string fileName = "file_" + std::to_string(f) + "." + ext;
                    std::string fileId   = dirId + "_" + fileName;
                    GourceFileInfo info;
                    info.fileSize       = sizeDist(gen);
                    info.creationTime   = time(nullptr) - 86400 * daysDist(gen);
                    info.lastAccessTime = time(nullptr) - 86400 * (daysDist(gen) % 30);
                    tree->AddFile(dirId, fileId, fileName, info);
                }
                createLevel(dirId, level + 1);
            }
        };
    createLevel("root", 0);
}
```

### Adding nodes interactively from a form

```cpp
addNodeBtn->onClick =
    [customTree, parentInput, nameInput, typeDropdown, sizeInput, statusLabel]() {
        std::string parent  = parentInput->GetText();
        std::string name    = nameInput->GetText();
        bool        isFolder = (typeDropdown->GetSelectedIndex() == 1);

        if (name.empty()) {
            statusLabel->SetText("Please enter a name!");
            return;
        }
        std::string nodeId = parent + "/" + name;

        if (isFolder) {
            customTree->AddDirectory(parent, nodeId, name);
            statusLabel->SetText("Added folder: " + name);
        } else {
            GourceFileInfo info;
            try { info.fileSize = std::stoi(sizeInput->GetText()) * 1024; }
            catch (...) { info.fileSize = 10240; }
            info.creationTime   = time(nullptr);
            info.lastAccessTime = time(nullptr);
            customTree->AddFile(parent, nodeId, name, info);
            statusLabel->SetText("Added file: " + name);
        }
        customTree->PerformLayout();
        nameInput->SetText("");
    };
```

## Rendering Pipeline

The Gource tree renders in this order:

1. Background (theme color)
2. Links between parents and children (curved or straight)
3. File-size halos (translucent area encoding bytes)
4. Node shapes (file dots, directory dots, collapsed-directory boxes)
5. Labels (always-on or hover-only)
6. Tooltip (over hovered node)

## Layout Modes

| Mode      | Behavior                                                              |
|-----------|------------------------------------------------------------------------|
| `Static`  | Instant radial placement, deterministic. No physics.                  |
| `Animated`| Force-directed simulation each frame; nodes glide into position.       |
| `Hybrid`  | Radial seed + force relaxation; converges quickly with smooth result.  |

## See Also

- [UltraCanvasBlockDiagramExamples](UltraCanvasBlockDiagramExamples.md) — flowchart-style block diagrams
- [UltraCanvasNodeDiagramExamples](UltraCanvasNodeDiagramExamples.md) — interactive graph editor with handles
- [UltraCanvasAdjacencyDiagramExamples](UltraCanvasAdjacencyDiagramExamples.md) — architectural room adjacency
- [UltraCanvasArcDiagramExamples](UltraCanvasArcDiagramExamples.md) — nodes on a baseline with arc edges
- [UltraCanvasTreeViewExamples](UltraCanvasTreeViewExamples.md) — non-radial hierarchical tree view
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — base element class
