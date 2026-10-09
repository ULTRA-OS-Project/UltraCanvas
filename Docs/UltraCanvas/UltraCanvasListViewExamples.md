# UltraCanvasListView Documentation

## Overview

**UltraCanvasListView** is a Model-View-Delegate list widget in the UltraCanvas framework. It cleanly separates data (the *model*), per-row drawing (the *delegate*), and selection state (the *selection*), allowing simple single-column lists, multi-column tables with headers, icon lists, and fully custom-painted rows to all share the same view class.

**Version:** 1.0.1
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
- **Sort indicator** in the sorted column's header (▲ ascending / ▼ descending, drawn as geometry in the header text colour) and a header-click callback to drive it
- **Single and multi-selection** modes via swappable selection objects
- **Optional grid lines and alternating row colors**
- **Built-in vertical scrollbar** with mouse wheel support
- **Keyboard navigation** (arrow keys, Page Up/Down, Home/End)
- **Per-row icon support** through `DecorationRole`
- **Hover tooltips** per cell, per row, and per column header
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
void SetModel(std::shared_ptr<IListModel> model);
IListModel* GetModel() const;

void SetDelegate(std::shared_ptr<IItemDelegate> delegate);
IItemDelegate* GetDelegate() const;

void SetSelection(std::shared_ptr<IListSelection> selection);
IListSelection* GetSelection() const;
```

The view shares ownership of the model, the delegate and the selection: all three are passed as `std::shared_ptr`, and the getters return plain pointers for reading.

### Styling

```cpp
void SetStyle(const ListViewStyle& style);
const ListViewStyle& GetStyle() const;

void SetRowHeight(int height);
int GetRowHeight() const;

void SetVariableRowHeights(bool enabled);
bool GetVariableRowHeights() const;
void InvalidateRowHeights();

void SetShowHeader(bool show);
bool GetShowHeader() const;

void SetSortIndicator(int column, bool ascending);   // -1 = none
void ClearSortIndicator();
int  GetSortColumn() const;
bool GetSortAscending() const;
```

`SetSortIndicator` marks a column as the one the rows are sorted by: its header
cell shows a small triangle, apex up for ascending and apex down for descending.
The triangle is drawn as a filled path in `headerTextColor` (never a text glyph,
so it does not depend on the header font carrying U+25B2/U+25BC and stays crisp
at any DPI); `ListViewStyle::sortIndicatorSize` sets its width, and it sits
after the title in a left- or centre-aligned column and before it in a
right-aligned one. The view only *shows* the order — sorting the rows is the
model owner's job, normally from `onHeaderClicked` (see Events / Callbacks).

`SetRowHeight` / `rowHeight` set the single height used by every row. For rows
of differing height, call `SetVariableRowHeights(true)`: the view then asks the
delegate for each row's height via `IItemDelegate::GetRowHeight(model, row)`
instead of using the uniform value. Scrolling, hit-testing, `GetRowRect`,
`EnsureRowVisible`, culling and Page Up/Down all follow the per-row heights
(internally the view keeps a lazily-rebuilt prefix-sum of row tops). Uniform
rows remain the default. If a custom delegate changes its sizing without the
model firing a data-changed signal — for example an async delegate that sizes a
row from a decoded image — call `InvalidateRowHeights()` to force a recompute.

```cpp
// A delegate that gives every third row extra room (in practice you would
// decide from the row's own data, e.g. a thumbnail vs. a plain text entry).
class MyDelegate : public UltraCanvasDefaultListDelegate {
public:
    int GetRowHeight(const IListModel* /*model*/, int row) const override {
        return (row % 3 == 0) ? 64 : 24;
    }
};
listView->SetDelegate(std::make_shared<MyDelegate>());
listView->SetVariableRowHeights(true);
```

The `ListViewStyle` struct controls colors, grid lines, alternating rows, header visibility, and the scrollbar style:

```cpp
struct ListViewStyle {
    Color backgroundColor = Colors::White;
    Color headerBackgroundColor = Color(240, 240, 240);
    Color headerTextColor = Colors::Black;
    Color gridLineColor = Color(220, 220, 220);

