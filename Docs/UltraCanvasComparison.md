# UltraCanvas compared with Qt, GTK and other cross-platform GUI libraries

An overview for programmers choosing a GUI framework: what UltraCanvas is,
everything it ships, and how it compares with Qt, GTK, wxWidgets, Dear ImGui,
Flutter and Electron. The UltraCanvas facts come from this repository
(`Masterfile_modules.md`, `Docs/UltraCanvas/UltraCanvasUIElements.md`, the
platform backends under `UltraCanvas/OS/`, the CI workflows). The facts about
the other frameworks are general knowledge as of 2026. Check them against each
project's current documentation before making a decision that depends on them.

---

## 1. UltraCanvas in one paragraph

UltraCanvas is a **modular C++20 UI and rendering framework** under the
**MIT licence**. It draws every element itself through one shared
Cairo + Pango + HarfBuzz render path, so an application looks and lays out the
same, pixel for pixel, on Windows, Linux, macOS and in the browser
(WebAssembly). The core is one shared library per platform
(`libUltraCanvas.so` / `.dylib` / `.dll`). Next to the usual widget toolkit,
that library contains a lot that other toolkits leave to third-party
libraries: about 70 chart and diagram elements, readers and writers for
vector, office, PDF and media formats, networking, databases, cryptography, a
virtual filesystem, a provider-agnostic AI module and a WebAssembly host. Its
documentation is written for AI coding assistants as well as for people
(`AGENTS.md`, `llms.txt`, docs whose examples CI compiles).

### Minimal application

```cpp
#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
using namespace UltraCanvas;

int main(int argc, char* argv[]) {
    UltraCanvasApplication app;
    if (!app.Initialize("MyApp")) return EXIT_FAILURE;

    WindowConfig config;
    config.title = "MyApp";
    config.width = 800;
    config.height = 600;
    auto window = CreateWindow(config);

    auto label  = CreateLabel("greeting", 20, 20, 300, 24, "Hello from UltraCanvas");
    auto button = CreateButton("quit", 20, 60, 120, 32, "Quit");
    button->onClick = [&app]() { app.RequestExit(); };

    window->AddChild(label);
    window->AddChild(button);
    window->Show();
    app.Run();
    return EXIT_SUCCESS;
}
```

It is plain C++: no meta-object compiler, no extra language, no code
generator, no runtime VM. Callbacks are `std::function` lambdas and widgets
are held in `std::shared_ptr`. The full version, with the per-platform entry
point lines, is in [GettingStarted.md](GettingStarted.md).

---

## 2. Feature inventory

### 2.1 Platforms and architecture

| Area | What UltraCanvas provides |
|---|---|
| Desktop platforms | **Windows** (Win32 + Cairo), **Linux** (X11 + Cairo, with GLX / EGL for GL), **macOS** (Cocoa + Cairo, Objective-C++). CI builds all three on x86-64 **and** ARM64 (`macos-15`, `macos-15-intel`, `ubuntu-22.04`, `ubuntu-22.04-arm`, `windows-latest`, `windows-11-arm`) |
| Browser | **WebAssembly** (Emscripten) backend, *experimental*. It runs in Chromium using the same Cairo/Pango pipeline, and the whole demo app builds as a web page |
| Mobile | **Android** backend: window, lifecycle, EGL/GLES, clipboard, IME, multi-touch gestures, SAF files. CI compiles it, but it has **not yet run on a device** because APK packaging is still open. No iOS |
| Others | BSD backend (X11). ULTRA OS is the native target platform |
| Rendering | One `IRenderContext` on Cairo, shared by every platform. Lines, shapes, paths, Bézier curves, linear / radial / conic gradients, compositing modes, clipping, transforms, nearest-neighbour or smooth image scaling, HiDPI device scale, dirty-rect composition, double buffering. `UltraCanvasGLSurface` embeds raw OpenGL |
| Text | Pango + HarfBuzz + FreeType shaping: full Unicode, bidirectional text and complex scripts, rich styled text, IME and multi-byte input |
| Layout | A **CSS-style layout engine** with Flex (direction, wrap, grow/shrink/basis, justify/align, gap, order), Grid (explicit and auto placement, `px`/`%`/`fr`/`auto`/`min-content`/`max-content`/`fit-content` tracks) and Block. It runs a two-phase Measure → Arrange pass, plus a form-grid helper for "caption: control" rows that still fit when the captions are translated |
| Events | Event bubbling with capture phases, keyboard focus, drag and drop, clipboard (text, images, files), custom cursors, smooth scrolling |
| Theming | `UltraCanvasTheme` colour roles (panel, ink, accent, focus ring and so on) with light and dark presets. Elements read their style from the theme |
| Accessibility | One role / name / text / value / event model per element, bridged to **AT-SPI** (Orca), **UI Automation** (Narrator, NVDA, JAWS) and **NSAccessibility** (VoiceOver) |
| Packaging | One shared core per platform. The UI-free modules are linked into it, so every application shares one copy. CMake ≥ 3.16, vcpkg / MSYS2 / Homebrew dependencies, packaging scripts for Linux (AppImage), macOS (signed and notarised `.app`) and Windows |

