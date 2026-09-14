# IODeviceManager — Architecture

Module status: **foundation, printers (CUPS + Windows), cameras (V4L2) and
scanners (SANE) landed; more backends in progress.**
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
| `Native` | CUPS job ✅ | CUPS job ✅ | needs the GDI renderer — see below |
| `GutenPrint` | CUPS **raw** job (`application/vnd.cups-raw`) ✅ | CUPS **raw** job ✅ | `StartDocPrinter`, `pDatatype = "RAW"` ✅ |
| `IPP` | IPP over HTTP | IPP over HTTP | IPP over HTTP |

A transport declares what it can carry, and a renderer declares what it
emits; `PrinterDevice` only offers the pairings that match. The two checks are
symmetric — `IPrintTransport::SupportsRaw()` against
`IPrintRenderer::ProducesRawStream()`, and `SupportsDocument()` against
everything else.

That second one matters on Windows. CUPS has a filter chain, so a PDF can be
handed over as-is and the native renderer is a pass-through. **The Windows
spooler has no equivalent**: it takes device-ready data, or EMF/XPS produced
by drawing to a printer DC. So `WindowsPrintTransport::SupportsDocument()` is
`false` until that renderer is written, and the Native renderer is not offered
on Windows — a caller learns this from `GetAvailableRenderers()` instead of
from a job that disappears. The raw path is complete, which is what
GutenPrint needs.

The GutenPrint renderer is therefore genuinely cross-platform: one rendering
path, three raw-transport shims of a few dozen lines each. Proposed surface
on `PrinterDevice`:

```cpp
std::vector<IOPrintRenderer> GetAvailableRenderers() const;
bool                         IsRendererAvailable(IOPrintRenderer) const;
IODeviceResult               SetRenderer(IOPrintRenderer);
IOPrintRenderer              GetRenderer() const;
```

`GetAvailableRenderers()` answers per device, not per platform. A renderer is
offered only when all four hold: it is compiled in, its library or tool is
present (`IsAvailable()`), it recognises this printer (`SupportsPrinter()`),
and — for one that emits a device-native stream (`ProducesRawStream()`) — the
platform transport can carry a raw job. `Auto` prefers GutenPrint where all
four hold, because its parameter set is the richer one.

`SetRenderer()` **refuses** a renderer that is not available rather than
accepting it and falling back at print time: a caller that asked for
GutenPrint needs to know it is not getting it.

### Option resolution

The GutenPrint parameter model — media type → resolution → cartridge → inkset
→ duplex, in that priority order — is implemented in `ResolvePrintOptions()`
and applies whichever renderer is chosen: the parameters are a property of how
printing works, not of one driver.

The order matters because the parameters are not independent. Media outranks
resolution, so asking for 2880 dpi on plain paper yields High, not a silent
substitution discovered in the output tray; photo black ink on plain paper
becomes matte black; a colour inkset is dropped for monochrome output.

Every substitution is reported through the `changes` out-parameter in words
meant for a user — *"A3 is not supported, using A4"* — so a print dialog can
say what it had to alter. `PrinterDevice::ResolveOptions()` exposes the same
answer before a job is sent.

**An unreported capability is not a refusal.** An empty capability list means
the backend did not say, not that nothing is supported, and the three-valued
`IOSupport` (`Unknown`/`No`/`Yes`) carries the same distinction for the
booleans. Only an explicit `No` constrains anything. A plain `bool` cannot
tell "this printer has no duplex unit" from "we could not read this printer's
capabilities", and conflating them strips options from a printer that would
have accepted them.

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
| 2 ✅ | `PrinterDevice`, renderer/transport seam, option resolver, CUPS backend (Linux + macOS), tests |
| 3 ✅ | Windows spooler backend: enumeration, capabilities, status, job queue, RAW transport |
| 4 | GutenPrint renderer — blocked on the licence decision above. With slices 2 and 3 in, this is one class and no other change on any platform. |
| 5 ✅ | `CameraDevice` + V4L2 (Linux) |
| 6 ✅ | `ScannerDevice` + SANE (Linux) |
| 7 | Windows GDI/XPS renderer, so `Native` works there too |
| 8 | Windows and macOS backends for camera and scanner |
| 9 | IPP / eSCL driverless, network cameras |
| 10 | Hot-plug watchers and the permission model |

Slices 2 and 3 ship the switch and both transports. Adding GutenPrint is then
a renderer class and nothing else: no change to `PrinterDevice`, and no change
to any transport, since `CupsPrintTransport` and `WindowsPrintTransport` both
already submit raw jobs. That is the whole point of having split the two.

Contributions of earlier prototype code should be re-landed through these
slices rather than dropped in whole: a large drop that does not compile
against the current tree costs more to review than it saves.

---

## Cameras

`CameraDevice` follows the same non-virtual-public / virtual-protected shape
as the lifecycle: callers use `StartStream()`/`StopStream()`, backends
implement `DoStartStream()`/`DoStopStream()` and call `DeliverFrame()` from
their capture thread.

