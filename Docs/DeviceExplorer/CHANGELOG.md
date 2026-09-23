#### 2026-09-23 *0.1.0*
- **First release.** DeviceExplorer (`Apps/DeviceExplorer`) shows the
  devices connected to this computer as the IODeviceManager module finds
  them — the UI the module has not had until now. It observes; it never
  opens, configures or prints to a device.
  - **Tree on the left.** The computer at the root, one branch per group,
    a row per device. The toolbar's *Group by* picks the grouping:
    *Category* (Printers, Scanners, Cameras …), *Connection* (USB, Network,
    Bluetooth …) or *Backend* (CUPS, SANE, V4L2, eSCL, the Windows spooler).
    Grouped by category, every category this build has a backend for is
    listed even when nothing was found, so "no scanners" reads as such and
    a category the build cannot search is never claimed to be empty. A
    device reporting an error is shown red, an offline one grey. Groups the
    user closes stay closed across rescans.
  - **Details on the right.** Titled sections for whatever is selected: a
    device's *General* (name, type, manufacturer, model, serial number,
    description, location), *Connection* (transport, backend, connection
    path, device id), *Status* (state, whether this application holds a
    session, last error) and every backend-specific attribute; a group's
    summary and member list, with how many devices each backend found; the
    computer's host name, operating system, device counts per category, the
    backends compiled into this build and when it last scanned. Serial
    numbers are masked by `UltraCanvasHardwareInfo::MaskIdentifier`, the
    rule the hardware panel uses.
  - **Live.** The scan runs on a worker thread, since SANE and CUPS can take
    seconds to answer; *Rescan* runs it again. After the first scan
    IODeviceManager's hot-plug watcher is started, and a device plugged in
    or removed appears or disappears without a rescan where the platform has
    a watcher (udev on Linux).
  - **Filter.** The filter box narrows the tree to devices whose name,
    manufacturer, model, backend, location, category or connection contains
    the text.
  - **Headless.** `--list` scans once and prints the same tree as text;
    `--details` adds every property, `--group category|connection|backend`
    picks the grouping (in the window too), `--show-serials` prints serial
    numbers unmasked. `--version`, `--help`.
  - **Tested.** `Tests/DeviceExplorerModelTest.cpp` drives the grouping,
    filter and property sections from test devices registered with the
    manager, with no display and no hardware.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
