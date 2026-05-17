# UltraCanvasListView Documentation

## Overview

**UltraCanvasListView** is a Model-View-Delegate list widget in the UltraCanvas framework. It cleanly separates data (the *model*), per-row drawing (the *delegate*), and selection state (the *selection*), allowing simple single-column lists, multi-column tables with headers, icon lists, and fully custom-painted rows to all share the same view class.

**Version:** 1.0.0
**Headers:**
- `include/UltraCanvasListView.h`
- `include/UltraCanvasListModel.h`
- `include/UltraCanvasListDelegate.h`
- `include/UltraCanvasListSelection.h`

**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement`

## Features

- **Model-View-Delegate architecture** — data, painting, and selection are independent
- **Single-column and multi-column models** with built-in implementations
- **Optional column headers** with per-column titles, widths, and alignment
- **Single and multi-selection** modes via swappable selection objects
- **Optional grid lines and alternating row colors**
- **Built-in vertical scrollbar** with mouse wheel support
- **Keyboard navigation** (arrow keys, Page Up/Down, Home/End)
- **Per-row icon support** through `DecorationRole`
- **Custom delegates** for fully bespoke row rendering

## Header Includes

```cpp
#include "UltraCanvasListView.h"
// Pulled in transitively:
//   UltraCanvasListModel.h, UltraCanvasListDelegate.h, UltraCanvasListSelection.h
```

## Class Reference

### Constructor

```cpp
UltraCanvasListView(const std::string& identifier,
                    int x, int y, int w, int h);
```

Creates a list view at the given position and size. Until a model is attached the list renders empty.

### Model / Delegate / Selection wiring

```cpp
void SetModel(IListModel* model);
IListModel* GetModel() const;

void SetDelegate(std::shared_ptr<IItemDelegate> delegate);
IItemDelegate* GetDelegate() const;

void SetSelection(std::shared_ptr<IListSelection> selection);
IListSelection* GetSelection() const;
```

The view never owns the raw model pointer — callers keep the model alive (typically via a `std::shared_ptr` whose `.get()` is passed in). Delegates and selections are shared via `std::shared_ptr`.

### Styling

```cpp
void SetStyle(const ListViewStyle& style);
const ListViewStyle& GetStyle() const;

void SetRowHeight(int height);
int GetRowHeight() const;

void SetShowHeader(bool show);
bool GetShowHeader() const;
```

The `ListViewStyle` struct controls colors, grid lines, alternating rows, header visibility, and the scrollbar style:

```cpp
struct ListViewStyle {
    Color backgroundColor = Colors::White;
    Color headerBackgroundColor = Color(240, 240, 240);
    Color headerTextColor = Colors::Black;
    Color gridLineColor = Color(220, 220, 220);

    float headerFontSize = 10;

    int rowHeight = 24;
    int headerHeight = 26;
    bool showHeader = false;
    bool showGridLines = false;
    bool alternateRowColors = false;
    Color alternateRowColor = Color(248, 248, 248);

    Color selectionBackgroundColor = Colors::Selection;
    Color hoverBackgroundColor = Colors::SelectionHover;

    ScrollbarStyle scrollbarStyle = ScrollbarStyle::Modern();
};
```

### Scrolling

```cpp
void ScrollToRow(int row);
void EnsureRowVisible(int row);
```

`EnsureRowVisible` only scrolls when the target row is currently off-screen; `ScrollToRow` always recenters.

## Model Reference

### Data roles

```cpp
enum class ListDataRole {
    DisplayRole = 0,    // std::string - primary text
    DecorationRole = 1, // std::string - icon path
    ToolTipRole = 2,    // std::string - tooltip text
    UserRole = 256      // Starting point for user-defined roles
};
```

### IListModel interface

Subclass `IListModel` to back the list with any data source:

```cpp
class IListModel {
public:
    virtual int GetRowCount() const = 0;
    virtual int GetColumnCount() const = 0;
    virtual ListDataValue GetData(const ListIndex& index, ListDataRole role) const = 0;
    virtual bool SetData(const ListIndex& index, ListDataRole role, const ListDataValue& value) = 0;
    virtual ListColumnDef GetColumnDef(int column) const;

