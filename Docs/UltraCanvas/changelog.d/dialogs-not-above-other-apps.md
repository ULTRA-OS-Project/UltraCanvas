- **Dialogs no longer stay on top of every other program's windows.**
  `DialogConfig` switched `alwaysOnTop` on for every dialog, so once X11 and
  macOS honoured that flag (`_NET_WM_STATE_ABOVE`, the floating level) a
  dialog - UltraFiler's *Proxy settings*, UltraMail's *Account settings*,
  every message box and file dialog - sat above every application and could
  not be sent behind another program's window; on Windows it had always been
  `WS_EX_TOPMOST`. The settings windows, made from a plain `WindowConfig`,
  behaved. `DialogConfig` leaves `alwaysOnTop` off now, and a dialog is kept
  above its own window only, through the parent `ShowModal` gives it:
  - **X11:** `WM_TRANSIENT_FOR`, which `ShowModal` already set.
  - **Windows:** the owner window. `SetTransientParent` now sets it
    (`GWLP_HWNDPARENT`) for a dialog created before its parent was known, as
    `CreateWindowExW` did for one created with `parentWindow`.
  - **macOS:** a child window of the parent's `NSWindow`
    (`addChildWindow:ordered:`); macOS had no link to the parent before. A
    dialog is attached when it is shown over a visible parent and detached when
    either is hidden or closed, so AppKit keeps it above that window, moves
    it with it and takes it into the parent's full-screen space. Dialogs
    only (`WindowType::Dialog`) - a popup, a tooltip and the splash screen
    keep their own levels - and never under a desktop window.

  On all three, a click on another of the application's windows still
  raises the modal, and `alwaysOnTop` stays for what is meant to be above
  everything - a recording bar, a floating palette, the splash screen
  (`DialogStackingTest`: a dialog is not above, is transient for its window,
  and an `alwaysOnTop` window still is above).
