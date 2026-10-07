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
| eSCL: discovery on Windows | ✅ `Plugins/UltraNet/mdns` browses with `DnsServiceBrowse` and resolves each instance with `DnsServiceResolve`, so a Windows browse now yields host, port and TXT like the Avahi and Bonjour backends. The entry points are bound at run time, so on Windows older than 10 1703 the module still loads and falls back to the PTR-only query — names without addresses, which a caller skips. **Not yet run on Windows**: the name arithmetic is tested (`Tests/MdnsNamesTest`) and the translation unit compiles and links against `dnsapi`, but nobody has browsed a real network with it. Naming a scanner in `ULTRACANVAS_ESCL_SCANNERS` remains the way to reach one on another subnet. |
| mDNS: `dn` differs between backends | ❌ every backend puts the *full* service name in `dn` ("Office Printer._ipp._tcp.local"), not the instance alone - checked on Avahi against a live advertisement - but Avahi and the Windows backend give it readable while Bonjour passes through the escaped `fullname` it is given. Both consumers now cope with both forms through one helper, `DnsSdInstanceName` (`UltraCanvasIODeviceDnsSd.h`): the IPP backend for every printer's name, eSCL for every scanner's name, ahead of its model (`ty`), which two scanners of one model share. A scanner with no `ty` used to be shown as "Office Scanner._uscan._tcp.local" and is now "Office Scanner" (checked on Avahi against a live advertisement). The plugin's own `dn` is unchanged; making it backend-neutral would mean the plugin reporting the instance itself, through `Mdns::SplitInstanceName`, which lives in the plugin and not in the core library. |
| eSCL: PDF pages | ❌ JPEG and PNG are decoded; a scanner asked for `application/pdf` would need the PDF plugin to rasterise it. The backend asks for an image format it can decode rather than accepting one it cannot. |
| eSCL: HTTPS with a self-signed certificate | ✅ trust on first use: the scanner's key is learned the first time its certificate fails verification and every later connection is pinned to it; a changed key is refused, not relearned. `Tests/IODeviceScannerESCLLiveTest` checks it on every Linux CI run against a TLS-only scanner the test starts (`Tests/IODeviceScannerESCLLiveScanner.py`), including a scanner restarted with a different certificate; CI sets `ULTRACANVAS_TEST_ESCL_REQUIRED`, which turns a skip (no Python 3 or `openssl`) into a failure. Not yet run on Windows or macOS TLS. See [Architecture.md](Architecture.md#self-signed-certificates-trust-on-first-use). |
| eSCL: one scanner, one entry | ✅ fixed. A scanner advertising both `_uscan._tcp` and `_uscans._tcp` was listed twice, because the device id is its URL and the two differ. It is now recognised by the TXT record's `uuid` (or its host when it has none) and listed once, over plain HTTP, with the TLS address in the `escl-tls-url` attribute. Checked against a scanner advertised both ways over Avahi: two entries before, one after. |
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
| GutenPrint: multi-page jobs | ✅ fixed. The CUPS raster writer put a sync word before every page instead of once per stream, so GutenPrint's filter stopped after page 1 without an error and every multi-page job printed its first page only. Confirmed with the real `rastertogutenprint` (1 page printed before, 3 after), and now tested. |
| GutenPrint: colour separation to CMYK/KCMY | ❌ deliberately. GutenPrint is handed RGB and does its own separation against the ink set, which is better than anything we would do. Listed so the choice is visible, not because it is missing. |
| GutenPrint: shipping the tools on Windows | ❌ the renderer works there and the RAW transport carries it, but nothing installs GutenPrint on Windows. Needs the binaries packaged beside the application and `ULTRACANVAS_GUTENPRINT_DRIVER`/`_FILTER` pointed at them. |
| GutenPrint: streaming a long document | ❌ the whole rasterised document is held in memory before the filter runs, because `RunProcessCaptured` takes its input as one block. A page of RGB at 360 dpi is ~36 MB, so this suits letters and photographs and would not suit a book. Fixed by teaching the process runner to pull input a block at a time, after which one page need exist at once. |
| GutenPrint: per-model options beyond the PPD defaults | 🔨 media, resolution, colour mode and page size reach the raster header; the cartridge and inkset parameters in `IOPrintOptions` are resolved but not yet passed through as PPD options. |
| Windows spooler backend: enumeration, capabilities, status, job queue | ✅ |
| Windows spooler backend: supply levels | ✅ from the driver's bidi channel (`IBidiSpl` `GetAll` on `\Printer.Consumables`), and when that is silent, from the printer over IPP at an address read from the queue's port (IPP port URL, a Standard TCP/IP port's `HostAddress`, or for a WSD port the PnP-X `IpAddress` of the WSD device in the queue's device container). The parsing, the port-to-address guesses and the container matching are tested on every platform, and the IPP query against `ippeveprinter`. Not yet verified against real Windows hardware. |
| Windows RAW transport (`StartDocPrinter`, datatype `RAW`) | ✅ this is the path GutenPrint uses |
| **Windows GDI renderer** | ✅ `Native` now works on Windows. Prints raster images and plain text by drawing onto a printer DC; honours paper size, orientation, copies, collation, colour mode, duplex and quality through a driver-validated `DEVMODE`. |
| Windows GDI renderer: PDF and other documents | ⚠️ a PDF on its own is still refused by name with `NotSupported`, not half-printed - laying one out needs the PDF plugin. A PDF that comes with its pages (`IOPrintJob::pages`, as a formatted document's does: `CreateRichDocumentPrintPages`) prints: the pages are drawn. |
| Windows GDI renderer: images from memory | ❌ the document loader is path-based, so a job carrying image bytes rather than a path is refused rather than spooled through a temp file behind the caller's back. |
| XPS print path | ❌ GDI covers every driver Windows will show; XPS would matter for an XPS-only device or for higher-fidelity transparency. |
| Windows printer maintenance | ❌ |
| **macOS printing** | 🔨 printing itself works through the CUPS backend, which is built for macOS as well as Linux, and the print panel is wired up (`NSPrintPanel` → `NSPrintInfo` → `IOPrintOptions`). What is missing is duplex, which is not on `NSPrintInfo` at all — it lives in the `PMPrintSettings` underneath — so it is left at its default rather than guessed. Untested on real hardware. |
| Paper size recognition | ✅ by dimensions from the CUPS dest-info API, replacing the prior stub that returned A4 for every size a printer reported |
| Page rendering (document/image → page raster) | ✅ `RasterPageTarget` draws an `IPrintPageSource` onto an off-screen surface — the same page sources the GDI path uses, so text wrapping and pagination are shared rather than reimplemented. |
| Page ranges | ✅ `IOPrintJob::pageRange` had been declared since the module was written and read by nothing, so a range chosen in a print dialog was dropped between the dialog and the queue. It now reaches the payload, and from there the IPP `page-ranges` attribute under CUPS and the page loop on the GDI path. |
| **IPP driverless printing** | ✅ one backend for Linux, macOS and Windows (`core/`, like eSCL). Discovers `_ipp._tcp` and `_ipps._tcp` over DNS-SD, plus `ULTRACANVAS_IPP_PRINTERS`; sends a document the printer renders as it is, and draws text and images as PWG raster otherwise. Checked against CUPS's reference printer (`ippeveprinter`); **not yet run against a physical printer.** See [Architecture.md](Architecture.md#driverless-printing-the-ipp-backend). |
| IPP: DNS-SD discovery | ✅ its own browse, no longer only what CUPS already knew. With CUPS compiled in, a printer a CUPS queue already reaches is left to CUPS rather than listed twice. |
| IPP: `Get-Printer-Attributes` parsing | ✅ the full RFC 8010 decoder - every value type, collections to any sane depth, and a malformed reply refused rather than read past. Capabilities, status, supplies (`marker-*` and PWG's `printer-supply`) and jobs are read from it. |
| IPP: device ids never matched CUPS's | ✅ found and worked around. The CUPS backend keys on `printer-uuid`, which `cupsGetDests2` does not return (checked, CUPS 2.4.7), so the registry could never collapse a printer found both ways. The IPP backend matches CUPS queues itself instead - see [Architecture.md](Architecture.md#enumeration-double-counting). |
| IPP: the same printer twice on Windows | ✅ a DNS-SD printer at a host a spooler queue prints to (IPP, Standard TCP/IP or WSD port) is left to the queue (`IppPrinterIsWindowsQueue`, tested on every platform). Matched on the mDNS host name and every address the printer answered from: the mDNS plugin reports them all, IPv4 and IPv6, on every backend. Not yet verified against real Windows hardware. |
| IPP: `ipps://` with a self-signed certificate | ✅ trust on first use, as for eSCL scanners. Checked against CUPS's `ippeveprinter` over `ipps://` on Linux (connect, status, six jobs); not yet on Windows or macOS TLS. One offering both is still reached over `ipp://`. |
| IPP: authentication | ❌ a printer answering HTTP 401 is reported as `AccessDenied` by name; no credentials are sent. |
| IPP: PDF to a printer that takes no PDF | ⚠️ refused by name on its own (laying it out needs the PDF plugin). With its pages beside it (`IOPrintJob::pages`) the pages are drawn as PWG raster instead - also when the printer cannot select a page range from the PDF. |
| IPP: AirPrint's URF raster | ❌ not written. A printer taking URF but not PWG raster - older AirPrint-only models - still prints PDF and JPEG as they are; text and other images are refused by name. |
| IPP: document streaming | ❌ the request and document go as one HTTP body, so a document is in memory whole. Fine for the PDFs and photos this path carries; PWG raster is the heavier case - an A4 page of text at 300 dpi in grey is about 1.3 MB, because anti-aliased glyphs defeat run-length compression - so a long raster job would want `Create-Job` / `Send-Document` with a chunked body. |
| IPP: tested against a live printer | ✅ `Tests/IODevicePrinterIPPLiveTest` starts CUPS's reference printer `ippeveprinter` and prints to it through the public API: text as PWG raster, a page range, a PDF as it is, a refusal, jobs. Skipped where `ippeveprinter` is not installed (`cups-ipp-utils`) or cannot start: it needs a running DNS-SD daemon (Avahi) even with advertising off, and IPv6, so a container without IPv6 always skips. CI's Linux rows install it, start Avahi and set `ULTRACANVAS_TEST_IPP_REQUIRED`, which turns a skip into a failure, so there it must run. |
| SNMP supply/component monitoring | ⚠️ exists outside the tree; needs net-snmp declared, and should be reconsidered against UltraNet |
| Maintenance (cleaning, alignment, nozzle check) | ⚠️ two competing architectures were written; pick one. Both shell out to `escputil` with the device path interpolated unquoted into `popen` — fix before landing. |
| Cloud printing | ❌ architecture only |
| **Integration with `UltraCanvasNativeDialogs::ShowPrintDialog()`** | ✅ the dialog now asks (`RequestPrintSettings()` → `NativePrintResult`) and the job goes through `PrinterDevice`, so the printer, copies, collation, paper size, orientation, duplex, quality and page range the user picked are the ones the queue receives. Texter and UltraFiler keep the `bool` signature they already call. |
| Print dialog: "Print to File" destination | ❌ reported in the result (`printToFile`, `outputFilePath`) and refused by name, rather than silently spooled to a queue the user did not choose. Needs a renderer that writes a document rather than submitting one. |
| Print dialog: printing anything but plain text | ⚠️ `PrintDocumentWithDialog()` prints a document (a PDF), with its pages beside it for the renderers that must draw (`pages`); Texter prints word-processing tabs this way. An image file from a dialog has no bridge yet. |
| Print dialog on Android | ❌ `RequestPrintSettings()` reports cancelled. Android prints through a Java-side `PrintManager` job, not a settings dialog that hands choices back; bridging it is a JNI slice. |
| Print dialog on WASM | ❌ and will stay so: the browser's print UI neither reports what the user chose nor lets a page choose for them, by design. `ShowPrintDialog()` there still hands the text to the browser to print. |

**Types referenced by the prior printer code but defined nowhere:**
`IOPrinterCapabilities`, `IOPrintJobStatus`, `IOPrintJobInfo`,
`IOPaperTrayStatus`, `IOImageData`, `IOColorMode`.

---

## Categories not started

Beyond scanner, camera and printer, `IODeviceCategory` has these, and none
has a backend (`README.md` lists them as planned):

Storage · NetworkAdapter · GPIO · Serial · Bluetooth · Barcode · Biometric

The README once advertised 19 categories, including Display, HID/Gamepad,
RFID, Keyboard, Mouse, Touchscreen and Stylus, which are not even
`IODeviceCategory` values; it no longer does. Adding one is an enum value
first, a backend second.

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