    // Notification callbacks (the view wires these up automatically)
    std::function<void()> onDataChanged;
    std::function<void(int row)> onRowChanged;
    std::function<void(int row)> onRowInserted;
    std::function<void(int row)> onRowRemoved;
};
```

### UltraCanvasSimpleListModel (single column)

```cpp
class UltraCanvasSimpleListModel : public IListModel {
public:
    void AddItem(const ListItem& item);
    void AddItem(const std::string& label, const std::string& iconPath = "");
    void InsertItem(int row, const ListItem& item);
    void RemoveItem(int row);
    void Clear();
    void SetItems(const std::vector<ListItem>& newItems);

    int GetItemCount() const;
    const ListItem& GetItem(int row) const;
    ListItem& GetItem(int row);
};

struct ListItem {
    std::string label;
    std::string iconPath;
    std::string tooltip;
    void* userData = nullptr;
};
```

### UltraCanvasMultiColumnListModel

```cpp
class UltraCanvasMultiColumnListModel : public IListModel {
public:
    void AddColumn(const ListColumnDef& colDef);
    void SetColumns(const std::vector<ListColumnDef>& colDefs);

    void AddItem(const MultiColumnListItem& item);
    void InsertItem(int row, const MultiColumnListItem& item);
    void RemoveItem(int row);
    void Clear();

    int GetItemCount() const;
    const MultiColumnListItem& GetItem(int row) const;
};

struct ListColumnDef {
    std::string title;
    int width = 100;
    TextAlignment alignment = TextAlignment::Left;
};

struct MultiColumnListItem {
    std::vector<std::string> labels;
    std::vector<std::string> iconPaths;
    std::string tooltip;
    void* userData = nullptr;
};
```

## Delegate Reference

### IItemDelegate

```cpp
class IItemDelegate {
public:
    virtual void RenderItem(IRenderContext* ctx, const IListModel* model,
                            int row, int column,
                            const ListItemStyleOption& option) = 0;
    virtual int GetRowHeight(const IListModel* model, int row) const;
};

struct ListItemStyleOption {
    Rect2Di rect;             // Full row rectangle
    bool isSelected = false;
    bool isHovered = false;
    bool isFocused = false;
    bool isDisabled = false;
    int row = -1;
    int column = 0;
    int columnCount = 1;
    int columnX = 0;
    int columnWidth = 0;
    TextAlignment columnAlignment = TextAlignment::Left;
};
```

### UltraCanvasDefaultListDelegate

The built-in delegate handles selection/hover backgrounds, optional icon (read from `DecorationRole`), and text (read from `DisplayRole`).

```cpp
class UltraCanvasDefaultListDelegate : public IItemDelegate {
public:
    void SetFontSize(float size);
    void SetIconSize(int size);
    void SetIconSpacing(int spacing);
    void SetTextPadding(int padding);
    void SetRowHeight(int height);

    void SetTextColor(const Color& color);
    void SetSelectedTextColor(const Color& color);
};
```

## Selection Reference

```cpp
class IListSelection {
public:
    virtual void Select(int row, bool addToSelection = false) = 0;
    virtual void Deselect(int row) = 0;
    virtual void Clear() = 0;
    virtual void SelectRange(int fromRow, int toRow) = 0;
    virtual bool IsSelected(int row) const = 0;
    virtual std::vector<int> GetSelectedRows() const = 0;
    virtual int GetCurrentRow() const = 0;
    virtual bool HasSelection() const = 0;
};

class UltraCanvasSingleSelection : public IListSelection { /* ... */ };
class UltraCanvasMultiSelection  : public IListSelection { /* ... */ };
```

If `SetSelection()` is never called, the view installs a single-selection by default.

## Events / Callbacks

```cpp
std::function<void(int row)> onItemClicked;
std::function<void(int row)> onItemDoubleClicked;
std::function<void(int row)> onItemActivated;
std::function<void(const std::vector<int>&)> onSelectionChanged;
std::function<void(int row)> onItemHovered;
```

`onSelectionChanged` fires whenever the selection set changes (single or multi-select). `onItemActivated` fires on Enter or double-click.

## Usage Examples

### Simple single-selection list

```cpp
auto simpleModel = std::make_shared<UltraCanvasSimpleListModel>();
simpleModel->AddItem(ListItem("Apple",  "", "A common red fruit"));
simpleModel->AddItem(ListItem("Banana", "", "A yellow tropical fruit"));
simpleModel->AddItem(ListItem("Cherry", "", "Small red stone fruit"));
simpleModel->AddItem(ListItem("Date",   "", "Sweet desert fruit"));