Two rules carry the weight, and both are asserted in
`Tests/IODeviceCameraTest`:

- **No frame reaches a callback after `StopStream()` returns.** `StopStream()`
  clears the streaming flag first, so a capture loop testing
  `ShouldKeepStreaming()` winds down, then `DoStopStream()` joins the thread.
  By the time it returns, whatever the callback captured is safe to destroy.
- **A backend stops its own thread in its own destructor.** `~CameraDevice()`
  calls no virtuals, for the same reason `~IODevice()` does not: the derived
  object is already gone, so a thread still calling `DeliverFrame()` would be
  reading freed memory.

`DeliverFrame()` deliberately does not take `deviceMutex` — `StopStream()`
holds it while joining the capture thread, so locking there would deadlock.
The two fields it touches are atomic, and the callback is cleared only after
the join.

### Controls

Controls are enumerated, not declared as a struct of booleans:

```cpp
CameraControlRange range = camera->GetControlRange(CameraControl::Exposure);
if (range.supported) {
    camera->SetControl(CameraControl::Exposure, sliderPosition);  // clamped and stepped
}
```

Which controls exist is a property of the device. A struct with one field per
control has to guess the union of every camera in advance and still cannot say
whether a given camera has one — `CameraCapabilities::controls` lists only
what this camera actually reports, so iterating it enumerates them.
`SetControl()` clamps to the range and snaps to the step, so a caller can pass
a raw slider position; `V4L2_CID_EXPOSURE_ABSOLUTE` with minimum 3 and step 4
accepts 3, 7, 11 — not 0, 4, 8.

### Configuration

`SetConfiguration()` refuses a format/resolution pair the camera does not
offer rather than accepting it and capturing something else, which a caller
would discover only by inspecting frames. `ResolveConfiguration()` fills in
what was left unset — preferring an uncompressed format so pixels are readable
without a decoder, and the largest resolution that format offers — and reports
what it chose, the same contract `ResolvePrintOptions()` has.

An empty resolution list means the driver did not enumerate them (some report
a continuous range instead), so it reads as "did not say" rather than
"supports none" — the same rule the printer capabilities follow.

### V4L2 backend

Enumerates `/dev/video*`, skipping nodes without `V4L2_CAP_VIDEO_CAPTURE`:
modern kernels give one camera several nodes, and the metadata ones would
otherwise appear as cameras that never yield a frame. Capability walk is
`VIDIOC_ENUM_FMT` → `ENUM_FRAMESIZES` → `ENUM_FRAMEINTERVALS`, controls via
`VIDIOC_QUERYCTRL`, capture via mmap'd buffers. The device is opened
non-blocking with `poll()` supplying the timeout, so a stalled camera cannot
wedge the caller, and every ioctl retries on `EINTR` — a signal is not a
device error. Frames carry the driver's own timestamp rather than a clock read
in the callback, which has already drifted from when the sensor was exposed.

---

## Scanners

The page loop is where a scanner API earns or loses its keep. A feeder run
ends when the tray empties, and a backend signals that the only way it can —
by not producing a page — which is also how it signals a failure. Conflating
the two throws away every page already scanned, so `DoScanPage()` returns
`DeviceNotFound` for an empty feeder specifically, and `ScanPages()` treats
that as a normal end once at least one page has come through.

A run ends in five ways, all covered by `Tests/IODeviceScannerTest`: the tray
empties, the caller's callback returns false, `maxPages` is reached,
`CancelScan()` is called, or the source is a flatbed, which has one page by
definition. The page count rides back in `IODeviceResult::backendCode` even
on a cancelled or failed run, so a caller knows what it got.

`CancelScan()` deliberately takes no lock. The scanning thread holds
`deviceMutex` for the whole run, so a cancel that waited for it could never
arrive in time to cancel anything.

### Configuration

Colour mode and paper source are enumerations — a scanner either has them or
does not — so an unsupported one is refused. Resolution is a number on a
scale, and scanners expose arbitrary values, so it is **snapped** to the
nearest offered rather than refused: turning down 301 dpi on a device that
does 300 helps nobody. `NearestResolution()` prefers the closest value at or
*below* the request, because scanning higher costs time and memory
quadratically and is not a substitution to make silently.

### SANE backend

Enumerates with `local_only` false so networked backends (`net`, `escl`,
`airscan`) are included — a driverless network scanner is the common case now.
Options are walked by name rather than index, because backends order them
freely, and sources are matched by substring since "ADF Duplex", "Duplex ADF"
and "Automatic Document Feeder" all mean the same thing. Geometry crosses from
SANE's fixed-point millimetres to this module's hundredths of a millimetre in
one place rather than at every call site. `sane_init`/`sane_exit` are
process-global and not reference-counted by the library, so the count is kept
in the backend: a second scanner opening must not re-init, and the first one
closing must not tear the library out from under the others.
