# Introduction

## What UltraCanvas is

UltraCanvas is a **C++20 framework for graphical applications**. It gives an
application everything between `main()` and the pixels on the screen:

- **windows** and the platform event loop,
- a **layout engine** modelled on CSS (block, flex, grid, table),
- an **event system** - mouse, keyboard, text input and IME, touch and
  gestures, drag and drop, focus, timers,
- a **2D vector renderer** with paths, gradients, patterns, blend modes,
  groups and masks, and Unicode text layout,
- a catalogue of **elements** (widgets): text entry, buttons and choices,
  lists, trees and tables, menus and toolbars, pickers and dialogs, media
  players and viewers, and about seventy chart, diagram, code and document
  elements,
- **services** an application needs anyway: a universal file loader, a
  clipboard, translations, settings, themes that follow the desktop,
  accessibility, notifications, single-instance handling,
- and **sibling modules** that are UI-free: networking (UltraNet), AI
  (UltraAI), databases (UltraDatabase), cryptography (UltraCrypt), secrets
  (UltraVault), archives (VirtualFS), video (VideoFX), messaging and more.

It is the foundation of ULTRA OS and of the applications in this repository -
an office suite (UltraOffice, Texter), a file manager (UltraFiler), a paint
program (UltraPaint), a mail client (UltraMail), a video editor (UltraVideo),
a smart-home centre and a few dozen more. They are all written against the
same public API described in this wiki.

## Platforms

| Platform | Platform layer |
|---|---|
| Linux | `OS/Linux/` - X11, and Wayland in `OS/Linux/Wayland/` |
| Windows | `OS/MSWindows/` - Win32 |
| macOS | `OS/MacOS/` - Cocoa |
| BSD | `OS/BSD/` |
| WebAssembly | `OS/WASM/` - a browser canvas |
| Android, iOS | `OS/Android/`, `OS/iOS/` |
| ULTRA OS | the Linux layer, with its own desktop shell |

Drawing is the same on every platform: one 2D backend built on Cairo with
Pango text layout (`UltraCanvas/libspecific/Cairo/`) sits behind the
`IRenderContext` interface, so an element renders identically everywhere.
Platform code lives only under `UltraCanvas/OS/<Platform>/`; everything else
is shared. An application never includes a platform header from the
framework - it includes `UltraCanvasApplication.h`, `UltraCanvasWindow.h` and
the element headers, and the build picks the right implementation.

## Design principles

1. **One API everywhere.** The same source compiles for every platform. The
   few places an application's entry point differs (X11 threading on Linux,
   `WinMain` on Windows) are shown in [Getting started](Getting-Started.md).
2. **Everything visible is an element.** If it takes input, shows a picture or
   presents a value, it is an element - use the framework's or add one. Hand
   -painting a control with `DrawText` and a private caret is treated as a
   defect, because it loses the caret, selection, clipboard, undo, IME,
   focus, theming, DPI scaling and accessibility the element already has
   (`scripts/check_ui_reuse.py` enforces it).
3. **Layout, not coordinates.** Containers describe how they place their
   children (a flex row, a grid ...) and the engine measures and places
   everything each frame. See [Layout](Layout.md).
4. **Shared ownership, raw back-references.** Elements are held in
   `std::shared_ptr`; callbacks capture them raw so the tree never keeps
   itself alive. See [Events and callbacks](Events-and-Callbacks.md#ownership).
5. **Wrapped engines.** Third-party libraries (Cairo, yyjson, FFmpeg,
   poppler ...) are always behind an UltraCanvas-owned API, so a public header
   never exposes a third-party type and a backend can be swapped.
6. **Text is UTF-8, paths are UTF-8, numbers in files are locale-free.**
   Every string the API takes or returns is UTF-8, file paths are converted
   with `PathToUtf8` / `PathFromUtf8`, and a number written into a file goes
   through `FormatFloatClassic`, never the C++ locale.
7. **Every visible text is translatable.** Text a person reads goes through
   `Tr()`. See [Files, text and localisation](Files-Text-And-Localisation.md).
8. **PascalCase names, understandable on their own.** `SetText`,
   `GetSelectedIndex`, `onSelectionChanged`.

## Architecture

```
 Application code
   │  elements, windows, dialogs, callbacks
   ▼
 UltraCanvas public API            UltraCanvas/include/  (+ include/Plugins/)
   ├─ Elements (UltraCanvasUIElement → UltraCanvasContainer → ...)
   ├─ CSSLayout engine             include/CSSLayout/
   ├─ Events (UCEvent), focus, timers, drag and drop
   ├─ IRenderContext (2D drawing, text layout, images)
   ├─ Services: FileLoader, Clipboard, Theme, Localisation, Settings, ...
   └─ Element plug-ins: charts, diagrams, documents, vector, video, ...
   ▼
 Shared implementation             UltraCanvas/core/, libspecific/
   ▼
 Platform layer                    UltraCanvas/OS/{Linux,MSWindows,MacOS,...}
   ▼
 OS windowing                      X11 / Wayland, Win32, Cocoa, browser, ...
 (drawing: Cairo + Pango behind IRenderContext on every platform)
```

Sibling modules (UltraNet, UltraAI, VirtualFS, ...) sit beside UltraCanvas:
they have no UI of their own and can be used from a console program too.

### The element hierarchy

Every element derives from [`UltraCanvasUIElement`](Elements/UltraCanvasUIElement.md),
which is also a layout node (`CSSLayout::Element`). An element that holds other
elements derives from [`UltraCanvasContainer`](Elements/UltraCanvasContainer.md),
which adds children, scrolling and the container's layout mode. A window
([`UltraCanvasWindowBase`](Elements/UltraCanvasWindowBase.md)) is itself a
container. Charts and diagrams derive from family bases
(`UltraCanvasChartElementBase`, the diagram bases) that add axes, legends,
tooltips and export.

```
UltraCanvasUIElement
 ├─ UltraCanvasButton, UltraCanvasLabel, UltraCanvasTextInput, UltraCanvasSlider, ...
 ├─ UltraCanvasChartElementBase → UltraCanvasLineChartElement, UltraCanvasBarChartElement, ...
 └─ UltraCanvasContainer
     ├─ UltraCanvasTabbedContainer, UltraCanvasSplitPane, UltraCanvasGroupBox, ...
     └─ UltraCanvasWindowBase → UltraCanvasWindow → UltraCanvasModalDialog
```

## Where the documentation lives

| Need | Where |
|---|---|
| This wiki: overview, concepts, full element reference | `Docs/Wiki/` |
| Which element to use for a need | [`Docs/UltraCanvas/UltraCanvasUIElements.md`](../UltraCanvas/UltraCanvasUIElements.md) |
| Worked, compiled examples per component | `Docs/UltraCanvas/UltraCanvas<Component>*.md` |
| Sibling modules | `Docs/Modules/<Name>/README.md` |
| The module registry and public surfaces | [`Masterfile_modules.md`](../../Masterfile_modules.md) |
| Building an app step by step (with an AI assistant) | [`Docs/GettingStarted.md`](../GettingStarted.md) |
| The layout engine in depth | [`Docs/CSSLayout.md`](../CSSLayout.md) |
| Repository rules for contributors and AI agents | [`AGENTS.md`](../../AGENTS.md) |
| Live demos of every element | `Apps/DemoApp` (`UltraCanvasDemo`) |

Next: [Feature overview](Feature-Overview.md) · [Getting started](Getting-Started.md)
