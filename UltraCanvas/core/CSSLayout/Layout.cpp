// core/CSSLayout/Layout.cpp
// Chainable setters on struct Layout and struct LayoutItem.
// Version: 1.1.0
// Last Modified: 2026-05-31
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"
#include "UltraCanvasDebug.h"

namespace UltraCanvas {
    namespace CSSLayout {

        // -------------------- Layout (container side) --------------------

        namespace {
            FlexLayout& asFlex(Layout& L) {
                L.display = DisplayType::Flex;
                if (!std::holds_alternative<FlexLayout>(L.data)) {
                    L.data = FlexLayout{};
                }
                return std::get<FlexLayout>(L.data);
            }

            GridLayout& asGrid(Layout& L) {
                L.display = DisplayType::Grid;
                if (!std::holds_alternative<GridLayout>(L.data)) {
                    L.data = GridLayout{};
                }
                return std::get<GridLayout>(L.data);
            }
        }

        Layout& Layout::SetDisplay(DisplayType dt) {
            if (dt == DisplayType::NoDisplay) {
                prevDisplay = display;
            }
            display = dt;
            return *this;
        }

        Layout& Layout::Show() {
            display = prevDisplay;
            if (display == DisplayType::NoDisplay) display = DisplayType::Block; // fallback
            return *this;
        }

        Layout& Layout::Hide() {
            prevDisplay = display;
            display = DisplayType::NoDisplay;
            return *this;
        }

        Layout& Layout::SetFlex(FlexDirection d, FlexWrap w) {
            auto& fl = asFlex(*this);
            fl.direction = d;
            fl.wrap = w;
            return *this;
        }

        Layout& Layout::SetFlexDirection(FlexDirection d) {
            asFlex(*this).direction = d;
            return *this;
        }

        Layout& Layout::SetFlexWrap(FlexWrap w) {
            asFlex(*this).wrap = w;
            return *this;
        }

        Layout& Layout::SetFlexGap(float gap) {
            auto& fl = asFlex(*this);
            fl.gap.row    = Dimension::Px(gap);
            fl.gap.column = Dimension::Px(gap);
            return *this;
        }

        Layout& Layout::SetFlexGap(float row, float column) {
            auto& fl = asFlex(*this);
            fl.gap.row    = Dimension::Px(row);
            fl.gap.column = Dimension::Px(column);
            return *this;
        }

        Layout& Layout::SetFlexJustifyContent(JustifyContent jc) {
            asFlex(*this).justifyContent = jc;
            return *this;
        }

        Layout& Layout::SetFlexAlignItems(AlignItems ai) {
            asFlex(*this).alignItems = ai;
            return *this;
        }

        Layout& Layout::SetFlexAlignContent(AlignContent ac) {
            asFlex(*this).alignContent = ac;
            return *this;
        }

        Layout& Layout::SetGrid() {
            (void)asGrid(*this);
            return *this;
        }

        Layout& Layout::SetGridAlignItems(AlignItems ai) {
            asGrid(*this).alignItems = ai;
            return *this;
        }

        Layout& Layout::SetGridColumns(std::vector<GridTrackSize> tracks) {
            asGrid(*this).columns.tracks = std::move(tracks);
            return *this;
        }

        Layout& Layout::SetGridRows(std::vector<GridTrackSize> tracks) {
            asGrid(*this).rows.tracks = std::move(tracks);
            return *this;
        }

        Layout& Layout::SetGridGap(float gap) {
            auto& gl = asGrid(*this);
            gl.rowGap    = Dimension::Px(gap);
            gl.columnGap = Dimension::Px(gap);
            return *this;
        }

        Layout& Layout::SetGridGap(float row, float column) {
            auto& gl = asGrid(*this);
            gl.rowGap    = Dimension::Px(row);
            gl.columnGap = Dimension::Px(column);
            return *this;
        }

        Layout& Layout::SetGridAutoFlow(GridAutoFlow f) {
            asGrid(*this).autoFlow = f;
            return *this;
        }

