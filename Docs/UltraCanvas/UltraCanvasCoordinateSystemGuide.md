# UltraCanvas Coordinate System & Positioning Guide

A practical guide to how positions work in UltraCanvas, so your custom
elements draw and respond to input in the right place.

> **TL;DR**
> - Inside `Render()` and `OnEvent()`, everything is **element‑local**: the
>   origin `(0,0)` is your element's own top‑left.
> - **Never** use `GetBounds()` (or `GetX()`/`GetY()`) to draw or to hit‑test.
>   Those return *parent‑relative* coordinates and will double‑offset you.
> - To place an element, drive the **layout** (flex / `layoutItem`), or use
>   `SetElementAbsolutePosition()` — not raw `SetBounds()` in the middle of a
>   laid‑out tree.

---

## 1. The three coordinate spaces

Every element lives in a tree. A point can be expressed in three frames:

| Frame | Origin `(0,0)` is… | Who uses it |
|-------|--------------------|-------------|
| **Local** | the element's own top‑left | `Render()`, `OnEvent()`, all your drawing/hit‑testing |
| **Parent** | the parent container's top‑left | `finalBounds` / `GetBounds()` — layout output |
| **Window** | the window's top‑left | input plumbing, popups, global positioning |

`finalBounds` (returned by `GetBounds()`) is your element's box **in its
parent's frame**. It is the *result* of layout, not something you read while
drawing.

### The rendering contract

Before the framework calls your `Render(ctx, dirtyRect)`, the parent container
has **already translated the render context to your element's origin**:

```cpp
// UltraCanvasContainer::Render — simplified
ctx->PushState();
ctx->Translate(adjustedChildBounds.TopLeft());   // origin is now the child's top-left
child->Render(ctx, childDirty);                  // childDirty is in LOCAL space too
ctx->PopState();
```

So inside your `Render()`, drawing at `(0,0)` paints your top‑left corner. The
`dirtyRect` you receive is already in local coordinates as well.

### The event contract

Before the framework calls your `OnEvent(event)`, it maps the pointer into your
local frame:

```cpp
// UltraCanvasApplication — simplified
event.pointer = elem->MapToLocal(event.pointerWindow, nullptr);
elem->OnEvent(event);
```

So `event.pointer` is **already element‑local**. `event.pointer == (0,0)` means
the cursor is at your top‑left. (If you ever need window or screen coordinates,
they are still on the event as `event.pointerWindow` and `event.pointerGlobal`.)

---

## 2. Drawing: do it in local space

The base class shows the canonical pattern — it draws its background/border
using `GetLocalBounds()`:

```cpp
void UltraCanvasUIElement::Render(IRenderContext* ctx, const Rect2Df&) {
    auto bnds = GetLocalBounds();        // (0, 0, width, height)
    // …draws background + borders within bnds…
}
```

Your override should follow suit:

```cpp
void MyElement::Render(IRenderContext* ctx, const Rect2Df& dirty) {
    // GOOD — local frame, origin at the element's top-left.
    const Rect2Di b(0, 0, (int)GetWidth(), (int)GetHeight());

    ctx->PushState();
    ctx->SetFillPaint(background);
    ctx->FillRectangle(b);

    // Sub-regions are offsets from the local origin, never from GetBounds().
    Rect2Di header(0, 0, b.width, 32);
    Rect2Di body  (0, 32, b.width, b.height - 32);
    // …draw…
    ctx->PopState();
}
```

Helpers that return **local** rects (use these in `Render`):

| Getter | Returns (local frame) |
|--------|-----------------------|
| `GetLocalBounds()` | `(0, 0, width, height)` |
| `GetLocalContentRect()` | local box minus border + padding |

Helpers that return **parent‑frame** rects (do **not** use them to draw):

| Getter | Returns (parent frame) |
|--------|------------------------|
| `GetBounds()` | `finalBounds` = `(x, y, width, height)` |
| `GetContentRect()` | parent‑frame box minus border + padding |
| `GetPaddingRect()`, `GetMarginRect()` | parent‑frame boxes |

