# IODeviceManager — what is not built yet

Companion to [Architecture.md](Architecture.md), which describes the design.
This file tracks the distance between that design, the claims in
[README.md](README.md), and the code actually in the tree.

It exists because the module's documentation ran well ahead of its code: the
overview that preceded this file marked Scanner and Camera "✅ 100% Complete,
production-ready, ~11,525 lines" for a module that had no source at all. Read
a ✅ below as "in the tree, compiled and tested", and nothing else.

Last reviewed: 2026-09-14 (after the Windows printer slice).

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
| **Hot-plug watchers** | ❌ | `SetDeviceChangeCallback` exists, but only enumeration fires it. Nothing watches udev (Linux), `WM_DEVICECHANGE` (Windows) or IOKit notifications (macOS), so a device plugged in after a scan goes unnoticed until something rescans. |
| **Permission model** | ❌ | macOS gates camera and microphone behind TCC, Windows behind capability prompts, Linux behind udev rules and group membership. `IODeviceResultCode::AccessDenied` exists; nothing requests permission or reports why it was refused. `UltraCanvasAudioDevices` already has `MicrophonePermission` + `RequestMicrophonePermission` — generalise that, do not reinvent it. |

---

## Device classes

| Class | State |
|---|---|
| `PrinterDevice` | ✅ |
| `CameraDevice` | ⚠️ a version exists outside the tree, but it declares nine `override`s for methods its own base never had and leaves three pure virtuals unimplemented, so every concrete camera stays abstract. Rewrite. |
| `ScannerDevice` | ❌ |

---

## Scanner — the largest hole

Advertised as finished across five protocols. **No scanner source has ever
existed in this repository.**

| Item | State |
|---|---|
| `ScannerDevice` + scanner types (resolution, colour mode, scan area, ADF, duplex) | ❌ |
| SANE (Linux) | ❌ |
| WIA (Windows) | ❌ |
| TWAIN (Windows) | ❌ |
| ICA (macOS) | ❌ |
| eSCL / AirScan driverless network scanning | ❌ |
| Multi-page ADF, preview mode, capability detection | ❌ |

---

## Camera

| Item | State |
|---|---|
| **V4L2 webcam backend (Linux)** | ❌ — never written, and it is the most-used camera backend of all |
| `CameraDevice` implementation | ❌ |
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
| **GutenPrint renderer** | ❌ the seam it plugs into is complete and tested on both transports; the renderer itself is blocked on the licence decision below |
| Windows spooler backend: enumeration, capabilities, status, job queue | ✅ |
| Windows RAW transport (`StartDocPrinter`, datatype `RAW`) | ✅ this is the path GutenPrint uses |
| **Windows GDI/XPS renderer** | ❌ the spooler cannot process a document on its own, so `Native` is not offered on Windows until this exists. The transport says so through `SupportsDocument()`, so it shows up in `GetAvailableRenderers()` rather than as a failed job. |
| Windows printer maintenance | ❌ |
| **macOS printing** | ❌ functions were written but under names nothing calls, so effectively zero |
| Paper size recognition | ✅ by dimensions from the CUPS dest-info API, replacing the prior stub that returned A4 for every size a printer reported |
| Page rendering (document/image → page raster) | ❌ |
| IPP: real mDNS/DNS-SD discovery | ❌ the prior version piggybacked on CUPS and only found what CUPS already knew |
| IPP: full `Get-Printer-Attributes` parsing | ❌ |
| SNMP supply/component monitoring | ⚠️ exists outside the tree; needs net-snmp declared, and should be reconsidered against UltraNet |
| Maintenance (cleaning, alignment, nozzle check) | ⚠️ two competing architectures were written; pick one. Both shell out to `escputil` with the device path interpolated unquoted into `popen` — fix before landing. |
| Cloud printing | ❌ architecture only |
| Integration with `UltraCanvasNativeDialogs::ShowPrintDialog()` | ❌ that dialog already ships and is used by Texter and UltraFiler |

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
| **Dependency records** for libgphoto2, net-snmp, GutenPrint | ❌ absent from `Docs/Dependencies.md`, `master_dependencies.yaml` and `THIRD_PARTY_LICENSES.md`. SANE, V4L2, WIA, TWAIN, ICA, CUPS and FFmpeg are already recorded. |
| Per-category documentation | 🔨 |
| DemoApp examples | ❌ repo pattern is `Apps/DemoApp/UltraCanvas<Thing>Examples.cpp`; the prototypes were standalone `main()` programs |
| Per-category tests against fakes | 🔨 foundation done, categories follow |
| **Device-picker UI** | ❌ no scanner/camera/printer chooser exists. When one is built it must be assembled from the element catalogue — see the prohibition in `AGENTS.md`. |

---

## ❓ Open decisions

1. **GutenPrint: linked or subprocess.** libgutenprint is GPL-2.0-or-later;
   UltraCanvas is MIT, so linking makes the distributed binary GPL. This
   repository's existing pattern for GPL tools is "runtime, not linked"
   (QEMU, Wine). Recommendation and full trade-off in
   [Architecture.md](Architecture.md#open-decision-linked-or-subprocess).
   Blocks the GutenPrint renderer.
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