    float headerFontSize = 10;
    int sortIndicatorSize = 8;      // width of the ▲/▼ sort triangle (px)

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

### Column widths

```cpp
void SetColumnWidth(int column, int width);   // this view only; the model keeps ListColumnDef::width
int  GetColumnWidth(int column) const;        // the effective width
bool ColumnsUserAdjusted() const;             // the user dragged a border: stop fitting

// What fitting a column to its content takes, measured on the context the
// view is painted on:
int MeasureHeaderWidth(IRenderContext* ctx, int column) const;   // whole title + sort triangle
int MeasureColumnTextWidth(IRenderContext* ctx, int column, const FontStyle& font) const;
```

`MeasureHeaderWidth` is the narrowest width at which the column's header shows
its whole title in `headerFontSize`, with room for the sort triangle whether or
not the column is the sorted one - so a fitted column keeps its width when the
order changes, and a translated title is measured as it reads.
`MeasureColumnTextWidth` is the widest `DisplayRole` text of the column's rows
in `font`, without a delegate's padding; each distinct text is measured once
and remembered, so a column of thousands of dates costs a few hundred
measurements. UltraMail fits its Date column this way: the widest date in bold
plus the 6 px its delegate leaves before the text, and at least the header.

```cpp
// A Date column (column 3) as wide as its widest date plus a 6 px gap, never
// narrower than its header - measured where a render context is at hand.
class DateFittedList : public UltraCanvasListView {
public:
    using UltraCanvasListView::UltraCanvasListView;
    void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override {
        if (ctx && !ColumnsUserAdjusted()) {
            FontStyle font;
            font.fontSize = 10;
            const int dates = MeasureColumnTextWidth(ctx, 3, font);
            SetColumnWidth(3, std::max(dates + 6, MeasureHeaderWidth(ctx, 3)));
        }
        UltraCanvasListView::Render(ctx, dirtyRect);
    }
};
```

### Scrolling

```cpp
void ScrollToRow(int row);
void EnsureRowVisible(int row);
```

`EnsureRowVisible` only scrolls when the target row is currently off-screen; `ScrollToRow` always recenters.
Called before the view has been laid out (no height yet), `EnsureRowVisible`
remembers the row and reveals it once the view has its size, so a list filled
and selected while its window is still being built opens with that row in view.

```cpp
ScrollMetrics GetScrollMetrics() const;
```

The numbers the scrollbar is built from at this moment: `rows`, `rowHeight`
(0 when the delegate sizes rows), `contentHeight`, `viewportHeight` (the
rows area, header excluded), `maxScroll`, `scrollOffset`,
`scrollbarVisible`, `scrollbarBounds` (0,0 0x0 while the scrollbar is
hidden), and the element's `width` and `height`. For diagnostics and tests. The view also recomputes them before
every paint and, if the scrollbar disagrees (a model that grew between an
arrange and the paint), logs `scrollbar was stale at paint` to
`debugOutput` and refreshes it first.

### Tooltips

```cpp
void SetShowItemTooltips(bool enable);   // default: true
bool GetShowItemTooltips() const;

// Consulted before the model; row == -1 means the header cell of `column`.
// Return "" to fall back to the model tooltip / ListColumnDef::tooltip.
std::function<std::string(int row, int column)> tooltipProvider;

// What the hover tooltip would show (row == -1 for a column header); "" if none
std::string GetTooltipTextAt(int row, int column) const;

// Header column under an element-local point, -1 when outside it
int GetHeaderColumnAt(int x, int y, int* columnStartX = nullptr) const;
```

Tooltips are on by default and need no wiring: the view watches the hovered
cell and asks `UltraCanvasTooltipManager` to show

- the cell's `ToolTipRole` text when the pointer rests on a row — per-cell for
  a multi-column model, falling back to the row-wide `tooltip`; and
- `ListColumnDef::tooltip` when the pointer rests on a column header.

The tooltip is refreshed whenever the hovered *cell* changes, so moving sideways
across a row swaps it, and hidden when the cell has no tooltip, when the pointer
leaves the list, and when the wheel scrolls rows out from under it.

```cpp
// Per-column header tooltips
multiModel->AddColumn(ListColumnDef("Size", 70, TextAlignment::Right,
                                    "Size on disk, rounded to one decimal"));

// Per-cell tooltips, with a row-wide fallback
MultiColumnListItem item({"main.cpp", "C++ Source", "2.4 KB", "2025-03-15"});
item.tooltip = "src/main.cpp";            // used by any cell without its own
item.SetCellTooltip(2, "2,458 bytes");    // Size column only
multiModel->AddItem(item);

// Computed tooltips (no data stored in the model)
listView->tooltipProvider = [](int row, int column) -> std::string {
    if (row < 0) return {};               // let the header use its ListColumnDef
    return "Row " + std::to_string(row);
};

listView->SetShowItemTooltips(false);     // opt out entirely
```

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
    // Every row at once, one change notification (AddItem notifies per row).
    void SetItems(std::vector<MultiColumnListItem> newItems);