        // -------------------- LayoutItem (child side) --------------------

        namespace {
            FlexItem& asFlexItem(LayoutItem& L) {
                if (std::holds_alternative<GridItem>(L.data)) {
                    debugOutput << "CSSLayout WARNING: a flex LayoutItem setter "
                                   "(SetFlexGrow/Shrink/Basis/Order) was called on an item already "
                                   "configured as a GRID item; its grid settings will be discarded. "
                                   "Check that the element's parent uses the matching display type." << std::endl;
                }
                if (!std::holds_alternative<FlexItem>(L.data)) {
                    L.data = FlexItem{};
                }
                return std::get<FlexItem>(L.data);
            }

            GridItem& asGridItem(LayoutItem& L) {
                if (std::holds_alternative<FlexItem>(L.data)) {
                    debugOutput << "CSSLayout WARNING: a grid LayoutItem setter "
                                   "(SetGridColumn/Row/JustifySelf/...) was called on an item already "
                                   "configured as a FLEX item; its flex settings (grow/shrink/basis/"
                                   "align-self) will be discarded. Check that the element's parent uses "
                                   "the matching display type." << std::endl;
                }
                if (!std::holds_alternative<GridItem>(L.data)) {
                    L.data = GridItem{};
                }
                return std::get<GridItem>(L.data);
            }
        }

        LayoutItem& LayoutItem::SetFlexGrow(float g) {
            asFlexItem(*this).grow = g;
            return *this;
        }

        LayoutItem& LayoutItem::SetFlexShrink(float s) {
            asFlexItem(*this).shrink = s;
            return *this;
        }

        LayoutItem& LayoutItem::SetFlexBasis(const Dimension& b) {
            asFlexItem(*this).basis = b;
            return *this;
        }

        LayoutItem& LayoutItem::SetFlex(float g, float s, const Dimension& b) {
            auto& fi = asFlexItem(*this);
            fi.grow = g;
            fi.shrink = s;
            fi.basis = b;
            return *this;
        }

        LayoutItem& LayoutItem::SetAlignSelf(AlignSelf a) {
            // align-self applies to both flex and grid items. Mutate whichever
            // variant arm is active; default to FlexItem if neither.
            if (std::holds_alternative<GridItem>(data)) {
                std::get<GridItem>(data).alignSelf = a;
            } else {
                asFlexItem(*this).alignSelf = a;
            }
            return *this;
        }

        LayoutItem& LayoutItem::SetFlexOrder(int o) {
            asFlexItem(*this).order = o;
            return *this;
        }

        LayoutItem& LayoutItem::SetGridColumn(GridLine start, GridLine end) {
            auto& gi = asGridItem(*this);
            gi.columnStart = start;
            gi.columnEnd   = end;
            return *this;
        }

        LayoutItem& LayoutItem::SetGridRow(GridLine start, GridLine end) {
            auto& gi = asGridItem(*this);
            gi.rowStart = start;
            gi.rowEnd   = end;
            return *this;
        }

        LayoutItem& LayoutItem::SetGridRowColSimplified(int row, int col, int rowSpan, int colSpan) {
            CSSLayout::GridLine cs; cs.type = CSSLayout::GridLineKind::Line; cs.index = col + 1;
            CSSLayout::GridLine rs; rs.type = CSSLayout::GridLineKind::Line; rs.index = row + 1;
            CSSLayout::GridLine ce; ce.type = CSSLayout::GridLineKind::Span; ce.index = colSpan;
            CSSLayout::GridLine re; re.type = CSSLayout::GridLineKind::Span; re.index = rowSpan;
            return SetGridColumn(cs, ce).SetGridRow(rs, re);
        }

        LayoutItem& LayoutItem::SetJustifySelf(JustifySelf j) {
            
            asGridItem(*this).justifySelf = j;
            return *this;
        }

        LayoutItem& LayoutItem::SetGridAlignSelf(AlignSelf a) {
            asGridItem(*this).alignSelf = a;
            return *this;
        }

    }
}
