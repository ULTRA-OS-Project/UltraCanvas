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
| `Native` | CUPS job ✅ | CUPS job ✅ | GDI drawing session ✅ |
| `GutenPrint` | CUPS **raw** job (`application/vnd.cups-raw`) ✅ | CUPS **raw** job ✅ | `StartDocPrinter`, `pDatatype = "RAW"` ✅ |
| `IPP` | IPP over HTTP | IPP over HTTP | IPP over HTTP |

A transport declares what it can carry, and a renderer declares what it
emits; `PrinterDevice` only offers the pairings that match. The checks are
symmetric, one per shape a payload can take:

| Renderer says | Transport must say | Payload carries |
|---|---|---|
| `ProducesRawStream()` | `SupportsRaw()` | the device's own command stream |
| `ProducesPageSource()` | `SupportsPageSource()` | pages to be **drawn** |
| neither | `SupportsDocument()` | a document for the platform's driver |

### The third shape: pages, not bytes

The first two rows were enough until Windows. CUPS has a filter chain, so a
PDF is handed over as-is and the native renderer is a pass-through. **The
Windows spooler has no equivalent**: it takes device-ready data, and that is
all. A document reaches a Windows printer only by being *drawn* — `CreateDC`,
`StartDoc`, then per page `StartPage`, GDI calls, `EndPage`, and the driver
turns those calls into the device's own commands.

That is not a byte stream at any point, so it does not fit a payload of
`filePath` or `data`. Forcing it to would mean inventing a container — a
multi-page EMF wrapper — that exists only to be unwrapped again three
function calls later. Instead the payload gained a third member,
`IPrintPageSourcePtr pages`, and the transport drives it.

So `WindowsPrintTransport::SupportsDocument()` is still `false`, and that is
not a gap any more — it is what Windows is. `SupportsPageSource()` is `true`,
and that is what makes `Native` available there.

Two consequences worth stating, because both are load-bearing:

**Pagination happens against the device, not before it.** `IPrintPageSource`
has a `Prepare(IPrintPageTarget&)` that runs once with the real target before
the page count is known, because the same text is a different number of pages
on A4 at 600 dpi than on Letter at 300. A source cannot honestly answer
"how many pages?" until it has met the printer.

**The layout is platform-neutral on purpose.** Fitting, wrapping and
pagination live in `core/` and reach the device only through the abstract
`IPrintPageTarget`, so they are exercised by `Tests/IODevicePrinterTest`
against a fake target with a synthetic font — on every platform in the
matrix, not only the one that needs them. What is left in `OS/MSWindows` is
the part that genuinely needs Win32: the DC, the `DEVMODE`, the font handles
and `StretchDIBits`.

> One trap, recorded so it is not reintroduced: the target's method is
> `DrawTextLine`, not `DrawText`, because `<windows.h>` defines `DrawText` as
> a macro. A virtual named `DrawText` is silently renamed by the preprocessor
> in any translation unit that includes windows.h, so the override stops
> overriding and the class turns abstract — and the compiler blames the
> override, not the macro. `GetLastError()` cost this module the same lesson.

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

### The print dialog asks; PrinterDevice prints

`UltraCanvasNativeDialogs::ShowPrintDialog()` shipped long before this module
did, and Texter and UltraFiler both call it. Each platform printed by itself:

- **Linux** built a real GTK print dialog, read `GtkPrintSettings` and
  `GtkPageSetup` out of it, used **neither**, and ran
  `lpr -P "<printer>" "<tempfile>"` through `system()`.
- **Windows** showed no print dialog at all. It wrote a temp file and invoked
  the shell's `print` verb, so the dialog the user saw was Notepad's and its
  settings never came back. The temp file was left behind deliberately.
- **macOS** wrote the text to a fixed path in the temp directory — the same
  path on every call, so two documents printed in quick succession raced for
  it — handed it to `NSWorkspace`, and returned `true` without waiting for
  anything.

So on every platform the user chose copies, collation, paper size,
orientation, duplex and a page range, and every one of those was discarded.

`PrinterDevice` already honours all of them. The fix is therefore not more
printing code but **less**: the dialog's job is narrowed to *asking*.

```cpp
NativePrintResult chosen =
    UltraCanvasNativeDialogs::RequestPrintSettings("report.txt", window);

if (chosen.IsOK()) {
    IODeviceResult printed =
        PrintTextWithSettings(chosen, "report.txt", text);
}
```

