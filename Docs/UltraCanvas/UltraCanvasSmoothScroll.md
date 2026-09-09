# UltraCanvasSmoothScroll — framework-wide smooth scrolling and zoom

`UltraCanvasSmoothScroll.h` / `core/UltraCanvasSmoothScroll.cpp`

Scrolling and wheel zoom **glide to their target instead of jumping**, in every
UltraCanvas application, without the application opting in. A wheel notch, a
page step, an arrow-key reveal or a zoom step is eased over ~150 ms with an
ease-out cubic — quick off the mark so the view answers the input at once,
gentle onto the target so it does not appear to stop dead.

Nothing about the *amount* scrolled changed: only how the view travels there.

---

## Application-wide defaults

Smooth scrolling is **on by default**. An application that wants the old instant
jumps, or a different feel, sets it once at start-up:

```cpp
#include "UltraCanvasSmoothScroll.h"

UltraCanvas::SetSmoothScrollingEnabled(false);   // instant everywhere
UltraCanvas::SetSmoothScrollDuration(220);       // a slower glide (ms)
```

| Function | Meaning |
|---|---|
| `SetSmoothScrollingEnabled(bool)` / `IsSmoothScrollingEnabled()` | Master switch. Off means every animated scroll and zoom lands immediately. |
| `SetSmoothScrollDuration(int ms)` / `GetSmoothScrollDuration()` | Length of one animated step. `<= 0` disables the animation as surely as the switch. |

Both are read **at the moment a scroll starts**, so a change takes effect on the
next wheel notch — there is nothing to re-apply to existing elements.

`ScrollbarStyle::smoothScrolling` and `ScrollbarStyle::smoothScrollDuration`
(see [UltraCanvasScrollbarExamples](UltraCanvasScrollbarExamples.md)) initialise from these same
values, which is what keeps the scrollbar-backed elements and the self-rendered
views in step. A style can still override either for one bar.

---

## What glides

Every element that scrolls through a real `UltraCanvasScrollbar` child —
containers, list and tree views, dropdowns, menus with a scrollbar — was already
animated by the scrollbar itself. The change is that the **self-rendered views**,
which keep their scroll position in plain members, now glide too:

| Element | What glides |
|---|---|
| `UltraCanvasFilerWidget` | Wheel; keyboard navigation revealing an entry (`EnsureVisible`) |
| `UltraCanvasAlbum` | Wheel (vertical, and horizontal in Filmstrip) |
| `UltraCanvasTextArea` | Wheel and the `ScrollUp/Down/Left/Right` API the page keys go through |
| `UltraCanvasTreeView` | Wheel (through the scrollbar where one is up, `ScrollBy` where not) |
| `UltraCanvasMenu` | Wheel over a scrolling item list |
| `UltraCanvasCalendarView` (date picker) | Wheel and the scroll track's page steps, in `Scrolling` nav mode |
| `UltraCanvasMarkdownDisplay` | Wheel (`ScrollBy`) |
| `UltraCanvasPDFView` | Wheel scrolling of the page and of the thumbnail strip |
| `UltraCanvasGanttChartElement` | Wheel (timeline, both axes) |
| `UltraCanvasKanbanBoardElement` | Wheel (board sideways, and each column on its own) |
| `UltraCanvasGitGraph` | Diff pane file list and diff text |

Wheel **zoom** eases in as well — the zoom factor is spread over the same
duration while the point under the cursor stays put:

| Element | |
|---|---|
| `UltraCanvasDiagramViewport` | The shared pan/zoom viewport: MindMap, NodeDiagram, CompositorDiagram |
| Class, ER, Flow, Sequence, Requirement, Pert diagrams; GitGraph; GourceTree; Dendrogram | Each element's own zoom-about-cursor |
| `UltraCanvasZoomPanImage` (image viewer), `UltraCanvasMediaSurface` (media viewer) | |
| `UltraCanvasVectorElement` | |
| `UltraCanvasContourChartElement`, `UltraCanvasTimelineChart` | |
| 3D charts (`ScatterPlot3D`, `ContourSurface3D`, `ContourSurfaceGL`) and `UltraCanvasSTLElement` | The camera dolly |

### What deliberately does not glide

- **Dragging a scrollbar thumb, or panning with the mouse.** The view has to
  track the pointer exactly; a glide chasing the cursor would trail the thing
  being dragged. Pressing a scrollbar also drops the hover state, since the
  pointer is parked on the bar rather than on content.
- **Keeping the text caret on screen.** `UltraCanvasTextArea::EnsureCursorVisible`
  runs on every keystroke and must be true the instant the key is handled, so it
  positions the view directly and cancels any glide the wheel started. Explicit
  scrolling (wheel, page keys) still glides.
