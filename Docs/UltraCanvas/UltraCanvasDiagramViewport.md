# UltraCanvasDiagramViewport

Shared pan / zoom / minimap / controls viewport for canvas-style diagram
elements.

- Header: `include/Plugins/Diagrams/UltraCanvasDiagramViewport.h`
- Source: `UltraCanvas/Plugins/Diagrams/UltraCanvasDiagramViewport.cpp`
- Tests: `Tests/DiagramViewportTest.cpp` (target `DiagramViewportTest`)
- Version: 1.0.0

This is **not** a UI element. It is a plain helper object that a diagram element
owns as a member and forwards to. It has no bounds of its own, no event loop and
no render pass — the host element decides when to call it.

Current consumers: `UltraCanvasNodeDiagram`, `UltraCanvasCompositorDiagram`.

---

## Why it exists

`UltraCanvasNodeDiagram` and `UltraCanvasCompositorDiagram` each carried their
own copy of the same machinery: zoom/pan state and clamping, screen↔world
transforms, zoom-at-cursor, `FitView`/`CenterOn`, a snap grid, a minimap panel
with a draggable viewport indicator, and a controls panel with zoom/fit/lock
buttons. The two copies had already begun to diverge — the compositor was
correct on coordinates, the node diagram was not (see *Coordinate convention*).

Everything listed above now lives here once.

---

## Coordinate convention

**Everything in this component is element-local.** Local space is
`(0, 0, width, height)`; the host element's absolute position on screen never
enters into it.

This matches how the framework actually dispatches input:

- containers translate pointer coordinates into the child's local space before
  calling `OnEvent` (`UltraCanvasContainer::HandleScrollbarEvents`), and
- `UltraCanvasUIElement::Contains()` tests against `GetLocalBounds()`, which is
  always `(0, 0, w, h)`.

> **Bug fixed by the extraction.** `UltraCanvasNodeDiagram::PointInMinimap()`,
> `FindControlButtonAt()` and `HandleMouseWheel()` added or subtracted
> `finalBounds.x/y` against coordinates that were already element-local. That
> double-counted the element origin, so minimap and controls hit-testing, and
> zoom-at-cursor, were offset by exactly the element's position whenever the
> diagram was not placed at `(0, 0)`. `UltraCanvasCompositorDiagram` had it
> right; the shared component adopts that convention for both.

The host must keep the viewport's idea of its size current:

```cpp
void MyDiagram::SyncViewportSize() {
    viewport.SetViewportSize(GetWidth(), GetHeight());
}
```

Call it at the top of `Render()` and before any size-dependent operation
(`FitView`, `ZoomAtPoint`, overlay hit-testing).

---

## Content bounds

The viewport never inspects host data structures. The host reports the
world-space extent of whatever it draws through `DiagramContentBounds`:

```cpp
DiagramContentBounds MyDiagram::ComputeContentBounds() const {
    DiagramContentBounds bounds;
    for (const auto& [id, node] : nodes) {
        bounds.Include(Rect2Dd(node.x, node.y, node.width, node.height));
    }
    return bounds;
}
```

`Include(x, y)` seeds both min and max on the first call, so a default-
constructed value is `!valid` rather than a spurious box at the origin.
`IsUsable()` additionally requires a non-zero area — `FitView` and the minimap
projection both reject anything that fails it instead of producing NaNs.

---

## Pan and zoom

```cpp
viewport.SetZoomLevel(1.5);           // clamped to [minZoom, maxZoom]
viewport.SetZoomRange(0.25, 4.0);     // re-clamps the current level immediately
viewport.ZoomIn();                     // default factor 1.2
viewport.ZoomOut(1.1);
viewport.SetPanOffset(x, y);
viewport.PanBy(dx, dy);                // for drag-to-pan
viewport.CenterOn(worldX, worldY);
```

`ZoomAtPoint(localCursor, factor)` is the wheel-zoom primitive: it keeps the
world point under the cursor pinned while the zoom changes, and returns `false`
when the zoom was already clamped and nothing moved (consume the event anyway).

```cpp
bool MyDiagram::HandleMouseWheel(const UCEvent& event) {
    SyncViewportSize();
    double factor = (event.wheelDelta > 0) ? 1.1 : (1.0 / 1.1);
    if (!viewport.ZoomAtPoint(Point2Di(event.pointer.x, event.pointer.y), factor)) {
        return true;   // at the limit; still handled
    }
    RequestRedraw();
    return true;
}
```

`FitView(content, padding)` fits and centres the given content, returning
`false` if the content or the available area is degenerate.

Transforms: `ScreenToWorld` (local pixels → world), `WorldToScreen` (world →
integer local pixels), `WorldToScreenD` (same, in doubles) and
`GetVisibleWorldRect()`.

---

## Rendering with the transform

The viewport does not push the transform for you — the host stays in control of
its own render order:

