# Layout

UltraCanvas lays out its UI with a **CSS-style engine** (`UltraCanvas::CSSLayout`,
headers in `UltraCanvas/include/CSSLayout/`). Every element is a layout node;
there are no separate layout-manager objects. A container chooses *how* it
places its children through its `layout` member; each child describes
*itself* through `layoutItem`, its size, `boxConstraints`, margin and padding.
The engine measures and places everything once per frame - you never compute
coordinates.

In depth: [`Docs/CSSLayout.md`](../CSSLayout.md) (the element contract, the
box model, stretch rules, tables, troubleshooting) and
[`UltraCanvasLayoutExamples.md`](../UltraCanvas/UltraCanvasLayoutExamples.md)
(worked examples, live on the demo's *Layout* page).

## The pieces

| You want | Write |
|---|---|
| children side by side | `container->layout.SetFlexRow()` |
| children stacked | `container->layout.SetFlexColumn()` (or the default block layout) |
| rows and columns that line up | `container->layout.SetGrid().SetGridColumns({...})` |
| children that wrap onto new lines | `.SetFlexWrap(CSSLayout::FlexWrap::Wrap)` |
| space between children | `.SetFlexGap(px)` / `.SetGridGap(px)` |
| space inside / outside a box | `SetPadding(...)` / `SetMargin(...)` |
| a child that takes the free space | `child->layoutItem.SetFlexGrow(1)` |
| push the next children to the far end | `container->AddStretchSpacer()` |
| a fixed gap at one spot | `container->AddSpacer(px)` |
| children filled across the container | `.SetFlexAlignItems(CSSLayout::AlignItems::Stretch)` |
| one child stretched | `child->layoutItem.SetAlignSelf(CSSLayout::AlignSelf::Stretch)` |
| a box that never gets narrower than N | `boxConstraints` with `minWidth` |
| hide a child and close the gap | `child->SetVisible(false)` |
| a child in a given grid cell | `child->layoutItem.SetGridRowColSimplified(row, col, rowSpan, colSpan)` |
| a form whose captions line up | [`UltraCanvasFormLayout`](../UltraCanvas/UltraCanvasFormLayout.md) |
| a scrolling area | `CreateScrollableContainer(...)` or `ContainerStyle::autoShowScrollbars` |

## Who sizes an element

The constructor decides (see also [Core concepts](Core-Concepts.md#creating-elements-the-three-constructor-shapes)):

| Constructor | Result |
|---|---|
| `(id, x, y, w, h)` with a non-zero x or y | pinned at (x, y), out of the flow (`PositionType::AbsoluteUI`) |
| `(id, w, h)` or `(id, 0, 0, w, h)` | in the flow, fixed size - the parent places it |
| `(id)` or `(id, text)` | in the flow, sized by its content (and by the parent's stretch, if asked) |

A set width (in a column) or height (in a row) beats the container's
stretch. To move or resize an element after it is built, use
`SetElementSize` / `SetElementAbsolutePosition` (or a container's
`PlaceChildAt`), not `SetBounds` / `SetPosition` / `SetSize`, which write the
engine's result and are overwritten by the next pass.

## A typical application shell

```cpp
// Header row, then a body with a sidebar and a content area that fills the rest.
window->layout.SetFlexColumn();

auto header = std::make_shared<UltraCanvasContainer>("Header");
header->SetPadding(6);
header->layout.SetFlexRow().SetFlexGap(6).SetFlexAlignItems(CSSLayout::AlignItems::Center);
header->AddChild(std::make_shared<UltraCanvasButton>("New",  Tr("New")));
header->AddChild(std::make_shared<UltraCanvasButton>("Open", Tr("Open")));
header->AddStretchSpacer();                                    // Help goes to the right edge
header->AddChild(std::make_shared<UltraCanvasButton>("Help", Tr("Help")));

auto body = std::make_shared<UltraCanvasContainer>("Body");
body->layoutItem.SetFlexGrow(1);                               // the body takes the remaining height
body->layout.SetFlexRow().SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

auto sidebar = std::make_shared<UltraCanvasContainer>("Sidebar", 220, 0);   // 220 px wide
sidebar->SetPadding(10);
sidebar->layout.SetFlexColumn().SetFlexGap(8);

auto content = std::make_shared<UltraCanvasContainer>("Content");
content->layoutItem.SetFlexGrow(1);                            // fills the width beside the sidebar

body->AddChild(sidebar);
body->AddChild(content);
window->AddChild(header);
window->AddChild(body);
```

<!-- doc-check: std::shared_ptr<UltraCanvasWindow> window; -->

## A grid

```cpp
const CSSLayout::GridTrackSize share{CSSLayout::GridTrackSizeKind::Fr, CSSLayout::Dimension::Fr(1)};

auto tiles = std::make_shared<UltraCanvasContainer>("Tiles");
tiles->layout.SetGrid()
             .SetGridColumns({share, share, share})            // three equal columns
             .SetGridGap(12)
             .SetGridJustifyItems(CSSLayout::JustifyItems::Stretch)
             .SetGridAlignItems(CSSLayout::AlignItems::Stretch);
```

Children are auto-placed row by row; `SetGridRowColSimplified` puts one in a
given cell or span.

## Rules worth remembering

- **Nothing stretches unless asked**: `AlignItems::Stretch` on the container,
  or `AlignSelf::Stretch` on the child.
- **Margins add up** (no margin collapsing); gaps go only *between* children.
- **Containers do not scroll by default.**
- `layoutItem` holds either flex-item or grid-item data - do not mix the two
  on one child.
- An element that draws itself must report a content size
  (`MeasureOwnContent`) or be given one, or it measures 0 x 0 and is never
  drawn.
- Do not change the element tree from inside an `Arrange()` override; defer
  the change with `StartTimer(1, false, ...)`.
- `ULTRACANVAS_LAYOUT_AUDIT=1` lists every child whose fixed size outranked a
  stretch. The demo app's `--audit-layout` (and `--pseudo-loc`, every text 40 %
  longer) fails on a label or button narrower than its text.

## Layout helpers

| Helper | Use |
|---|---|
| [`UltraCanvasSpacer`](Elements/UltraCanvasSpacer.md) | a fixed or stretching gap |
| [`UltraCanvasSeparator`](Elements/UltraCanvasSeparator.md) | a line between groups |
| `UltraCanvasFormLayout` | caption / field rows that align and translate |
| [`UltraCanvasSplitPane`](Elements/UltraCanvasSplitPane.md) | resizable panes |
| [`UltraCanvasTabbedContainer`](Elements/UltraCanvasTabbedContainer.md) | tabs |
| [`UltraCanvasToolbar`](Elements/UltraCanvasToolbar.md) | a bar that grows to fit or overflows into a menu |
| `UltraCanvasMenuLayout`, `UltraCanvasToolbarLayout` | user-configurable menus and toolbars |
| `UltraCanvasLabelPlacement` | labels for charts and diagrams that do not overlap |

All layout elements: [Layout and structure](Groups/Layout-and-structure.md).

Next: [Events and callbacks](Events-and-Callbacks.md)
