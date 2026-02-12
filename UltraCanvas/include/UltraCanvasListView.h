// include/UltraCanvasListView.h
// Virtual scrolling list view with unified interfaces and header support
// Version: 2.0.0
// Last Modified: 2026-01-24
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasScrollArea.h"
#include "UltraCanvasVirtualInterfaces.h"
#include "UltraCanvasTemplate.h"
#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasRenderContext.h"
#include <vector>
#include <functional>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

namespace UltraCanvas {

// Forward declarations
class UltraCanvasMenu;

// ===== LIST VIEW ENUMS =====

enum class ListSelectionMode {
    None,       // No selection allowed
    Single,     // Single item selection
    Multiple,   // Multiple non-contiguous selection
    Extended    // Range selection with Shift+Click
};

enum class ListLayoutMode {
    List,       // Vertical list (single column)
    Grid        // Multi-column grid layout
};

// ===== COLUMN DEFINITION =====

struct ListViewColumn {
    std::string title;                              // Column header text
    int width = 100;                                // Column width in pixels
    int minWidth = 50;                              // Minimum width
    int maxWidth = 500;                             // Maximum width
    bool resizable = true;                          // Can user resize column
    bool sortable = true;                           // Can click to sort
    bool visible = true;                            // Column visibility
    
    // Data extraction function
    std::function<std::string(const VirtualDataItem&)> getValue;
    
    // Optional custom sort comparison
    std::function<bool(const VirtualDataItem&, const VirtualDataItem&)> compareFunc;
    
    ListViewColumn() = default;
    
    ListViewColumn(const std::string& headerTitle, int w = 100)
        : title(headerTitle), width(w) {}
    
    ListViewColumn(const std::string& headerTitle, int w,
                   std::function<std::string(const VirtualDataItem&)> getter)
        : title(headerTitle), width(w), getValue(getter) {}
};

// ===== LIST VIEW STYLE =====

struct ListViewStyle {
    // Item dimensions
    int itemHeight = 32;                            // Fixed item height
    int itemSpacing = 0;                            // Vertical spacing between items
    int itemPaddingLeft = 8;                        // Left padding
    int itemPaddingRight = 8;                       // Right padding
    
    // Header
    int headerHeight = 28;                          // Header row height
    bool showHeader = false;                        // Show column headers
    Color headerBackgroundColor = Color(240, 240, 240, 255);
    Color headerTextColor = Colors::Black;
    Color headerBorderColor = Color(200, 200, 200, 255);
    
    // Grid layout
    int gridColumns = 3;                            // Number of columns in grid mode
    int gridColumnSpacing = 8;                      // Horizontal spacing
    int gridRowSpacing = 8;                         // Vertical spacing
    
    // Colors
    Color backgroundColor = Colors::White;
    Color alternateRowColor = Color(248, 248, 248, 255);
    Color selectedColor = Color(220, 235, 255, 255);
    Color selectedBorderColor = Color(100, 150, 255, 255);
    Color hoverColor = Color(240, 245, 250, 255);
    Color focusColor = Color(100, 150, 255, 255);
    Color disabledColor = Color(200, 200, 200, 255);
    Color separatorColor = Color(220, 220, 220, 255);
    
    // Visual options
    bool showAlternateRows = false;
    bool showHoverEffect = true;
    bool showFocusIndicator = true;
    bool showSeparators = false;
    int separatorHeight = 1;
    
    static ListViewStyle Default() { return ListViewStyle(); }
    
    static ListViewStyle Compact() {
        ListViewStyle style;
        style.itemHeight = 24;
        style.itemPaddingLeft = 4;
        style.itemPaddingRight = 4;
        style.headerHeight = 24;
        return style;
    }
    
