- **Tooltips are shown in a window of their own, above every other window.**
  `UltraCanvasTooltipManager` drew the tooltip into the window it belongs to
  while that window composited its frame, so any window above that one
  covered it, and it stopped at that window's edges. It showed worst on
  UltraDesktop: the bars live in the desktop window, which is kept below all
  others, so a tooltip of the taskbar or the right bar went under whatever
  application was open there. The tooltip now has a window of its own - the
  new `WindowType::Tooltip` - placed on the screen above and to the right of
  the pointer with the monitor's room, not the window's, so it also reaches
  past the edge of a small window or a bar.
  - `WindowType::Tooltip`: above every other window, never focused, and the
    pointer passes through it to the element it describes. On X11 an
    override-redirect window typed `_NET_WM_WINDOW_TYPE_TOOLTIP` with an empty
    input shape (the X11 build now links `xext`), mapped raised; on Windows
    `WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`, layered and
    `WS_EX_TRANSPARENT`, shown without activation; on Wayland a popup of the
    window the tooltip belongs to with no input region, or, beside a desktop
    on the layer shell, a surface on the overlay layer that never takes the
    keys.
  - The window is opaque, so in it the tooltip has square corners and no soft
    shadow of its own (a compositing window manager gives a tooltip window
    one).
  - It is no application window: a closing window takes its tooltip and the
    tooltip's window with it (`UltraCanvasTooltipManager::ForgetWindow`, from
    `CleanupWindowReferences`), and it is never counted by
    `ClosingEndsApplication`, jumped to, reported to assistive technology,
    used to place a parentless dialog or brought to the front for a second
    instance.
  - `UltraCanvasTooltipManager::SetUseOwnWindow(false)` draws the tooltip
    inside its window as before; so does every platform that cannot place
    such a window (macOS, the browser, Android) and any case where the window
    cannot be made. `GetTooltipWindow()` says where the tooltip on screen is.
  - A window moving to a monitor of another scale no longer hides the tooltip
    when the window is the tooltip's own: it is drawn again at the new scale
    (`WindowScaleChanged`). While the tooltip is in its own window, a caret
    blink in the window below no longer composites that window in full.
  - `TooltipOwnWindowTest` (X11, under Xvfb) covers a window with another,
    shows a tooltip of the first and reads the screen: the tooltip is on top,
    outside its window, lets the pointer through, follows the pointer, is
    reused when shown again, and closes with its window.