### 2.2 UI elements (about 60 in the core)

| Group | Elements |
|---|---|
| Text entry | `TextInput` (caret, selection, undo/redo, clipboard, validation, password reveal), `TextArea` (plain, syntax-highlighted code, or live Markdown with inline LaTeX math), `RichTextEdit` (a word-processing model that round-trips ODT / DOCX), `AutoComplete`, `TagInput`, `Spinner`, password-strength meter and rule legend, spell checker |
| Buttons and choices | Push / split / icon `Button`, `Checkbox`, `Radio` / `RadioGroup`, `Switch`, `Dropdown`, `SegmentedControl`, `Slider`, `Rating`, `Stepper`, `CurveEditor`, `GradientEditor`, `ToolGroupButton` |
| Text, images, media | `Label` (selectable), `Badge`, `Chip`, `ImageElement` (any bitmap, SVG, animated GIF / WebP), `ZoomPanImage`, `PaintSurface` (layers, selection, brushes), `VectorElement` (SVG / XAR / DXF / DWG / EMF / WMF / AI view and edit), `MediaViewer` (images, video, audio, PDF, text, spreadsheets, eBooks, fonts), video / audio players and recorders, waveform, level meter, `VideoTimeline`, `TimecodeField`, `Album`, `Slideshow`, `EBookViewer`, `WebView` (HTML + CSS rendered as native elements, without JavaScript), `LaTeXView`, `FontViewer`, `STLElement` (3D mesh) |
| Layout and structure | `Container` (scrolling built in), `Scrollbar`, `SplitPane` / `Splitter`, `TabbedContainer`, `GroupBox`, `Toolbar` (badges, reordering, scrolling), `Separator`, `Spacer`, `WaveSeparator` |
| Navigation and data | `Menu` (menu bar, popup, context), menu configuration widget, `Breadcrumb`, `Pagination`, `ListView` (virtualised, model-driven, multi-column table with sortable, resizable headers), `TreeView`, `FilerWidget` (file browser), hardware-info panel, message centre |
| Pickers and dialogs | `ColorPicker`, `ColorSwatchBar`, `DatePicker` / `DateRangePicker` / `CalendarView`, `TimePicker` / clock face, `ModalDialog` (Markdown details), file open / save / folder dialog (framework or native), input dialog, new-document dialog, progress dialog, `BusyIndicator`, image export with per-format settings, Curves, metadata dialog, 3D-model view dialog, print dialog, `TooltipManager`, splash screen, notification toasts |

### 2.3 Charts, diagrams and codes (about 70 plugin elements, in the same library)

Every one of them takes its data through setters and draws itself. Each is an
ordinary element: you add it with `AddChild` like a button.

- **Charts:** line, bar, scatter, area. Pie, donut, 3D pie. Sunburst,
  treemap, nested areas. Bubble (XY, packed, matrix). Funnel, pyramid,
  waterfall, dumbbell, diverging bar, population pyramid, Marimekko. Radar,
  parallel coordinates. Polar, radial bar, activity rings, circular
  infographic. Heatmap, hexbin, calendar heatmap. Jitter / box / violin.
  Contour (2D, and 3D surfaces in software or GL), 3D scatter. Spectrogram.
  OHLC candlesticks with volume. Quadrant (SWOT / BCG / Eisenhower). Chord.
  **Gantt** (dependencies, critical path), **Kanban** with cumulative flow,
  date-accurate timeline. On top of these, a three-phase chart engine for
  writing new chart types.
