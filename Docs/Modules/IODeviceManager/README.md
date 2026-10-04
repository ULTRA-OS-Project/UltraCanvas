# IODeviceManager - One API for the Devices an Application Operates

## Scanners, Cameras and Printers Behind a Single Interface

> **API contract and design: [Architecture.md](Architecture.md). What is and
> is not built yet: [Gaps.md](Gaps.md).** This page is the overview; where it
> and those two disagree, they win.

**IODeviceManager** is UltraCanvas' hardware layer for peripherals an
application *operates*: it discovers devices, opens a session with one,
configures it and does work with it — scanning a page, capturing a frame,
printing a document. Every device, whatever backend found it, is an `IODevice`
with the same identity, lifecycle and error reporting, and every category adds
its own operations on top (`ScannerDevice`, `CameraDevice`, `PrinterDevice`).

Today that covers **printers** on Linux, macOS and Windows plus driverless
network (IPP) printers everywhere, **scanners** on Linux plus network (eSCL)
scanners everywhere, and **webcams** on Linux. The
other categories and platform backends below are planned; the tables say which
is which.

---

## Purpose

- **One API per kind of device** — a scanner is a `ScannerDevice` whether SANE
  or eSCL found it; a printer is a `PrinterDevice` whether CUPS, the Windows
  spooler or IPP did. Application code does not branch on the backend.
- **Several backends per category** — each backend registers an *enumerator*
  for one category, and the manager merges them: on Linux, SANE and eSCL both
  report scanners, and a scanner reachable through both is listed once.
- **Discovery and hot-plug** — `EnumerateDevices()` finds what is there now;
  `StartMonitoring()` follows devices being plugged in and out where the
  platform has a watcher (Linux, with libudev).
- **Only what the build can do** — a backend whose library was not found at
  configure time is simply not registered, and `GetRegisteredBackends()` says
  which ones are.

---

## Key Functionality

### Device Enumeration

`Initialize()` registers the backends this build was compiled with;
`EnumerateDevices()` runs every enumerator for a category and merges the
results. Devices come back constructed but not connected.

```cpp
#include "IODeviceManager/UltraCanvasIODeviceManager.h"
using namespace UltraCanvas;

auto& manager = IODeviceManager::GetInstance();
manager.Initialize();
manager.EnumerateAllDevices();            // or EnumerateDevices(IODeviceCategory::Scanner)

for (const IODeviceInfo& info : manager.GetDeviceInfos(IODeviceCategory::Printer)) {
    std::printf("%s (%s, %s)\n", info.name.c_str(), info.backend.c_str(),
                IODeviceTransportToString(info.transport));
}
```

### Scanning

`GetDevice()` returns the generic `IODevicePtr`; cast it to the category's
class for its operations. `Scan()` acquires one page, `ScanPages()` runs the
document feeder until it is empty.

```cpp
#include "IODeviceManager/UltraCanvasIODeviceScanner.h"

auto scanner = std::dynamic_pointer_cast<ScannerDevice>(
    manager.GetDevice(IODeviceCategory::Scanner, 0));
if (scanner && scanner->Connect()) {
    ScanConfiguration config;
    config.resolutionDpi = 300;
    config.colorMode = ScanColorMode::Color;
    scanner->SetConfiguration(config);

    ScannedImage page;
    if (IODeviceResult result = scanner->Scan(page)) {
        // page.data holds page.width x page.height pixels, page.channels per pixel
    } else {
        std::printf("Scan failed: %s\n", result.message.c_str());
    }
    scanner->Disconnect();
}
```

### Cameras

A camera captures single frames or streams them to a callback, and exposes
its controls (brightness, focus, zoom, pan, tilt …) through one call pair.

```cpp
#include "IODeviceManager/UltraCanvasIODeviceCamera.h"

auto camera = std::dynamic_pointer_cast<CameraDevice>(
    manager.GetDevice(IODeviceCategory::Camera, 0));
if (camera && camera->Connect()) {
    CameraFrame frame;
    camera->CaptureFrame(frame);                       // one still

    camera->StartStream([](const CameraFrame& f) {     // runs on the camera's thread
        // f.data, f.resolution, f.format, f.frameNumber
    });
    camera->SetControl(CameraControl::Zoom, 150);      // where the device has the control
    camera->StopStream();
    camera->Disconnect();
}
```

### Printing

A printer prints a file or a job, reports its status, queue and supply
levels, and lets the application choose who renders the page: the platform's
own driver, GutenPrint, or - for a driverless network printer - the printer
itself, over IPP.

```cpp
#include "IODeviceManager/UltraCanvasIODevicePrinter.h"

auto printer = std::dynamic_pointer_cast<PrinterDevice>(
    manager.GetDevice(IODeviceCategory::Printer, 0));
if (printer && printer->Connect()) {
    printer->PrintFile("report.pdf", "Quarterly report");
    printer->Disconnect();
}
```

