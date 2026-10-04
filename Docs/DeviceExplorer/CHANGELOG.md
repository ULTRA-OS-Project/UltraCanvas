#### 2026-09-29 *0.2.0*
- **Printers show their status and ink or toner.** Selecting a printer adds
  two sections above its details:
  - *Printer status*: Ready, Printing or Stopped, the printer's own reason
    (`media-empty`, `door-open`, `paused` …), whether it accepts jobs, how
    many are queued, and when it was asked.
  - *Supplies*: each cartridge, drum or waste tank with its level; *low* at
    10 % or less, *not reported* when the printer gives no level, never
    shown as 0 %.

  A printer only answers over an open session, so DeviceExplorer now opens
  one briefly. It opens it only if nothing else has it open, reads, and
  closes it again; it still never prints or configures anything. The
  question runs on a worker thread, since a network printer can take
  seconds, and shows *Asking the printer…* until the answer arrives. One
  question runs at a time, and a selection made meanwhile waits, so clicking
  through ten printers asks the last. Answers are kept for 30 seconds and
  dropped on a rescan. A printer that cannot be reached says *Could not ask
  the printer* and why. On Windows the levels come from the printer's
  driver, or for a network printer whose driver keeps quiet, from the
  printer over IPP (IODeviceManager's spooler backend does both). A printer
  reached neither way shows its state only.
- **`--list --details`** asks each printer the same way and prints the two
  sections after its other properties.
- **Driverless network printers** (IPP Everywhere, AirPrint, Mopria) appear
  under *Printers* without any change here, now that IODeviceManager has an
  IPP backend (framework 0.9.86).
  They are listed with backend *IPP* and connection *Network*. A printer on
  another subnet is named in `ULTRACANVAS_IPP_PRINTERS`. The user guide's
  table of what DeviceExplorer can find, and its troubleshooting, now say so.
- Checked against a CUPS queue whose toner and ink levels were set in
  `printers.conf`, running and then stopped (`cupsdisable`), in the window and
  with `--list --details`. The IPP route could not be run here:
  `ippeveprinter` needs IPv6, which this build machine lacks.
  `DeviceExplorerModelTest` covers the formatting: 14 new checks, 60 in all.

#### 2026-09-29 *0.1.1*
- **Ctrl-C and SIGTERM exit in order.** The signal handler called
  `RequestExit()` (which logs and runs a callback) and then `std::exit`,
  running the static destructors under live threads. It now makes the one
  call a handler may, `UltraCanvasApplicationBase::RequestExitFromSignal()`,
  and the main loop turns it into the same shutdown as a closed window.

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