- **Diagrams:** flow chart (with an editing palette), isometric 3D blocks,
  node graph and node editor with typed sockets, UML class and sequence
  diagrams, ER diagrams (Chen, Crow's Foot), SysML requirements, PERT, mind
  map, dendrogram, Gource-style tree, git graph, Sankey, arc, adjacency,
  L/T/X/Y matrices and QFD House of Quality, Venn / Euler, word cloud,
  parliament seats, SWOT, fishbone, hub diagrams, narrative timeline, packet
  diagram, ticker board / LED sign, gauges (speedometer, ring, battery,
  thermometer, LED bar, and the linear progress bar).
- **Codes and documents:** QR codes, 1D barcodes (EAN, UPC, Code 39/128, ITF,
  Codabar and others), a PDF view with thumbnails and search, a Markdown
  display.

### 2.4 Files and formats (`UltraCanvasFileLoader`)

One facade loads, saves and converts every user file. It accepts local paths
and `http(s)://` URLs, decompresses gzip / zlib / Zstandard / LZ4
transparently, and keeps one runtime inventory of which formats the current
build reads and writes. Every dialog builds its file-type list from that
inventory. Save settings for each format (JPEG quality, PNG compression, TIFF
codec and so on) are stored once and shared by all applications.

- **Bitmaps:** PNG, JPEG, BMP, GIF, WebP, AVIF, QOI, TIFF and more, through
  libvips, plus animated images.
- **Vector:** SVG, XAR, EPS, CDR, PDF, EMF, WMF, AI, DXF and DWG. A native
  DWG decoder reads R13 to R2018 without any external program. Vector
  editing is built in (rulers, guides, snapping, handles), as is
  vectorisation of bitmaps.
- **Documents:** ODT and DOCX read and written. DOC, legacy XLS (OLE2
  compound files, including encrypted Office files), spreadsheets with
  Excel-style formulas, HTML + CSS, Markdown, LaTeX documents and formulas,
  eBooks, PDF (render, print, search).
- **Media and 3D:** video and audio playback and capture. Through **VideoFX**
  (FFmpeg): probing, frames, trim, effects, joins and export. STL and other
  3D models rendered to bitmaps. Font files viewable without installing them.

### 2.5 Modules that are not UI

They ship in the same shared core. Each has a C-style, result-returning API
and wraps its open-source engine so that the engine can be replaced.

| Module | Purpose |
|---|---|
| **UltraNet** | HTTP(S), WebSocket, FTP, TCP / UDP, TLS (verification on by default), DNS |
| **UltraDatabase** | Named connections, parameter binding only (no string-built SQL), transactions, cursors, async queries, migrations. SQLite is bundled; PostgreSQL is implemented; MySQL / MariaDB, MSSQL, Redis, MongoDB and DuckDB are specified as driver plug-ins |
| **UltraCrypt / UltraVault** | AEAD-only cryptography (XChaCha20-Poly1305 + Argon2id on libsodium) and a credential vault |
| **VirtualFS** | Virtual filesystem, archives, compression, RAM discs |
| **UltraAI** | Provider-agnostic AI: text LLMs, embeddings, speech-to-text, text-to-speech, vision, image / video / music generation, translation, code assist. Adapters for Anthropic, OpenAI (and OpenAI-compatible servers), local Qwen / llama.cpp / Ollama / vLLM, and mocks for tests |
| **UltraMessage** | Desktop notification / message feed and its journal |
| **NetworkMonitor** | System-wide socket table with the process that owns each socket |
| **WasmHost** | Runs sandboxed WebAssembly modules (wasmtime, WASI preview 1, a memory cap per instance, a time limit per call) |
| **UltraWin** | Runs Windows applications on Linux as single native windows (Wine as a spawned child process, so no GPL code is linked) |
| **Desktop integration** | File associations, trash, single instance, file and folder watching, volume monitor, `.desktop` files / `.lnk` shortcuts / property lists / app bundles, host file icons, camera and scanner access, hardware info, printing, elevated file operations |

### 2.6 Developer experience and quality gates

- **Applications built on it:** about 25 real applications in `Apps/` act as
  the framework's test bed: a text editor (Texter), a file manager
  (UltraFiler), a viewer, a paint program, a vector illustration program
  (ArtCreator), mail, a video editor, a web browser, a password manager, an
  authenticator, a desktop, settings and others. The DemoApp shows every
  element.
- **Documentation:** about 100 per-component docs. CI compiles their C++
  examples against the real headers. There is an element catalogue, a module
  registry, and `llms.txt` / `llms-full.txt` for LLM tools.
- **Built for AI-assisted development:** `AGENTS.md` sets the conventions
  that Claude Code, Copilot, Cursor and similar tools follow. CI enforces
  them with checkers: no hand-painted controls, every element listed in the
  catalogue, no `shared_ptr` cycles in callbacks, UTF-8 file paths,
  locale-independent number formatting, and documentation that matches the
  code.
- **Conventions:** PascalCase everywhere, one `Create*` factory per element,
  `std::shared_ptr` ownership, plain C++20.

---

## 3. Comparison

### 3.1 Feature table

✅ built in · 🟡 partial, experimental or via an official add-on · ❌ not available / third-party only

| Feature | **UltraCanvas** | **Qt 6** | **GTK 4** | **wxWidgets** | **Dear ImGui** | **Flutter** | **Electron** |
|---|---|---|---|---|---|---|---|
| Language | C++20 | C++ (+ QML / JS) | C (bindings: C++, Python, Rust …) | C++ | C++ | Dart | JavaScript / TypeScript |
| Licence | **MIT** | LGPLv3 / GPL / commercial | LGPL 2.1+ | wxWindows (LGPL + exception) | MIT | BSD-3 | MIT |
| Rendering model | Self-drawn (Cairo) | Self-drawn (Widgets: styled; Quick: GPU scene graph) | Self-drawn (GSK, GPU) | **Native OS controls** | Immediate mode, GPU | Self-drawn (Skia / Impeller, GPU) | Chromium |
| Windows / macOS / Linux | ✅ / ✅ / ✅ (X11) | ✅ / ✅ / ✅ | 🟡 / 🟡 / ✅ | ✅ / ✅ / ✅ | ✅ / ✅ / ✅ | ✅ / ✅ / ✅ | ✅ / ✅ / ✅ |
| Wayland (native) | ❌ planned | ✅ | ✅ | ✅ (through GTK) | depends on backend | ✅ | ✅ |
| ARM64 desktop builds in CI | ✅ all three OSes | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Android / iOS | 🟡 compiles, not run / ❌ | ✅ / ✅ | ❌ / ❌ | ❌ / ❌ | 🟡 / 🟡 (you write the backend) | ✅ / ✅ | ❌ / ❌ |
| WebAssembly / browser | 🟡 experimental | ✅ | 🟡 (Broadway) | ❌ | ✅ | ✅ | n/a (it is a browser) |
| Pixel-identical look on every OS | ✅ | 🟡 (style-dependent) | ✅ | ❌ (native by design) | ✅ | ✅ | ✅ |
| Native OS look and feel | ❌ | 🟡 (native styles) | ❌ | ✅ | ❌ | ❌ | ❌ |
| Extra preprocessor / VM / runtime | none | moc (+ QML engine) | none (GObject) | none | none | Dart VM / AOT | Node + Chromium |
| CSS-style Flex / Grid layout | ✅ | 🟡 (layouts, QML anchors) | 🟡 (boxes, grid, CSS styling) | 🟡 (sizers) | ❌ | ✅ (own model) | ✅ |
| Theming, dark mode | ✅ | ✅ | ✅ | 🟡 (follows OS) | ✅ | ✅ | ✅ |
| Accessibility (screen readers) | ✅ AT-SPI / UIA / NSAccessibility | ✅ | ✅ | ✅ (native) | ❌ | ✅ | ✅ |
| Visual UI designer | ❌ | ✅ Designer / Creator | 🟡 (Cambalache) | 🟡 (third-party) | ❌ | 🟡 (DevTools) | 🟡 (web tools) |
| Declarative UI language | ❌ (C++ only) | ✅ QML | 🟡 (Blueprint / XML) | 🟡 (XRC) | ❌ | ✅ (Dart widgets) | ✅ HTML |
| UI string translation tooling | ❌ not documented | ✅ Linguist | ✅ gettext | ✅ gettext | ❌ | ✅ intl | 🟡 libraries |
| Rich text / word-processor editor | ✅ ODT / DOCX round trip | 🟡 QTextEdit (HTML, ODT write) | 🟡 TextView | 🟡 wxRichTextCtrl | ❌ | ❌ | 🟡 libraries |
| Code editor with highlighting | ✅ | ❌ (QScintilla, third-party) | 🟡 GtkSourceView (add-on) | 🟡 wxStyledTextCtrl | ❌ | ❌ | 🟡 Monaco, … |
| Charts | ✅ about 35 types | 🟡 Qt Charts / Qt Graphs (GPL or commercial) | ❌ | ❌ | 🟡 ImPlot (third-party) | ❌ (packages) | ❌ (JS libraries) |
| Diagrams (UML, ER, Gantt, Sankey, mind map …) | ✅ about 30 types | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ (JS libraries) |
| QR / barcodes | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| SVG display | ✅ | ✅ (Qt SVG, Tiny 1.2) | ✅ (librsvg) | 🟡 | ❌ | 🟡 (package) | ✅ |
| Vector formats (AI, CDR, XAR, EMF, DXF, DWG …) | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| PDF view / print | ✅ / ✅ | ✅ Qt PDF / ✅ | 🟡 (Poppler) / ✅ | 🟡 / ✅ | ❌ | ❌ | ✅ |
| Video / audio playback | ✅ | ✅ Qt Multimedia | ✅ GStreamer | 🟡 wxMediaCtrl | ❌ | 🟡 (plugins) | ✅ |
| Video editing (trim, effects, export) | ✅ VideoFX | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |
| Web view | 🟡 HTML + CSS, no JS | ✅ WebEngine (Chromium) | ✅ WebKitGTK | ✅ (system engine) | ❌ | 🟡 (plugin) | ✅ |
| Networking | ✅ HTTP / WS / FTP / TLS / DNS | ✅ Qt Network | 🟡 (GIO / libsoup) | 🟡 (basic sockets) | ❌ | ✅ | ✅ (Node) |
| Database access | ✅ SQLite, PostgreSQL (+ planned drivers) | ✅ Qt SQL | ❌ | ❌ | ❌ | 🟡 (packages) | 🟡 (Node packages) |
| Crypto / credential vault | ✅ | 🟡 (hashes, TLS) | ❌ (libsecret, separate) | ❌ | ❌ | 🟡 | 🟡 (Node crypto) |
| AI / LLM integration | ✅ UltraAI | ❌ | ❌ | ❌ | ❌ | 🟡 (packages) | 🟡 (packages) |
| WebAssembly plugin host | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | 🟡 (V8) |
| Docs written for AI coding assistants | ✅ AGENTS.md, llms.txt, CI-compiled examples | ❌ | ❌ | ❌ | ❌ | 🟡 | ❌ |
| Maturity / production track record | 🟡 young (0.9.x) | ✅ 30 years | ✅ 25+ years | ✅ 30 years | ✅ | ✅ | ✅ |
| Community and third-party ecosystem | 🟡 small | ✅ very large | ✅ large | ✅ medium | ✅ large | ✅ very large | ✅ very large |
| Typical "hello world" footprint | medium (one shared core + Cairo / Pango / libvips) | medium to large | medium | small | tiny | medium | large (about 100 MB+) |

### 3.2 Pros and cons

#### UltraCanvas

| ➕ Plus | ➖ Minus |
|---|---|
| MIT licence: static or dynamic linking, closed source allowed, no commercial licence to buy | Young (version 0.9.x). APIs still move, and no long-term-support releases yet |
| Plain C++20: no moc, no QML, no GObject, no VM | Small community. Few Stack Overflow answers or third-party widgets |
| Same pixels and layout on every platform (one Cairo / Pango path) | No native look: applications do not use the OS's own controls |
| A very wide set of elements in one library: about 60 widgets, about 70 charts and diagrams, codes, media, rich text, a code editor | Linux runs on X11 only; native Wayland is planned |
| File formats other toolkits lack (DWG / DXF, AI, CDR, XAR, EMF, ODT / DOCX, XLS, LaTeX, STL) behind one loader | Mobile is not ready: Android compiles but has not run on a device, and there is no iOS |
| Networking, databases, crypto, VFS, AI, a WASM host and video editing are in the box, so there are fewer dependencies to choose and integrate | WebAssembly is experimental (tested in Chromium only) |
| CSS-style Flex / Grid layout, known from the web | No visual designer and no declarative UI language. UI is written in C++ |
| Accessibility bridges for all three desktops | No documented UI string translation workflow (like Qt Linguist or gettext) yet |
| Documentation and CI checks made for AI-assisted development: assistants produce code that follows the house rules | CPU rendering through Cairo by default. GPU only through an explicit GL surface |
| About 25 real applications prove the elements in daily use | Depends on a GNOME-family C stack (Cairo, Pango, GLib, libvips) on every platform, including Windows and macOS |

#### Qt 6

| ➕ Plus | ➖ Minus |
|---|---|
| The most complete and mature C++ toolkit. Desktop, mobile, embedded and web | Dual licensing: LGPL obligations, or an expensive commercial licence. Some modules (Charts, some tooling) are GPL or commercial only |
| QML / Qt Quick for fluid GPU UIs, plus Qt Widgets for classic desktop UIs | moc and its macros, plus a second language (QML / JS) for modern UIs |
| Qt Creator and Designer, Linguist, a large ecosystem, many jobs | Large framework and deploy size; a WebEngine build ships Chromium |
| Excellent i18n, accessibility, Wayland and HiDPI | No diagram widgets. Charts are limited by their licence |

#### GTK 4

| ➕ Plus | ➖ Minus |
|---|---|
| LGPL, mature, the native toolkit of GNOME | Windows and macOS are second-class (look, packaging, fewer users) |
| First-class Wayland, a GPU renderer (GSK), CSS styling | C / GObject API; C++ needs gtkmm, which tracks GTK with a delay |
| Bindings for many languages (Python, Rust, Vala, JS) | No mobile. Few high-level widgets (no charts, diagrams or rich text editor) |
| Good accessibility and gettext i18n | Breaking changes from GTK 3 to GTK 4 made porting expensive |

#### wxWidgets

| ➕ Plus | ➖ Minus |
|---|---|
| Uses truly native controls: apps look like the OS | Looks and behaves differently per OS, so it needs testing on each one |
| Permissive licence (LGPL with a static-link exception), small binaries | Dated, MFC-like API style. Smaller modern ecosystem |
| Long-term stability | No mobile or web. Custom-drawn and modern effects are hard |

#### Dear ImGui

| ➕ Plus | ➖ Minus |
|---|---|
| MIT, tiny, trivial to embed in a game or engine | Immediate mode: not meant for conventional end-user applications |
| Very fast for tools, debug UIs and editors | No real text shaping or accessibility, limited IME |
| GPU-rendered through any graphics API | You supply windowing and backends. No file, network or media layers |

#### Flutter

| ➕ Plus | ➖ Minus |
|---|---|
| One codebase for mobile, desktop and web, with excellent mobile support | Dart, not C++. C++ code needs FFI bridges |
| Hot reload, a rich widget set, GPU rendering (Impeller) | Desktop is less mature than mobile. Heavier runtime |
| Huge package ecosystem | Charts, diagrams and file formats come from third-party packages of varying quality |

#### Electron

| ➕ Plus | ➖ Minus |
|---|---|
| Web technologies (HTML / CSS / JS) and the entire npm ecosystem | Ships Chromium + Node with every app: 100 MB+ on disk, high memory use |
| The fastest path for web teams; the same UI as a web app | Slower start-up, weaker native integration, a large security surface |
| Mature: VS Code, Slack, Discord | Not a C++ framework. Native code goes through Node add-ons |

---

## 4. When to choose UltraCanvas

**A good fit when:**

- You write **C++** and want a **permissive MIT** licence, with no LGPL
  relinking duties and no commercial licence fees.
- The application is **data-, document- or graphics-heavy**: dashboards,
  engineering and CAD viewers, office and publishing tools, media tools,
  diagram editors. The built-in charts, diagrams and format readers then
  replace a pile of third-party libraries.
- You want the **same look on Windows, macOS and Linux** (x86-64 and ARM64),
  and not three native looks.
- You build with **AI coding assistants**. The repository's docs and CI
  checks are designed so that an assistant produces code that matches the
  framework.

**Choose something else when:**

- **Mobile (iOS / Android) is a first-class requirement today.** Use Qt or
  Flutter.
- You need **native Wayland**, a **native OS look**, or a **long-term-support
  release** right now. Use Qt, GTK (Linux-first) or wxWidgets (native look).
- Your team wants a **visual designer** or a **declarative UI language**
  (QML, Dart, HTML). Use Qt Quick, Flutter or Electron.
- You need a **full browser engine with JavaScript** inside the application.
  Use Qt WebEngine, WebKitGTK or Electron.
- It is an in-game or engine **debug / tool UI**. Use Dear ImGui.