- **Positioning, as opposed to scrolling.** Opening a folder, switching view
  type, `ScrollTo(line)`, a Go-to-line, fitting a diagram to its content, putting
  the viewport back on its anchor entry after a resize: these set where the view
  *is* rather than move it, so they land at once.
- **PDF wheel zoom.** Every intermediate scale would invalidate the rasterised
  page cache and re-render the page, so easing it would cost roughly nine full
  page rasterisations per notch. PDF *scrolling* glides; its zoom steps.
- **Value steppers.** The wheel over a spinner, a time picker or a paged
  calendar changes a value or flips a page — it is not scrolling.

### Not converted

Four views scroll by **whole rows or items** rather than by pixels — their scroll
position *is* a row index, and the renderer draws starting from that row. Easing
an index only spreads the same row-by-row steps over 150 ms, which reads worse
than the jump. Making them genuinely smooth means first changing the scroll
position to a pixel offset and teaching the paint and hit-test paths a sub-row
remainder, which is a change to each view rather than an adoption of this
animator:

- `UltraCanvasSpreadsheet` — the scroll position is a cell coordinate on the
  sheet model (`SetScrollPosition(row, col)`), read by the grid painter, the
  header painters, hit-testing and the scrollbar fractions.
- `UltraCanvasTextArea` in Hex editing mode (`hexFirstVisibleRow`).
- The `UltraCanvasFileDialog` file list and the `UltraCanvasNewDocumentDialog`
  document list (item indices, also used for click hit-testing).

---

## Using it in a new element

An element that keeps its own scroll offset binds one animator per axis. The
writer is the single place the offset is stored, so an eased step goes through
exactly the clamp and repaint an instant scroll would:

```cpp
// member
UltraCanvasSmoothScroll scrollAnimY;

// once (constructor)
scrollAnimY.Bind([this] { return static_cast<double>(scrollOffsetY); },
                 [this](double v) {
                     scrollOffsetY = static_cast<int>(std::lround(v));
                     ClampScroll();
                     RequestRedraw();
                 });

// wheel / page key
scrollAnimY.AnimateBy(-event.wheelDelta * kWheelStep, 0, MaxScrollY());
```

| Call | |
|---|---|
| `AnimateBy(delta, lo, hi)` / `AnimateTo(value, lo, hi)` | Glide. Consecutive calls **chain** onto the pending target, so a fast wheel spin is one long glide rather than a stutter. |
| `PendingValue()` | Where an in-flight glide is heading (the current value when idle). Ask this rather than reading the member when deciding "is the target already reached?". |
| `Jump(value, lo, hi)` | Land now, dropping any glide. |
| `Cancel()` | Drop a glide and leave the value where it got to — **call this before writing the bound member directly**, or the animation will overwrite it. |
| `SetDuration(ms)` | Per-instance override; `< 0` (default) follows the application-wide duration. |

The animator holds a repeating ~60 Hz timer only while a glide is running, and
stops it on landing and in its destructor.

## Using it for wheel zoom

Zooming about the cursor is multiplicative, and applying two factors in a row
about the same point is exactly the same as applying their product once — the pan
solve holds that point fixed after each application. `UltraCanvasSmoothZoom` uses
that: it eases in log space and hands the element a run of small **incremental
factors**, which the element applies with the very code it used for the single
one. No transform maths is duplicated or reinterpreted:

```cpp
// members
UltraCanvasSmoothZoom zoomAnim;
Point2Di zoomCursor;

// wheel handler
if (!zoomAnim.IsBound()) {
    zoomAnim.Bind([this](double f) { ApplyZoomFactorAtCursor(f, zoomCursor); },
                  [this] { RequestRedraw(); });
}
zoomCursor = event.pointer;
zoomAnim.ZoomBy(event.wheelDelta > 0 ? 1.1 : 0.9, zoomLevel, minZoom, maxZoom);
```

The element keeps clamping its own zoom; the range passed to `ZoomBy` only stops
notches spun against a limit from piling up into a factor that would take a
visible moment to unwind.

For a zoom that steps **additively** rather than multiplicatively (as
`UltraCanvasVectorElement` does), use the plain `UltraCanvasSmoothScroll` on the
zoom level and re-solve the pan from a document anchor captured once at the start
of the gesture.

---

## See also

- [UltraCanvasScrollbarExamples](UltraCanvasScrollbarExamples.md) — the scrollbar
  control, which animates its own position and now takes its defaults from here.
- [UltraCanvasFilerWidget](UltraCanvasFilerWidget.md) — its hover icon menu and
  tooltip re-derive on every eased step, so they follow the file that is under
  the cursor as the folder glides past.