### Network Scanners

eSCL (AirScan / Mopria) scanners are found through mDNS on the local network.
A scanner mDNS cannot reach — on another subnet, since mDNS does not cross
routers — is named in the `ULTRACANVAS_ESCL_SCANNERS` environment variable, a
comma-separated list of base URLs:

```sh
ULTRACANVAS_ESCL_SCANNERS=http://192.168.1.50/eSCL ./MyApp
```

### Network Printers

IPP Everywhere, AirPrint and Mopria printers need no driver: they are found
through mDNS, and a document the printer renders itself (PDF, JPEG) is sent
as it is, while text and other images are drawn as PWG raster. On Linux and
macOS a printer CUPS already offers is left to CUPS rather than listed twice.
A printer on another subnet is named in `ULTRACANVAS_IPP_PRINTERS`, a
comma-separated list of `ipp://`, `ipps://`, `http://` or `https://`
addresses:

```sh
ULTRACANVAS_IPP_PRINTERS=ipp://192.168.1.20/ipp/print ./MyApp
```

### Printer Status

`GetStatus()` reports what a printer is doing — idle, printing or stopped, the
reason (`media-empty`, `door-open`, `paused`), whether it accepts jobs and how
many are queued — and `GetSupplyLevels()` its ink or toner, with a percentage
where the printer gives one. Both need an open session and return nothing
otherwise:

```cpp
if (printer->Connect()) {
    IOPrinterStatus status = printer->GetStatus();
    for (const IOSupplyLevel& supply : printer->GetSupplyLevels()) {
        if (supply.IsLow()) { /* supply.description, supply.percentRemaining */ }
    }
    printer->Disconnect();
}
```

CUPS, the Windows spooler and IPP all report status. Supply levels come from
CUPS (`marker-*`) and IPP (`marker-*` and PWG's `printer-supply`). On Windows
the spooler itself has no levels, so the backend asks in two steps:

1. The printer driver's bidirectional channel (`IBidiSpl`,
   `\Printer.Consumables`), which drivers with a status monitor answer.
2. If the driver says nothing and the queue prints to a network address, the
   printer itself over IPP (builds with UltraNet). The address comes from the
   queue's port: an IPP port's URL, or a Standard TCP/IP port's host, tried at
   `/ipp/print`, `/ipp` and `/` on port 631. A printer that does not answer
   is left alone for a minute, so a switched-off printer costs the connect
   timeout (5 s) once, not on every call.

A USB or WSD queue whose driver keeps quiet, or a printer that answers
neither, gives an empty list rather than zero. DeviceExplorer shows both for
the selected printer.

### Your Own Devices

Hardware reached through an application's own code registers with the manager
like any other device: derive from `IODevice` (or a category class), implement
`DoConnect()` / `DoDisconnect()`, and register an instance. It then appears in
`GetDevices()`, and in DeviceExplorer, alongside the built-in ones.

```cpp
class MyDevice : public IODevice {
public:
    explicit MyDevice(const IODeviceInfo& info) : IODevice(info) {}
protected:
    IODeviceResult DoConnect() override { /* open it */ return IODeviceResult::Ok(); }
    void DoDisconnect() override { /* close it */ }
};

IODeviceInfo info;
info.deviceId = "my-device-1";
info.name = "Bench power supply";
info.category = IODeviceCategory::Custom;
manager.RegisterDevice(std::make_shared<MyDevice>(info));
```

A backend that discovers devices of its own registers an enumerator instead
(`RegisterEnumerator(category, "MyBackend", ...)`), so rescans and hot-plug
merging apply to it too.

---

## Device Categories

| Category | Examples | Status |
|----------|----------|--------|
| **Printer** | Inkjet, laser, label printers; local and network queues | ✅ Available — CUPS (Linux, macOS), Windows spooler, IPP driverless network printers (all platforms); GutenPrint as an alternative renderer |
| **Scanner** | Flatbed, ADF, network scanners | 🚧 Partial — SANE (Linux) and eSCL network scanners (all platforms); no USB scanners on macOS or Windows yet |
| **Camera** | Webcams | 🚧 Partial — V4L2 webcams on Linux; no camera backend on macOS or Windows; DSLRs and network cameras (RTSP/ONVIF) not yet |
| **Custom** | Anything an application reaches through its own code | ✅ Available — `RegisterDevice()` / `RegisterEnumerator()` |
| **Microphone**, **Speaker** | Audio input and output | ❓ Undecided — `UltraCanvasAudioDevices` already lists them; whether this module wraps it is an open decision in [Gaps.md](Gaps.md) |
| **Storage** | USB drives, SD cards | 📋 Planned |
| **NetworkAdapter**, **Serial**, **Bluetooth** | | 📋 Planned |
| **GPIO** | Raspberry Pi and embedded boards | 📋 Planned |
| **Barcode**, **Biometric** | Barcode readers, fingerprint readers | 📋 Planned |