`NativePrintResult` is `IOPrintDialogChoice` — the printer's name plus an
`IOPrintOptions` and a page range. It is **not** a new vocabulary of
copies/paper/duplex enums: a print dialog exists to produce a print job, and
`IOPrintOptions` is what `PrinterDevice::Print()` takes, so anything else
would be a type to convert rather than a type to use. It is defined in the
printer types rather than beside the other dialog results because the dialog
header reaches the whole widget stack through `UltraCanvasModalDialog.h`, and
IODeviceManager depends on no UI.

Three things follow from doing it this way:

**One rule for what a sheet of paper is.** GTK quotes paper in millimetres,
Win32 in tenths of a millimetre through a `DEVMODE`, AppKit in points. All
three convert to hundredths of a millimetre and go to the same
`IOPaperSizeFromDimensions()` the CUPS backend already used — a size is
recognised by its measurements, not by the name the printer, driver or
desktop gave it. On Windows the `DMPAPER_*` code is not mapped by hand but
looked up in the *driver's own* table through `DeviceCapabilities`, because
the codes do not all line up: `DMPAPER_B4` is JIS B4 at 257×364 mm while ISO
B4 is 250×353. A size with no name in the table is carried by its
measurements as `Custom` rather than substituted for A4.

**The page range finally goes somewhere.** `IOPrintJob::pageRange` had existed
since the module was written and was read by nothing — the payload had
nowhere to carry it, and a transport only ever sees the payload. It now
reaches `IOPrintPayload`, and from there the IPP `page-ranges` attribute under
CUPS and the page loop on the GDI path, where it is applied *after*
pagination because that is the first moment "pages 2–4" can be checked
against a document that has pages.

**What is testable is separated from what needs a dialog.** Matching the name
a dialog returns to a device, and turning a dialog answer into a job, are
ordinary functions over data; they live in
`UltraCanvasIODevicePrintDialog.cpp` and are covered by the printer tests. The
half that names `RequestPrintSettings()` is a separate translation unit, so a
reference to a platform dialog never enters the test's link line.

Two things are reported rather than acted on. **"Print to File"** comes back
as `printToFile` and is refused by name, because silently spooling to a queue
the user did not choose is the worse of the two wrong answers. **Duplex on
macOS** is left at its default: it is not on `NSPrintInfo` at all — it lives
in the `PMPrintSettings` underneath — and claiming a value would be inventing
one.

### Decided: subprocess, not linked

**libgutenprint is GPL-2.0-or-later. UltraCanvas is MIT.** Linking it would
make every distributed binary a GPL work. That was a product decision rather
than a technical one, and it has been taken: **GutenPrint is run, not linked.**

| | Linked (`libgutenprint`) | **Subprocess (chosen)** |
|---|---|---|
| Quality / control | Full: every parameter, in-process | Good: whatever the tools expose — which is the whole PPD |
| Licence effect | Distributed binary becomes GPL | None. Matches this repo's existing "runtime, not linked" pattern (QEMU, Wine) |
| Windows | Needs an MSYS2/MinGW build of the library | Needs the GutenPrint binaries shipped alongside |
| Failure mode | Link error if absent | Clean: the renderer is simply not offered |

GutenPrint ships two programs, and between them they are a complete interface:

| Program | What it does |
|---|---|
| `gutenprint.5.3` | `list` names every model it drives — about 3,500 — each with its IEEE-1284 device id. `cat <uri>` writes that model's PPD. |
| `rastertogutenprint.5.3` | Reads a page of CUPS raster on standard input, writes the printer's own command language on standard output. |

So the renderer lays the document out, rasterises it, pipes the raster
through the filter, and the bytes that come back **are** the payload. They are
a device-native stream, so `ProducesRawStream()` is true and they travel as a
raw job — `application/vnd.cups-raw` under CUPS, datatype `RAW` through the
Windows spooler. **Both raw transports already existed, so this added one
renderer and changed no transport**, which is what the renderer/transport
split was for.

Four things are worth keeping in mind about the implementation.

**A page is drawn, not converted.** The same `IPrintPageSource` the Windows
GDI renderer uses — the same wrapped text, the same fitted image, the same
pagination — is driven against a `RasterPageTarget`, which draws onto an
off-screen surface instead of onto a printer device context. One layout, two
destinations; that is what `IPrintPageTarget` being abstract buys. Deciding
*what* a job contains is shared too, in `MakePageSourceForJob`, so the two
renderers cannot drift about which file extensions are text.

