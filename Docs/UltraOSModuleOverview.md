# ULTRA OS modules and libraries: an overview

A short summary of every module and library in the ULTRA OS / UltraCanvas
project: what each one is for, what it can do, and how far it has been built.
The authoritative registry, with the full public function surface of every
module, is [`Masterfile_modules.md`](../Masterfile_modules.md). Each module's
own documentation is under `Docs/Modules/<Name>/` or `Docs/UltraCanvas/`. For
how UltraCanvas compares with Qt, GTK and others, see
[UltraCanvasComparison.md](UltraCanvasComparison.md).

**Status legend:** ✅ implemented and in the build · 🟡 partly implemented ·
📝 specified or proposed only

---

## Overview table

| Module | Purpose in one line | Status |
|---|---|---|
| [UltraCanvas core](#ultracanvas-core) | Cross-platform C++20 UI, rendering, layout and events | ✅ |
| [Chart and diagram plugins](#chart-diagram-and-code-plugins) | About 70 charts, diagrams, gauges and codes as UI elements | ✅ |
| [FileLoader](#fileloader) | Loads, saves and converts every user file through one facade | ✅ |
| [File-format plugins](#file-format-plugins) | Vector, document, 3D-model, video and text format readers and writers | ✅ |
| [HTMLReader](#htmlreader) | HTML + CSS parsed and laid out as native elements | ✅ |
| [PixelFX](#pixelfx) | Bitmap processing and analysis engine | ✅ |
| [AudioFX](#audiofx) | Audio effects, analysis and clean-up | 🟡 |
| [VideoFX](#videofx) | Headless video editing and export on FFmpeg | ✅ |
| [IODeviceManager](#iodevicemanager) | Printers, scanners and cameras behind one API | 🟡 |
| [OCR](#ocr) | Text recognition in images and scanned pages | 🟡 |
| [Vectorizer](#vectorizer) | Bitmap to SVG tracing | ✅ |
| [QRCode and Barcode](#qrcode-and-barcode) | Generating QR codes and 1D barcodes | ✅ |
| [UltraNet](#ultranet) | Networking: HTTP(S), WebSocket, FTP, TCP/UDP, TLS, DNS | ✅ |
| [NetworkMonitor](#networkmonitor) | System-wide socket table with the owning process | ✅ |
| [UltraDatabase](#ultradatabase) | Named database connections with mandatory parameter binding | 🟡 |
| [UltraCrypt](#ultracrypt) | Hashing, AEAD encryption, key derivation, secure random | ✅ |
| [UltraVault](#ultravault) | System-wide store for credentials and secrets | 🟡 |
| [VirtualFS](#virtualfs) | Archives as folders, compression, RAM discs | ✅ |
| [UltraCloud](#ultracloud) | Cloud storage accounts, uploads and share links | ✅ |
| [UltraAI](#ultraai) | Provider-agnostic AI: LLM, speech, vision, generation | ✅ |
| [UltraMessage](#ultramessage) | Messages, notifications and commands between applications | 🟡 |
| [WasmHost](#wasmhost) | Sandboxed WebAssembly runtime for applications | 🟡 |
| [UltraWin](#ultrawin) | Windows applications on Linux as native windows | 🟡 |
| [UltraAndroid](#ultraandroid) | Android applications on Linux as native windows | 📝 |
| [UltraScript](#ultrascript) | Scripting and automation language (an AppleScript equivalent) | 📝 |
| [SmartHome](#smarthome) | Smart-home devices: Matter, Thread, Zigbee, Z-Wave, KNX | 🟡 |
| [OAuth sign-in service](#oauth-sign-in-service) | Server that brokers OAuth2 sign-ins for mail and cloud | ✅ |

---

## UI and rendering

### UltraCanvas core

**Purpose:** the universal UI layer of ULTRA OS. One C++20 code base
renders every application on Windows, Linux, macOS, WebAssembly (experimental)
and Android (in progress). It is one shared library per platform, and most
UI-free modules are linked into it.

**Capabilities:**
- About 60 UI elements: text inputs, code and Markdown editor, rich-text
  editor with ODT/DOCX round trips, buttons, choices, sliders, menus,
  toolbars, tabs, split panes, a virtualised list and table view, tree view,
  file browser, date / time / colour pickers, dialogs, tooltips, toasts,
  media players and recorders, a video timeline, an image viewer, a paint
  surface and a vector editing canvas.
- Cairo + Pango + HarfBuzz rendering: gradients, paths, compositing, HiDPI,
  Unicode and complex scripts, plus an OpenGL surface.
- A CSS-style layout engine (Flex, Grid, Block), a theme with colour roles
  for light and dark, event bubbling and capture, drag and drop, clipboard.
- Accessibility bridges for AT-SPI, UI Automation and NSAccessibility.
- Desktop integration: file associations, trash, single instance, folder
  watching, volume monitor, hardware info, spell checker, printing,
  shortcuts and app bundles, host file icons.
- Raster editing (`UCRasterDocument`: layers, selection, brush engine) and
  vector editing (`VectorStorage`, Bézier paths, gradient editor).
- Data formats: JSON (yyjson), vector and 3D-model storage, a CAD palette.

**Docs:** `Docs/UltraCanvas/UltraCanvasUIElements.md` (the element catalogue)
and about 100 component docs in `Docs/UltraCanvas/`.

### Chart, diagram and code plugins

**Purpose:** ready-made visualisation elements in the same library, under
`UltraCanvas/Plugins/{Charts,Diagrams,QRCode,Barcode}`.

**Capabilities:**
- **About 35 chart types:** line, bar, area, scatter, pie and donut,
  sunburst, treemap, bubble, funnel, waterfall, Marimekko, radar, polar,
  heatmap, contour and 3D surface, 3D scatter, spectrogram, candlesticks,
  Gantt, Kanban, timeline. A chart engine lets you write new chart types.
- **About 30 diagram types:** flow chart, UML class and sequence, ER, SysML
  requirements, PERT, mind map, Sankey, Venn, word cloud, fishbone, git
  graph, node editor, parliament, matrices / House of Quality, ticker board,
  gauges (including the framework's progress bar).

---

## Files and media

### FileLoader

**Purpose:** the one way an application loads, saves or converts a user
file, so no application has its own format list or reader code.

**Capabilities:**
- Loads from paths or `http(s)://` URLs. Decompresses gzip, zlib,
  Zstandard and LZ4 transparently.
- Keeps a runtime inventory of which formats the current build reads and
  writes, and which plugin provides each one. Every Open / Save dialog's
  file types come from it.
- Stores save settings per format (JPEG quality, PNG compression, TIFF codec
  and so on) once, for every application.
- Lets an application register its own codecs (`RegisterMediaCodec`).

**Docs:** `Docs/Modules/FileLoader/README.md`, `formats.md` (the full
load / save matrix).

### File-format plugins

**Purpose:** the per-format readers and writers behind FileLoader, under
`UltraCanvas/Plugins/`.

**Capabilities:**
- **Vector:** SVG, XAR, EPS, CDR, PDF, EMF, WMF, AI, DXF, and DWG (a native
  decoder for R13 to R2018).
- **Documents:** ODT and DOCX (read and write), DOC, legacy XLS (OLE2,
  including encrypted Office files), eBooks, LaTeX documents and formulas,
  PDF through MuPDF (view, search, print), Markdown.
- **3D models:** STL, OBJ, PLY, 3DS, FBX, COLLADA, STEP, X3D, Blend,
  Alembic, MS3D, DirectX `.x`. They are rendered to bitmaps or shown in the
  STL element.
- **Video:** WebM, plus the platform media frameworks for playback.

### HTMLReader

**Purpose:** the framework's single implementation of reading HTML and CSS.

**Capabilities:** an HTML parser and document model, CSS style sheets and
the cascade, building native UltraCanvas elements from a page, a page loader,
HTML forms (filled in and submitted), import into rich documents, and the
`UltraCanvasWebView` element. There is no JavaScript. UltraWeb and the
Markdown / rich-text paths use it.

### PixelFX

**Purpose:** the bitmap processing and analysis engine, built on libvips.

**Capabilities:** more than 300 operations in 14 categories: arithmetic,
colour-space conversion (RGB, HSV, Lab, CMYK, XYZ …), format and bit-depth
casts, filtering (convolution, edges, blur, sharpen), histograms,
morphology, FFT / frequency domain, geometric transforms, and metadata
decoding (EXIF and others). UltraPaint and UltraViewer use it.

**Docs:** `Docs/Modules/PixelFX/README.md`.

### AudioFX

**Purpose:** audio processing for applications: loading, effects, analysis
and clean-up.

**Capabilities (as documented):** universal audio loading, effects
processing, effect chains and batch processing, analysis, noise reduction.
Today the demo app has a live playground (choose a track, apply an effect,
hear and see the result), on top of the core's audio player, recorder and
waveform elements. There is no separate library target yet.

**Docs:** `Docs/Modules/AudioFX/README.md`.

### VideoFX

**Purpose:** headless video editing and conversion on FFmpeg. It is a
shared library of its own, so FFmpeg stays out of the core.

**Capabilities:**
- Probing media and pulling frames.
- A segment timeline with trim, speed, 26 effect types and 30 transitions.
- Text, image and picture-in-picture overlays.
- Pan and zoom on stills, with built-in face detection to keep faces in
  shot, and one-call slideshows.
- Background music with fades, looping and ducking.
- Joins and export, run as a job on a worker thread.

UltraVideo is built on it.

**Docs:** `Docs/Modules/VideoFX/README.md`.

### IODeviceManager

**Purpose:** one API for the peripherals an application operates.

**Capabilities:** discovering devices, sessions, configuration and work.
Today that means:
- **Printers:** Linux, macOS and Windows, plus driverless IPP printers on
  every platform.
- **Scanners:** Linux, plus eSCL network scanners on every platform.
- **Webcams:** Linux.

Each category adds its own operations (`ScannerDevice`, `CameraDevice`,
`PrinterDevice`). DeviceExplorer is built on it.

**Docs:** `Docs/Modules/IODeviceManager/` (`Architecture.md`, `Gaps.md`).

### OCR

**Purpose:** extracting machine-readable text from images and scanned
pages, local-first.

**Capabilities:** a plugin under `UltraCanvas/Plugins/Documents/OCR` with
engine selection, a Tesseract engine, a language catalogue, image bridging
and exporters (version 0.1). UltraAI's vision analysers are the planned
cloud alternative.

**Docs:** `Docs/Modules/OCR/`.

### Vectorizer

**Purpose:** turning bitmaps into vector graphics.

**Capabilities:** traces PNG, JPEG, WebP, TIFF or any libvips image into
colour or black-and-white SVG (phase 1 implemented). Next it is planned for
the image element ("Trace bitmap…") and the PDF viewer.

**Docs:** `Docs/Modules/Vectorizer/`.

### QRCode and Barcode

**Purpose:** machine-readable codes as UI elements.

**Capabilities:**
- **QR codes:** configurable error correction, URLs, vCards, Wi-Fi and
  custom data, with styling, and export to PNG, SVG or PDF.
- **1D barcodes:** EAN, UPC, Code 39/128, ITF, Codabar and others, with the
  human-readable line.

**Docs:** `Docs/Modules/QRCode/`, `Docs/UltraCanvas/UltraCanvasBarcodeElement.md`.

---

## Networking, data and security

### UltraNet

**Purpose:** every kind of network communication for clients and servers,
behind one consistently named API (`UltraNet_*`). TLS verification is on by
default.

**Capabilities:**
- HTTP(S): synchronous and asynchronous (streaming, cancel, pause, resume,
  transfer statistics), downloads and uploads, server-sent events.
- WebSocket, FTP / FTPS / SFTP, TCP / UDP, TLS, DNS.
- OAuth2 with PKCE through the system browser, and a shared app registry.

Protocol plugins live under `Plugins/UltraNet/`.

**Docs:** `Docs/Modules/UltraNet/README.md`.

### NetworkMonitor

**Purpose:** the operating system's socket table with the process behind
every connection, as a library (what `ss -p` / `netstat -p` show). It only
observes; it never blocks or changes traffic.

**Capabilities:** Linux (netlink `sock_diag`), Windows and macOS back ends,
persistence, host-name resolution and change events (phase 2 complete).
UltraNetMonitor is built on it.

**Docs:** `Docs/Modules/NetworkMonitor/README.md`.

### UltraDatabase

**Purpose:** unified database access through named connections that an
application defines once. Values are always bound as parameters, so SQL
injection through the API is impossible, and credentials come from
UltraVault.

**Capabilities:**
- Queries (synchronous and asynchronous), prepared statements,
  transactions, cursors, cancellation and versioned migrations.
- SQLite is bundled, and the PostgreSQL driver is implemented.
- MySQL / MariaDB, MSSQL, Redis / Valkey, MongoDB and DuckDB are specified
  as driver plugins.

**Docs:** `Docs/Modules/UltraDatabase/README.md`.

### UltraCrypt

**Purpose:** the single place where cryptography is implemented, for data at
rest and data integrity. There is one cipher and one key-derivation
function, and no algorithm menu.

**Capabilities (on libsodium):**
- Secure buffers, secure zeroing, constant-time comparison, a CSPRNG.
- SHA-1 and SHA-2 hashes and HMAC.
- XChaCha20-Poly1305 AEAD, with Argon2id and HKDF for keys.
- Base16 / Base32 / Base64.

Tested against published test vectors.

**Docs:** `Docs/Modules/UltraCrypt/README.md`.

### UltraVault

**Purpose:** the system-wide home for API keys, tokens and passphrases, so
no application stores its own secrets.

**Capabilities:** `Put` / `Get` / `Delete` / `List`, with namespaced keys
(`vendor.app.purpose`) and ACLs. It has a memory back end and an encrypted
file back end (Argon2id + XChaCha20-Poly1305, tamper-evident). UltraAI,
UltraCloud and UltraDatabase take their credentials from it. Back ends for
the platform keychains are planned.

**Docs:** `Docs/Modules/UltraVault/`.

### VirtualFS

**Purpose:** archives and scratch storage. Files inside archives, including
archives nested inside archives, are reached as if they were ordinary
folders.

**Capabilities:**
- Archive traversal and format detection on libarchive.
- Entry caching and password handling.
- Raw buffer compression.
- RAM discs: native on Linux and macOS; on Windows through ImDisk if it is
  installed, otherwise storage-backed.

**Docs:** `Docs/Modules/VirtualFS/README.md`.

### UltraCloud

**Purpose:** cloud accounts and "upload this and give me a share link", so
no application talks to a cloud provider on its own.

**Capabilities:**
- Providers: Nextcloud / ownCloud, generic WebDAV, Dropbox, OneDrive,
  Google Drive (OAuth2 + PKCE, with automatic token refresh), FTP / FTPS /
  SFTP.
- Operations: list, upload, download, share links (with password and
  expiry), delete and rename.
- Accounts are stored in UltraDatabase and secrets in UltraVault.
- Shared add-account and link-picker dialogs, and provider plugin libraries.

**Docs:** `Docs/Modules/UltraCloud/README.md`.

---

## AI, messaging and runtimes

### UltraAI

**Purpose:** provider-agnostic AI. Applications ask for a capability, and the
user chooses a cloud service or a local model.

**Capabilities:**
- Text LLMs, embeddings, speech-to-text, text-to-speech and vision.
- Image, video and music generation, translation and code assist.
- Adapters: Anthropic, OpenAI and OpenAI-compatible servers, Qwen,
  llama.cpp, Ollama, vLLM, ComfyUI, ElevenLabs, MiniMax, and a mock for
  tests.
- API keys are resolved through UltraVault.

**Docs:** `Docs/Modules/UltraAI/README.md`.

### UltraMessage

**Purpose:** the message channel of ULTRA OS, modelled on the RISC OS Wimp.
Applications send each other structured messages (broadcast or targeted,
recorded delivery that bounces when unacknowledged, reply by return),
receive what the platform publishes, and expose commands that scripts and
AI agents may call.

**Capabilities:** a broker with in-process hosting and lock-file election,
a Unix-socket / named-pipe transport, `Post` / `PostRecorded` / `Request` /
`Reply`, a journal, and a bridge to the UI (phase 1 done, phase 2 feed
adapters started). The desktop message centre and the notification toasts
use it.

**Docs:** `Docs/Modules/UltraMessage/`.

### WasmHost

**Purpose:** runs WebAssembly modules for UltraCanvas applications, safely.

**Capabilities:** compiling from binary or text format, linking the host
functions an application offers, WASI preview 1 with nothing granted, a
memory cap per instance and a time limit per call. It wraps wasmtime, which
can be replaced. It is on by default on Linux and Windows (phase 1).
UltraWeb is its first user.

### UltraWin

**Purpose:** runs Windows applications on Linux / ULTRA OS as single native
windows, never a Windows desktop, with the user's home folder mapped as
`U:`.

**Capabilities (stage 1):** isolated Wine environments with persisted drive
mappings, launching and supervising applications, and a component installer
(a winetricks wrapper). Wine runs as a child process, so no GPL code is
linked. A QEMU/KVM + FreeRDP virtual-machine tier is planned.

**Docs:** `Docs/Modules/UltraWin/README.md`.

### UltraAndroid

**Purpose:** runs other people's Android applications on Linux / ULTRA OS as
single native windows, with the user's folders at one fixed mount point. It
is not the Android back end, which runs UltraCanvas applications *on*
Android.

**Status:** specified only. It plans a host-side stage, a container tier
and a VM tier behind one API.

### UltraScript

**Purpose:** the scripting language of ULTRA OS, a cross-platform
equivalent of AppleScript for automating UltraCanvas applications.

**Planned capabilities:** three syntax styles (Modern, Natural, Classic), a
recorder that turns user actions into script code, an SDEF-compatible
dictionary generated from element metadata, and a Script Editor
application.

**Status:** specified only (`Docs/Research/UltraScriptSpecification.md`).

---

## Home and server

### SmartHome

**Purpose:** controlling smart-home devices from ULTRA OS.

**Capabilities:** a `SmartHomeManager` (device registry, command queue,
threads), a dashboard panel, device cards and other widgets. It has
protocol back ends for Matter, Thread, Zigbee, Z-Wave and KNX.

**Status:** the engine and UI targets build. Every protocol back end is
switched off until its vendor SDK is vendored.

**Docs:** `SmartHome/STATUS.md`, `Docs/Modules/Smarthome/README.md`.

### OAuth sign-in service

**Purpose:** a small FastAPI server (`Server/OAuthCallback`, running at
`oauth-callback.ultraos.eu`). It stands between the applications and the
OAuth2 providers: Google, Microsoft and Yahoo mail for UltraMail, and Google
Drive, OneDrive and Dropbox for UltraCloud.

---

## Applications built on the modules

These are not libraries, but they show the modules in use. Each one
versions itself with its own changelog in `Docs/<App>/`.

| Application | What it is | Main modules |
|---|---|---|
| DemoApp | Showcase of every element and module | all |
| Texter | Text and code editor with tabs | core, FileLoader |
| UltraFiler | Windows Explorer-style file manager | core, VirtualFS, FileLoader |
| UltraViewer | Universal media viewer | core (`UltraCanvasMediaViewer`), FileLoader |
| UltraPaint | Bitmap editor (Paint Shop Pro / GIMP class) | raster editing, PixelFX |
| ArtCreator | Vector illustration program (Xara / ArtWorks class) | vector editing |
| UltraVideo | Video editor | VideoFX |
| UltraMail | Mail client | UltraNet, UltraDatabase, UltraVault, UltraCloud |
| EmailCleaner | Clean-up of large, old mailboxes | UltraNet, UltraDatabase |
| UltraSocial | Posting to social networks | UltraNet, UltraDatabase, UltraVault, UltraCrypt |
| UltraWeb | Browser for web pages and WebAssembly apps | HTMLReader, WasmHost |
| UltraPress | Website and shop builder that publishes WebAssembly | UltraNet, UltraCloud |
| UltraAIApp | Dashboard for the AI services | UltraAI |
| UltraClaude | Desktop chat with Claude through the Claude Code CLI | core |
| UltraPassword | Password vault | UltraCrypt |
| UltraAuthenticator | TOTP / HOTP sign-in codes | UltraCrypt |
| UltraClipboard | Clipboard history | core |
| UltraCleaner | Removes caches, temporary files and leftovers | core |
| UltraNetMonitor | Live view of which process holds which connection | NetworkMonitor |
| DeviceExplorer | Printers, scanners and cameras | IODeviceManager |
| AnchorPoint | Peer-to-peer file sharing without server storage | UltraNet, UltraCrypt |
| UltraFIBU | German double-entry accounting (DATEV, ELSTER) | UltraDatabase, UltraCrypt, UltraCloud |
| UltraDesktop | The ULTRA OS desktop (also beside or instead of Explorer) | core, UltraMessage |
| UOS-Settings | System settings of ULTRA OS | core |
| UltraWinManager / UltraWinSetup | Windows-application management | UltraWin |
| UltraCanvasStart | Sets a computer up for writing UltraCanvas apps | core |
| UltraMessageCli | Command-line client for UltraMessage | UltraMessage |
