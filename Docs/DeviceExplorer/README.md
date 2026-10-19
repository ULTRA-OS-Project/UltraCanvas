# DeviceExplorer

## Overview

DeviceExplorer shows the devices connected to this computer — printers,
scanners and cameras today, more as IODeviceManager grows backends — the way
the [IODeviceManager](../Modules/IODeviceManager/README.md) module finds them.
It is that module's user interface: a structured tree of everything found on
the left, and everything the module knows about the selected entry on the
right.

It **only looks**. It never changes a setting, prints, scans or captures, and
nothing it shows is written anywhere. The one thing it opens a device for is
to ask a selected printer for its status and ink or toner — a printer only
answers that over an open session — and it closes the printer again as soon as
it has the answer. A printer another part of the application already has open
is asked without being closed.

- Source: [`Apps/DeviceExplorer`](../../Apps/DeviceExplorer/README.md)
- Version: its own, from the first line of [`CHANGELOG.md`](CHANGELOG.md).
  `cmake/UltraCanvasVersion.cmake` reads it into `DEVICEEXPLORER_VERSION`,
  which the window title, the About box and `--version` print. The app does
  not move when the framework releases.
- Build option: `BUILD_DEVICEEXPLORER` (on by default); target and binary
  `DeviceExplorer`.

## Layout

```
┌ DeviceExplorer ─────────────────────────────────────────────────────────────┐
│ Group by [Category ▾]  [Filter devices…      ]  Rescan  Expand all  …  About│
├───────────────────────────┬─────────────────────────────────────────────────┤
│ ▾ workstation             │ HP LaserJet M404                                │
│   ▾ Printers (2)          │ Printer · Network · Available (not open)        │
│       HP LaserJet M404    │ ┌ General ──────────────────────────────────────┐│
│       PDF                 │ │ Name           HP LaserJet M404              ││
│   ▾ Scanners (1)          │ │ Manufacturer   HP                            ││
│       Canon LiDE 300      │ │ Serial number  ********3456                  ││
│   ▾ Cameras (1)           │ ├ Connection ───────────────────────────────────┤│
│       Integrated Webcam   │ │ Connection     Network                       ││
│                           │ │ Backend        CUPS                          ││
│                           │ ├ Status ───────────────────────────────────────┤│
│                           │ │ State          Available (not open)          ││
└───────────────────────────┴─────────────────────────────────────────────────┘
```

### The tree

The computer is the root. Below it, one branch per group, and a row per
device. **Group by** in the toolbar decides what a group is:

| Group by | Branches | Useful for |
|---|---|---|
| **Category** (default) | Printers, Scanners, Cameras, … | "What do I have?" |
| **Connection** | USB, Network, Bluetooth, Serial, … | "What is plugged in, and what is on the network?" |
| **Backend** | CUPS, SANE, V4L2, eSCL, Windows spooler, … | "Which driver stack found this?" |

Grouped by category, every category this build has a backend for is listed
**even when nothing was found**, greyed out with a count of `(0)`. A category
the build has no backend for is not listed at all: DeviceExplorer could never
find anything there, and showing it empty would be a claim it cannot make.

Row colours: **red** — the device reports an error; **grey** — the device is
offline, or a group is empty. Hover a device for its type, connection and
state.

Groups you collapse stay collapsed across rescans and hot-plug updates;
**Expand all** / **Collapse all** act on every group at once.

### The details

Select a row to see everything known about it, in titled sections.

**A device**

| Section | Contents |
|---|---|
| Printer status *(printers)* | State (Ready, Printing, Stopped), the printer's reason (`media-empty`, `door-open`, `paused` …), whether it accepts jobs, how many are queued, and when it was asked |
| Supplies *(printers)* | Each ink or toner cartridge, drum or waste tank the printer reports, with its level in percent — *low* at 10 % or less, *not reported* when the printer gives no level |
| General | Name, type, manufacturer, model, serial number (masked), description, location |
| Connection | Connection (USB, Network, …), backend, connection path (device node, CUPS/IPP URI), device id |
| Status | State, whether this application holds a session, last error |
| Backend details | Whatever else the backend reported (driver, PPD, capabilities …) |

Rows the backend left empty are not shown.

For a printer, the two status sections come first. Selecting it shows *Asking
the printer…* while DeviceExplorer asks — on a worker thread, since a network
printer can take seconds — and then the answer. The answer is kept for 30
seconds, so clicking back and forth does not ask again; after that, or after a
rescan, selecting the printer asks anew. A printer that cannot be reached says
*Could not ask the printer* and why. On Windows the levels come from the
printer's driver, or, for a network printer whose driver does not report
them, from the printer itself over IPP. A printer reached neither way shows
its state and no *Supplies*.

**A group** — how many devices it holds, how many are open or reporting an
error, which backends searched it and how many each found, and the list of
its devices.

**The computer** — host name, operating system, kernel, manufacturer and
model; the device count per category; the backends compiled into this build;
when the last scan ran and whether hot-plug monitoring is on.

### The toolbar

| Control | What it does |
|---|---|
| Group by | Category / Connection / Backend — rebuilds the tree, keeps the selection |
| Filter | Narrows the tree to devices whose name, manufacturer, model, backend, location, category or connection contains the text; opens every group while it is set |
| Rescan | Asks every backend again. Runs in the background; the button is disabled until it finishes |
| Expand all / Collapse all | Every group at once |
| About | Version, what the app does, and the backends in this build |

The status text at the right says how many devices were found (and how many
the filter shows), and whether changes are being watched for.

## Live updates

The first scan starts when the window opens and runs on a worker thread —
SANE and CUPS can each take seconds to answer, and the window never waits for
them. After it, DeviceExplorer turns on IODeviceManager's hot-plug watcher.
While the watcher runs, a device plugged in or removed appears or disappears by
itself, and the selection stays on the device you were looking at.

