- **macOS: screen positions are top-left, and popups and tooltips have windows
  of their own there too.** The macOS backend gave and took window positions
  in Cocoa's coordinates, which run up from the bottom of the primary screen,
  while every helper that places a window - `CenterOnParent`, the clamp that
  keeps a window grown to its content on its monitor, a toast's corner, a
  viewer opened over its host - works in top-left coordinates as X11 and
  Windows give them. Only what is symmetric top to bottom, centring, came out
  right; a toast meant for the top right of the screen went to the bottom.
  Popups and the tooltip, placed below and above the pointer, could not be
  given windows of their own on macOS for the same reason.
  - `SetWindowPosition`, `GetWindowPosition`, `GetScreenBounds`, the new
    `GetContentScreenOrigin` override (below the title bar), a window's
    create-time `config.x` / `config.y` and its moves (`onWindowMove`) are in
    top-left screen coordinates: points, x right and y down from the top of
    the primary screen, a screen above it at negative y. A window without a
    position opens where it always did. `SetWindowSize` keeps the window's
    top-left corner where it was, as on the other platforms (Cocoa keeps the
    bottom-left one). An event's `pointerGlobal` is flipped about the primary
    screen too, not the one with the key window.
  - `WindowType::Popup` and `WindowType::Tooltip` on macOS: a borderless
    window that never becomes key (ordered front with `orderFrontRegardless`,
    so the window the popup or tooltip belongs to keeps the keys), at the
    pop-up menu level, out of the window cycle and Mission Control and shown
    over a full-screen window, transparent where nothing is drawn - a menu's
    rounded corners show what is beneath, no outline needs cutting - and with
    the system's shadow, which follows what is drawn. A popup's window takes
    the first click (`acceptsFirstMouse:`); a tooltip's lets the pointer
    through.
  - `UltraCanvasWindowBase::SetPopupsInOwnWindows` and
    `UltraCanvasTooltipManager::SetUseOwnWindow` are on by default on macOS
    now; only the browser, Android and iOS draw popups and tooltips inside
    their window.
