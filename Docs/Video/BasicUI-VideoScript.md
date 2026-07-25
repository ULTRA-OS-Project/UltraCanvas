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

**Menus** — Menus are the command center of a desktop application: they organize
every available action into a compact, discoverable hierarchy. UltraCanvas covers
the full range — a classic main menu bar across the top of the window, context
menus that open on right-click, and free-floating popup menus. In the demo you can
navigate nested submenus, toggle menu items that behave as checkboxes or radio
buttons, and see how the same menu API can be completely restyled with custom
colors and fonts.

**Toolbar** — A toolbar puts the most frequently used actions one click away,
usually as a row of icon buttons under the menu bar. The demo shows the classic
horizontal toolbar, a vertical variant for tool palettes on the side of a window,
and a ribbon-style toolbar that groups commands into tabbed sections, as known
from modern office applications.

**Tabs** — Tabbed containers let several views share the same screen space, with
only one visible at a time — the standard pattern for settings pages, editors, and
browsers. In the demo you see tabs positioned on top, tabs on the side for
navigation-heavy layouts, and closable tabs with a close button on each tab, as
you would use for open documents.

**Split Pane** — A split pane divides the window into resizable regions separated
by a draggable divider — think of a file manager or an IDE, where a navigation
panel sits next to the main content. The demo shows horizontal and vertical
splits in the style of VSCode, and demonstrates how splits can be nested inside
each other to build complete multi-panel workspaces.

**Layout System** — Rather than positioning every element with fixed pixel
coordinates, the layout system computes positions and sizes automatically and
keeps the interface consistent when the window is resized. The demo walks through
the three layout managers: vertical and horizontal box layouts that stack elements
in one direction, a grid layout that aligns elements in rows and columns, and a
flex layout that distributes and wraps children dynamically.

**Segmented Control** — A segmented control presents a small set of mutually
exclusive options as one connected strip of buttons — a compact, modern
alternative to a group of radio buttons, typically used for view switches like
"day, week, month". The demo renders the same control in bordered, iOS, flat,
and bar styles, shows a toggle mode where a segment can be deselected again, and
a fit-to-content mode where each segment sizes itself to its label.

**Group Box** — A group box visually gathers related controls under a common
title, giving structure to forms and settings dialogs. The demo shows the classic
framed style, a header style, and a borderless flat style, with configurable
title alignment. Two interactive variants stand out: the checkable group, whose
title checkbox enables or disables everything inside, and the collapsible group,
which folds away to save space.

**Text Input** — The text input is the workhorse of every form: a field where the
user types data. UltraCanvas adds validation and formatting on top. In the demo
you type into a single-line field, a multi-line text area, a password field with
masked characters, and a numeric field that only accepts numbers.

**AutoComplete** — AutoComplete extends a text input with a live suggestion list
that filters while the user types — familiar from search boxes and address
fields. The demo feeds the suggestions in two ways: from a fixed, static item
list, and from a dynamic provider callback that computes matches on the fly, and
lets you try both in an interactive playground.

**Label** — The label is the simplest element — it displays text — but it carries
the typography of the whole interface. The demo shows plain body-text labels,
large header text for titles and sections, and status labels whose color and
style communicate a state at a glance.

**Button** — The button is the fundamental trigger for user actions. Beyond the
standard push button, the demo shows icon buttons that combine a glyph with text
or stand alone, toggle buttons that stay pressed to represent an on/off state,
and the distinctive three-section button — a split button whose left, middle, and
right zones can carry separate actions.

**Dropdown / ComboBox** — When the user must pick from a list but screen space is
scarce, a dropdown collapses the choice into a single line that expands on click.
The demo shows the simple dropdown for plain selection, the editable combo box
that also accepts free typed input, and a multi-select variant where several
entries can be checked at once.

**Checkbox / Radio / Switch** — These are the classic controls for yes/no
decisions and exclusive choices. The demo covers the standard two-state checkbox,
the tri-state checkbox whose third, indeterminate state is useful for "partially
selected" parent items, the modern switch toggle for on/off settings, and radio
button groups where selecting one option deselects the others.