    int GetItemCount() const;
    const MultiColumnListItem& GetItem(int row) const;
};

struct ListColumnDef {
    std::string title;
    int width = 100;
    TextAlignment alignment = TextAlignment::Left;
    std::string tooltip;        // shown when hovering this column's header

    ListColumnDef(const std::string& t, int w = 100,
                  TextAlignment a = TextAlignment::Left);
    ListColumnDef(const std::string& t, int w, TextAlignment a,
                  const std::string& tip);
};

struct MultiColumnListItem {
    std::vector<std::string> labels;
    std::vector<std::string> iconPaths;
    std::string tooltip;                     // row-wide fallback tooltip
    std::vector<std::string> cellTooltips;   // optional, per column
    void* userData = nullptr;

    void SetCellTooltip(int column, const std::string& tip);
    const std::string& GetCellTooltip(int column) const;  // falls back to tooltip
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
    // Rows inserted (count > 0) or removed (count < 0) at `row`: the same items
    // stay selected at their new rows (the view calls it from the model).
    virtual void ShiftRows(int row, int count);
};

class UltraCanvasSingleSelection : public IListSelection { /* ... */ };
class UltraCanvasMultiSelection  : public IListSelection { /* ... */ };
```

If `SetSelection()` is never called, the view installs a single-selection by default.

The selection follows the items, not the row numbers: when the model inserts
or removes rows (`InsertItem`, `RemoveItem`), the view moves the selection,
the keyboard focus and the hover with them, so a row inserted above the
selected one leaves the same item selected. That moves no item in or out of
the selection, so it raises no `onSelectionChanged`; a selected row that is
removed leaves the selection, and that is reported. `SetItems` / `Clear`
replace every row, and the caller selects again.

## Events / Callbacks

```cpp
std::function<void(int row)> onItemClicked;
std::function<void(int row)> onItemDoubleClicked;
std::function<void(int row)> onItemActivated;
std::function<void(const std::vector<int>&)> onSelectionChanged;
std::function<void(int row)> onItemHovered;

// Cell-level (multi-column aware). posInCell is relative to the cell's
// top-left corner. onCellHovered reports (-1, -1) when the pointer leaves
// the rows area.
std::function<void(int row, int column, const Point2Di& posInCell)> onCellClicked;
std::function<void(int row, int column, const Point2Di& posInCell)> onCellHovered;

// A click (press and release in the same cell) on a column header. A press
// on a resize border starts a drag instead and never reports a click.
std::function<void(int column)> onHeaderClicked;

