# Forms: captions that line up and still translate

`UltraCanvasFormLayout.h` is the "caption: control" form — the shape of nearly
every dialog. It is a header of four inline helpers over one CSS grid, and it
exists because the obvious alternative is quietly broken.

```cpp
#include "UltraCanvasFormLayout.h"

auto form = CreateFormGrid("export-form");
window->AddChild(form);

AddFormRow(form, "name",   "Name:",   fileNameInput);
AddFormRow(form, "format", "Format:", formatDropdown);
AddFormWideRow(form, interlaceCheckbox);      // a control that is its own caption
```

## Why not a row per field

A flex row per field gives every caption its own width, which leaves two bad
options:

| | What goes wrong |
|---|---|
| Hard-code the caption width (`CreateLabel(id, 0, 0, 80, 24)`) | The first translation longer than 80 px is cut off — "Compress:" is fine, "Komprimierung:" is not. |
| Let each row size its own caption | Every row's control starts at a different x, and the dialog looks ragged. |

Worse, sections built as separate containers each size their captions
separately, so the fields of one section line up with each other and with
nothing else — which is what the image export dialog used to do.

One grid solves both at once. The caption column is `auto`, so it is exactly as
wide as the widest caption **in the whole form**, in whatever language; the
control column is `1fr`, so every control starts where that column ends and
takes the rest of the row. Nothing measures text, nothing is placed by
coordinate, and a longer caption widens the column instead of clipping.

## The helpers

| Call | What it makes |
|---|---|
| `CreateFormGrid(id, rowGap, columnGap)` | The grid: columns `[auto, 1fr]`, items centred vertically, stretched into its parent. Add it to the window or section. |
| `AddFormRow(grid, id, "Caption:", control)` | A row. Returns the caption label, so a caller that hides rows can hide the caption with them. |
| `AddFormRow(grid, caption, control)` | The same with a caption the caller built (to restyle it, or to keep a pointer). |
| `AddFormWideRow(grid, element)` | An element across both columns: a checkbox, a section heading, a note. |
| `CreateFormCellRow(id, gap, height)` | A flex row for controls that share one cell — `700 × 500 [x] Lock`, a slider and its value, a checkbox and a button. |
| `CreateFormCaption(id, text)` | A caption sized by its own text (no width), if you need one outside a row. |

Rows hidden with `SetVisible(false)` leave the grid entirely (`display: none`),
so a form can swap one set of rows for another — the export dialog shows the
current format's options that way — without leaving holes or empty tracks.

## Buttons

A button sized to fit "Cancel" cuts off "Abbrechen". Give it an auto width with
a floor instead — `UltraCanvasModalDialog::SizeButtonToLabel` is the reference:

```cpp
button->size.width = CSSLayout::Dimension::Auto();
CSSLayout::BoxConstraints limits = button->boxConstraints.value_or(CSSLayout::BoxConstraints{});
limits.minWidth = CSSLayout::Dimension::Px(96);
button->boxConstraints = limits;
button->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
```

## Make the window resizable

The grid degrades gracefully when a caption grows — the control column gives up
the space — but a fixed-size window has nowhere to give it back from. A dialog
whose captions are translated should set `config_.resizable = true` with a
sensible `minWidth`, so a user who needs more room can take it.

## The engine rule underneath

A grid item that spans both columns does **not** drag the `auto` column out to
its own width: CSS Grid §12.5 says an item whose span crosses a flexible track
contributes nothing to the base size of the intrinsic tracks it also spans, and
`GridLayout.cpp` implements that. Without it, one wide full-width note would
make the caption column as wide as the note and push every control across the
dialog. `Tests/CSSLayoutFormGridTest.cpp` pins this, together with the
column-sharing and hidden-row behaviour.

## See also

- [CSSLayout](../CSSLayout.md) — the layout engine and the element contract
- [UltraCanvasUIElements](UltraCanvasUIElements.md) — which element to use for a field