✅ is used as [Gaps.md](Gaps.md) uses it: in the tree, compiled and covered
by a test. A planned category exists as an `IODeviceCategory` value and
nothing else.

---

## Backends by Platform

| Category | Linux | Windows | macOS | Network |
|----------|-------|---------|-------|---------|
| **Printer** | ✅ CUPS | ✅ Windows spooler (GDI rendering) | 🚧 CUPS (no duplex; untested on hardware) | ✅ IPP (all platforms; tested against CUPS's reference printer, not yet a physical one) |
| **Printer renderer** | ✅ GutenPrint | 🚧 GutenPrint (works; its tools are not shipped) | ✅ GutenPrint | ✅ IPP driverless (PWG raster, or the document as it is) |
| **Scanner** | ✅ SANE | 📋 WIA, TWAIN | 📋 ICA | ✅ eSCL (all platforms) |
| **Camera (webcam)** | ✅ V4L2 | 📋 Media Foundation | 📋 AVFoundation | — |
| **Camera (DSLR)** | 📋 libgphoto2 | 📋 WIA | 📋 ImageCapture | — |
| **Camera (network)** | — | — | — | 📋 RTSP, ONVIF |
| **Hot-plug watching** | ✅ udev (needs libudev) | 📋 | 📋 | — |

CUPS, SANE and libudev are optional at configure time: a build that did not
find one has no backend for it. [Docs/Dependencies.md](../../Dependencies.md)
lists the packages.

---

## Why IODeviceManager?

### For Developers
- **One lifecycle for every device** — `Connect()` / `Disconnect()`, a state
  machine and a last-error slot owned by the base class, so no backend
  reimplements them slightly differently.
- **Results, not exceptions** — every blocking call returns an
  `IODeviceResult` with a typed code, a message and the backend's own status
  code, the same shape as `UltraNetResult` and `UltraDbResult`.
- **Sessions survive a rescan** — re-enumerating keeps the existing object for
  a device that is still present, so an `IODevicePtr` held by the application
  never goes stale.

### For Applications
- **Backend independence** — the same code prints through CUPS on Linux, the
  spooler on Windows or IPP to a network printer anywhere, and scans through
  SANE or eSCL.
- **Room to grow** — a new backend is one enumerator; applications that
  already enumerate a category see its devices without changing.

---

## Architecture Highlights

- **Public headers** in `UltraCanvas/include/IODeviceManager/`,
  platform-neutral code in `UltraCanvas/core/IODeviceManager/`, platform
  backends in `UltraCanvas/OS/<Platform>/`.
- **`IODevice` base** with category classes (`ScannerDevice`, `CameraDevice`,
  `PrinterDevice`); backends implement protected `Do…` hooks.
- **Enumerators, not per-category methods** — several backends can serve one
  category on one platform without colliding at link time.
- **Singleton manager** — `IODeviceManager::GetInstance()` holds the registry.
- **Explicit backend registration** from `Initialize()`, not static
  initialisers, so a static build does not silently lose its backends.

See [Architecture.md](Architecture.md) for the reasoning behind each.

---

## Get Started

```cpp
#include "IODeviceManager/UltraCanvasIODeviceManager.h"
using namespace UltraCanvas;

auto& manager = IODeviceManager::GetInstance();
manager.Initialize();

// What can this build find?
for (const std::string& backend : manager.GetRegisteredBackends(IODeviceCategory::Scanner)) {
    std::printf("scanner backend: %s\n", backend.c_str());
}

// Discover, then use a device through the generic interface.
manager.EnumerateDevices(IODeviceCategory::Scanner);
if (IODevicePtr device = manager.GetDevice(IODeviceCategory::Scanner, 0)) {
    if (IODeviceResult result = device->Connect()) {
        // ... cast to ScannerDevice for scanning, as above ...
        device->Disconnect();
    } else {
        std::printf("%s\n", result.message.c_str());
    }
}

manager.Shutdown();
```

---

## See It: DeviceExplorer

The **DeviceExplorer** application (`Apps/DeviceExplorer`) is IODeviceManager's
user interface: every device the compiled-in backends find, as a tree grouped by
category, connection or backend, with the selected device's full description —
manufacturer, model, masked serial, transport, backend, connection path, state,
last error and backend-specific attributes — on the right. It follows hot-plug
changes through `StartMonitoring()` and has a headless `--list` mode. See
[Docs/DeviceExplorer/README.md](../../DeviceExplorer/README.md).

---

*IODeviceManager - Part of the UltraCanvas Framework*
