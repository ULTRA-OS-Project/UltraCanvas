# Layout Examples

**Version:** 2.0.0
**Last Modified:** 2026-10-07

UltraCanvas lays out its UI with a CSS-style engine (`UltraCanvas::CSSLayout`).
Every `UltraCanvasUIElement` is a layout node, so there are no separate layout
manager objects: a container chooses how it places its children through its
`layout` member (flex row, flex column, grid, table or plain block stacking),
and each child describes itself through `layoutItem` (grow, shrink, alignment,
grid cell), `size`, `boxConstraints` and its margin and padding. The engine
measures and places everything once per frame; you never compute coordinates.

This page is a set of worked examples. The full reference — the element
contract, the box model, the stretch rules, tables, troubleshooting — is
[Docs/CSSLayout.md](../CSSLayout.md). The demo app shows the same patterns live
on its **Layout** page (`Apps/DemoApp/UltraCanvasLayoutExamples.cpp`), whose
second tab covers label placement for charts and diagrams
([UltraCanvasLabelPlacement.md](UltraCanvasLabelPlacement.md)).

## The pieces

| You want | Write |
|---|---|
| children side by side | `container->layout.SetFlexRow()` |
| children stacked | `container->layout.SetFlexColumn()` (or leave the default block layout) |
| rows and columns that line up | `container->layout.SetGrid().SetGridColumns({...})` |
| children that wrap onto new lines | `.SetFlexWrap(CSSLayout::FlexWrap::Wrap)` |
| space between children | `.SetFlexGap(px)` / `.SetGridGap(px)` |
| space inside / outside a box | `SetPadding(...)` / `SetMargin(...)` |
| a child that takes the free space | `child->layoutItem.SetFlexGrow(1)` |
| push the next children to the far end | `container->AddStretchSpacer()` |
| children filled across the container | `.SetFlexAlignItems(CSSLayout::AlignItems::Stretch)` |
| a box that never gets narrower than N | `boxConstraints` with `minWidth` |
| hide a child and close the gap | `child->SetVisible(false)` |

Headers used on this page:

```cpp
#include "UltraCanvasContainer.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasCheckbox.h"
#include "UltraCanvasFormLayout.h"
#include "CSSLayout/CSSLayout.h"
```

## The constructor decides who sizes the element

Every widget has the same three constructor shapes, and the one you pick tells
the engine how to treat it:

| Constructor | Example | Result |
|---|---|---|
| `(id, x, y, w, h)` with a non-zero `x` or `y` | `UltraCanvasButton("b", 10, 10, 80, 30)` | pinned at (x, y), out of the flow (`PositionType::AbsoluteUI`) |
| `(id, w, h)` or `(id, 0, 0, w, h)` | `UltraCanvasButton("b", 80, 30, "OK")` | in the flow, fixed size; the parent places it |
| `(id)` or `(id, text)` | `UltraCanvasButton("b", "OK")` | in the flow, sized by its content (and by the parent's stretch, if asked) |

Children of a flex or grid container use the last two forms. A 0 for `w` or
`h` means "automatic" on that axis.

## A row: a toolbar

```cpp
auto toolbar = std::make_shared<UltraCanvasContainer>("toolbar");
toolbar->SetPadding(6);
toolbar->layout.SetFlexRow()
               .SetFlexGap(6)
               .SetFlexAlignItems(CSSLayout::AlignItems::Center);   // buttons centred vertically

auto newButton  = std::make_shared<UltraCanvasButton>("tb-new",  "New");
auto openButton = std::make_shared<UltraCanvasButton>("tb-open", "Open");
auto saveButton = std::make_shared<UltraCanvasButton>("tb-save", "Save");
auto helpButton = std::make_shared<UltraCanvasButton>("tb-help", "Help");

toolbar->AddChild(newButton);
toolbar->AddChild(openButton);
toolbar->AddChild(saveButton);
toolbar->AddSpacer(15);           // a fixed 15 px gap
toolbar->AddStretchSpacer();      // takes all free width: Help ends up at the right edge
toolbar->AddChild(helpButton);

window->AddChild(toolbar);
```

The buttons are as wide as their text. The toolbar has no size of its own: in
the window's default block layout it takes the full width, and it is as tall as
its tallest button plus padding.

## A column: a sidebar

```cpp
auto sidebar = std::make_shared<UltraCanvasContainer>("sidebar", 220, 0);   // 220 px wide, auto height
sidebar->SetPadding(10);
sidebar->layout.SetFlexColumn()
               .SetFlexGap(8)
               .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);         // children as wide as the column

auto heading = std::make_shared<UltraCanvasLabel>("sb-heading", "Library");
heading->SetFontWeight(FontWeight::Bold);

auto search = std::make_shared<UltraCanvasTextInput>("sb-search");          // no width: the column gives it one
search->SetPlaceholder("Search");

auto importButton = std::make_shared<UltraCanvasButton>("sb-import", 120, 28, "Import...");

sidebar->AddChild(heading);
sidebar->AddChild(search);
sidebar->AddStretchSpacer();      // when the column is taller than its content, Import sits at the bottom
sidebar->AddChild(importButton);  // keeps its own 120 px: a set size beats the container's stretch
```

## A grid: tiles

A grid needs its columns; rows are added automatically as children are
auto-placed, row by row. A track is a `CSSLayout::GridTrackSize`: the default
one is `auto` (as big as its content), `Fr` shares the free space, `Fixed` is
pixels.

```cpp
const CSSLayout::GridTrackSize share{CSSLayout::GridTrackSizeKind::Fr, CSSLayout::Dimension::Fr(1)};
const CSSLayout::GridTrackSize tall{CSSLayout::GridTrackSizeKind::Fixed, CSSLayout::Dimension::Px(120)};

auto tiles = std::make_shared<UltraCanvasContainer>("tiles");
tiles->SetPadding(10);
tiles->layout.SetGrid()
             .SetGridColumns({share, share, share})        // three equal columns
             .SetGridRows({tall, tall})                    // two 120 px rows
             .SetGridGap(12)
             .SetGridJustifyItems(CSSLayout::JustifyItems::Stretch)   // fill the cell's width
             .SetGridAlignItems(CSSLayout::AlignItems::Stretch);      // and its height

for (int i = 0; i < 6; ++i) {
    auto tile = std::make_shared<UltraCanvasContainer>("tile" + std::to_string(i));
    tile->SetBackgroundColor(Color(245, 245, 250, 255));
    tiles->AddChild(tile);                                 // auto-placed: 3 per row
}
```

To put a child in a particular cell, or let it span cells, give it a place.
Row and column are zero-based; the spans default to 1. Placed children go in
first, and the auto-placed ones fill the free cells around them, adding `auto`
rows when the declared ones run out:

```cpp
auto banner = std::make_shared<UltraCanvasLabel>("banner", "Overview");
banner->layoutItem.SetGridRowColSimplified(0, 0, 1, 3);   // row 0, column 0, 1 row, 3 columns
tiles->AddChild(banner);
```

## Wrapping: cards that flow onto new lines

```cpp
auto gallery = std::make_shared<UltraCanvasContainer>("gallery");
gallery->SetPadding(15);
gallery->layout.SetFlexRow()
               .SetFlexWrap(CSSLayout::FlexWrap::Wrap)
               .SetFlexGap(15);                     // between cards and between lines

for (int i = 0; i < 8; ++i) {
    auto card = std::make_shared<UltraCanvasContainer>("card" + std::to_string(i), 0, 110);  // auto width, 110 px tall
    card->SetBackgroundColor(Colors::White);
    card->SetPadding(12);
    card->layout.SetFlexColumn().SetFlexGap(8);
    card->layoutItem.SetFlex(1, 1, CSSLayout::Dimension::Px(220));   // start at 220 px, share what is left
    card->AddChild(std::make_shared<UltraCanvasLabel>("card-title" + std::to_string(i),
                                                      "Card " + std::to_string(i + 1)));
    gallery->AddChild(card);
}
```

A line holds as many 220 px cards as fit; the cards on it then grow equally to
fill the line. Give the cards a fixed width instead (`("card", 220, 110)`) and
leave out `SetFlex` to keep them all the same size, then use
`SetFlexJustifyContent(CSSLayout::JustifyContent::SpaceAround)` to spread them.

## Gaps, padding and margins

```cpp
auto panel = std::make_shared<UltraCanvasContainer>("panel");
panel->SetPadding(10);                 // all four sides
panel->SetPadding(8, 16);              // vertical, horizontal
panel->SetPadding(4, 12, 8, 12);       // top, right, bottom, left
panel->SetMargin(0, 0, 10, 0);         // space outside the box, same argument orders
panel->layout.SetFlexRow().SetFlexWrap(CSSLayout::FlexWrap::Wrap)
             .SetFlexGap(6, 12);       // row gap (between lines), column gap (between items)
```

- A gap only goes *between* children; padding goes inside the container's edge.
- Margins add up: two stacked siblings with 10 px margins facing each other are
  20 px apart (there is no margin collapsing).
- `SetGridGap(row, column)` takes the same order as `SetFlexGap`.
- `AddSpacer(n)` puts a fixed gap at one spot; `AddStretchSpacer(grow)` absorbs
  free space.

## Alignment and stretching

Two directions matter in a flex container: the **main axis** (the direction
of the row or column) and the **cross axis** (across it).

- Main axis: `SetFlexJustifyContent(...)` places the children as a group
  (`Start`, `End`, `Center`, `SpaceBetween`, `SpaceAround`, `SpaceEvenly`), and
  `layoutItem.SetFlexGrow(n)` lets a child take free space.
- Cross axis: `SetFlexAlignItems(...)` for all children, `layoutItem.SetAlignSelf(...)`
  for one.

Nothing stretches unless the layout asks for it, and the most specific
statement wins:

1. the child's own `SetAlignSelf(CSSLayout::AlignSelf::Stretch)` — wins even
   over a size the child was given;
2. the child's own width or height — wins over the container's stretch;
3. the container's `SetFlexAlignItems(CSSLayout::AlignItems::Stretch)`;
4. the default, `AlignItems::Start`: each child at its own size, at the start.

```cpp
auto form = std::make_shared<UltraCanvasContainer>("settings", 320, 0);
form->layout.SetFlexColumn()
            .SetFlexGap(8)
            .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);    // the container asks for stretch

auto nameField = std::make_shared<UltraCanvasTextInput>("set-name");         // no width: stretched to 320
auto okButton = std::make_shared<UltraCanvasButton>("set-ok", 100, 30, "OK");   // own width: stays 100, at the left
auto applyButton = std::make_shared<UltraCanvasButton>("set-apply", 100, 30, "Apply to all");
applyButton->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch);        // own stretch: full width after all
auto resetButton = std::make_shared<UltraCanvasButton>("set-reset", "Reset");
resetButton->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Center);         // centred, at its text width

form->AddChild(nameField);
form->AddChild(okButton);
form->AddChild(applyButton);
form->AddChild(resetButton);
```

Centring something in a pane, both ways:

```cpp
auto pane = std::make_shared<UltraCanvasContainer>("pane");
pane->layout.SetFlexRow()
            .SetFlexJustifyContent(CSSLayout::JustifyContent::Center)
            .SetFlexAlignItems(CSSLayout::AlignItems::Center);
pane->AddChild(std::make_shared<UltraCanvasLabel>("empty-note", "Nothing selected"));
```

Alignment is *safe*: a child larger than the space it is centred in starts at
the leading edge and overflows at the far one, where a scrollbar can reach it.

In a grid, the same rules apply per cell with `SetGridJustifyItems` /
`SetGridAlignItems` on the container, and `layoutItem.SetJustifySelf(...)` /
`layoutItem.SetGridAlignSelf(...)` on a child:

```cpp
auto badge = std::make_shared<UltraCanvasLabel>("tile-badge", "New");
badge->layoutItem.SetGridRowColSimplified(1, 2);
badge->layoutItem.SetJustifySelf(CSSLayout::JustifySelf::End);
badge->layoutItem.SetGridAlignSelf(CSSLayout::AlignSelf::Start);   // top-right corner of its cell
```

## A form

Caption-and-field forms belong in **one** grid with an `auto` caption column
and a `1fr` field column: the caption column is then exactly as wide as the
widest caption, in any language, and every field starts at the same x.
`UltraCanvasFormLayout.h` builds that grid for you
([UltraCanvasFormLayout.md](UltraCanvasFormLayout.md)):

```cpp
auto accountForm = CreateFormGrid("account-form");          // [auto, 1fr], 8 px rows, 12 px columns
auto userName = std::make_shared<UltraCanvasTextInput>("acc-name");
auto userEmail = std::make_shared<UltraCanvasTextInput>("acc-email");
auto rememberMe = std::make_shared<UltraCanvasCheckbox>("acc-remember", "Remember me");

AddFormRow(accountForm, "acc-name-row", "Name:", userName);
AddFormRow(accountForm, "acc-email-row", "Email:", userEmail);
AddFormWideRow(accountForm, rememberMe);                    // across both columns

window->AddChild(accountForm);
```

The same grid by hand, as the demo page builds it — note that the inputs and
the button have no width, so the grid's stretch reaches them:

```cpp
auto contact = std::make_shared<UltraCanvasContainer>("contact");
contact->SetPadding(10);
contact->layout.SetGrid()
               .SetGridColumns({CSSLayout::GridTrackSize{},      // auto: the widest caption
                                CSSLayout::GridTrackSize{CSSLayout::GridTrackSizeKind::Fr,
                                                         CSSLayout::Dimension::Fr(1)}})
               .SetGridGap(10)
               .SetGridJustifyItems(CSSLayout::JustifyItems::Stretch)
               .SetGridAlignItems(CSSLayout::AlignItems::Center);

const char* captions[] = {"Name:", "Email:", "Phone:"};
for (int row = 0; row < 3; ++row) {
    auto caption = std::make_shared<UltraCanvasLabel>("ct-caption" + std::to_string(row), captions[row]);
    auto field = std::make_shared<UltraCanvasTextInput>("ct-field" + std::to_string(row));
    caption->layoutItem.SetGridRowColSimplified(row, 0);
    field->layoutItem.SetGridRowColSimplified(row, 1);
    contact->AddChild(caption);
    contact->AddChild(field);
}

auto submit = std::make_shared<UltraCanvasButton>("ct-submit", "Submit");
submit->layoutItem.SetGridRowColSimplified(3, 0, 1, 2);    // row 3, spanning both columns
contact->AddChild(submit);
```

## Nesting: an application shell

Real windows are boxes in boxes. Each container runs its own layout; a child
container is just another item to its parent.

```cpp
window->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

auto header = std::make_shared<UltraCanvasContainer>("header", 0, 40);     // auto width, 40 px tall
header->SetPadding(0, 10);
header->layout.SetFlexRow().SetFlexGap(8).SetFlexAlignItems(CSSLayout::AlignItems::Center);
header->AddChild(std::make_shared<UltraCanvasLabel>("app-title", "My App"));

auto body = std::make_shared<UltraCanvasContainer>("body");
body->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);
body->layoutItem.SetFlexGrow(1);                                           // all the height that is left

auto navigation = std::make_shared<UltraCanvasContainer>("navigation", 200, 0);
navigation->layoutItem.SetFlexShrink(0);                                   // never squeezed below 200 px
auto content = std::make_shared<UltraCanvasContainer>("content");
content->layoutItem.SetFlexGrow(1);                                        // all the width that is left
body->AddChild(navigation);
body->AddChild(content);

auto statusBar = std::make_shared<UltraCanvasLabel>("status", "Ready");
statusBar->SetPadding(4, 10);

window->AddChild(header);
window->AddChild(body);
window->AddChild(statusBar);
```

A box whose content can grow (a row of counters, a translated caption) should
not get a fixed width — a container clips what does not fit. Leave the width
automatic and set a minimum instead:

```cpp
auto counterTile = std::make_shared<UltraCanvasContainer>("counter-tile", 0, 0, 0, 0);
counterTile->size.height = CSSLayout::Dimension::Px(96);
CSSLayout::BoxConstraints limits;
limits.minWidth = CSSLayout::Dimension::Px(176);
counterTile->boxConstraints = limits;
counterTile->layout.SetFlexColumn().SetFlexAlignItems(CSSLayout::AlignItems::Center);
```

## Changing the layout at run time

```cpp
statusBar->SetVisible(false);           // leaves the layout entirely; neighbours close the gap
statusBar->SetVisible(true);

navigation->SetElementSize(CSSLayout::Dimension::Px(260), CSSLayout::Dimension::Auto());
content->layoutItem.SetFlexGrow(2);
content->InvalidateLayout();            // a direct layoutItem / size edit needs this
```

`SetVisible`, `SetElementSize`, `SetPadding`, `SetMargin` and `AddChild`
re-run the layout themselves. A direct edit of `layout`, `layoutItem`, `size`
or `boxConstraints` after the window has been shown needs `InvalidateLayout()`.

## Common mistakes

- **A non-zero x or y on a flex or grid child.** `(id, 10, 10, w, h)` pins the
  child at (10, 10) and takes it out of the flow; the container no longer
  places it. Use `(id, w, h)`, `(id, 0, 0, w, h)` or `(id)`.
- **Expecting stretch by default.** Since 2026-10-06 nothing is stretched unless
  the container asks for `AlignItems::Stretch` (grid: `JustifyItems::Stretch` /
  `AlignItems::Stretch`) or the child asks for `AlignSelf::Stretch`.
- **A size on a widget that should fill.** A width (in a column) or height (in a
  row) beats the container's stretch. Build the widget without that size, or
  give it `layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch)`. Run with
  `ULTRACANVAS_LAYOUT_AUDIT=1` to list every child whose size outranked a
  stretch.
- **Mixing flex and grid item settings.** `layoutItem` holds either flex-item or
  grid-item data. Calling `SetGridRowColSimplified` after `SetFlexGrow` (or the
  other way round) discards the first set and prints a warning. For a grid
  child use `SetGridAlignSelf`; `SetAlignSelf` only reaches the grid data when
  the child was placed in the grid first.
- **Moving widgets by hand.** `SetBounds`, `SetPosition` and `SetSize` write
  the engine's result, and the next layout pass overwrites them. Use
  `SetElementSize`, `SetElementAbsolutePosition`, or a container's
  `PlaceChildAt`.
- **A widget with no content size and no size given.** A chart, a canvas or a
  custom-drawn element that does not report a content size measures 0 x 0 and
  is never drawn. Give it a size, or let the layout size it (`SetFlexGrow(1)`
  plus a stretching container).
- **`AddSpacer(n)` in a short row.** The spacer is n x n, so in a row it is also
  n px tall and can make an auto-height row taller.
- **Changing children from inside an `Arrange()` override.** Report from
  `Arrange` and act on the next turn of the event loop
  (`UltraCanvasApplication::StartTimer(1, false, ...)`).
- **Expecting scrollbars.** Containers do not scroll by default. A real scroll
  view is `CreateScrollableContainer(...)` or a `ContainerStyle` with
  `autoShowScrollbars = true`.

## What replaced the old layout managers

Earlier versions of this page documented `UltraCanvasBoxLayout`,
`UltraCanvasGridLayout` and `UltraCanvasFlexLayout` (`CreateHBoxLayout`,
`CreateVBoxLayout`, `AddUIElement`, `LayoutAlignment`, `SizeMode`, the headers
`UltraCanvasBoxLayout.h` and friends). They no longer exist:

| Old | Now |
|---|---|
| HBox / VBox layout | `layout.SetFlexRow()` / `layout.SetFlexColumn()` |
| `SetSpacing(n)` | `SetFlexGap(n)` / `SetGridGap(n)` |
| `AddSpacing(n)` / `AddStretch(n)` | `AddSpacer(n)` / `AddStretchSpacer(n)` |
| grid rows/columns with Auto / Star / Fixed | `GridTrackSize` with `Auto` / `Fr` / `Fixed` |
| `AddUIElement` with a row and column | `AddChild(element)` plus `layoutItem.SetGridRowColSimplified(row, column)` |
| item alignment `Fill` | `AlignItems::Stretch` / `AlignSelf::Stretch` |

## See also

- [Docs/CSSLayout.md](../CSSLayout.md) — the layout engine reference
- [UltraCanvasFormLayout.md](UltraCanvasFormLayout.md) — form grids
- [UltraCanvasUIElements.md](UltraCanvasUIElements.md) — which element to use
- `Apps/DemoApp/UltraCanvasLayoutExamples.cpp` — the demo's Layout page
- `Apps/CSSLayoutTests` — the engine's behaviour, pinned by tests
