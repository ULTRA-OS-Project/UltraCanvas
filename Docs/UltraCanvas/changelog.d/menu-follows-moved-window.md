- **A menu stayed open, floating above everything, after its window had moved
  or gone behind another.** A menu opened from the menu bar (or a dropdown's
  list, any popup) is shown in a top-level window of its own, kept above every
  other window.
  - **Moved window (Windows).** The Windows layer never handled `WM_MOVE`, so
    dragging the window by its title bar left the menu where it had been. A
    press on the frame - title bar, border - is now a click outside the popups
    (`UltraCanvasWindowBase::ClosePopupsOnFramePress`, on `WM_NCLBUTTONDOWN`
    and its right / middle / X siblings and `WM_ENTERSIZEMOVE`), so the menu
    closes before the window moves, as a native menu does. A popup that does
    not close on a click outside follows the window instead, live during the
    drag (`MovePopupHostsWithWindow` on `WM_MOVE`). `GetConfig().x / y` and
    `onWindowMove` follow a move on Windows too, which they never did.
  - **Moved window (X11, macOS).** The window manager keeps the title-bar
    press to itself there, so the menu followed the dragged window instead.
    A window found moved since its first popup opened
    (`ClosePopupsIfWindowMoved`, on X11's ConfigureNotify and macOS's
    `windowDidMove`) now closes the popups that close on a click outside.
  - **Window no longer in front.** A popup closed when the focus left the
    application, but stayed open when it went to another window of the same
    application - a second document window brought to the front - floating
    over it. It now closes unless the focus went to its own window or a
    dialog of it (`IsSelfOrDialogOfSelf`, by the transient parent).
  - **Window hidden or minimised.** Its popups close (`HandleWindowHidden`);
    the windows of those that stay open are hidden, and never shown while
    their window is minimised.
  - **Window covered without losing the focus.** A popup's own window was
    topmost, above every window, so another program's window put in front
    without taking the focus came under the menu. It is now kept above its
    own window only - an owned window on Windows (no `WS_EX_TOPMOST`), a
    child window at its level on macOS, the window's transient on X11 - so
    whatever covers the window covers the menu. A window not in the ordinary
    stack still has its popups above every window
    (`UltraCanvasWindowBase::PopupsAboveEveryWindow`: a desktop - UltraDesktop's
    bars are kept below the applications - a notification, an overlay, a lock
    screen, or one `alwaysOnTop`).
