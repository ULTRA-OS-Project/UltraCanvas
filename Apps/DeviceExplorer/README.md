# DeviceExplorer

Shows the devices connected to this computer — printers, scanners, cameras —
as the IODeviceManager module finds them: a structured tree on the left, the
selected entry's details on the right. It is the module's user interface. It
observes; it never opens, configures or prints to a device.

User guide: [`Docs/DeviceExplorer/README.md`](../../Docs/DeviceExplorer/README.md).
The module underneath: [`Docs/Modules/IODeviceManager/README.md`](../../Docs/Modules/IODeviceManager/README.md).
Changelog and version: [`Docs/DeviceExplorer/CHANGELOG.md`](../../Docs/DeviceExplorer/CHANGELOG.md).

## Layout

| Path | What is in it |
|---|---|
| `ui/DeviceExplorerModel.*` | No UI: the `DeviceInventory` snapshot of IODeviceManager's registry, `GroupDevices` (category / connection / backend, with the filter), and the `PropertySection`s the details panel lists for a device, a group and the computer. `--list` and the test use it as well |
| `ui/DeviceExplorerWindow.*` | The window: toolbar, the `UltraCanvasTreeView` of devices and the `UltraCanvasColumnsTreeView` of properties in an `UltraCanvasSplitPane`, the scan thread and the UI timer |
| `main.cpp` | GUI bootstrap and the command line: `--list`, `--details`, `--group`, `--show-serials`, `--version`, `--help` |
| `DeviceExplorer.desktop` | The freedesktop shortcut; `make install` places it with the app icon (`media/appicon/DeviceExplorer.png` / `.svg`, the PNG rendered from the SVG) in the `hicolor` icon theme |

Tree icons are in `media/icons/DeviceExplorer/` (plus the framework's own
`print.png` and `network.svg`).

## Data flow

- **Scan.** `ScanInventory()` initialises the manager (registering the
  backends this build compiled in), runs `EnumerateAllDevices()` and snapshots
  the registry. SANE and CUPS can take seconds, so the window runs it on a
  worker thread and parks the result in one slot; a 200 ms UI timer applies
  it. *Rescan* does the same again.
- **Hot-plug.** After the first scan the worker calls `StartMonitoring()`.
  The manager re-enumerates the category that changed on its watcher thread
  and fires the change callback, which only sets a flag; the UI timer then
  re-reads the registry with `SnapshotInventory()` — cheap, nothing is
  enumerated twice.
- **Rebuild.** Every new inventory, grouping or filter rebuilds the tree.
  The selected node id and the set of groups the user collapsed are kept
  across rebuilds; a device that disappears leaves the computer shown, and
  comes back selected if it returns.
- **Teardown.** The window joins the scan thread, stops monitoring and
  clears the change callback before it goes; `main.cpp` calls
  `IODeviceManager::Shutdown()` last.

Nothing is painted by hand: every control is a framework element
(`scripts/check_ui_reuse.py` passes with no exemption).

## Building

Built by default with the rest of the tree (`BUILD_DEVICEEXPLORER`, on);
target and binary `DeviceExplorer`. Which device backends it can use is
decided by the framework's configure step — CUPS and SANE need their
development packages installed when UltraCanvas is configured.

```bash
cmake --build build --target DeviceExplorer
./build/DeviceExplorer --list --details
```

## Test

`Tests/DeviceExplorerModelTest.cpp` (built with `-DBUILD_TESTS=ON`, run by
`ctest -R DeviceExplorerModelTest`) registers fake devices and backends with
the manager and checks the grouping, the filter, the property sections, the
serial mask and the `--list` text. It needs no display and no hardware.
