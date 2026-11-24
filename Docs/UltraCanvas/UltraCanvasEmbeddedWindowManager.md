# UltraCanvasEmbeddedWindowManager — a window manager inside the program

<!-- doc-check: cairo_surface_t* output; cairo_surface_t* SurfaceOf(uint64_t id); float scale; void ResizeWindow(uint64_t id, int width, int height); void DeliverMouseMove(uint64_t id, UltraCanvas::Point2Di local); -->

Where a platform gives a program one surface and no window manager - an
Android app drawing on a TV through a `Presentation`, a web page's canvas -
the windows of that program still need stacking, focus, title bars, moving,
resizing and a pointer. `UltraCanvasEmbeddedWindowManager` is that window
manager: it keeps the windows of one output and composites their surfaces
into it.

```cpp
#include "UltraCanvasEmbeddedWindowManager.h"
using namespace UltraCanvas;

UltraCanvasEmbeddedWindowManager wm;
wm.SetOutputSize(1920, 1080);                       // logical px

EmbeddedWindowSpec desktop;
desktop.layer = EmbeddedWindowLayer::Desktop;
desktop.fillsOutput = true;                         // always the whole output
wm.AddWindow(1, desktop);
wm.Show(1);

EmbeddedWindowSpec editor;
editor.rect = Rect2Di(-1, -1, 900, 640);            // -1: the manager places it
editor.title = "Texter";
wm.AddWindow(2, editor);
wm.Show(2);                                         // shown, raised, focused

wm.onGeometryChanged = [](uint64_t id, const Rect2Di& r) { ResizeWindow(id, r.width, r.height); };
wm.Compose(output, scale, [](uint64_t id) { return SurfaceOf(id); }, wm.TakeDamage());
```

