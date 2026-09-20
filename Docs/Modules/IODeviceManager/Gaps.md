# IODeviceManager — what is not built yet

Companion to [Architecture.md](Architecture.md), which describes the design.
This file tracks the distance between that design, the claims in
[README.md](README.md), and the code actually in the tree.

It exists because the module's documentation ran well ahead of its code: the
overview that preceded this file marked Scanner and Camera "✅ 100% Complete,
production-ready, ~11,525 lines" for a module that had no source at all. Read
a ✅ below as "in the tree, compiled and tested", and nothing else.

Last reviewed: 2026-09-19 (after the GutenPrint renderer).

---

## Legend

| Mark | Means |
|---|---|
| ✅ | In the tree, compiles, covered by a test |
| 🔨 | Started, incomplete — details in the row |
| ❌ | Not written |
| ⚠️ | Written elsewhere but not usable as-is — a rewrite, not a port |
| ❓ | Blocked on a decision, not on work |

---

## Foundation

| Item | State | Notes |
|---|---|---|
| `IODevice` base, lifecycle, error slot | ✅ | |
| `IODeviceManager` registry, enumerator merge | ✅ | |
| `IODeviceResult`, device-generic types | ✅ | |
| Foundation tests against a fake backend | ✅ | `Tests/IODeviceManagerTest` |
| Hot-plug watching (`IDeviceWatcher`, `StartMonitoring`) | ✅ | |
| udev watcher (Linux) | ✅ | filtered in-kernel, `add`/`remove` only, 250 ms coalescing |
| **Hot-plug watchers for Windows and macOS** | ❌ | `WM_DEVICECHANGE` and IOKit notifications. Without one, `StartMonitoring()` reports `BackendUnavailable` there and a caller rescans on its own schedule. |
| **Permission model** | ❌ | macOS gates camera and microphone behind TCC, Windows behind capability prompts, Linux behind udev rules and group membership. `IODeviceResultCode::AccessDenied` exists; nothing requests permission or reports why it was refused. `UltraCanvasAudioDevices` already has `MicrophonePermission` + `RequestMicrophonePermission` — generalise that, do not reinvent it. |

---

## Device classes

| Class | State |
|---|---|
| `PrinterDevice` | ✅ |
| `CameraDevice` | ✅ |
| `ScannerDevice` | ✅ |

---

## Scanner

Previously the largest hole: advertised as finished across five protocols with
no scanner source in the repository at all. `ScannerDevice` and the SANE
backend now exist; the other four protocols do not.

| Item | State |
|---|---|
| `ScannerDevice` + scanner types (resolution, colour mode, scan area, ADF, duplex) | ✅ |
| SANE (Linux) | ✅ enumeration, option walk, page loop, cancellation |
| WIA (Windows) | ❌ |
| TWAIN (Windows) | ❌ |
| ICA (macOS) | ❌ |
| **eSCL / AirScan driverless network scanning** | ✅ one backend for Linux, macOS and Windows, because eSCL is plain HTTP and XML. Reads `ScannerCapabilities`, posts a `ScanJobs` document, collects pages from `NextDocument` and cancels with `DELETE`. Lives in `core/`, not under `OS/`, and sits alongside SANE rather than replacing it. |
| eSCL: discovery on Windows | ❌ the scanning works there, but `Plugins/UltraNet/mdns`'s Windows browse is a stub — a raw `DnsQuery_W` for PTR records that returns no host, port or TXT. Until that is finished, a Windows scanner is reached by naming it in `ULTRACANVAS_ESCL_SCANNERS`. Finishing it (`DnsServiceBrowse`) would also serve IPP driverless printing, which needs the same discovery. |
| eSCL: PDF pages | ❌ JPEG and PNG are decoded; a scanner asked for `application/pdf` would need the PDF plugin to rasterise it. The backend asks for an image format it can decode rather than accepting one it cannot. |
| eSCL: HTTPS with a self-signed certificate | ❌ `_uscans._tcp` is browsed and an `https://` base URL is built, but scanners generally present self-signed certificates and nothing yet opts into accepting them. |
| Multi-page ADF, capability detection | ✅ |
| Preview mode | ❌ detected but not exposed |

---

## Camera

| Item | State |
|---|---|
| **V4L2 webcam backend (Linux)** | ✅ enumeration, capability walk, mmap streaming capture, UVC controls |
| `CameraDevice` implementation | ✅ |
| DSLR: gphoto2 / WIA / ImageCapture | ⚠️ rewrite — see the device-class note above |
| Webcam: MediaFoundation / AVFoundation | ⚠️ rewrite |
| Network camera (RTSP) | ⚠️ rewrite, and it calls libav\* and raw sockets directly; it should go through `libspecific/Video/IVideoBackend.h` and UltraNet |
| ONVIF discovery | ❌ described as "framework ready", which meant not implemented |
| PTZ control, preset positions | ❌ same |
| Video recording / encoding | ❌ |

