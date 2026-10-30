- **UltraCanvasDesktopShell hosts the notification area on Linux.**
  `UltraCanvasTrayHost::Start` was refused on X11 ("not available on X11
  yet"). The new `OS/Linux/UltraCanvasLinuxTrayHost.cpp` is a
  StatusNotifierWatcher and StatusNotifierHost on the D-Bus session bus
  (GDBus, on a thread and `GMainContext` of its own, over a private
  connection): it owns `org.kde.StatusNotifierWatcher` and exports
  `/StatusNotifierWatcher`, takes the items programs register (a bus
  name, a name and path, or a path alone), reads each item's `Id`,
  `Status`, icon, tooltip, `ItemIsMenu` and `Menu` over
  `org.freedesktop.DBus.Properties` (`org.kde.StatusNotifierItem`, else
  `org.freedesktop.StatusNotifierItem`), follows its `New*` signals and
  drops it when its connection leaves the bus. A `Passive` item is left
  out; `NeedsAttention` shows the attention icon. An `IconPixmap` (the
  frame nearest 22 px, ARGB32 in network byte order) is written once as
  a PNG in the disk cache's `desktop-icons` folder, named after its
  pixels; an `IconName` resolves through the item's `IconThemePath`,
  then the installed icon themes. `appClass` is the item's `Id`, else
  the program's name from `/proc`; `processId` comes from the bus.
  - The item's dbusmenu (`com.canonical.dbusmenu` at `Menu`) is handed
    over as `DesktopTrayIcon::menu` - labels with the `_` mnemonics
    stripped, separators, check marks and radio items with their state,
    icons, submenus, hidden entries left out - read after one
    `AboutToShow` and again on `LayoutUpdated` /
    `ItemsPropertiesUpdated`; `ActivateMenuItem` sends
    `Event(id, "clicked")`. `Click` sends `Activate` (`ContextMenu` for
    an item that is only a menu), `SecondaryActivate` or `ContextMenu`
    at the pointer's position, read from the X query connection; a
    right click on an icon with a menu is the desktop's to draw.
  - `Start` refuses, saying who, while another watcher owns the name
    (KDE's or GNOME's) and without a session bus, so it never autolaunches
    one. `Stop` closes the connection, which releases the names at once.
  - `Tests/DesktopShellTest.cpp --tray` (CTest `DesktopShellTrayTest`,
    under `dbus-run-session`) exports a StatusNotifierItem of its own
    over GDBus and checks the listing, the PNG, the menu as data, clicks,
    menu activation, hiding, `LayoutUpdated`, the drop on disconnect, the
    refusal of a second host and that `Stop` returns within seconds.
