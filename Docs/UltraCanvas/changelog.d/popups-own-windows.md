- **Every popup is shown in a window of its own, above every other window.**
  A popup - a menu or submenu, a dropdown's list, an autocomplete list, a
  date, time or colour picker, anything opened with `OpenPopup` - was
  composited onto the window it was opened in, so any window above that one
  covered it. On UltraDesktop every context menu of the bars went under
  whatever application was open beside them, since the bars live in the
  desktop window, which is kept below all others. Each popup now has a window
  of its own, `WindowType::Popup`, placed where the popup is, above every
  other window; the popup stays its window's in every other way.
  - The popup is still laid out and drawn in the window it belongs to, which
    keeps the focus and the keys. The pointer over its own window is handed
    to that window, at the popup's place there, before anything else sees it
    - global handlers, hover, capture, a click outside, a modal dialog - so
    menus, dropdowns and the rest work unchanged.
    `UltraCanvasWindowBase::FindElementAtPoint` also finds a popup that
    reaches past the window's edge.
  - `WindowType::Popup` (unused until now) is a popup's own window: on X11
    override-redirect, typed `_NET_WM_WINDOW_TYPE_POPUP_MENU`, mapped raised;
    on Windows `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`, shown
    without activation and answering `WM_MOUSEACTIVATE` with
    `MA_NOACTIVATE`; on Wayland a popup of the window it belongs to, or
    beside a layer-shell desktop a surface on the overlay layer that never
    takes the keys.
  - The window is cut to the popup's opaque pixels
    (`UltraCanvasWindowBase::SetNativeWindowShape`: the X11 shape extension,
    a window region on Windows; Wayland's surfaces carry their alpha), so a
    menu's rounded corners show what is beneath. It has no soft drop shadow of
    its own and does not fade in (a menu with `enableAnimations` appears in
    full); a compositing window manager gives popup windows a shadow.
  - The window follows the popup: made as it opens (on the next turn of the
    loop when it opens while the windows are being drawn), placed, sized and
    shown as it is laid out and drawn, moved when the window it belongs to
    moves, hidden as it closes and closed on the next turn of the loop. The
    caret of a field in a popup is drawn in the popup's window, and the
    window shows the pointer its owner shows.
  - A popup that closes on a click outside it also closes when the focus
    leaves the application, as it would otherwise float above other
    programs' windows; focus moving to another window of the application (a
    dialog the popup opened) leaves it open.
  - A popup's window is no application window: closed with the window it
    belongs to, skipped by `ClosingEndsApplication`, the accessibility tree,
    the placement of a dialog without a parent and single-instance raising,
    as `JumpToLastWindow` already skipped it.
  - `UltraCanvasWindowBase::SetPopupsInOwnWindows(false)` composites popups
    onto their window as before, and so does every platform that cannot place
    such a window (macOS, the browser, Android) and any popup whose window
    cannot be made. `GetPopupWindow(popup)` says where a popup is shown.
  - `UltraCanvasCaret::Composite` takes an offset, for a caret drawn in a
    popup's own window.
  - `PopupOwnWindowTest` (X11, under Xvfb) covers a window with another,
    opens a menu in the first and reads the screen back: the menu is on top,
    cut to its rounded outline, a click in its window runs the item under it,
    a click outside and focus leaving the application close it, a popup past
    the window's edge takes clicks there, and its window closes with the
    window it belongs to. `CaretStackingTest` and `TextMetricsScreenshotTest`,
    which read a menu back from the window's own surface, composite it there.