```cpp
void MyDiagram::Render(IRenderContext* ctx, const Rect2Df& dirtyRect) {
    SyncViewportSize();

    ctx->SetFillPaint(backgroundColor);
    ctx->FillRectangle(GetLocalBounds());

    ctx->PushState();
    Point2Dd pan = viewport.GetPanOffset();
    ctx->Translate(pan.x, pan.y);
    ctx->Scale(viewport.GetZoomLevel(), viewport.GetZoomLevel());

    RenderContentInWorldSpace(ctx);   // world coordinates below this point

    ctx->PopState();

    // Overlays are screen space and must come after PopState.
    if (viewport.IsMinimapVisible())  RenderMinimap(ctx);
    if (viewport.AreControlsVisible()) RenderControls(ctx);
}
```

To keep a stroke visually 1px regardless of zoom, divide by the zoom level:
`ctx->SetStrokeWidth(1.0 / viewport.GetZoomLevel())`.

---

## Minimap

`RenderMinimap` draws the panel and the viewport indicator, and calls back so
the host can plot its own items. The fill paint is pre-set to
`DiagramMinimapConfig::nodeColor` before the callback runs.

```cpp
void MyDiagram::RenderMinimap(IRenderContext* ctx) {
    viewport.RenderMinimap(ctx, ComputeContentBounds(),
        [&](const DiagramMinimapProjection& projection) {
            for (const auto& [id, node] : nodes) {
                Point2Dd tl = projection.WorldToMinimap(node.x, node.y);
                double w = std::max(1.0, node.width  * projection.scale);
                double h = std::max(1.0, node.height * projection.scale);
                ctx->FillRectangle(Rect2Dd(tl.x, tl.y, w, h));
            }
        });
}
```

`HandleMinimapDrag(localPos, content)` inverts exactly the same projection, so a
click always centres the view on the world point drawn under the cursor. Wire it
on both mouse-down (jump) and mouse-move (drag):

```cpp
if (viewport.PointInMinimap(mousePos) && viewport.MinimapConfig().pannable) {
    isDraggingMinimap = true;
    viewport.HandleMinimapDrag(mousePos, ComputeContentBounds());
    RequestRedraw();
    return true;
}
```

Configuration is `viewport.MinimapConfig()` (mutable reference) or
`SetMinimapConfig()`. Beyond the usual colours and size it exposes
`innerPadding` (gap between panel edge and projected content) and
`contentMargin` (world-space margin added around the content bounds).

---

## Controls overlay

Buttons are laid out vertically in the order zoom-in, zoom-out, fit, lock, but
only the groups enabled in `DiagramControlsConfig` are present. **Indices are
therefore not fixed** — never hard-code them. Resolve with
`GetControlButtonRole(index)`, or let `ApplyControlButton` do it:

```cpp
int btnIdx = viewport.FindControlButtonAt(mousePos);
if (btnIdx >= 0) {
    SyncViewportSize();
    DiagramControlButton role =
        viewport.ApplyControlButton(btnIdx, ComputeContentBounds());
    if (role == DiagramControlButton::ToggleLock) {
        isInteractive = !isInteractive;   // lock state belongs to the host
    }
    RequestRedraw();
    return true;
}
```

`ApplyControlButton` performs zoom-in / zoom-out / fit itself and returns the
role. It deliberately does **not** act on `ToggleLock`: what "locked" means is
the host element's business.

`RenderControls(ctx, hoveredButton, locked)` draws the panel and its glyphs.
Glyphs are vector primitives, not text — text glyphs at 14pt rendered too small
and off-centre inside a 28px square.

> The enumeration `DiagramControlButton` uses `NoAction`, not `None`, because
> X11's `Xlib.h` defines `None` as a macro and this header is reachable from app
> code that includes X11.

---

## Snap grid

```cpp
viewport.SetSnapToGrid(true);
viewport.SetSnapGrid(25.0, 25.0);
Point2Dd placed = viewport.SnapPoint(worldPoint);
```

`SnapPoint` rounds to the nearest intersection on both axes, symmetrically for
negative coordinates. A zero or negative spacing is ignored rather than dividing
by zero.

---

## Migration notes (existing consumers)

`UltraCanvasNodeDiagram` 2.1.0 and `UltraCanvasCompositorDiagram` keep their
public APIs. Their overlay types are now aliases:

| Element type | Now an alias of |
|---|---|
| `NodeDiagramPanelPosition` | `DiagramPanelPosition` |
| `NodeDiagramSnapGrid` | `DiagramSnapGrid` |
| `NodeDiagramMinimapConfig` / `CompositorMinimapConfig` | `DiagramMinimapConfig` |
| `NodeDiagramControlsConfig` / `CompositorControlsConfig` | `DiagramControlsConfig` |

Two behavioural notes:

1. **`UltraCanvasCompositorDiagram::GetPanOffset()` now returns `Point2Dd`**
   (was `Point2Df`). The shared viewport keeps pan/zoom in double precision;
   the compositor's own world coordinates remain `Point2Df`, converted in its
   `ScreenToWorld` / `WorldToScreen` wrappers.
2. The shared config structs carry the node diagram's light-theme defaults. The
   compositor's dark defaults are applied in its constructor through
   `MakeCompositorMinimapDefaults()` / `MakeCompositorControlsDefaults()`, so
   its appearance is unchanged.