**Whether there is a watcher depends on the platform and on how UltraCanvas was
built:**

| Platform | Hot-plug watching |
|---|---|
| Linux | Only when the framework was built with **libudev** (`libudev-dev` on Debian/Ubuntu, `systemd-devel` on Fedora). Configuring UltraCanvas prints `udev: <version> - hot-plug watching ENABLED` when it found it, `[-] udev - not found (no hot-plug watching on this build)` when it did not. |
| macOS, Windows | Not yet — IODeviceManager has no watcher there. |

The computer node shows which case you are in: *Hot-plug monitoring* reads
*On* or *Off*, and with it off the status text says *press Rescan after
plugging in*. Nothing else changes — every device is still found, it just
takes a **Rescan** to see one that arrived or left after the last scan.

Even with udev, the watcher reacts to what the kernel sees: USB, video and
sound devices arriving or leaving. A network printer or eSCL scanner that comes
online is not a kernel event, so it still needs a **Rescan**.

## What it can find

DeviceExplorer lists what IODeviceManager's backends report — nothing more.
Which backends a build carries depends on the platform and on the libraries
found when it was configured; the computer node lists them.

| Category | Linux | Windows | macOS | Network |
|---|---|---|---|---|
| Printers | CUPS | Windows spooler | CUPS | IPP driverless: IPP Everywhere, AirPrint, Mopria (all platforms) |
| Scanners | SANE | — | — | eSCL (AirScan / Mopria) |
| Cameras | V4L2 | — | — | — |

Network scanners and printers are found over DNS-SD. One DNS-SD cannot reach
— on another subnet — is named in `ULTRACANVAS_ESCL_SCANNERS` or
`ULTRACANVAS_IPP_PRINTERS` (a comma-separated list of addresses) before
DeviceExplorer starts. A driverless printer that a CUPS queue already reaches
is listed once, under CUPS; on Windows, one the machine already has a print
queue for is listed once, under the Windows spooler.

Planned categories (microphones, speakers, storage, serial, Bluetooth, GPIO …)
appear in the tree automatically once IODeviceManager has a backend for them;
see the module's [Architecture.md](../Modules/IODeviceManager/Architecture.md)
and [Gaps.md](../Modules/IODeviceManager/Gaps.md). For the machine's own
hardware — CPU, memory, drives, USB controllers — see
[UltraCanvasHardwareInfo](../UltraCanvas/UltraCanvasHardwareInfo.md); the two
are deliberately separate.

## Privacy

Serial numbers identify their owner's hardware, and a device window is the
kind of screen that ends up in a screenshot or a bug report. DeviceExplorer
masks them by the same rule as the hardware panel,
`UltraCanvasHardwareInfo::MaskIdentifier`: only the last four characters are
shown. `--list --show-serials` prints them in full on the command line.

## Command line

```
DeviceExplorer                         open the window
DeviceExplorer --group connection      open it grouped by connection
DeviceExplorer --list                  scan once, print the tree, exit
DeviceExplorer --list --details        ... with every property of every device,
                                       and each printer's status and supplies
DeviceExplorer --list --group backend  ... grouped by backend
DeviceExplorer --list --show-serials   ... with serial numbers unmasked
DeviceExplorer --version
DeviceExplorer --help
```

`--list` needs no display, so it works over ssh and in CI:

```
workstation  (3 devices, grouped by Category)
├─ Printers (2)
│  ├─ HP LaserJet M404  [Network, CUPS, Available (not open)]
│  └─ PDF  [Virtual, CUPS, Available (not open)]
├─ Scanners (0)
│  └─ (none found)
└─ Cameras (1)
   └─ Integrated Webcam  [USB, V4L2, Available (not open)]
```

With `--details`, a printer is asked for its status the same way the window
asks, one printer after another, and the answer follows its other sections:

```
│  └─ OfficeLaser  [Unknown connection, CUPS, Offline]
│       …  General, Connection, Status, Backend details …
│       Printer status
│         State: Stopped
│         Reason: paused
│         Accepting jobs: Yes
│         Jobs queued: 0
│         Asked at: 2026-09-29 12:23:52
│       Supplies
│         Black toner: 7 % - low
│         Cyan ink: 72 %
```

## Troubleshooting

| Symptom | Cause |
|---|---|
| No category at all under the computer | The build carries no device backend (the computer node says so). Rebuild with CUPS / SANE development packages installed. |
| *Scanners (0)* although a scanner is connected | SANE has no driver for it, or the user may not access the USB device (udev rule / `scanner` group). `scanimage -L` should list it first. |
| *Cameras (0)* on Linux | No `/dev/video*` node the user can open — check membership of the `video` group. |
| A device stays after it was unplugged, or a new one does not appear | No hot-plug watcher: on Linux the framework was built without libudev (install `libudev-dev` and reconfigure), on macOS and Windows there is none yet. Press **Rescan**. |
| A network printer or scanner that just came online is missing | Network devices are not kernel events, so no watcher sees them; press **Rescan**. |
| A network printer never appears | DNS-SD does not cross routers: name it in `ULTRACANVAS_IPP_PRINTERS=ipp://<address>/ipp/print`. A printer that offers only `ipps://` with a self-signed certificate, or asks for a password, is not supported yet. |
| *Could not ask the printer* | The printer did not answer the status request: switched off, unreachable, or (CUPS) the queue was deleted. The reason line says which. |
| A printer shows no *Supplies* | It reported none — common for printers that have no level sensors. On Windows it also happens when the driver does not report levels *and* the printer cannot be asked over IPP: it is connected by USB, it does not speak IPP, or a firewall blocks port 631. |