auto simpleList = std::make_shared<UltraCanvasListView>("SimpleListView", 20, 125, 460, 225);
simpleList->SetModel(simpleModel.get());
simpleList->SetRowHeight(22);

simpleList->onItemClicked = [statusLabel, simpleModel](int row) {
    const auto& item = simpleModel->GetItem(row);
    statusLabel->SetText("Clicked row " + std::to_string(row) +
                         "\nItem: " + item.label +
                         "\nTooltip: " + item.tooltip);
};
simpleList->onItemDoubleClicked = [statusLabel, simpleModel](int row) {
    statusLabel->SetText("Double-clicked '" + simpleModel->GetItem(row).label + "'");
};

container->AddChild(simpleList);
```

### Multi-column list with header

```cpp
auto multiModel = std::make_shared<UltraCanvasMultiColumnListModel>();
multiModel->AddColumn(ListColumnDef("File Name", 170, TextAlignment::Left));
multiModel->AddColumn(ListColumnDef("Type",       90, TextAlignment::Left));
multiModel->AddColumn(ListColumnDef("Size",       70, TextAlignment::Right));
multiModel->AddColumn(ListColumnDef("Modified",  110, TextAlignment::Left));

multiModel->AddItem(MultiColumnListItem({"main.cpp",   "C++ Source",  "2.4 KB",  "2025-03-15"}));
multiModel->AddItem(MultiColumnListItem({"utils.h",    "C++ Header",  "1.1 KB",  "2025-03-14"}));
multiModel->AddItem(MultiColumnListItem({"README.md",  "Markdown",    "3.8 KB",  "2025-03-10"}));
multiModel->AddItem(MultiColumnListItem({"Makefile",   "Build Script","0.9 KB",  "2025-02-28"}));

auto multiList = std::make_shared<UltraCanvasListView>("MultiColumnListView", 500, 125, 480, 225);
multiList->SetModel(multiModel.get());

ListViewStyle multiStyle;
multiStyle.headerFontSize = 10;
multiStyle.showHeader     = true;
multiStyle.showGridLines  = true;
multiStyle.headerHeight   = 26;
multiStyle.rowHeight      = 22;
multiList->SetStyle(multiStyle);

auto multiListDelegate = std::make_shared<UltraCanvasDefaultListDelegate>();
multiListDelegate->SetFontSize(10);
multiList->SetDelegate(multiListDelegate);

auto multiSelection = std::make_shared<UltraCanvasMultiSelection>();
multiList->SetSelection(multiSelection);

multiList->onSelectionChanged = [statusLabel](const std::vector<int>& rows) {
    std::string rowList;
    for (size_t i = 0; i < rows.size(); i++) {
        if (i > 0) rowList += ", ";
        rowList += std::to_string(rows[i]);
    }
    statusLabel->SetText(std::to_string(rows.size()) +
                         " items selected\nRows: [" + rowList + "]");
};
```

### Styled list with alternating rows and multi-select

```cpp
auto styledModel = std::make_shared<UltraCanvasSimpleListModel>();
styledModel->AddItem("Crimson Red");
styledModel->AddItem("Sunset Orange");
styledModel->AddItem("Golden Yellow");
styledModel->AddItem("Lime Green");
styledModel->AddItem("Forest Green");
styledModel->AddItem("Sky Blue");

auto styledList = std::make_shared<UltraCanvasListView>("StyledListView", 20, 420, 460, 160);
styledList->SetModel(styledModel.get());