// A right-button press in the rows area: `row` is the row under the pointer
// (-1 below the rows), selected alone first so the menu acts on it. When
// set, the press is consumed; when not, a right press behaves like a left.
std::function<void(int row, const UCEvent& event)> onContextMenu;
```

`onSelectionChanged` fires whenever the selection set changes (single or multi-select). `onItemActivated` fires on Enter or double-click. Both `onItemClicked` and `onCellClicked` fire on a click, the cell-level one second.

`onHeaderClicked` is where sorting is wired up: re-order the rows by the
column, toggling the direction when it is already the sort column, then tell
the view which column is sorted so the header shows it. The ready-made way to
re-order is to show the model through an
[`UltraCanvasListSortFilterProxy`](UltraCanvasListSortFilterProxy.md). A header press no
longer counts as a click on "no row", so it leaves the selection alone.

`onContextMenu` is where a right-click menu is wired up. The view has
already selected the row under the pointer, so the handler reads the
selection (or `row`) and opens a popup menu at the pointer:

```cpp
class FileListPanel {
public:
    FileListPanel(UltraCanvasWindowBase& window,
                  std::shared_ptr<UltraCanvasListView> listView)
        : window_(&window), listView_(std::move(listView)) {
        listView_->onContextMenu = [this](int row, const UCEvent& event) {
            contextMenu_ = std::make_shared<UltraCanvasMenu>("listCtx", 0, 0, 200, 0);
            contextMenu_->SetMenuType(MenuType::PopupMenu);
            contextMenu_->AddItem(MenuItemData::Submenu("Export", {
                MenuItemData::Action("As CSV…", [this]() { ExportCsv(); }),
            }));
            PopupElementSettings settings;
            contextMenu_->OpenMenu(event.pointerWindow, *window_, settings);
        };
    }

private:
    void ExportCsv();                                // your export code
    UltraCanvasWindowBase* window_;
    std::shared_ptr<UltraCanvasListView> listView_;
    std::shared_ptr<UltraCanvasMenu> contextMenu_;   // keeps the open menu alive
};
```

Keep the menu in a member: `OpenMenu` shows it, and a menu that goes out of
scope at the end of the handler closes before it is seen.

```cpp
// The view shows a sorting proxy over the source model
auto proxy = std::make_shared<UltraCanvasListSortFilterProxy>(model);
listView->SetModel(proxy);

listView->onHeaderClicked = [view = listView.get(), proxy](int column) {
    bool ascending = (view->GetSortColumn() == column) ? !view->GetSortAscending() : true;
    proxy->SortByColumn(column, ascending ? ListSortOrder::Ascending
                                          : ListSortOrder::Descending);
    view->SetSortIndicator(column, ascending); // ▲ or ▼ in that header cell
};
```

## Usage Examples

### Simple single-selection list

```cpp
auto simpleModel = std::make_shared<UltraCanvasSimpleListModel>();
simpleModel->AddItem(ListItem("Apple",  "", "A common red fruit"));
simpleModel->AddItem(ListItem("Banana", "", "A yellow tropical fruit"));
simpleModel->AddItem(ListItem("Cherry", "", "Small red stone fruit"));
simpleModel->AddItem(ListItem("Date",   "", "Sweet desert fruit"));

auto simpleList = std::make_shared<UltraCanvasListView>("SimpleListView", 20, 125, 460, 225);
simpleList->SetModel(simpleModel);
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
multiModel->AddColumn(ListColumnDef("File Name", 170, TextAlignment::Left,
                                    "Name of the file on disk"));
multiModel->AddColumn(ListColumnDef("Type",       90, TextAlignment::Left,
                                    "File type, derived from the extension"));
multiModel->AddColumn(ListColumnDef("Size",       70, TextAlignment::Right,
                                    "Size on disk, rounded to one decimal"));
multiModel->AddColumn(ListColumnDef("Modified",  110, TextAlignment::Left,
                                    "Date of the last write, YYYY-MM-DD"));

MultiColumnListItem mainCpp({"main.cpp", "C++ Source", "2.4 KB", "2025-03-15"});
mainCpp.tooltip = "src/main.cpp";               // any cell without its own
mainCpp.SetCellTooltip(2, "2,458 bytes");       // Size column
mainCpp.SetCellTooltip(3, "15 Mar 2025, 09:14");// Modified column
multiModel->AddItem(mainCpp);

multiModel->AddItem(MultiColumnListItem({"utils.h",    "C++ Header",  "1.1 KB",  "2025-03-14"}));
multiModel->AddItem(MultiColumnListItem({"README.md",  "Markdown",    "3.8 KB",  "2025-03-10"}));
multiModel->AddItem(MultiColumnListItem({"Makefile",   "Build Script","0.9 KB",  "2025-02-28"}));