    static ListViewStyle WithHeaders() {
        ListViewStyle style;
        style.showHeader = true;
        return style;
    }
};

// ===== LIST VIEW COMPONENT =====

class UltraCanvasListView : public UltraCanvasScrollArea, 
                           public IVirtualComponent {
public:
    // ===== CALLBACKS =====
    
    std::function<void(int)> onItemClick;
    std::function<void(int)> onItemDoubleClick;
    std::function<void(int, int, int)> onItemRightClick;
    std::function<void(const std::vector<int>&)> onSelectionChanged;
    std::function<void(int)> onItemHover;
    std::function<void()> onItemsChanged;
    std::function<void(int)> onColumnHeaderClick;
    std::function<void(int, bool)> onColumnSort;
    std::function<bool(int)> onItemSelecting;
    std::function<std::shared_ptr<UltraCanvasMenu>(int)> onCreateContextMenu;
    
private:
    // ===== DATA & RENDERING (Unified Interfaces) =====
    std::shared_ptr<IVirtualDataProvider> dataProvider;
    std::shared_ptr<UltraCanvasTemplate> itemTemplate;
    std::unordered_map<int, std::shared_ptr<UltraCanvasTemplate>> templateCache;
    VirtualRenderConfig renderConfig;
    
    // ===== COLUMNS & SORTING =====
    std::vector<ListViewColumn> columns;
    int sortedColumn = -1;
    bool sortAscending = true;
    int resizingColumn = -1;
    int resizeStartX = 0;
    int resizeStartWidth = 0;
    
    // ===== SELECTION STATE =====
    std::unordered_set<int> selectedIndices;
    int lastSelectedIndex = -1;
    int anchorIndex = -1;
    int hoveredIndex = -1;
    int focusedIndex = -1;
    
    // ===== CONFIGURATION =====
    ListViewStyle style;
    ListSelectionMode selectionMode = ListSelectionMode::Single;
    ListLayoutMode layoutMode = ListLayoutMode::List;
    
    // ===== RENDERING STATE =====
    bool needsDataRefresh = true;
    bool needsTemplateRebuild = false;
    bool keyboardNavigationEnabled = true;
    
public:
    // ===== CONSTRUCTOR =====
    
    UltraCanvasListView(const std::string& identifier = "ListView", 
                       long id = 0, int x = 0, int y = 0, int w = 400, int h = 600);
    
    virtual ~UltraCanvasListView() = default;
    
    // ===== IVIRTUALCOMPONENT INTERFACE =====
    
    void SetDataProvider(std::shared_ptr<IVirtualDataProvider> provider) override;
    std::shared_ptr<IVirtualDataProvider> GetDataProvider() const override;
    VirtualRenderConfig GetRenderConfig() const override;
    void SetRenderConfig(const VirtualRenderConfig& config) override;
    void RefreshData() override;
    void ScrollToItem(size_t index, bool alignTop = true) override;
    int GetItemAtPosition(int x, int y) const override;
    
    // ===== IVIRTUALRENDERER INTERFACE =====
    
    VirtualViewport GetViewport() const override;
    VirtualRange CalculateVisibleRange() const override;
    int CalculateItemPosition(size_t index) const override;
    int CalculateItemHeight(size_t index) const override;
    int CalculateTotalContentHeight() const override;
    size_t GetBufferSize() const override;
    void SetBufferSize(size_t bufferItems) override;
    void InvalidateRenderCache() override;
    
    // ===== ITEMPLATERENDERER INTERFACE =====
    
    void SetItemTemplate(std::shared_ptr<UltraCanvasTemplate> templateInstance) override;
    std::shared_ptr<UltraCanvasTemplate> GetItemTemplate() const override;
    void BindDataToTemplate(std::shared_ptr<UltraCanvasTemplate> templateInstance,
                           const VirtualDataItem& item, size_t index) override;
    void ClearTemplateCache() override;
    
    // ===== COLUMN MANAGEMENT =====
    
    void SetColumns(const std::vector<ListViewColumn>& cols);
    void AddColumn(const ListViewColumn& column);
    const std::vector<ListViewColumn>& GetColumns() const;
    void SetShowHeader(bool show);
    bool IsHeaderVisible() const;
    void SortByColumn(int columnIndex, bool ascending = true);
    void ClearSort();
    int GetSortedColumn() const { return sortedColumn; }
    bool IsSortAscending() const { return sortAscending; }
    
    // ===== STYLE CONFIGURATION =====
    
    void SetStyle(const ListViewStyle& newStyle);
    const ListViewStyle& GetStyle() const { return style; }
    ListViewStyle& GetStyleRef() { return style; }
    
    // ===== SELECTION MANAGEMENT =====
    
    void SetSelectionMode(ListSelectionMode mode);
    ListSelectionMode GetSelectionMode() const { return selectionMode; }
    void SelectItem(int index, bool clearOthers = true);
    void DeselectItem(int index);
    void ToggleSelection(int index);
    void SelectRange(int startIndex, int endIndex);
    void SelectAll();
    void ClearSelection();
    bool IsItemSelected(int index) const;
    std::vector<int> GetSelectedIndices() const;
    int GetFirstSelectedIndex() const;
    int GetSelectedCount() const { return static_cast<int>(selectedIndices.size()); }
    
    // ===== LAYOUT MODE =====
    
    void SetLayoutMode(ListLayoutMode mode);
    ListLayoutMode GetLayoutMode() const { return layoutMode; }
    
    // ===== KEYBOARD NAVIGATION =====
    
    void SetKeyboardNavigationEnabled(bool enabled);
    bool IsKeyboardNavigationEnabled() const { return keyboardNavigationEnabled; }
    
    // ===== ITEM COUNT =====
    
    size_t GetItemCount() const;
    
protected:
    // ===== VIRTUAL METHODS FROM UltraCanvasScrollArea =====
    
    Size2Di CalculateContentSize(const Size2Di& viewportSize) override;
    void RenderContent(IRenderContext* ctx) override;
    bool HandleContentEvent(const UCEvent& event) override;
    
private:
    // ===== RENDERING METHODS =====
    
    void RenderHeader(IRenderContext* ctx);
    void RenderItem(IRenderContext* ctx, size_t index);
    void RenderItemWithColumns(IRenderContext* ctx, const VirtualDataItem& item,
                              int x, int y, int width, int height);
    void RenderItemDefault(IRenderContext* ctx, const VirtualDataItem& item,
                          int x, int y, int width, int height);
    void DrawItemBackground(IRenderContext* ctx, size_t index, const VirtualDataItem& item,
                           int x, int y, int width, int height);
    void DrawSelectionHighlight(IRenderContext* ctx, int x, int y, int width, int height);
    void DrawHoverHighlight(IRenderContext* ctx, int x, int y, int width, int height);
    void DrawFocusIndicator(IRenderContext* ctx, int x, int y, int width, int height);
    void DrawSortIndicator(IRenderContext* ctx, int x, int y, bool ascending);
    
    void CalculateItemRect(size_t index, int& x, int& y, int& width, int& height);
    
    // ===== EVENT HANDLERS =====
    
    bool HandleHeaderEvent(const UCEvent& event);
    bool HandleMouseDown(const UCEvent& event);
    bool HandleMouseUp(const UCEvent& event);
    bool HandleMouseMove(const UCEvent& event);
    bool HandleMouseDoubleClick(const UCEvent& event);
    bool HandleKeyDown(const UCEvent& event);
    void HandleItemSelection(int index, bool ctrlPressed, bool shiftPressed);
    bool CheckColumnResize(int x, int y);
    void HandleHeaderClick(int x);
};

// ===== FACTORY FUNCTIONS =====

std::shared_ptr<UltraCanvasListView> CreateListView(
    const std::string& identifier = "ListView",
    int x = 0, int y = 0, int w = 400, int h = 600);

std::shared_ptr<UltraCanvasListView> CreateGridView(
    const std::string& identifier = "GridView",
    int x = 0, int y = 0, int w = 400, int h = 600,
    int columns = 4);

std::shared_ptr<UltraCanvasListView> CreateFileListView(
    const std::string& identifier = "FileList",
    int x = 0, int y = 0, int w = 600, int h = 400);

} // namespace UltraCanvas