---

## Printer

The type vocabulary that exists for this category is genuinely good — the
GutenPrint parameter model in particular. The behaviour behind it is mostly
absent.

| Item | State |
|---|---|
| Printer type vocabulary (paper, media, quality, duplex, GutenPrint params) | ✅ |
| `PrinterDevice` + renderer selection | ✅ |
| CUPS backend: enumeration, capabilities, status, supplies, jobs (Linux/macOS) | ✅ |
| CUPS transport, driver documents and raw streams | ✅ |
| Option resolver in GutenPrint priority order | ✅ replaces the `FIXME` that returned its input unchanged |
| **GutenPrint renderer** | ✅ run as a subprocess, never linked, so the framework stays MIT. Matches a printer to one of GutenPrint's ~3,500 models, gets that model's PPD from GutenPrint's own driver program, rasterises the page and pipes it through `rastertogutenprint`. Produces a device-native stream, so it travels as a raw job on all three platforms. |
| GutenPrint: colour separation to CMYK/KCMY | ❌ deliberately. GutenPrint is handed RGB and does its own separation against the ink set, which is better than anything we would do. Listed so the choice is visible, not because it is missing. |
| GutenPrint: shipping the tools on Windows | ❌ the renderer works there and the RAW transport carries it, but nothing installs GutenPrint on Windows. Needs the binaries packaged beside the application and `ULTRACANVAS_GUTENPRINT_DRIVER`/`_FILTER` pointed at them. |
| GutenPrint: streaming a long document | ❌ the whole rasterised document is held in memory before the filter runs, because `RunProcessCaptured` takes its input as one block. A page of RGB at 360 dpi is ~36 MB, so this suits letters and photographs and would not suit a book. Fixed by teaching the process runner to pull input a block at a time, after which one page need exist at once. |
| GutenPrint: per-model options beyond the PPD defaults | 🔨 media, resolution, colour mode and page size reach the raster header; the cartridge and inkset parameters in `IOPrintOptions` are resolved but not yet passed through as PPD options. |
| Windows spooler backend: enumeration, capabilities, status, job queue | ✅ |
| Windows RAW transport (`StartDocPrinter`, datatype `RAW`) | ✅ this is the path GutenPrint uses |
| **Windows GDI renderer** | ✅ `Native` now works on Windows. Prints raster images and plain text by drawing onto a printer DC; honours paper size, orientation, copies, collation, colour mode, duplex and quality through a driver-validated `DEVMODE`. |
| Windows GDI renderer: PDF and other documents | ❌ refused by name with `NotSupported`, not half-printed. Needs the PDF plugin to paginate; the seam it would plug into is done. |
| Windows GDI renderer: images from memory | ❌ the document loader is path-based, so a job carrying image bytes rather than a path is refused rather than spooled through a temp file behind the caller's back. |
| XPS print path | ❌ GDI covers every driver Windows will show; XPS would matter for an XPS-only device or for higher-fidelity transparency. |
| Windows printer maintenance | ❌ |
| **macOS printing** | 🔨 printing itself works through the CUPS backend, which is built for macOS as well as Linux, and the print panel is wired up (`NSPrintPanel` → `NSPrintInfo` → `IOPrintOptions`). What is missing is duplex, which is not on `NSPrintInfo` at all — it lives in the `PMPrintSettings` underneath — so it is left at its default rather than guessed. Untested on real hardware. |
| Paper size recognition | ✅ by dimensions from the CUPS dest-info API, replacing the prior stub that returned A4 for every size a printer reported |
| Page rendering (document/image → page raster) | ✅ `RasterPageTarget` draws an `IPrintPageSource` onto an off-screen surface — the same page sources the GDI path uses, so text wrapping and pagination are shared rather than reimplemented. |
| Page ranges | ✅ `IOPrintJob::pageRange` had been declared since the module was written and read by nothing, so a range chosen in a print dialog was dropped between the dialog and the queue. It now reaches the payload, and from there the IPP `page-ranges` attribute under CUPS and the page loop on the GDI path. |
| IPP: real mDNS/DNS-SD discovery | ❌ the prior version piggybacked on CUPS and only found what CUPS already knew |
| IPP: full `Get-Printer-Attributes` parsing | ❌ |
| SNMP supply/component monitoring | ⚠️ exists outside the tree; needs net-snmp declared, and should be reconsidered against UltraNet |
| Maintenance (cleaning, alignment, nozzle check) | ⚠️ two competing architectures were written; pick one. Both shell out to `escputil` with the device path interpolated unquoted into `popen` — fix before landing. |
| Cloud printing | ❌ architecture only |
| **Integration with `UltraCanvasNativeDialogs::ShowPrintDialog()`** | ✅ the dialog now asks (`RequestPrintSettings()` → `NativePrintResult`) and the job goes through `PrinterDevice`, so the printer, copies, collation, paper size, orientation, duplex, quality and page range the user picked are the ones the queue receives. Texter and UltraFiler keep the `bool` signature they already call. |
| Print dialog: "Print to File" destination | ❌ reported in the result (`printToFile`, `outputFilePath`) and refused by name, rather than silently spooled to a queue the user did not choose. Needs a renderer that writes a document rather than submitting one. |
| Print dialog: printing anything but plain text | ❌ `PrintTextWithDialog()` is the only bridge; an image or a PDF from a dialog needs the same treatment, and for PDF the renderer gap two rows up. |
| Print dialog on Android | ❌ `RequestPrintSettings()` reports cancelled. Android prints through a Java-side `PrintManager` job, not a settings dialog that hands choices back; bridging it is a JNI slice. |
| Print dialog on WASM | ❌ and will stay so: the browser's print UI neither reports what the user chose nor lets a page choose for them, by design. `ShowPrintDialog()` there still hands the text to the browser to print. |

