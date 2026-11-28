# UltraCanvas Wiki

**UltraCanvas** is a modular, cross-platform **C++20 UI and rendering
framework** for Linux (X11 and Wayland), Windows, macOS, BSD, WebAssembly, Android,
iOS and ULTRA OS. One code base, one API: windows, a CSS-style layout engine,
an event system, a 2D vector renderer, about <!-- count:elements -->202<!-- /count --> ready-made
elements - from a button to a Gantt chart, a spreadsheet or a slide editor -
and sibling modules for networking, AI, databases, cryptography, archives and
video.

This wiki documents all of it: what the framework is, how a program is put
together, and a reference page for **every element** with **every callback**
(<!-- count:callbacks -->836<!-- /count --> in all), every option, setter and query, and the option
structs and enums it is configured with.

## Start here

| Page | What it covers |
|---|---|
| [Introduction](Introduction.md) | What UltraCanvas is, its design principles, the platforms and the architecture |
| [Feature overview](Feature-Overview.md) | Everything the framework can do, in one page |
| [Getting started](Getting-Started.md) | Build the tree, write a first application, add it to the build |
| [Core concepts](Core-Concepts.md) | Application, windows, elements, identifiers, ownership, the element tree |
| [Layout](Layout.md) | The CSS-style layout engine: flex rows and columns, grids, tables, sizing |
| [Events and callbacks](Events-and-Callbacks.md) | How input reaches an element, callbacks, event filters, timers, threads |
| [Styling and themes](Styling-And-Themes.md) | Style structs, colour roles, light / dark / high-contrast, the host's look |
| [Rendering](Rendering.md) | `IRenderContext`: drawing, paths, gradients, text, images, custom elements |
| [Files, text and localisation](Files-Text-And-Localisation.md) | `UltraCanvasFileLoader`, UTF-8 paths, `Tr()` translations, clipboard |
| [Modules](Modules.md) | UltraNet, UltraAI, UltraDatabase, VirtualFS, VideoFX and the other siblings |

## Element groups

Every element belongs to one group. A group page lists its elements with what
each is for and the callbacks it has; each element links to its own page.

<!-- groups:begin -->
<!-- groups:end -->

## Reference

- [Element index](Element-Index.md) - every element class A-Z, with its header
- [Callback index](Callback-Index.md) - every callback of every element A-Z

## How this wiki is made

The guide pages (this one, *Introduction* to *Modules*) are written by hand.
The reference - `Groups/`, `Elements/`, the two indexes and `_Sidebar.md` - is
**generated from the headers** by `scripts/generate_wiki.py`, using the
element catalogue
([`Docs/UltraCanvas/UltraCanvasUIElements.md`](../UltraCanvas/UltraCanvasUIElements.md))
for the grouping and the one-line purpose of each element. It therefore lists
exactly what the code declares. After changing a public header or the
catalogue, regenerate it:

```bash
python3 scripts/generate_wiki.py
```

`python3 scripts/generate_wiki.py --check` exits non-zero when the generated
pages are out of date.

The deeper, example-driven documentation - about 300 pages of explained,
compiled C++ - stays in [`Docs/UltraCanvas/`](../UltraCanvas/README.md); every
element page links to the guides that cover it.