It is the Android backend's desktop mode underneath
([`OS/Android/README.md`](../../UltraCanvas/OS/Android/README.md#desktop-mode-a-tv-or-monitor),
`UltraCanvasAndroidDesktop`), which MobilePC uses to put UltraDesktop on a TV.
It knows nothing of the platform and nothing of elements: windows are ids, a
rectangle each, and a cairo image surface the caller hands it when it
composites. `Tests/EmbeddedWindowManagerTest` exercises all of it with no
display.

## Contents

- [What it keeps](#what-it-keeps)
- [Bands](#bands)
- [Title bars](#title-bars)
- [The pointer](#the-pointer)
- [Compositing](#compositing)
- [API](#api)
- [Related](#related)

## What it keeps

Every rectangle is the window's **content** in the output's logical pixels.

- **Placement.** A window whose `rect` has `x` or `y` below 0 is placed: a
  dialog (`transientFor`) centred over its window, anything else cascaded from
  the work area's top left - centred when it is too big to step. Nothing is
  larger than the work area, and part of every window stays on the output.
- **The work area** is the output less the strips windows reserve with
  `ReserveEdges` - a desktop's bars. Maximize fills it (title bar included).
- **Focus.** Showing a window focuses it; a press in a window raises and
  focuses it; minimizing, hiding or removing the focused window passes the
  focus to the window a dialog belongs to, else the topmost one.
  `onFocusChanged` reports each change. A frame, a popup, a tooltip and a
  notification never take it (`focusable` false).
- **State.** `Minimize`, `Maximize`, `Restore`, `ToggleMaximize`; a dialog is
  raised with its window. `onStateChanged` reports a change the manager made.

## Bands

The stack is ordered by band, bottom first, and `Raise` moves a window to the
top of its own band only - a click on the desktop never brings it above the
windows, a dialog never covers a menu:

| `EmbeddedWindowLayer` | For |
|---|---|
| `Desktop` | the desktop's window (wallpaper, bars) |
| `Normal` | application windows and their dialogs |
| `AlwaysOnTop` | a recording bar, a floating palette |
| `Notification` | toasts |
| `Popup` | a menu's or a list's own window |
| `Tooltip` | tooltips |
| `Lock` | a lock screen, over everything |

`ListApplicationWindows()` is what a taskbar lists: the focusable windows of
the `Normal` and `AlwaysOnTop` bands, without frames.

## Title bars

A decorated window gets its title bar as a **window of its own**, attached with
`AttachFrame(clientId, frameId)`: the manager keeps it just above the window,
as wide as it, and moves, raises, shows, hides and minimizes it with it. The
backend draws it like any window; the framework's is
`UltraCanvasEmbeddedWindowFrame` - an `UltraCanvasLabel` and three
`UltraCanvasButton`s in a borderless window, so it behaves and looks like
every other control (hover, theme, scale). A press on its title calls
`BeginMove(clientId)`; a double click there maximizes or restores.

Resizing needs no window: the edges and corners just **outside** a decorated,
resizable, unmaximized window take the pointer (`resizeGrip`, 6 px), with the
resize pointer shapes.

## The pointer

The pointer is the manager's, drawn by `Compose`. A backend feeds it either
absolute positions (`PointerMove`, `PointerButton`) or relative motion
(`MovePointerBy`, from a touchpad or a captured mouse). Each call returns an
`EmbeddedPointerRoute`: the window to deliver the event to, the point in that
window's own coordinates, and the window the pointer just left (to send it a
`MouseLeave`). A button pressed in a window keeps the pointer there until the
last button is released, as on a desktop. During a move or a resize the
manager uses the events itself and routes nothing.

```cpp
EmbeddedPointerRoute route = wm.PointerMove(Point2Di(640, 400));
if (route.windowId) DeliverMouseMove(route.windowId, route.local);
```

The shape is the window's own choice (`SetWindowCursor`, which a backend calls
from `SelectMouseCursorNative`) over its content, and the manager's over an
edge it resizes by. The shapes are drawn as vectors (`DrawPointer`), white
with a dark outline, at the output's scale.

## Compositing

Every change adds **damage** - a window presented (`WindowPresented`), moved,
raised, shown or hidden, the pointer moved - in logical pixels, merged as it
comes. `Compose` draws only the damaged rectangles of the output: the
background (`backgroundRgb`), every shown window bottom first with a soft
shadow under the decorated ones, and the pointer. Each window's surface is
copied pixel for pixel: a surface with a device scale (the framework's
windows have one) is drawn at that scale, so a window rendered at the
output's scale is never resampled. `Compose` returns the physical rectangle
it drew, which is what a backend copies to the screen.

## API

| Call | Meaning |
|---|---|
| `SetOutputSize(w, h)` / `GetOutputSize()` | the output, logical px; windows that fill it follow, maximized ones re-maximize, the rest are kept on it |
| `ReserveEdges(id, l, r, t, b)` / `GetWorkArea()` | strips kept free for a window; four zeros release them |
| `AddWindow(id, spec)` → `Rect2Di` | joins hidden; the rectangle it was placed at |
| `RemoveWindow(id)`, `HasWindow(id)`, `GetInfo(id, out)` | |
| `AttachFrame(clientId, frameId)` | a title bar window for a window |
| `ListWindows()` / `ListApplicationWindows()` | stacking order, bottom first |
| `Show`, `Hide`, `SetTitle`, `SetRect`, `Raise`, `Activate` | `SetRect` is the window's own request: kept on the output and in its limits |
| `Minimize`, `Maximize`, `Restore`, `ToggleMaximize` | |
| `GetFocused()`, `GetKeyboardTarget()`, `TopmostFocusable(except)` | |
| `SetPointerPosition`, `MovePointerBy`, `SetPointerVisible`, `SetWindowCursor` | |
| `HitTest(point, &id)` → `EmbeddedWindowPart` | `Content`, a resize edge, or `Nothing` |
| `PointerMove(point)`, `PointerButton(point, down)` → `EmbeddedPointerRoute` | |
| `BeginMove(clientId)`, `IsMovingOrResizing()` | |
| `AddDamage`, `DamageAll`, `WindowPresented`, `HasDamage`, `TakeDamage` | |
| `Compose(output, scale, surfaceOf, damage)` → `Rect2Di` | |
| `onGeometryChanged`, `onFocusChanged`, `onStateChanged` | callbacks to the backend |
| `titleBarHeight` (32), `resizeGrip` (6), `keepVisible` (48), `backgroundRgb` | logical px; the colour as `0xRRGGBB` |

Single-threaded: every call on the thread that owns the windows.

## Related

- [UltraCanvasDesktopShell](UltraCanvasDesktopShell.md) - a desktop's window
  list, screens and applications, answered from this manager where the
  platform has no window manager of its own (*Inside one program*).
- [UltraCanvasHostedApps](UltraCanvasHostedApps.md) - applications that run
  inside their host program, as MobilePC's do.
- [MobilePC](../MobilePC/README.md) - UltraDesktop on a TV, on this manager.
