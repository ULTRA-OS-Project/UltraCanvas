// include/UltraCanvasFormLayout.h
// The "caption: control" form: a two-column grid whose first column is `auto`
// and whose second is `1fr`.
//
// Why this exists rather than a row per field: a flex row per field gives
// every caption its own width, so either the captions carry a hard-coded pixel
// width (and the first translation that is longer than it gets cut off) or the
// controls start at a different x on every line. One grid solves both — the
// caption column is exactly as wide as the widest caption in it, whatever
// language the captions are in, and every control starts where that column
// ends.
//
//     auto form = CreateFormGrid("settings");
//     window->AddChild(form);
//     AddFormRow(form, "name", "Name:", nameInput);
//     AddFormRow(form, "size", "Size:", sizeRow);
//     AddFormWideRow(form, interlaceCheckbox);     // its own caption
//
// Rows hidden with SetVisible(false) leave the grid entirely (display:none),
// so a form can show a different set of rows without leaving gaps.
// Version: 1.0.0
// Last Modified: 2026-09-13
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "CSSLayout/CSSLayout.h"

#include <memory>
#include <string>

namespace UltraCanvas {

    // The grid every row goes into. Add it to the window (or a section) as a
    // stretched child; the rows are its children.
    inline std::shared_ptr<UltraCanvasContainer> CreateFormGrid(const std::string& identifier,
                                                                 float rowGap = 8.0f,
                                                                 float columnGap = 12.0f) {
        auto grid = std::make_shared<UltraCanvasContainer>(identifier, 0, 0, 0, 0);
        grid->layout.SetGrid();
        CSSLayout::GridTrackSize captionColumn;                       // auto: the widest caption
        CSSLayout::GridTrackSize controlColumn;
        controlColumn.kind = CSSLayout::GridTrackSizeKind::Fr;
        controlColumn.value = CSSLayout::Dimension::Fr(1);
        grid->layout.SetGridColumns({captionColumn, controlColumn});
        grid->layout.SetGridGap(rowGap, columnGap);
        grid->layout.SetGridAlignItems(CSSLayout::AlignItems::Center);
        grid->layoutItem.SetFlexGrow(0).SetFlexShrink(0).SetAlignSelf(CSSLayout::AlignSelf::Stretch);
        return grid;
    }

    // A caption sized by its own text — no width, so the grid column measures
    // it. Use it when a row needs the label object (to restyle or hide it).
    inline std::shared_ptr<UltraCanvasLabel> CreateFormCaption(const std::string& identifier,
                                                               const std::string& text) {
        auto label = std::make_shared<UltraCanvasLabel>(identifier, -1, -1, -1, -1, text);
        label->SetAlignment(TextAlignment::Left, VerticalAlignment::Middle);
        return label;
    }

    // "caption:  [control]". Returns the caption so the caller can keep it
    // (hiding a row means hiding its caption too).
    inline std::shared_ptr<UltraCanvasLabel> AddFormRow(
            const std::shared_ptr<UltraCanvasContainer>& grid,
            const std::string& identifier, const std::string& caption,
            const std::shared_ptr<UltraCanvasUIElement>& control) {
        if (!grid || !control) return nullptr;
        auto label = CreateFormCaption(identifier + "-caption", caption);
        grid->AddChild(label);
        grid->AddChild(control);
        return label;
    }

    inline void AddFormRow(const std::shared_ptr<UltraCanvasContainer>& grid,
                           const std::shared_ptr<UltraCanvasLabel>& caption,
                           const std::shared_ptr<UltraCanvasUIElement>& control) {
        if (!grid || !caption || !control) return;
        grid->AddChild(caption);
        grid->AddChild(control);
    }

    // A control that is its own caption (a checkbox, a heading, a note) across
    // both columns.
    inline void AddFormWideRow(const std::shared_ptr<UltraCanvasContainer>& grid,
                               const std::shared_ptr<UltraCanvasUIElement>& element) {
        if (!grid || !element) return;
        CSSLayout::GridLine autoStart;
        CSSLayout::GridLine spanTwo;
        spanTwo.type = CSSLayout::GridLineKind::Span;
        spanTwo.index = 2;
        element->layoutItem.SetGridColumn(autoStart, spanTwo);
        grid->AddChild(element);
    }

    // A row of controls that share one cell: "700 x 500 [x] Lock", a slider
    // and its value, a checkbox and a button.
    inline std::shared_ptr<UltraCanvasContainer> CreateFormCellRow(const std::string& identifier,
                                                                   float gap = 8.0f,
                                                                   int height = 28) {
        auto row = std::make_shared<UltraCanvasContainer>(identifier, 0, 0, 0, height);
        row->layout.SetFlexRow().SetFlexGap(gap).SetFlexAlignItems(CSSLayout::AlignItems::Center);
        return row;
    }

} // namespace UltraCanvas
