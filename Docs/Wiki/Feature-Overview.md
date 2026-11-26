# Feature overview

Everything UltraCanvas offers, grouped. Each line links to where it is
documented in this wiki or in the component docs (`Docs/UltraCanvas/`).

## Application and windows

- **Application object and event loop** - `UltraCanvasApplication`:
  `Initialize`, `Run`, `RequestExit`, signal-safe exit, timers on the main
  thread (`StartTimer` / `StopTimer`), `PostToUIThread` from worker threads,
  file-descriptor watches. → [ApplicationBase](Elements/UltraCanvasApplicationBase.md)
- **Windows** - `CreateWindow(WindowConfig)`: title, size, position, min /
  max size, resizable, modal, always-on-top, opacity, icon, window types (standard,
  dialog, popup, tool, overlay, desktop, notification, tooltip ...); close, resize, move, focus, minimise, maximise
  callbacks; native file drag out of the window. → [WindowBase](Elements/UltraCanvasWindowBase.md)
- **Dialogs** - message, information, question, warning, error, confirmation
  and input dialogs in one call (`UltraCanvasDialogManager`), custom modal
  dialogs, the framework's own or the native file dialogs, progress dialogs,
  a property list, a new-document dialog. → [Application, windows and dialogs](Groups/Application-Windows-And-Dialogs.md)
- **Embedded windows** - windows inside a window (`UltraCanvasEmbeddedWindowManager`),
  splash screen, start page, single instance, instance and session locks,
  launch at login, jump to the last window.
- **Multi-monitor and DPI** - device scale per window, crisp hairlines,
  high-DPI images and icons.

## Elements (widgets)

About 200 elements, all built the same way - created with a `Create*`
factory or `std::make_shared`, laid out by the engine, themed, focusable,
accessible:

| Group | Highlights |
|---|---|
| [Text entry](Groups/Text-entry.md) | text input (password, validation, undo), multi-line text / code editor with syntax highlighting and live Markdown, rich-text word processor with a document navigator and format bar, auto-complete, tag input, spinner, password strength meter |
| [Buttons and choices](Groups/Buttons-and-choices.md) | push / split / icon / toggle buttons, checkbox, radio group, switch, dropdown, segmented control, chip group, gallery picker, slider and range slider, rating, stepper, curve and gradient editors |
| [Text, images and media](Groups/Text-images-and-media.md) | label, badge, chip, image (every bitmap format, SVG, animations), zoom / pan viewer, paint surface, vector viewer and editor, slide editor and slide-show player, media viewer, video / audio players and recorders, waveform, level meter, video timeline, eBook viewer, web view, LaTeX, font viewer, OpenGL surface, 3D STL viewer |
| [Layout and structure](Groups/Layout-and-structure.md) | container with scrolling, card, collapsible panel, split panes, tabs, group box, toolbar, separators and spacers, form layout, sidebar navigation |
| [Navigation and data views](Groups/Navigation-and-data-views.md) | menus and menu bars, list view (models, delegates, sorting / filtering proxies), tree view, columns view, spreadsheet with formulas and pivots, breadcrumb, pagination, file manager widget, Kanban board, calendar |
| [Pickers, dialogs and feedback](Groups/Pickers-dialogs-and-feedback.md) | colour picker, swatches and palettes, date / time / range pickers, file dialog, busy indicator, toasts and notifications, alerts, tooltips, progress |
| [Charts](Groups/Charts.md) | line, bar, area, pie, scatter (2D / 3D), bubble, radar, polar, heat-map, histogram, box, financial / candlestick, waterfall, funnel, pyramid, Sankey, sunburst, tree-map, Gantt, timeline, contour, ridgeline, parallel coordinates, ternary, Mekko and many more |
| [Diagrams](Groups/Diagrams.md) | flow chart, mind map, node and block diagrams, UML class / sequence / ER / requirement diagrams, org charts, Venn, fishbone, SWOT, PERT, Git graph, gauges and progress bars, ticker board, word cloud |
| [Codes and documents](Groups/Codes-and-documents.md) | QR codes and barcodes, PDF, CDR, XAR, EPS and other document / vector views |
| [Module widgets - Smart Home](Groups/Module-widgets-Smart-Home.md) | device tiles and controls, sensors, scenes, automations, energy, scheduler |

The full list with every callback and option: [Element index](Element-Index.md).

## Layout

- A **CSS-style engine** (`CSSLayout`): block flow, flex rows and columns
  with grow / shrink / wrap / gap / alignment, grids, tables, absolute
  positioning, margins, padding, borders, min / max constraints,
  content-sized elements. → [Layout](Layout.md)
- `UltraCanvasFormLayout` for label / field forms whose captions line up in
  every language; stretch spacers; toolbars that grow to fit or overflow into
  a menu.

