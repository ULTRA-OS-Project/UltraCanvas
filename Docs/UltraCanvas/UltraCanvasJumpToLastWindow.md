# Jump to Last Window

Fast window switching for multi-window UltraCanvas applications: one keyboard
shortcut or one mouse button raises the window that was used before the
current one and puts keyboard focus back into the input field that was active
there. Repeated triggers toggle between the two most recently used windows.

## What it does

The application object (UltraCanvas' window manager for the windows of the
running application) keeps a most-recently-used history of window focus.
Every focus change — clicking into a window, the OS window manager focusing a
window, or a jump itself — moves that window to the front of the history.

`JumpToLastWindow()`:

1. Picks the most recently used *other* window that is still eligible
   (created, not closing/hidden, visible or minimized; transient popup
   windows are skipped).
2. Restores it if it is minimized, then raises and focuses it
   (`RaiseAndFocus()`).
3. Updates the focus bookkeeping immediately, so a second trigger jumps
   straight back — the two most recent windows toggle.
4. Because every `UltraCanvasWindow` preserves its focused element while the
   window is in the background, the same input field (text input, text area,
   spreadsheet cell, …) receives keyboard input again after the jump — no
   extra clicks needed.

When a **modal** window is active the jump is refused: modality may not be
bypassed by a shortcut.

Scope note: UltraCanvas manages the windows of the running application, so
the jump switches between windows of the same application. Switching to a
window of *another* application remains the job of the OS window manager
(Alt-Tab and friends).

## Enabling it

The feature ships disabled; an application binds a key, a mouse button, or
both:

```cpp
auto* app = UltraCanvasApplication::GetInstance();

// Plain F6 jumps to the last used window
app->SetJumpToLastWindowKey(UCKeys::F6);

// ...or a modified key: Ctrl+`
app->SetJumpToLastWindowKey(UCKeys::Grave, /*ctrl=*/true);

// ...and/or the mouse "back" thumb button
app->SetJumpToLastWindowMouseButton(UCMouseButton::Back);

// Programmatic jump (e.g. from a menu item)
app->JumpToLastWindow();

// Unbind
app->ClearJumpToLastWindowKey();
app->SetJumpToLastWindowMouseButton(UCMouseButton::NoneButton);
```

The trigger is handled centrally in the application's event dispatch, before
any window or element sees the event, so a bound key or button never leaks
into the UI as text input or a click. Keyboard matching is exact on the
modifier set (`F6` does not fire while Ctrl is held unless Ctrl is part of
the binding).

`SetJumpToLastWindowKey(key, ctrl, shift, alt, meta)` — binds a `UCKeys`
virtual key with an exact modifier combination; fires on `KeyDown`.

`SetJumpToLastWindowMouseButton(button)` — binds a `UCMouseButton`; fires on
`MouseDown`. Typically one of the new side/thumb buttons:

| UCMouseButton | X11 button | Windows |
|---------------|-----------|---------|
| `Back`        | 8         | XBUTTON1 |
| `Forward`     | 9         | XBUTTON2 |

(The `Back`/`Forward` buttons are newly translated by the Linux and Windows
back-ends and can also be used by regular element event handlers.)

`JumpToLastWindow()` returns `true` when a jump happened, `false` when there
is no other eligible window or a modal window is active.

## Demo

The DemoApp binds **F6** and the mouse **Back** button. Open a second window
(e.g. "Show Source" or the docs window), type into a text field, jump back
and forth: each window's caret position/input focus survives the round trip.

## Implementation notes

- History container: `UCWeakMRUList<T>` in
  `include/UltraCanvasFocusHistory.h` — a small header-only MRU list of
  `weak_ptr`s (capacity 32). Destroyed windows expire out of it harmlessly;
  closing windows are removed explicitly in `CleanupWindowReferences()`.
  Unit-tested display-free in `Tests/FocusHistoryTest.cpp` (CTest target
  `FocusHistoryTest`).
- All application-side focus changes now funnel through
  `SetFocusedWindowInternal()`, which sends proper `WindowBlur`/`WindowFocus`
  events to the windows involved and maintains the history. (This also fixed
  the click-to-focus path, which previously re-dispatched the raw MouseDown
  event to both windows instead of blur/focus events.)
- The jump updates `focusedWindow` synchronously instead of waiting for the
  native focus notification (X11 `_NET_ACTIVE_WINDOW` round trip is
  asynchronous); the later native `WindowFocus` event becomes a no-op.