**GutenPrint is handed RGB and left to separate it.** Its PPDs also offer CMY,
CMYK and KCMY, but choosing those would mean separating the colour ourselves
against a specific ink set at a specific resolution — the one thing GutenPrint
is unambiguously better at than anything written here. The raster it gets is
RGB (or grey), which is also what its PPDs default to.

**The raster is uncompressed.** The sync word is `RaS3`: version 3,
big-endian, no run-length encoding. A v2 encoder is the kind of code that is
wrong in ways which surface on one printer at one resolution; the raster goes
down a pipe to a filter that reads it immediately, so the size costs nothing
but a moment of memory.

**Input and output are pumped together.** `RunProcessCaptured` polls the
child's stdin, stdout and stderr in one loop with non-blocking pipe ends. This
is not tidiness: `poll()` reporting the pipe writable means *one byte* is
free, so a blocking 64 KB write parks in the kernel until the child drains it
— and if the child is meanwhile blocked writing output nobody is reading,
neither side moves again. That deadlock was hit during development, on the
first page large enough to fill a pipe buffer, which is to say on every real
page and never on a small test.

And one thing that is **not** an implementation detail: `RunProcessCaptured`
takes an argument **list**, and executes the program directly — `execvp`, or
`CreateProcessW` — so no shell ever sees it. The prototype this module
replaces built a command line by pasting a device path into a string and
handing it to `popen()`, which runs a shell. There is nothing to escape here
because nothing parses.

**Windows.** The renderer is built and offered there on the same terms as
anywhere else, and the RAW transport carries what it produces. What Windows
does not have is an installer that puts GutenPrint on the machine, so in
practice the tools have to be shipped beside the application and pointed at
with `ULTRACANVAS_GUTENPRINT_DRIVER` / `ULTRACANVAS_GUTENPRINT_FILTER`.
Absent those, `GetAvailableRenderers()` does not list GutenPrint and `Native`
takes the job.

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
| 4 ✅ | GutenPrint renderer, run as a subprocess — one renderer class, no transport change, on all three platforms |
| 5 ✅ | `CameraDevice` + V4L2 (Linux) |
| 6 ✅ | `ScannerDevice` + SANE (Linux) |
| 7 ✅ | Hot-plug watching + the udev watcher (Linux) |
| 8 ✅ | Windows GDI renderer, so `Native` works there too — images and plain text; PDF pagination and XPS still open |
| 8a ✅ | The OS print dialog wired to `PrinterDevice` on Linux, Windows and macOS, so the settings it collects reach the queue; page ranges carried end to end |
| 9 | Windows and macOS backends for camera and scanner |
| 10 | Hot-plug watchers for Windows and macOS; the permission model |
| 11 🔨 | eSCL driverless scanning ✅ (all three platforms; Windows discovery still open); IPP driverless and network cameras to come |

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

### eSCL backend — one file, every platform

eSCL (Apple calls it AirScan, Mopria calls it Mopria Scan) is what a network
scanner speaks when nobody has installed a driver for it. It is plain HTTP and
XML, and that is exactly why it was built before WIA, TWAIN or ICA: **each of
those is one platform's work for one platform's scanners, while this is one
file in `core/` that serves Linux, macOS and Windows alike.**

Four calls are the whole protocol:

| Call | What it does |
|---|---|
| `GET {base}/ScannerCapabilities` | what the scanner can do |
| `POST {base}/ScanJobs` | start a run; the reply's `Location` names the job |
| `GET {job}/NextDocument` | the next page — or **404 when there are no more** |
| `DELETE {job}` | cancel |

**That 404 is the protocol's way of saying the feeder is empty**, and it lands
exactly on the rule this module already had: `DoScanPage()` returns
`DeviceNotFound`, and `ScanPages()` reads that as the end of a run rather than
a failure — but only once a page has arrived, so a 404 on the very first page
stays the error it is. A job that produced nothing was a bad job, not an empty
tray. The two protocols were designed apart and agree exactly, which is a good
sign the rule was the right one.

A job covers a *run*, not a page, so one is opened only when none is and the
page fetch is what repeats. A flatbed's job is closed as soon as its one page
arrives: left open, the next `Scan()` would fetch from a spent job and read
its 404 as an empty feeder on a device with no feeder.

Two translation units, for the same reason the printer path has them. The
protocol arithmetic — units, colour-mode names, the capability document, the
job URL — is pure data and lives in `...ESCLProtocol.cpp`, which the tests
link without UltraNet or the image stack. What needs a network is next door.

