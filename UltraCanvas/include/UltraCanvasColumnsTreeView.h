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
// matches the base UltraCanvasTreeView behaviour. Columns renders three aligned
// columns (Name / Type / Value) with an accent-filled Type column, matching an
// IDE-style debugger "Variables" panel. Both modes keep the tree hierarchy and
// expand/collapse.
enum class TreeDisplayMode {
    Classic = 0,   // single-text rows (delegates to the base tree)
    Columns = 1    // aligned Name / Type / Value columns
};

// ===== COLUMN STYLE =====
// Geometry and colours used when TreeDisplayMode::Columns is active.
struct TreeColumnStyle {
    int   typeColumnWidth      = 128;   // fixed width of the Type column (px)
    int   valueColumnWidth     = 0;    // 0 => Value column takes the remaining width to the right
    int   columnGap            = 8;    // horizontal gap between columns (px)
    int   typeColumnPadding    = 4;    // padding around the Type accent fill (px)
    Color typeColumnBackground = Color(255, 190, 130);  // orange accent behind the Type column
    Color typeTextColor        = Color(40, 40, 40);     // Type column text
    Color valueTextColor       = Color(40, 40, 40);     // Value column text
    Color groupHeaderBackground = Colors::Black;        // full-width section-header bar
    Color groupHeaderTextColor  = Colors::White;        // section-header text
    bool  showColumnSeparators  = false;                // thin vertical rules between columns
    Color columnSeparatorColor  = Color(210, 210, 210);
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

    // Column geometry/colours used by Columns mode.
    void SetColumnStyle(const TreeColumnStyle& style) { columnStyle = style; RequestRedraw(); }
    const TreeColumnStyle& GetColumnStyle() const { return columnStyle; }

protected:
    // Full-width section-header bar for group-header rows (Columns mode only).
    bool RenderNodeFullRow(IRenderContext* ctx, TreeNode* node, int nodeY,
                           const Rect2Di& contentRect, int rowWidth) override;

    // Draw the Name / Type / Value columns (Columns mode), else fall back to the
    // base single-text label.
    void RenderNodeLabel(IRenderContext* ctx, TreeNode* node, int nodeY,
                         int textX, int nodeWidth, int sbWidth,
                         const Rect2Di& contentRect) override;

private:
    // Row painter: draws the Name/Type/Value columns for a normal (non-header) row.
    void RenderNodeColumns(IRenderContext* ctx, TreeNode* node, int nodeY, int textX,
                           int rowLeft, int rowWidth);

    TreeDisplayMode displayMode = TreeDisplayMode::Columns;  // columns by default
    TreeColumnStyle columnStyle;
};

} // namespace UltraCanvas
