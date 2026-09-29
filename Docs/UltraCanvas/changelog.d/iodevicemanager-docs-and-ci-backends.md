- **The dependency tables no longer claim IODeviceManager backends that do
  not exist.** `Docs/Dependencies.md` and the DemoApp's in-app copy
  (`UltraCanvasDependenciesExamples.cpp`) listed ICA and AVFoundation for macOS
  and WIA, TWAIN and Media Foundation for Windows. None of them has a backend.
  The one Windows device backend is the printer backend on the print spooler
  (`OS/MSWindows/UltraCanvasWindowsIODevicePrinter.cpp`, winspool and gdi32).
  The single "Scanners / cameras / print" row is now three, one per category,
  listing only what `UltraCanvasIODeviceBackends.cpp` registers:
  - Printers: CUPS on Linux and macOS, the Windows print spooler.
  - Scanners: SANE on Linux, and the eSCL network backend (over UltraNet) on
    all three.
  - Cameras: V4L2 on Linux.

  ICA, WIA, TWAIN, AVFoundation and Media Foundation are marked *planned*, and
  a note says that on macOS and Windows a USB scanner or any camera is not
  found yet. The Win32 row of the library-links table now names winspool.
- **IODeviceManager's README describes the module that exists.** It marked
  Scanner, Camera and NetworkCamera "Production" and listed WIA, TWAIN, ICA,
  MediaFoundation, AVFoundation, ONVIF and RTSP backends that were never
  written. Its examples called `DiscoverNetworkCameras()`, `CapturePhoto()`,
  `SetPTZ()`, `AddeSCLScanner()`, `Scan(config, bytes)` and
  `ScanColorMode::RGB`, none of which exist.
  - The category and backend tables now carry each entry's real state, taken
    from `Gaps.md`: printers available on all three platforms, scanners and
    webcams partial, the rest planned.
  - Microphone and Speaker are marked as the open decision `Gaps.md` records.
    The categories that were never `IODeviceCategory` values are gone.
  - Every example is rewritten against the headers and compiles: scanning
    through `ScannerDevice::Scan(ScannedImage&)`, cameras through
    `CaptureFrame` / `StartStream` / `SetControl`, printing through
    `PrintFile`, eSCL scanners named in `ULTRACANVAS_ESCL_SCANNERS`, and a
    custom device through `RegisterDevice`.
  - `intro.md`, which the DemoApp shows as the module's introduction, no
    longer claims TWAIN, WIA, ONVIF or libgpiod. `Gaps.md` says which
    categories exist.
- **CI now builds IODeviceManager's optional Linux backends.** The Linux jobs
  install `libudev-dev`, `libcups2-dev` and `libsane-dev`, so configure reports
  the udev hot-plug watcher, the CUPS printer backend and the SANE scanner
  backend as ENABLED. `OS/Linux/UltraCanvasLinuxIODeviceWatcher.cpp`,
  `core/IODeviceManager/UltraCanvasIODevicePrinterCUPS.cpp` and
  `OS/Linux/UltraCanvasLinuxIODeviceScanner.cpp` are now compiled on every pull
  request. Until now no CI build had any of the three libraries, so these
  files compiled to nothing and a compile error in them would have gone
  unnoticed.
  - `package-linux.sh` leaves `libudev.so` on the host instead of bundling it:
    libudev reads the running udev's database, so it has to be the system's
    own.
  - libcups and libsane are bundled like every other library. Leaving them to
    the host would stop every packaged application from starting on a system
    without them, because the core library links both. The Linux package
    therefore now prints through CUPS and scans through SANE, which it could
    not before. The bundled SANE loader still uses the host's scanner
    drivers: Debian's libsane reads `/etc/sane.d` and searches
    `/usr/lib/<multiarch>/sane`, `/usr/lib/sane` and `/usr/lib64/sane`, which
    covers the Debian, Arch and Fedora layouts.
