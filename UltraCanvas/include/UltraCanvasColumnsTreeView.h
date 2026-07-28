// include/UltraCanvasColumnsTreeView.h
// Tree view variant that renders IDE-style Name / Type / Value columns (plus
// full-width section-header bars) on top of the generic UltraCanvasTreeView.
// Extracted from the Modern display mode that used to live in the base tree.
// Last Modified: 2026-07-26
#pragma once

#include "UltraCanvasTreeView.h"

namespace UltraCanvas {

// ===== TREE DISPLAY MODE =====
// How each row is laid out. Classic renders a single line of text (data.text) and
// matches the base UltraCanvasTreeView behaviour. Columns renders an arbitrary set
// of aligned columns defined via SetColumns() (an IDE-style debugger "Variables"
// panel, for example). Both modes keep the tree hierarchy and expand/collapse.
enum class TreeDisplayMode {
    Classic = 0,   // single-text rows (delegates to the base tree)
    Columns = 1    // aligned, user-defined columns
};

// ===== COLUMN DEFINITION =====
// One column of the columnar layout. The order of columns in the vector is the
// left-to-right paint order. Exactly one column should set isTreeColumn (the one
// that hosts the indent/expander/icon and draws node->data.text); every other
// column draws its cell from node->data.cells, keyed by this column's id.
struct TreeViewColumn {
    std::string   id;                        // key into TreeNodeData::cells
    std::string   title;                     // header label (drawn only when a header is enabled)
    int           width      = 0;            // > 0 => fixed px; <= 0 => flexible (shares remaining space)
    int           minWidth   = 0;            // floor for flexible columns / resize
    float         flexWeight = 1.0f;         // relative share among flexible columns
    TextAlignment alignment  = TextAlignment::Left;
    Color         textColor  = Color(40, 40, 40);        // default cell text colour
    Color         accentBackground = Colors::Transparent;// optional fill behind the cell (Transparent => none)
    int           accentPadding    = 0;      // px added around the accent fill
    bool          isTreeColumn = false;      // hosts indent/expander/icon; draws node->data.text
};

// ===== COLUMN STYLE =====
// Global (non-per-column) geometry and colours used by TreeDisplayMode::Columns.
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

// ===== COLUMNS TREE VIEW =====
class UltraCanvasColumnsTreeView : public UltraCanvasTreeView {
public:
    // Reuse the base constructors (identifier / x,y,w,h / w,h variants).
    using UltraCanvasTreeView::UltraCanvasTreeView;

    // ===== DISPLAY MODE (Columns / Classic) =====
    // Switch between the columnar layout and the base single-text layout. Safe to
    // toggle at runtime; triggers a redraw. Node data is shared between modes.
    void SetDisplayMode(TreeDisplayMode mode);
    TreeDisplayMode GetDisplayMode() const { return displayMode; }

    // ===== COLUMNS =====
    // Define the columns (left-to-right). If never called, a default Name/Type/Value
    // set is installed lazily so the view still renders sensibly.
    void SetColumns(std::vector<TreeViewColumn> cols);
    const std::vector<TreeViewColumn>& GetColumns() const { return columns_; }
    // Index of the tree column (first isTreeColumn, else 0). Empty columns => 0.
    int  TreeColumnIndex() const;

    // Optional fixed header band showing the column titles. Off by default.
    void SetShowColumnHeader(bool show);
    bool GetShowColumnHeader() const { return showHeader_; }

    // Whether the user can drag column boundaries to resize. On by default.
    void SetColumnsResizable(bool on) { resizable_ = on; }
    bool GetColumnsResizable() const { return resizable_; }

    // Global (non-per-column) geometry/colours used by Columns mode.
    void SetColumnStyle(const TreeColumnStyle& style) { columnStyle = style; RequestRedraw(); }
    const TreeColumnStyle& GetColumnStyle() const { return columnStyle; }

    // Intercept mouse events for column-boundary resizing; otherwise defers to base.
    bool OnEvent(const UCEvent& event) override;

protected:
    // Full-width section-header bar for group-header rows (Columns mode only).
    bool RenderNodeFullRow(IRenderContext* ctx, TreeNode* node, int nodeY,
                           const Rect2Di& contentRect, int rowWidth) override;

    // Draw the user-defined columns (Columns mode), else fall back to the base
    // single-text label.
    void RenderNodeLabel(IRenderContext* ctx, TreeNode* node, int nodeY,
                         int textX, int nodeWidth, int sbWidth,
                         const Rect2Di& contentRect) override;

    // Fixed header band (column titles). Height is 0 unless a header is enabled.
    int  GetHeaderHeight() const override;
    void RenderHeader(IRenderContext* ctx, const Rect2Di& headerRect) override;

private:
    // Laid-out geometry of one column on a row. x/w are the drawn text region (the
    // tree column's x follows the indent); slotWidth is the column's allocated width
    // (indent-independent), used for resize tracking.
    struct ColLayout { int x; int w; int slotWidth; };

    // Resolve every column's [x, w] for a row of the given box. Non-tree columns are
    // positioned from the fixed row edges (indent-independent => aligned across rows);
    // the tree column starts at textX. Pass textX == treeColumnLeft for the header.
    std::vector<ColLayout> LayoutColumns(int rowLeft, int rowWidth, int textX) const;

    // Row painter: draws the user-defined columns for a normal (non-header) row.
    void RenderNodeColumns(IRenderContext* ctx, TreeNode* node, int nodeY, int textX,
                           int rowLeft, int rowWidth);

    // Install the default Name/Type/Value columns if none were defined yet.
    void EnsureDefaultColumns();

    // The x of the tree column's left edge for a level-0 row (indent-independent
    // reference used for header layout and resize hit-testing).
    int  BaseTreeColumnLeft() const;
    // Index of the resizable boundary near element-local x (right edge of a column),
    // or -1 if none. Fills outX with the boundary's x when found.
    int  BoundaryAtX(int x, int* outX) const;

    TreeDisplayMode displayMode = TreeDisplayMode::Columns;  // columns by default
    TreeColumnStyle columnStyle;
    std::vector<TreeViewColumn> columns_;                    // empty => default set installed lazily
    bool showHeader_ = false;
    bool resizable_  = true;

    // Active column-resize drag (dragCol_ < 0 => not dragging).
    int dragCol_    = -1;
    int dragStartX_ = 0;
    int dragStartW_ = 0;
};

} // namespace UltraCanvas