auto multiList = std::make_shared<UltraCanvasListView>("MultiColumnListView", 500, 125, 480, 225);
multiList->SetModel(multiModel);

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
styledList->SetModel(styledModel);

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
iconList->SetModel(iconModel);
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

### A custom delegate with clickable cells (the domain dashboard)

The DemoApp's *Domain Dashboard* page
(`Apps/DemoApp/UltraCanvasListViewDashboardExamples.cpp`) is a table whose
cells are more than text: a link, a status, an action, a sparkline, a figure, a
plan and a row-menu button. A list view does not hold elements per row; one
`IItemDelegate` paints every cell from the model, and the view's cell
callbacks make parts of the cells clickable. That is what keeps it fast at any
size: only the rows on screen are painted (the page's *Add 1,000 domains*
button shows it).

The model keeps records and answers text and tooltips per column; the delegate
reads the record directly for what is not text (the sparkline's history):

<!-- doc-check:
struct DomainRecord { std::string domain; bool active = true; bool insightsEnabled = false; std::vector<float> history; int visitors = 0; std::string plan = "Free"; };
enum DomainColumn { ColDomain, ColStatus, ColInsights, ColTrend, ColVisitors, ColPlan, ColMenu, ColCount };
enum { kCellPadding = 10 };
const Color kLinkColor(0, 102, 204);
const Color kTextColor(40, 40, 40);
void DrawSparkline(IRenderContext* ctx, const std::vector<float>& values, const Rect2Dd& box);
std::shared_ptr<UltraCanvasListView> list;
-->

```cpp
class DomainListModel : public IListModel {
public:
    std::vector<DomainRecord> rows;   // domain, active, history, visitors, plan
    int GetRowCount() const override { return static_cast<int>(rows.size()); }
    int GetColumnCount() const override { return ColCount; }
    ListColumnDef GetColumnDef(int column) const override;   // titles, widths, header tooltips
    ListDataValue GetData(const ListIndex& index, ListDataRole role) const override;
    bool SetData(const ListIndex&, ListDataRole, const ListDataValue&) override { return false; }
    bool SortBy(int column, bool ascending);                  // reorders rows, NotifyDataChanged()
    void RowChanged(int row) { NotifyRowChanged(row); }
};

class DomainRowDelegate : public IItemDelegate {
public:
    int hoverRow = -1, hoverColumn = -1;
    bool hoverOnTarget = false;

    void RenderItem(IRenderContext* ctx, const IListModel* model, int row, int column,
                    const ListItemStyleOption& option) override {
        const auto* domains = dynamic_cast<const DomainListModel*>(model);
        if (!domains || row < 0 || row >= domains->GetRowCount()) return;
        const Rect2Dd cell(option.columnX + kCellPadding, option.rect.y,
                           option.columnWidth - 2 * kCellPadding, option.rect.height);
        if (column == ColTrend) {                         // not text: draw it
            DrawSparkline(ctx, domains->rows[row].history, cell);
            return;
        }
        ctx->SetTextAlignment(option.columnAlignment);
        ctx->SetTextVerticalAlignment(VerticalAlignment::Middle);
        ctx->SetTextPaint(column == ColDomain ? kLinkColor : kTextColor);   // and status green, ...
        ctx->DrawTextInRect(GetStringValue(model->GetData({row, column},
                                           ListDataRole::DisplayRole)), cell);
    }
    int GetRowHeight(const IListModel*, int) const override { return 44; }

    // Whether a cell-local point is on what the cell lets you click: the
    // link's or "Enable"'s text (its width measured while painting), or ⋮.
    bool OnTarget(const DomainListModel& model, int row, int column, const Point2Di& inCell) const;
};
```

The view paints the row background (selection, hover) before the delegate
runs, so the delegate draws content only. The default selection is a strong
blue; a dashboard with coloured text sets a light one in its `ListViewStyle`
(`selectionBackgroundColor`) so the colours stay readable.

Clicks and hover come per cell, with the point inside the cell - the same
space the delegate painted in - so the handler can tell the link text from the
empty part of its cell:

```cpp
struct DashboardState {                                   // what the callbacks share
    std::shared_ptr<DomainListModel> model;
    std::shared_ptr<DomainRowDelegate> delegate;
    UltraCanvasListView* list = nullptr;                  // raw: the view owns the callbacks
    int sortColumn = -1;
    bool sortAscending = true;
    void OpenRowMenu(const std::string& domain, const Point2Di& at);
};
auto state = std::make_shared<DashboardState>();
state->list = list.get();

list->onCellHovered = [state](int row, int column, const Point2Di& inCell) {
    state->delegate->hoverOnTarget = row >= 0 && state->delegate->OnTarget(*state->model, row, column, inCell);
    state->delegate->hoverRow = row;
    state->delegate->hoverColumn = column;
    state->list->SetMouseCursor(state->delegate->hoverOnTarget ? UCMouseCursor::Hand
                                                               : UCMouseCursor::Default);
    state->list->RequestRedraw();                         // the link underlines
};
list->onCellClicked = [state](int row, int column, const Point2Di& inCell) {
    if (!state->delegate->OnTarget(*state->model, row, column, inCell)) return;
    if (column == ColDomain) OpenURL("https://" + state->model->rows[row].domain);
    if (column == ColMenu)
        state->OpenRowMenu(state->model->rows[row].domain,
                           UltraCanvasApplication::GetInstance()->GetCurrentEvent().pointerWindow);
};
list->onContextMenu = [state](int row, const UCEvent& event) {   // right-click a row
    if (row >= 0) state->OpenRowMenu(state->model->rows[row].domain, event.pointerWindow);
};
list->onHeaderClicked = [state](int column) {             // the model orders its rows
    if (column == ColMenu) return;
    state->sortAscending = state->sortColumn == column ? !state->sortAscending : true;
    state->sortColumn = column;
    if (state->model->SortBy(column, state->sortAscending))
        state->list->SetSortIndicator(column, state->sortAscending);
};
```

`state` is a small struct the callbacks share; it holds the list view as a raw
pointer, because the view owns the callbacks that hold `state` (a
`shared_ptr` back to the view would be a cycle). The menu's actions find their
row again by name when they run: sorting or removing rows while a menu is open
moves the row it was opened on.

What a delegate gives up is a real element per cell: the "Enable" action and
the ⋮ button are drawn and hit-tested, not `UltraCanvasButton`s, so they do not
take the keyboard focus one by one - the row does. That is how list views on
every desktop do it. When each row needs live controls of its own (a text
field, a slider), use a container of elements instead, for a short list.

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

The keys go on from the selection's current row, whoever selected it: a click,
a key, or the application through `GetSelection()->Select(row)` - after it
rebuilt or re-sorted the rows and selected the one the user was on, Down moves
to the row below that one. `ResetSelection()` clears the focus as well, so the
first Down after it selects the first row.

## Best Practices

1. **Keep the model alive for as long as the view uses it.** The view stores a raw pointer; if the model is destroyed first, the view will read freed memory.
2. **Notify on data changes.** When mutating a custom model, fire `onDataChanged` / `onRowChanged` / `onRowInserted` / `onRowRemoved` so the view repaints and rescrolls correctly.
3. **Use `UltraCanvasMultiSelection` for any list where Ctrl/Shift-Click should add to selection.** The default is single-selection.
4. **In uniform mode, tune `rowHeight` to match your content** when using icons larger than 16px, to avoid clipped icons. If rows genuinely differ in height, enable `SetVariableRowHeights(true)` and return the right height per row from `delegate->GetRowHeight(model, row)` instead.
5. **For large datasets**, keep `GetData()` cheap — it is called once per visible cell per repaint.

## See Also

- DemoApp → *Domain Dashboard* (`Apps/DemoApp/UltraCanvasListViewDashboardExamples.cpp`) — the custom-delegate example above, complete
- [UltraCanvasTreeView](UltraCanvasTreeViewExamples.md) — Hierarchical equivalent of ListView
- [UltraCanvasScrollbar](UltraCanvasScrollbar.md) — Scrollbar used internally
- [UltraCanvasLabel](UltraCanvasLabelExamples.md) — Static text display
- [UltraCanvasUIElement](UltraCanvasUIElement.md) — Base class documentation