**Types referenced by the prior printer code but defined nowhere:**
`IOPrinterCapabilities`, `IOPrintJobStatus`, `IOPrintJobInfo`,
`IOPaperTrayStatus`, `IOImageData`, `IOColorMode`.

---

## Categories not started

`README.md` advertises 19. Beyond scanner, camera and printer:

Storage · NetworkAdapter · Display · HID/Gamepad · GPIO · Serial ·
Bluetooth · Barcode · RFID · Biometric · Keyboard · Mouse · Touchscreen ·
Stylus

### ❓ Microphone and Speaker — a decision, not work

`UltraCanvasAudioDevices` already ships `ListInputDevices` /
`ListOutputDevices`, `GetDefaultInputDevice` / `GetDefaultOutputDevice`,
microphone permission and backend reflection, and
`UltraCanvasAudioRecorder` / `UltraCanvasAudioPlayer` are built on it.

Either IODeviceManager wraps that API for its Microphone and Speaker
categories, or those categories come out of the README. Two separate answers
to "list my microphones" is the worst of the three options.

---

## Cross-cutting

| Item | State |
|---|---|
| CMake feature detection (libsane, libgphoto2, libcups, v4l2, net-snmp, gutenprint) and the matching `ULTRACANVAS_HAS_*` flags | 🔨 added per backend as each lands |
| **Dependency records** for libgphoto2 and net-snmp | ❌ absent from `Docs/Dependencies.md`, `master_dependencies.yaml` and `THIRD_PARTY_LICENSES.md`. GutenPrint is now recorded in all three, including why it is run rather than linked; SANE, V4L2, WIA, TWAIN, ICA, CUPS and FFmpeg were already there. |
| Per-category documentation | 🔨 |
| DemoApp examples | ❌ repo pattern is `Apps/DemoApp/UltraCanvas<Thing>Examples.cpp`; the prototypes were standalone `main()` programs |
| Per-category tests against fakes | 🔨 foundation done, categories follow |
| **Device-picker UI** | ❌ no scanner/camera/printer chooser exists. When one is built it must be assembled from the element catalogue — see the prohibition in `AGENTS.md`. |

---

## ❓ Open decisions

1. ~~**GutenPrint: linked or subprocess.**~~ **Decided: subprocess.** The
   framework stays MIT and GutenPrint is run rather than linked, matching the
   QEMU/Wine pattern. Built; see
   [Architecture.md](Architecture.md#decided-subprocess-not-linked).
2. **Audio categories** — wrap `UltraCanvasAudioDevices`, or drop Microphone
   and Speaker from the README. See above.
3. **SNMP transport** — net-snmp as a new dependency, or build the Printer-MIB
   walk on UltraNet's existing UDP sockets.

---

## Re-landing the prototype code

A large body of printer, camera and SNMP code was written against an earlier,
incompatible shape of this module. It is being folded back in one slice at a
time rather than dropped in whole: the type vocabularies and the protocol call
sequences are worth keeping, the routing and glue are not. A drop that does
not compile against the tree costs more to review than it saves.