Three things are worth knowing about the data:

**The units are not the module's.** eSCL measures scan regions in
three-hundredths of an inch; everything here is hundredths of a millimetre.
The conversion rounds to nearest in both directions, because a scan area is
derived from a paper size and handed straight back, and truncating twice
leaves A4 a millimetre short.

**The XML is namespace-prefixed and the prefix is the vendor's choice.** One
scanner writes `scan:ColorMode`, another `escl:ColorMode`; the PWG-derived
elements carry `pwg:` or `sm:`. tinyxml2 does not strip prefixes, so every
lookup matches the local name after the last colon.

**`ScanCapabilities::Supports()` cannot be used to build a capability list.**
It answers "would this be accepted", and an empty list means *the backend has
not enumerated yet* — so it says yes to everything. Using it to deduplicate
while filling the list drops the first entry, after which the list is still
empty and so drops every entry. The parser uses `std::find` on the vector.
This is not hypothetical: the first version did exactly that, collected
nothing, and the tests built on `Supports()` passed anyway.

**Discovery is the one part that is not uniform.** `_uscan._tcp` is browsed
through UltraNet's mDNS plugin, which is complete on Linux (Avahi) and macOS
(Bonjour) and a stub on Windows — a raw `DnsQuery_W` for PTR records that
returns no host, port or TXT. So on Windows a scanner is named outright,
through `ULTRACANVAS_ESCL_SCANNERS`, which is also how any scanner on another
subnet is reached, since mDNS does not cross routers. The gap is recorded in
`Gaps.md`; closing it would serve IPP driverless printing too, which needs the
same discovery.

---

## Hot-plug watching

A watcher reports **which category changed, not which device**:

```cpp
class IDeviceWatcher {
    virtual IODeviceResult Start(CategoryChangedCallback onCategoryChanged) = 0;
    virtual void Stop() = 0;
};
```

The manager re-enumerates that category, and the merge `EnumerateDevices()`
already performs works out what appeared or disappeared and fires the change
callback. Nothing else was needed to make hot-plug work — the diffing was
already there for rescans.

That split is what keeps each platform's watcher small. udev, the Windows
device broadcast and IOKit all report kernel-level arrivals in their own
vocabulary, and none of them knows what a `ScannerDevice` is. Translating "a
video4linux node appeared" into "some camera changed" is all they have to do.

```cpp
manager.SetDeviceChangeCallback([](IODeviceChange change, const IODeviceInfo& info) {
    // now fires for real plug and unplug, not only after an explicit rescan
});
manager.StartMonitoring();
```

`SetDeviceWatcher()` substitutes one — for a test, or for an application whose
devices arrive through its own mechanism.

### The locking, which is the whole difficulty

`StopMonitoring()` joins the watcher's thread, and that thread is calling
`EnumerateDevices()`, which takes `registryMutex`. Holding either lock across
the join deadlocks. So monitoring state lives under its own `watcherMutex`,
and both `Stop()` and the join happen with **no lock held**. `Shutdown()`
stops monitoring before it touches the registry for the same reason. The
manager test drives this deliberately — its fake watcher reports from another
thread, because the deadlock only appears when the callback arrives from
somewhere other than the caller's — and the suite is clean under
ThreadSanitizer.

### udev watcher (Linux)

Filters `video4linux`, `usb` and `sound` **in the kernel**: without a filter
every uevent on the machine wakes the thread to be discarded. Only `add` and
`remove` count — a `change` action means a device reported a property, not
that it appeared, and re-enumerating on those makes a busy machine rescan
constantly.

Events are **coalesced over a 250 ms quiet period**. Plugging in one webcam
produces a burst — two or more video4linux nodes plus USB interfaces, each its
own uevent — and re-enumerating per event would rescan the hardware several
times for one physical act.

A `usb` arrival maps to Scanner, Printer *and* Camera, because the kernel says
a USB device appeared, not what it is: at that level a scanner and a printer
look alike, and only SANE or CUPS can say. Re-enumerating all three is cheap
next to guessing wrong, and the merge turns "nothing actually changed" into no
notification.

The poll waits on the udev fd **and an eventfd**, so `Stop()` wakes it
immediately instead of after a timeout — a verified stop takes as long as the
work, not as long as the poll interval.

Where no watcher is compiled in, `CreateDeviceWatcher()` returns null and
`StartMonitoring()` reports `BackendUnavailable`, so a caller can tell "this
platform cannot watch" from "nothing has been plugged in yet".