If you need padding/border insets while drawing, take them from the **local**
content rect or compute them directly (`GetBorderLeftWidth() + GetPaddingLeft()`),
not from the parent‑frame `GetContentRect()`.

---

## 3. Hit‑testing & input: also local space

```cpp
bool MyElement::OnEvent(const UCEvent& e) {
    switch (e.type) {
        case UCEventType::MouseDown: {
            // GOOD — event.pointer is already local; compare against local rects.
            if (Rect2Di(0, 0, 40, (int)GetHeight()).Contains(e.pointer)) {
                /* clicked the left gutter */
            }
            break;
        }
        case UCEventType::MouseWheel: {
            const bool inSidebar = e.pointer.x < sidebarWidth;  // local x
            break;
        }
    }
    return UltraCanvasUIElement::OnEvent(e);
}
```

Build geometry helpers that return **local** rects and reuse them from both
`Render()` and `OnEvent()` so the two can never drift apart:

```cpp
Rect2Di MyElement::SidebarArea() const {
    return Rect2Di(0, 0, sidebarWidth_, (int)GetHeight());   // local
}
```

Drag deltas (`pointer - anchor`) are frame‑independent, so panning logic that
only uses differences is safe in any frame.

---

## 4. Setting an element's position & size

Prefer, in order:

1. **Layout (best).** Let the CSS/flex engine place the element. Configure it
   through `layout` (container) and `layoutItem` (the element within its
   parent):

   ```cpp
   container->layout.SetFlexColumn().SetFlexGap(8)
            .SetFlexAlignItems(CSSLayout::AlignItems::Stretch);

   view->layoutItem.SetFlexGrow(1).SetFlexShrink(1)
        .SetAlignSelf(CSSLayout::AlignSelf::Stretch);
   ```

2. **Absolute, layout‑aware placement.** When you genuinely need to pin an
   element, use the layout‑integrated setters so the engine stays consistent:

   ```cpp
   el->SetElementAbsolutePosition({x, y});
   el->SetElementSize({w, h});
   ```

3. **`SetBounds()` — last resort.** It writes `finalBounds` directly. Fine for
   the initial size of a free‑floating/top‑level element or in tests, but inside
   a laid‑out tree the next layout pass can overwrite it. The header even warns:
   *"if possible don't use SetPosition/SetSize as it may break layout, use
   SetElementSize/SetElementAbsolutePosition."*

After changing anything that affects geometry, the box model setters already
call `InvalidateLayout()` for you; if you mutate layout fields directly, call it
yourself.

---

## 5. Converting between frames (when you actually need to)

Use these only at boundaries (popups, drag‑and‑drop across containers, custom
global hit‑tests) — not in normal `Render`/`OnEvent`:

| Call | Meaning |
|------|---------|
| `GetPositionInWindow()` | this element's top‑left in **window** coordinates |
| `GetBoundsInWindow()` | `GetBounds()` repositioned into the **window** frame |
| `MapToLocal(windowPt)` | window point → this element's **local** frame |
| `MapFromLocal(localPt)` | this element's **local** point → **window** frame |

```cpp
// A child wants to know where a local point lands on screen:
Point2Df onScreen = MapFromLocal({0, 0});   // my top-left, in window coords
```

---

## 6. The "Don't do" list

These are the patterns that bite. Each one compiles and "looks" right.

