- **`UltraCanvasDesktopShell`: the running desktop as a module.** The windows
  other applications have open and which one is active, the virtual desktops,
  the installed applications a launcher lists, a screenshot of the screen,
  the live state of the devices an info panel shows (webcam, microphone and
  speaker in use, Bluetooth, Wi-Fi with SSID, LAN, VPN, traffic, USB count,
  battery, keyboard layout) and the counts an application publishes for the
  desktop to show (`PublishNotice` / `ReadNotice`, one atomically written JSON
  file per application). `UltraCanvasDesktopShellMonitor` reports window and
  desktop changes on its own thread. Linux backend over EWMH, `XGetImage`,
  procfs and sysfs; a null backend elsewhere keeps the application list, the
  launcher and the notices working. The module the new UltraDesktop
  application (`Apps/UltraDesktop`, its own changelog) is built on, so any
  application gets the same window list. See
  `Docs/UltraCanvas/UltraCanvasDesktopShell.md`.
- **`UltraCanvasWaveSeparator`**: the S-curve between two groups on one bar,
  one group's colour up to the curve and the next group's after it. An
  in-flow element like `UltraCanvasSeparator`; in the catalogue.
- **`UltraCanvasToolbar` 1.5.0: item badges, real item reordering, wheel
  scrolling.** `SetItemBadge` / `SetItemBadgeCount` / `SetItemBadgeDot` /
  `ClearItemBadge` anchor an `UltraCanvasBadge` to an item (a count on the
  mail icon, a red dot on the webcam). `EnableItemReordering(true)` now
  reorders *items*: a press on an item followed by a drag moves it past its
  neighbours and `onItemReordered(from, to)` fires on release; before, the
  mode moved the whole toolbar on any press, which is what
  `ToolbarDragMode::Movable` is for. `MoveItem`, `GetItemIndex`,
  `GetItemOrder` and `GetItems` do the same from code (badges are children
  but never items). `ToolbarOverflowMode::Scroll` keeps items at their size
  and scrolls a full toolbar with the mouse wheel instead of squeezing them.
  `UltraCanvasButton::CanToggle` (new getter) is what the toolbar reads to
  reset a plain button's pressed look when its press became a drag.
- **`WindowType::Desktop`**: a window the size of the screen at the bottom of
  the stack, on every virtual desktop, out of taskbars and undecorated
  (`_NET_WM_WINDOW_TYPE_DESKTOP` plus the sticky, below and skip-taskbar
  states on X11) - the window a desktop shell draws its wallpaper and bars
  into. The X11 window size limit rises from 4096 to 16384 px so a 5K screen
  is a valid size.
- **UltraMail publishes its unread total** to the desktop through
  `UltraCanvasDesktopShell::PublishNotice("UltraMail", …)` whenever the
  account bar refreshes, so the desktop's mail icon carries the count.
- **X11 windows carry a `WM_CLASS`.** Every window now sets its class hint to
  the name `Initialize()` was given (`UltraFiler`; instance `ultrafiler`),
  which is what a taskbar - UltraDesktop's or any other desktop's - matches
  against a desktop entry's `StartupWMClass=` to find the application's icon.
  Before, no UltraCanvas window had one, so the `StartupWMClass` lines in the
  shipped `.desktop` files matched nothing. `UltraCanvasApplicationBase::GetAppName`
  (new) exposes the name.
- **An empty clipboard is no longer logged as an error.** The Linux clipboard
  reported "Selection conversion failed" whenever the owner had nothing in the
  requested target, which a clipboard monitor asks every half second; that
  answer is now silent.
