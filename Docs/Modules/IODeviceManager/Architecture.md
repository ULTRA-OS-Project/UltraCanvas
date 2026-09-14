# IODeviceManager — Architecture

Module status: **foundation landed, categories in progress.**
See [README.md](README.md) for what the module is for. This file is the
API contract and the design rationale behind it.

---

## Where the code lives

Same shape as UltraNet and UltraDatabase:

```
UltraCanvas/include/IODeviceManager/   Public headers
    UltraCanvasIODeviceTypes.h           Device-generic vocabulary
    UltraCanvasIODevice.h                IODevice base class
    UltraCanvasIODeviceManager.h         Registry and discovery
UltraCanvas/core/IODeviceManager/      Platform-neutral implementation
    UltraCanvasIODevice.cpp
    UltraCanvasIODeviceManager.cpp
    UltraCanvasIODeviceBackends.{h,cpp}  Internal: backend attachment point
UltraCanvas/OS/<Platform>/             Platform backends, flat, picked up by
                                       the existing OS/<Platform>/*.cpp glob
```

Everything is in `namespace UltraCanvas`. Platform directories are
`Linux`, `MSWindows`, `MacOS`, `Android`, `WASM`, `BSD` — the names the rest
of the tree already uses.

The foundation layer depends on nothing but the standard library, so
`Tests/IODeviceManagerTest` builds and runs on a machine with none of the
rendering dependencies installed.

---

## The three layers

```
Application
    │
    ▼
IODeviceManager          registry + discovery, knows no platform API
    │  runs enumerators
    ▼
IODevice  (ScannerDevice / CameraDevice / PrinterDevice)
    │  virtual dispatch
    ▼
Backend           SANE · V4L2 · gphoto2 · CUPS · WIA · TWAIN · ICA ·
                  AVFoundation · MediaFoundation · IPP · GutenPrint
```

### Results and state

Every blocking operation returns `IODeviceResult` — `Ok()` / `Error(code, msg)`
factories, an `explicit operator bool()`, a typed `IODeviceResultCode`, and a
`backendCode` slot carrying the backend's own status verbatim for the log.
This mirrors `UltraNetResult` and `UltraDbResult` so the three modules read
alike.

`IODevice` owns the lifecycle state machine. Backends implement
`DoConnect()` / `DoDisconnect()`; callers use `Connect()` / `Disconnect()`.
Two consequences of that split are load-bearing:

- **The base destructor calls no virtuals.** By the time `~IODevice()` runs
  the derived object is gone, so a virtual call from there dispatches into a
  dead object — and for a pure virtual, terminates. Each backend releases its
  own handles in its own destructor.
- **`Disconnect()` always reaches the backend**, even when `Connect()` never
  succeeded, because a `DoConnect()` that failed halfway may still hold
  handles.

`deviceMutex` is recursive: backend hooks are called with it held, and a
backend legitimately calls `SetState()` / `Fail()` both from inside those
hooks and from its own worker threads, where it is not.

---

## Backends register enumerators, not methods

An enumerator discovers the devices of **one category** through **one
backend** and returns them constructed but not connected:

```cpp
using IODeviceEnumerator = std::function<std::vector<IODevicePtr>()>;

manager.RegisterEnumerator(IODeviceCategory::Camera, "V4L2",    EnumerateV4L2Cameras);
manager.RegisterEnumerator(IODeviceCategory::Camera, "gphoto2", EnumerateGPhoto2Cameras);
```

`EnumerateDevices(category)` runs every enumerator registered for that
category and merges the results.

This is deliberately **not** a set of `EnumerateScanners()` /
`EnumerateCameras()` methods on the manager. With one method per category,
every platform backend has to define the same symbol — so a webcam backend
and a DSLR backend on one platform collide at link time, and whichever the
linker picks is the only one that ever runs. Two backends serving one
category is the normal case, not the exception:

| Category | Linux | Windows | macOS |
|---|---|---|---|
| Camera | V4L2 *and* gphoto2 *and* RTSP | MediaFoundation *and* WIA *and* RTSP | AVFoundation *and* ImageCapture *and* RTSP |
| Scanner | SANE *and* eSCL | WIA *and* TWAIN *and* eSCL | ICA *and* eSCL |
| Printer | CUPS *and* IPP *and* GutenPrint | Spooler *and* IPP *and* GutenPrint | CUPS *and* IPP *and* GutenPrint |

Backends attach through `Internal::RegisterCompiledBackends()` in
`core/IODeviceManager/UltraCanvasIODeviceBackends.cpp`, called once from
`Initialize()`. They do **not** self-register from a static initialiser:
UltraCanvas also builds as a static library, where the linker drops the
static initialisers of object files nothing else references — which would
silently leave a platform with no devices at all.

### Merge semantics

- A device still present keeps its **existing object**, so an open session
  survives a rescan and a caller's `IODevicePtr` never goes stale.
- A device that disappeared is disconnected and dropped, and the change
  callback fires `Removed`.
- The first backend to claim a device id wins, so a device reachable through
  two backends is registered once.
- One backend throwing does not hide the devices the others found; the
  result names the backend that failed.
- Change callbacks fire with the registry lock **released**, so a callback
  may re-enter the manager.

---

## Printer: switching between GutenPrint and the native driver

The requirement is that an application can choose GutenPrint or the OS's own
driver **on Linux, Windows and macOS alike**. Getting there needs one
distinction the earlier draft of this module missed.

### GutenPrint is two separate things

| | What it is | Where it runs |
|---|---|---|
| **libgutenprint** | Portable C library. Turns a raster page + a model name + a parameter set into the printer's **native command stream** (ESC/P2, PCL, BJL, dye-sub). | Anywhere it compiles — Linux, macOS, Windows |
| **The CUPS driver** (`rastertogutenprint` + PPDs) | A CUPS filter that wraps the library | Linux and macOS only |

Defining "the GutenPrint backend" as *CUPS with a GutenPrint PPD* — which is
what the earlier draft did — makes it structurally impossible on Windows,
because Windows has no CUPS. The library underneath has no such limitation.

### So: separate the renderer from the transport

**Renderer** — who turns the page into bytes the printer understands.
**Transport** — how those bytes reach the device. The transport is a property
of the platform; the renderer is the user's choice.

```cpp
enum class IOPrintRenderer {
    Auto,         // GutenPrint when it supports this model, else Native
    Native,       // the OS driver: CUPS PPD filter chain, or the Windows driver
    GutenPrint,   // libgutenprint in-process, output sent as a raw job
    IPP           // driverless — the printer itself renders PWG Raster / PDF
};
```

| Renderer | Linux transport | macOS transport | Windows transport |
|---|---|---|---|
| `Native` | CUPS job | CUPS job | `StartDocPrinter` (driver datatype) |
| `GutenPrint` | CUPS **raw** job (`application/vnd.cups-raw`) | CUPS **raw** job | `StartDocPrinter` with `pDatatype = "RAW"` |
| `IPP` | IPP over HTTP | IPP over HTTP | IPP over HTTP |

The GutenPrint renderer is therefore genuinely cross-platform: one rendering
path, three raw-transport shims of a few dozen lines each. Proposed surface
on `PrinterDevice`:

```cpp
std::vector<IOPrintRenderer> GetAvailableRenderers() const;
bool                         IsRendererAvailable(IOPrintRenderer) const;
IODeviceResult               SetRenderer(IOPrintRenderer);
IOPrintRenderer              GetRenderer() const;
```

`GetAvailableRenderers()` answers per device, not per platform: GutenPrint is
only offered when the build has it **and** it recognises that printer model.
`Auto` prefers GutenPrint where both hold, because its parameter set is the
richer one.