| ❌ Don't | ✅ Do instead | Why |
|---------|--------------|-----|
| `auto b = GetBounds(); ctx->FillRectangle(b);` in `Render()` | `Rect2Di b(0,0,(int)GetWidth(),(int)GetHeight());` | `GetBounds()` is parent‑relative; the ctx is **already** translated, so you double‑offset by `(finalBounds.x, finalBounds.y)`. The element drifts down‑right and clips wrong. |
| `event.pointer.x - GetX()` / `- GetY()` for "local" coords | use `event.pointer` directly | `event.pointer` is already local. Subtracting the parent‑relative `GetX()` shifts hit‑testing by the parent offset. |
| Hit‑test against `GetBounds()`‑based rects in `OnEvent()` | hit‑test against **local** rects (`0,0,w,h`) | Same parent‑offset bug, now in input. Clicks land in the wrong sub‑region. |
| Use `GetContentRect()` (parent frame) to inset drawing | use `GetLocalContentRect()` or `GetBorderLeftWidth()+GetPaddingLeft()` | `GetContentRect()` carries the parent‑frame `x/y`; mixing it into local drawing offsets you. |
| Assume `DrawText(text, pos)` positions by the **baseline** | `pos` is the text's **top‑left**; center with `CalculateCenteredTextPosition(text, rect)` or measure via `GetTextLineDimensions()` | Anchoring at the baseline pushes text low/out of its background pill. |
| Re‑add the parent's content origin (`ca.x/ca.y`) when placing children | the engine already places children at `border+padding+pos` | Double‑counts the parent's border/padding; spurious 1px scrollbars and shifts. |
| `SetBounds()` to position an element inside a flex/CSS container | drive `layout`/`layoutItem`, or `SetElementAbsolutePosition()` | The next layout pass overwrites raw `finalBounds`; your position "randomly" resets. |
| Hard‑code an element's screen position with magic numbers | let layout compute it, or convert via `MapFromLocal` | Breaks under resize, DPI, scrolling, and nesting. |
| Read `GetBounds().x/y` to figure out where to draw an overlay/badge | position it relative to your **local** content area | Overlay separates from the content it annotates (classic "badge text vs. background" mismatch). |
| Call expensive layout mutations from inside `Render()` | mutate state and `RequestRedraw()`; let layout settle next frame | Re‑entrant layout during paint causes flicker or feedback loops. (Firing a *value‑changed callback only when the value actually changes* is fine — it settles in one frame.) |

---

## 7. Worked example: the PDF viewer bug

`UltraCanvasPDFView` originally based its whole render and hit‑testing on
`GetBounds()`:

```cpp
// BEFORE — parent-relative, double-offset
void Render(IRenderContext* ctx, const Rect2Df&) {
    const Rect2Di b = GetBounds();        // e.g. (12, 94, W, H) inside its parent
    ctx->FillRectangle(b);                // …but ctx is already translated by (12, 94)
    // → the viewer paints at (24, 188): a white gap under the toolbar,
    //   the page badge slides off to the right, clipping is wrong.
}

case UCEventType::MouseWheel: {
    Point2Di local(event.pointer.x - GetX(), event.pointer.y - GetY()); // wrong
    bool inThumbs = local.x < thumbStripWidth;
}
```

The fix was to move everything to the local frame:

```cpp
// AFTER — local frame, single source of truth
void Render(IRenderContext* ctx, const Rect2Df&) {
    const Rect2Di b(0, 0, (int)GetWidth(), (int)GetHeight());
    ctx->FillRectangle(b);
}

Rect2Di PageContentArea() const {                 // local
    const int left = showThumbs_ ? thumbStripWidth : 0;
    return Rect2Di(left, 0, (int)GetWidth() - left, (int)GetHeight());
}

case UCEventType::MouseWheel: {
    bool inThumbs = event.pointer.x < thumbStripWidth;  // pointer is already local
}
```

Result: the viewer sits flush under the toolbar, the page‑number badge lines up
with its background, and thumbnail clicks hit the right page.

---

## 8. Checklist for a new custom element

- [ ] `Render()` draws from `(0,0)` using `GetLocalBounds()` / local rects.
- [ ] No `GetBounds()`, `GetX()`, `GetY()`, or `GetContentRect()` in `Render()`.
- [ ] `OnEvent()` treats `event.pointer` as local; hit‑tests against local rects.
- [ ] Geometry helpers return **local** rects and are shared by render + events.
- [ ] Text is positioned by its top‑left (center via `CalculateCenteredTextPosition`).
- [ ] Position/size set through layout or `SetElementAbsolutePosition` /
      `SetElementSize`, not raw `SetBounds()` inside a laid‑out tree.
- [ ] Frame conversions (`MapToLocal`/`MapFromLocal`/`…InWindow`) only at
      boundaries, never in the hot draw/input path.
</content>
