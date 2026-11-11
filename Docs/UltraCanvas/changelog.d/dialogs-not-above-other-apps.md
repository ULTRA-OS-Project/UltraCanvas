- **Dialogs no longer stay on top of every other program's windows.**
  `DialogConfig` switched `alwaysOnTop` on for every dialog, so once X11 and
  macOS honoured that flag (`_NET_WM_STATE_ABOVE`, the floating level) a
  dialog - UltraFiler's *Proxy settings*, UltraMail's *Account settings*,
  every message box and file dialog - sat above every application and could
  not be sent behind another program's window; on Windows it had always been
  `WS_EX_TOPMOST`. The settings windows, made from a plain `WindowConfig`,
  behaved. `DialogConfig` leaves `alwaysOnTop` off now, and a dialog is kept
  above its own window only: by `WM_TRANSIENT_FOR` on X11, which `ShowModal`
  sets, and on Windows by the owner window, which `SetTransientParent` now
  sets for a dialog created before its parent was known
  (`GWLP_HWNDPARENT`). A click on another of the application's windows still
  raises the modal. `alwaysOnTop` stays for what is meant to be above
  everything - a recording bar, a floating palette, the splash screen
  (`DialogStackingTest`: a dialog is not above, is transient for its window,
  and an `alwaysOnTop` window still is above).
