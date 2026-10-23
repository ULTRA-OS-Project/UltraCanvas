- **The window's contents are a panel of their own, which the UltraCanvas
  demo shows too.** `DeviceExplorerPanel` (`ui/DeviceExplorerPanel.*`) holds
  the whole user interface - title, toolbar, device tree and details, the scan
  and printer-status threads and the UI timer - and `DeviceExplorerWindow`
  only hosts it. The demo application's *IODeviceManager* page is this panel,
  compiled from the same files.
- **Longer translations fit.** The toolbar wraps onto a second line when its
  buttons no longer fit, the title and the description under it wrap, and no
  label has a fixed width any more (*Group by*, the status line, the details'
  title and subtitle).