## Events and input

- Mouse (buttons, wheel, horizontal wheel, double click, enter / leave),
  keyboard with cross-platform key codes and modifiers, text input and IME
  composition, touch, tap and pinch / rotate gestures, drag and drop of text
  and files in and out of the application, focus and keyboard navigation,
  mnemonics, global event filters. → [Events and callbacks](Events-and-Callbacks.md)
- Named callbacks on every element (`onClick`, `onTextChanged`,
  `onSelectionChanged` ...) - see the [Callback index](Callback-Index.md).
- Commands, menus and toolbars the user can configure
  (`UltraCanvasCommandRegistry`, `UltraCanvasMenuRegistry`, `MaterializeToolbar`).

## Rendering

- `IRenderContext`: lines, rectangles, rounded rectangles, circles, ellipses,
  arcs, Bézier curves, arbitrary paths with fill rules, dashes, caps and
  joins; linear, radial, elliptical, conic and mesh gradients; image and
  pixmap patterns; blend modes, groups, masks and opacity; transforms; clipping;
  hit testing. → [Rendering](Rendering.md)
- Unicode text layout (Pango): fonts, weights, slants, wrapping, alignment,
  markup, text outlines, caret and selection geometry, inline math.
- Images: PNG, JPEG, BMP, TIFF, GIF, WebP, AVIF, HEIC, JPEG XL, QOI, ICO, SVG,
  animated images; a shared decode cache; PixelFX image processing.
- OpenGL surfaces, PDF surfaces for printing and export, off-screen surfaces.

## Styling

- Every element has a **style struct** (`ButtonStyle`, `TextInputStyle`, ...)
  and convenience setters.
- **Themes**: colour roles, Light, Dark and two high-contrast presets, or
  the host desktop's look (`FromHost`), followed live. → [Styling and themes](Styling-And-Themes.md)

## Files, data and documents

- **`UltraCanvasFileLoader`** - every open / save / convert in one place: the
  dialogs, images, vector drawings, text and office documents (ODT, DOCX,
  DOC, LaTeX, Markdown, HTML), spreadsheets (ODS, XLSX, XLS, CSV),
  presentations (ODP, PPTX, PPT), audio, video, 3D models, PDF,
  eBooks, archives through VirtualFS. → [Files, text and localisation](Files-Text-And-Localisation.md)
- Document models: `UCRichDocument` (word processing), spreadsheets,
  presentations, `VectorStorage::VectorDocument`, `UCRasterDocument` (layers).
- JSON (`UltraCanvasJSON`), HTML and CSS reading (`HTMLReader`), CSV
  import / export, ZIP packages, compound files, Office encryption.
- File-system services: folder watcher, trash, file locks, file times, file
  associations, desktop entries, shell links, removable drives, network
  places, portable devices, thumbnails and previews, host file icons.

## Text and languages

- UTF-8 throughout, UTF-8 file paths (`PathToUtf8` / `PathFromUtf8`).
- Translations with `Tr`, `TrC`, `TrN` (plurals), `TrF` (formatted) and
  catalogues per language; system language detection; a localisation editor.
- Spell checking, language detection, emoji settings, locale-free number
  formatting for files.

## Platform services

- Clipboard (text, rich text, images, files, Office formats) and clipboard
  history; drag and drop.
- Accessibility bridge to the platform's screen readers (roles, names,
  states, actions on every element).
- Notifications and their activation, message centre (UltraMessage).
- Settings and settings watchers, data / settings / cache folders, disk
  caches, appearance settings, proxy settings.
- Hardware info, geolocation, wireless, volume monitor, audio and video
  devices, printers / scanners / cameras (IODeviceManager), network monitor.
- Desktop shell integration, hosted apps, Mac bundles, Windows MSIX.

## Sibling modules

UltraNet (HTTP, WebSocket, FTP, TCP / UDP, TLS, DNS), UltraAI (LLM, speech,
image / video / music generation), UltraDatabase, UltraCrypt, UltraVault,
VirtualFS (archives and compression), VideoFX, AudioFX, PixelFX, OCR,
QR codes, UltraIM, UltraRTC, UltraMessage, UltraLocalisation, UltraSite,
UltraCloud, UltraWin, WasmHost. → [Modules](Modules.md)

## Tooling

- A demo application (`UltraCanvasDemo`) that shows every element live, with
  a layout audit (`--audit-layout`, `--pseudo-loc`).
- Source checks run in CI: element reuse, catalogue completeness, callback
  ownership cycles, doc examples compile, locale numbers, translations,
  platform macro names, plug-in ABI.
- Packaging scripts for Linux (AppImage, packages), macOS (bundles,
  notarisation), Windows (installer, MSIX) and an SDK bundle.