ListViewStyle styledStyle;
styledStyle.backgroundColor          = Color(252, 252, 255);
styledStyle.alternateRowColors       = true;
styledStyle.alternateRowColor        = Color(240, 240, 248);
styledStyle.rowHeight                = 24;
styledStyle.selectionBackgroundColor = Color(100, 60, 180);
styledStyle.hoverBackgroundColor     = Color(220, 210, 240);
styledList->SetStyle(styledStyle);

auto styledDelegate = std::make_shared<UltraCanvasDefaultListDelegate>();
styledDelegate->SetFontSize(13.0f);
styledDelegate->SetTextPadding(10);
styledDelegate->SetSelectedTextColor(Colors::White);
styledList->SetDelegate(styledDelegate);

styledList->SetSelection(std::make_shared<UltraCanvasMultiSelection>());

styledList->onSelectionChanged = [statusLabel, styledModel](const std::vector<int>& rows) {
    std::string names;
    for (size_t i = 0; i < rows.size(); i++) {
        if (i > 0) names += ", ";
        names += styledModel->GetItem(rows[i]).label;
    }
    statusLabel->SetText(std::to_string(rows.size()) +
                         " items selected\nSelected: " + names);
};
```

### Icon list with a customized delegate

```cpp
std::string iconsDir = NormalizePath(GetResourcesDir() + "media/icons/");

auto iconModel = std::make_shared<UltraCanvasSimpleListModel>();
iconModel->AddItem(ListItem("C++",        iconsDir + "cpp.png",        "Systems programming language"));
iconModel->AddItem(ListItem("Python",     iconsDir + "python.png",     "General-purpose scripting language"));
iconModel->AddItem(ListItem("Java",       iconsDir + "java.png",       "Enterprise application language"));
iconModel->AddItem(ListItem("JavaScript", iconsDir + "javascript.png", "Web scripting language"));
iconModel->AddItem(ListItem("Rust",       iconsDir + "rust.png",       "Memory-safe systems language"));

auto iconList = std::make_shared<UltraCanvasListView>("IconListView", 500, 420, 480, 160);
iconList->SetModel(iconModel.get());
iconList->SetRowHeight(28);

auto iconDelegate = std::make_shared<UltraCanvasDefaultListDelegate>();
iconDelegate->SetFontSize(13.0f);
iconDelegate->SetIconSize(20);
iconDelegate->SetIconSpacing(8);
iconDelegate->SetTextPadding(8);
iconDelegate->SetRowHeight(28);
iconList->SetDelegate(iconDelegate);

iconList->onItemClicked = [statusLabel, iconModel](int row) {
    const auto& item = iconModel->GetItem(row);
    statusLabel->SetText("Selected '" + item.label + "'\nTooltip: " + item.tooltip);
};
```

## Keyboard Navigation

| Key                | Action                              |
|--------------------|-------------------------------------|
| Up Arrow           | Move focus to the previous row      |
| Down Arrow         | Move focus to the next row          |
| Page Up / Page Down| Move by one viewport page           |
| Home               | Jump to the first row               |
| End                | Jump to the last row                |
| Enter              | Activate the focused row            |
| Space              | Toggle selection on the focused row |
| Ctrl + Click       | Toggle row in multi-selection mode  |
| Shift + Click      | Range-select in multi-selection mode|

## Best Practices

1. **Keep the model alive for as long as the view uses it.** The view stores a raw pointer; if the model is destroyed first, the view will read freed memory.
2. **Notify on data changes.** When mutating a custom model, fire `onDataChanged` / `onRowChanged` / `onRowInserted` / `onRowRemoved` so the view repaints and rescrolls correctly.
3. **Use `UltraCanvasMultiSelection` for any list where Ctrl/Shift-Click should add to selection.** The default is single-selection.
4. **Tune `rowHeight` to match `delegate->GetRowHeight()`** when using icons larger than 16px — set both to avoid clipped icons.
5. **For large datasets**, keep `GetData()` cheap — it is called once per visible cell per repaint.

## See Also

- [UltraCanvasTreeView](UltraCanvasTreeViewExamples.md) — Hierarchical equivalent of ListView
- [UltraCanvasScrollbar](UltraCanvasScrollbar.md) — Scrollbar used internally
- [UltraCanvasLabel](UltraCanvasLabelExamples.md) — Static text display
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — Base class documentation
