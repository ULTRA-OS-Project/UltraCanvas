// core/CSSLayout/Layout.cpp
// Chainable setters on struct Layout and struct LayoutItem.
// Version: 1.0.0
// Last Modified: 2026-05-27
// Author: UltraCanvas Framework

#include "CSSLayout/CSSLayout.h"

namespace UltraCanvas {
    namespace CSSLayout {

        // -------------------- Layout (container side) --------------------

        namespace {
            FlexLayout& asFlex(Layout& L) {
                if (!std::holds_alternative<FlexLayout>(L.data)) {
                    L.data = FlexLayout{};
                }
                return std::get<FlexLayout>(L.data);
            }

            GridLayout& asGrid(Layout& L) {
                if (!std::holds_alternative<GridLayout>(L.data)) {
                    L.data = GridLayout{};
                }
                return std::get<GridLayout>(L.data);
            }
        }

        Layout& Layout::SetFlex(FlexDirection d, FlexWrap w) {
            display = DisplayType::Flex;
            auto& fl = asFlex(*this);
            fl.direction = d;
            fl.wrap = w;
            return *this;
        }

        Layout& Layout::SetFlexDirection(FlexDirection d) {
            display = DisplayType::Flex;
            asFlex(*this).direction = d;
            return *this;
        }

        Layout& Layout::SetFlexWrap(FlexWrap w) {
            display = DisplayType::Flex;
            asFlex(*this).wrap = w;
            return *this;
        }

        Layout& Layout::SetFlexGap(float gap) {
            display = DisplayType::Flex;
            auto& fl = asFlex(*this);
            fl.gap.row    = Dimension::Px(gap);
            fl.gap.column = Dimension::Px(gap);
            return *this;
        }

        Layout& Layout::SetFlexGap(float row, float column) {
            display = DisplayType::Flex;
            auto& fl = asFlex(*this);
            fl.gap.row    = Dimension::Px(row);
            fl.gap.column = Dimension::Px(column);
            return *this;
        }

        Layout& Layout::SetJustifyContent(JustifyContent jc) {
            display = DisplayType::Flex;
            asFlex(*this).justifyContent = jc;
            return *this;
        }

        Layout& Layout::SetAlignItems(AlignItems ai) {
            // align-items applies to both Flex and Grid containers. Default to
            // Flex if the layout hasn't been configured yet.
            if (std::holds_alternative<GridLayout>(data)) {
                display = DisplayType::Grid;
                std::get<GridLayout>(data).alignItems = ai;
            } else {
                display = DisplayType::Flex;
                asFlex(*this).alignItems = ai;
            }
            return *this;
        }

        Layout& Layout::SetAlignContent(AlignContent ac) {
            display = DisplayType::Flex;
            asFlex(*this).alignContent = ac;
            return *this;
        }

        Layout& Layout::SetGrid() {
            display = DisplayType::Grid;
            (void)asGrid(*this);
            return *this;
        }

        Layout& Layout::SetGridColumns(std::vector<GridTrackSize> tracks) {
            display = DisplayType::Grid;
            asGrid(*this).columns.tracks = std::move(tracks);
            return *this;
        }

        Layout& Layout::SetGridRows(std::vector<GridTrackSize> tracks) {
            display = DisplayType::Grid;
            asGrid(*this).rows.tracks = std::move(tracks);
            return *this;
        }

        Layout& Layout::SetGridGap(float gap) {
            display = DisplayType::Grid;
            auto& gl = asGrid(*this);
            gl.rowGap    = Dimension::Px(gap);
            gl.columnGap = Dimension::Px(gap);
            return *this;
        }

        Layout& Layout::SetGridGap(float row, float column) {
            display = DisplayType::Grid;
            auto& gl = asGrid(*this);
            gl.rowGap    = Dimension::Px(row);
            gl.columnGap = Dimension::Px(column);
            return *this;
        }

        Layout& Layout::SetGridAutoFlow(GridAutoFlow f) {
            display = DisplayType::Grid;
            asGrid(*this).autoFlow = f;
            return *this;
        }

        // -------------------- LayoutItem (child side) --------------------

        namespace {
            FlexItem& asFlexItem(LayoutItem& L) {
                if (!std::holds_alternative<FlexItem>(L.data)) {
                    L.data = FlexItem{};
                }
                return std::get<FlexItem>(L.data);
            }

            GridItem& asGridItem(LayoutItem& L) {
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

        LayoutItem& LayoutItem::SetOrder(int o) {
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