The GutenPrint parameter model — media type → resolution → cartridge →
inkset → duplex, in that priority order — applies when the renderer is
`GutenPrint`. Under `Native`, the subset with PPD or DEVMODE equivalents is
mapped across and the rest reports `NotSupported` rather than silently
succeeding.

### Open decision: linked or subprocess

**libgutenprint is GPL-2.0-or-later. UltraCanvas is MIT.** Linking it means
the distributed binary is GPL. That is a product decision, not a technical
one, and it must be made before the GutenPrint renderer is written.

| | Linked (`libgutenprint`) | Subprocess (GutenPrint's own tools) |
|---|---|---|
| Quality / control | Full: every parameter, in-process | Good: whatever the CLI exposes |
| Licence effect | Distributed binary becomes GPL | None — matches this repo's existing "runtime, not linked" pattern (QEMU, Wine in `Docs/Dependencies.md`) |
| Windows | Needs an MSYS2/MinGW build of the library | Needs the GutenPrint binaries shipped alongside |
| Failure mode | Link error if absent | Clean: renderer simply not offered |

**Recommendation:** subprocess, gated on `ULTRACANVAS_HAS_GUTENPRINT`, with
`Native` as the fallback whenever GutenPrint is absent. It keeps the
framework MIT and matches how this repository already handles GPL tools. If
ULTRA OS ships under GPL anyway, linking is the better technical answer and
nothing else in this design changes — only the renderer's internals.

Either way `libgutenprint`/GutenPrint must be added to `Docs/Dependencies.md`,
`master_dependencies.yaml` and `THIRD_PARTY_LICENSES.md` before the renderer
lands.

### Enumeration double-counting

CUPS already exposes IPP Everywhere queues, so a driverless printer is
reachable through both the CUPS and the IPP enumerator. The registry's
first-backend-wins rule collapses those only when both report the same device
id, which they do not by default. The printer backends must therefore derive
their device ids from something stable and shared — the device URI, or the
printer's UUID from its IPP attributes — rather than from the queue name.

---

## Usage

```cpp
#include "IODeviceManager/UltraCanvasIODeviceManager.h"

auto& manager = UltraCanvas::IODeviceManager::GetInstance();
manager.Initialize();

manager.EnumerateDevices(UltraCanvas::IODeviceCategory::Scanner);

for (const auto& info : manager.GetDeviceInfos(UltraCanvas::IODeviceCategory::Scanner)) {
    std::cout << info.name << " via " << info.backend << "\n";
}

if (auto device = manager.GetDevice(UltraCanvas::IODeviceCategory::Scanner, 0)) {
    if (auto result = device->Connect()) {
        // ... category-specific work through the derived class ...
        device->Disconnect();
    } else {
        std::cerr << result.message << "\n";
    }
}

manager.Shutdown();
```

Hot-plug:

```cpp
manager.SetDeviceChangeCallback(
    [](UltraCanvas::IODeviceChange change, const UltraCanvas::IODeviceInfo& info) {
        // Runs on the thread that caused the change — do not block here.
    });
```

Adding a device the manager did not enumerate — a test double, or hardware
behind an application's own backend — is `RegisterDevice(std::shared_ptr)`.

---

## What is not built yet

The foundation is in. Categories land one slice at a time, each one
compiling, tested and wired into CI before the next starts:

| Slice | Contents |
|---|---|
| 1 ✅ | `IODevice`, `IODeviceManager`, device-generic types, tests |
| 2 | `CameraDevice` + V4L2 (Linux) + a DemoApp example |
| 3 | `ScannerDevice` + SANE (Linux) |
| 4 | `PrinterDevice` + CUPS native renderer (Linux) |
| 5 | GutenPrint renderer + raw transport, all three platforms |
| 6 | Windows and macOS backends for slices 2–4 |
| 7 | IPP / eSCL driverless, network cameras |

Contributions of earlier prototype code should be re-landed through these
slices rather than dropped in whole: a large drop that does not compile
against the current tree costs more to review than it saves.