**Slider** — A slider lets the user pick a value from a continuous range by
dragging a handle — perfect for volume, brightness, or zoom, where the relative
position matters more than the exact number. The demo shows horizontal and
vertical orientations, and a range slider with two handles that selects an entire
interval, such as a price range in a filter.

**Spinner / SpinBox** — Where the slider is approximate, the spinner is precise:
a numeric field with arrow buttons for stepping the value up and down, which also
responds to arrow keys, the mouse wheel, and direct typing. The demo steps
through integer and decimal spinners, a list spinner that cycles through
predefined values instead of numbers, and a horizontal stepper layout with the
buttons on either side.

**Scrollbars** — Scrollbars navigate content that is larger than its viewport.
In UltraCanvas they are standalone, fully styleable elements rather than fixed
system widgets. The demo makes that tangible: several preset styles, custom color
schemes, control over corner radius and end shapes, horizontal orientation — and
as a highlight, a scrollbar whose handle is a custom SVG graphic.

**Breadcrumb** — A breadcrumb shows the user where they are inside a hierarchy —
a folder tree, a website, a document structure — and every segment of the path is
clickable to jump back. The demo is extensive: default, compact, pills,
file-explorer, and web-docs styles, configurable separators, segments with icons
or dropdown menus, and a live navigation example. It also demonstrates three
strategies for paths that grow too long: collapsing middle segments, ellipsizing,
and shrinking the text.

**Gauges** — Gauges turn a numeric value into an instrument reading — the natural
choice for dashboards, monitoring tools, and device UIs. One mode-driven
component covers them all. In the demo you see round analog dials with needles,
progress and LED bars, circular rings, and a set of specialized instruments:
battery indicators, thermometers, clocks, and digital display panels.

**Alert / Message Box** — When the application must interrupt the user — to
report a result, warn about a problem, or ask for confirmation — it raises a
modal, always-on-top alert dialog. The demo triggers the full palette: info and
success messages, warning and error dialogs, a yes/no confirmation, and a rich
alert with an expandable details section and custom buttons.

**Pagination** — Pagination splits a large dataset into pages and gives the user
a navigation strip to move between them — indispensable for tables and search
results. The demo shows numbered pagination with ellipsis windowing, so even
thousands of pages stay compact, a variant constructed directly from a total item
count and page size, and space-saving compact and simple modes.

**Rating** — The rating element displays or collects a score — the familiar row
of stars from reviews and feedback forms. The demo shows star ratings with whole
and half steps, alternative circle and square shapes, a read-only mode for
displaying an average score, and fully custom appearance via user-supplied SVG
symbols for the on, off, and half states.

**Stepper / Wizard** — A stepper guides the user through a multi-step process —
an installation, a checkout, an onboarding flow — and shows at a glance which
steps are done, active, and still ahead. The demo walks a horizontal wizard
forward and backward, adds step descriptions and an error state for a failed
step, switches to vertical orientation, and shows dot markers with non-linear
navigation, where the user may jump between steps freely.

**Chip / Tag Input** — Chips are compact tokens that represent small pieces of
information — tags, filters, email recipients. The demo shows chips in filled and
outlined styles, closable chips with a remove button, selectable filter chips
that toggle like switches, and the tag input field: a text box in which confirmed
entries turn into chips that wrap across multiple lines.

**Badge** — Badges are small, attention-grabbing indicators for counts and
statuses — the unread counter on a mail icon is the classic example. The demo
shows colored status pills with text, numeric counters that overflow gracefully
to "99+", minimal status dots, and overlay badges anchored to the corner of an
icon.

## 4. Conclusion — Basic UI Elements

That's the Basic UI layer of UltraCanvas: twenty-four production-ready components,
all fully implemented, all rendered by the framework itself, all pixel-identical on
Windows, Linux, macOS, and UltraOS. Each follows the same API pattern — create the
element, configure it through properties or the fluent builder, attach a callback —
and each is backed by source code and documentation directly inside the demo app.

These components are the building blocks. In the next sections, we'll go beyond
the basics: advanced text editing, tree and list views, charts, diagrams, vector
graphics, and multimedia — all on the same unified UltraCanvas API.
