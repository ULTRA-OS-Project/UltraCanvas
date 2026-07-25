# UltraCanvas API — Video Script: Basic UI Elements

Technical narration text for a video presenting the UltraCanvas API, based on the
"Basic UI Elements" section of the UltraCanvas demo application.

---

## 1. Introduction — The UltraCanvas Idea

UltraCanvas is a modern, cross-platform C++20 UI and rendering framework. The core
idea is simple: one codebase, one unified API — running natively on Windows, Linux,
macOS, and UltraOS.

Instead of wrapping native widgets, UltraCanvas renders every element itself through
its own rendering engine — built on Cairo, Direct2D, and Core Graphics — so your
application looks and behaves identically on every platform. Platform-specific code
is cleanly isolated; everything else — windowing, event handling with bubbling and
capture phases, layout, and drawing — goes through one consistent, modern C++ API
built on shared pointers, fluent builders, and std::function callbacks.

The result: rich graphical applications, from classic desktop UIs to data
visualization and multimedia — with a single API to learn.

## 2. Introduction — The "Basic UI Elements" Section

The UltraCanvas demo application is the living catalog of the framework. On the
left, a tree view lists every component category; on the right, each selected
component runs live — not as screenshots, but as real, interactive code. Each demo
page links directly to its C++ source file and its documentation, so what you see
is exactly what you can build.

The first category in the tree is "Basic UI Elements" — the foundation of every
UltraCanvas application. Twenty-four components: menus, toolbars, buttons, inputs,
containers, and indicators. Every one of them is fully implemented across all
platforms. Let's walk through them.

## 3. The Elements

**Menus** — Various menu types and styles: a context menu, a main menu bar, popup
menus, and nested submenu navigation. Menu items support checkbox and radio states,
and the demo shows fully custom-styled menus.

**Toolbar** — Tool and action bars in horizontal and vertical orientation, plus a
ribbon-style variant for grouped, tabbed command sets.

**Tabs** — Tabbed interface containers with tabs on top or on the side, and
closable tabs for document-style interfaces.

**Split Pane** — A resizable pane splitter in the style of VSCode: horizontal and
vertical splits, and nested split layouts for complex workspaces.

**Layout System** — The automatic layout engine: vertical and horizontal box
layouts, a grid layout, and a flex layout that wraps and distributes children —
so interfaces adapt to window size without manual positioning.

**Segmented Control** — A compact control for mutually exclusive options, shown in
bordered, iOS, flat, and bar styles, with a toggle mode and fit-to-content width.

**Group Box** — A titled container that frames a set of child elements: framed,
header, and flat styles, configurable title alignment, plus checkable and
collapsible groups.

**Text Input** — Text fields with validation and formatting: single-line input, a
multi-line text area, a password field, and numeric input.

**AutoComplete** — A text input with suggestion popup, fed either by a static item
list or a dynamic provider callback, demonstrated in an interactive live demo.

**Label** — Text display with formatting and styling: basic labels, header text,
and status labels.

**Button** — Interactive buttons with various styles and states: standard buttons,
icon buttons, toggle buttons, and the distinctive three-section split button.

**Dropdown / ComboBox** — Dropdown selection controls: a simple dropdown, an
editable combo box, and multi-select support.

**Checkbox / Radio / Switch** — Interactive toggle controls: standard checkboxes,
tri-state checkboxes, switch toggles, and radio button groups.

**Slider** — Range and value selection: horizontal and vertical sliders, and a
dual-handle range slider.

**Spinner / SpinBox** — Value selection via arrow buttons, arrow keys, mouse wheel,
or direct typing: integer and decimal spinners, list-value cycling, and a
horizontal stepper layout.

**Scrollbars** — Standalone, fully styleable scrollbars: preset styles, color
options, corner-radius and end-shape control, horizontal orientation — and even a
custom SVG scroll handle.

**Breadcrumb** — Hierarchical path navigation with clickable segments. The demo
shows default, compact, pills, file-explorer, and web-docs styles, configurable
separators, dropdown segments, icons, live navigation, and three overflow
strategies: collapse, ellipsize, and shrink-text.

**Gauges** — Mode-driven gauges from a single component: analog dials, progress and
LED bars, circular rings, batteries, thermometers, clocks, and digital panels.

**Alert / Message Box** — Modal, always-on-top dialogs: info, success, warning, and
error alerts, yes/no confirmation, and rich alerts with details and custom buttons.

**Pagination** — A page-navigation strip with ellipsis windowing, construction from
a total item count, plus compact and simple modes.

**Rating** — Star and shape ratings with half-step precision: stars, circles and
squares, read-only display, and custom SVG symbols for the on, off, and half states.

**Stepper / Wizard** — A multi-step progress indicator: a horizontal wizard,
step descriptions and error states, vertical orientation, and dot markers with
non-linear navigation.

**Chip / Tag Input** — Compact chips in filled and outlined styles, closable chips,
selectable filter chips, and a wrapping tag/token input field.

**Badge** — Count and status indicators: colored status pills, counters with 99+
overflow, status dots, and overlay badges anchored to icons.

## 4. Conclusion — Basic UI Elements

That's the Basic UI layer of UltraCanvas: twenty-four production-ready components,
all fully implemented, all rendered by the framework itself, all pixel-identical on
Windows, Linux, macOS, and UltraOS. Each follows the same API pattern — create the
element, configure it through properties or the fluent builder, attach a callback —
and each is backed by source code and documentation directly inside the demo app.

These components are the building blocks. In the next sections, we'll go beyond
the basics: advanced text editing, tree and list views, charts, diagrams, vector
graphics, and multimedia — all on the same unified UltraCanvas API.
