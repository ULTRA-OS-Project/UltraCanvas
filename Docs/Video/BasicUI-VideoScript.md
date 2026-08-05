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

### Menus

Menus are the command center of a desktop application: they organize every
available action into a compact, discoverable hierarchy. UltraCanvas covers the
full range — a classic main menu bar across the top of the window, context menus
that open on right-click, and free-floating popup menus.

Shown in the demo:

- Main menu bar, context menu, and popup menu
- Nested submenu navigation
- Menu items with checkbox and radio behavior
- Fully restyled menus with custom colors and fonts

### Toolbar

A toolbar puts the most frequently used actions one click away, usually as a row
of icon buttons under the menu bar.

Shown in the demo:

- Classic horizontal toolbar
- Vertical toolbar for tool palettes on the side of a window
- Ribbon-style toolbar that groups commands into tabbed sections, as known from
  modern office applications

### Tabs

Tabbed containers let several views share the same screen space, with only one
visible at a time — the standard pattern for settings pages, editors, and
browsers.

Shown in the demo:

- Tabs positioned on top
- Side tabs for navigation-heavy layouts
- Closable tabs with a close button on each tab, as used for open documents

### Split Pane

A split pane divides the window into resizable regions separated by a draggable
divider — think of a file manager or an IDE, where a navigation panel sits next
to the main content.

Shown in the demo:

- Horizontal and vertical splits in the style of VSCode
- Nested splits building complete multi-panel workspaces

### Layout System

Rather than positioning every element with fixed pixel coordinates, the layout
system computes positions and sizes automatically and keeps the interface
consistent when the window is resized.

Shown in the demo:

- Vertical and horizontal box layouts that stack elements in one direction
- Grid layout that aligns elements in rows and columns
- Flex layout that distributes and wraps children dynamically

### Segmented Control

A segmented control presents a small set of mutually exclusive options as one
connected strip of buttons — a compact, modern alternative to a group of radio
buttons, typically used for view switches like "day, week, month".

Shown in the demo:

- The same control in bordered, iOS, flat, and bar styles
- Toggle mode, where a segment can be deselected again
- Fit-to-content mode, where each segment sizes itself to its label

### Group Box

A group box visually gathers related controls under a common title, giving
structure to forms and settings dialogs.

Shown in the demo:

- Classic framed style, header style, and borderless flat style
- Configurable title alignment
- Checkable group, whose title checkbox enables or disables everything inside
- Collapsible group that folds away to save space

### Text Input

The text input is the workhorse of every form: a field where the user types
data. UltraCanvas adds validation and formatting on top.

Shown in the demo:

- Single-line input field
- Multi-line text area
- Password field with masked characters
- Numeric field that only accepts numbers

### AutoComplete

AutoComplete extends a text input with a live suggestion list that filters while
the user types — familiar from search boxes and address fields.

Shown in the demo:

- Suggestions from a fixed, static item list
- Suggestions from a dynamic provider callback that computes matches on the fly
- An interactive playground to try both

### Label

The label is the simplest element — it displays text — but it carries the
typography of the whole interface.

Shown in the demo:

- Plain body-text labels
- Large header text for titles and sections
- Status labels whose color and style communicate a state at a glance

### Button

The button is the fundamental trigger for user actions, and UltraCanvas goes
well beyond the standard push button.

Shown in the demo:

- Standard push buttons
- Icon buttons that combine a glyph with text or stand alone
- Toggle buttons that stay pressed to represent an on/off state
- The distinctive three-section button — a split button whose left, middle, and
  right zones can carry separate actions

### Dropdown / ComboBox

When the user must pick from a list but screen space is scarce, a dropdown
collapses the choice into a single line that expands on click.

Shown in the demo:

- Simple dropdown for plain selection
- Editable combo box that also accepts free typed input
- Multi-select variant where several entries can be checked at once

### Checkbox / Radio / Switch

These are the classic controls for yes/no decisions and exclusive choices.

Shown in the demo:

- Standard two-state checkbox
- Tri-state checkbox whose indeterminate state is useful for "partially
  selected" parent items
- Modern switch toggle for on/off settings
- Radio button groups where selecting one option deselects the others

### Slider

A slider lets the user pick a value from a continuous range by dragging a
handle — perfect for volume, brightness, or zoom, where the relative position
matters more than the exact number.

Shown in the demo:

- Horizontal and vertical orientations
- Range slider with two handles that selects an entire interval, such as a
  price range in a filter

### Spinner / SpinBox

Where the slider is approximate, the spinner is precise: a numeric field with
arrow buttons for stepping the value up and down, which also responds to arrow
keys, the mouse wheel, and direct typing.

Shown in the demo:

- Integer and decimal spinners
- List spinner that cycles through predefined values instead of numbers
- Horizontal stepper layout with the buttons on either side

### Scrollbars

Scrollbars navigate content that is larger than its viewport. In UltraCanvas
they are standalone, fully styleable elements rather than fixed system widgets.

Shown in the demo:

- Several preset styles
- Custom color schemes
- Control over corner radius and end shapes
- Horizontal orientation
- A scrollbar whose handle is a custom SVG graphic

### Breadcrumb

A breadcrumb shows the user where they are inside a hierarchy — a folder tree, a
website, a document structure — and every segment of the path is clickable to
jump back.

Shown in the demo:

- Default, compact, pills, file-explorer, and web-docs styles
- Configurable separators
- Segments with icons or dropdown menus
- A live navigation example
- Three overflow strategies for long paths: collapsing middle segments,
  ellipsizing, and shrinking the text

### Gauges

Gauges turn a numeric value into an instrument reading — the natural choice for
dashboards, monitoring tools, and device UIs. One mode-driven component covers
them all.

Shown in the demo:

- Round analog dials with needles
- Progress and LED bars
- Circular rings
- Specialized instruments: battery indicators, thermometers, clocks, and
  digital display panels

### Alert / Message Box

When the application must interrupt the user — to report a result, warn about a
problem, or ask for confirmation — it raises a modal, always-on-top alert
dialog.

Shown in the demo:

- Info and success messages
- Warning and error dialogs
- Yes/no confirmation
- Rich alert with an expandable details section and custom buttons

### Pagination

Pagination splits a large dataset into pages and gives the user a navigation
strip to move between them — indispensable for tables and search results.

Shown in the demo:

- Numbered pagination with ellipsis windowing, so even thousands of pages stay
  compact
- A variant constructed directly from a total item count and page size
- Space-saving compact and simple modes

### Rating

The rating element displays or collects a score — the familiar row of stars from
reviews and feedback forms.

Shown in the demo:

- Star ratings with whole and half steps
- Alternative circle and square shapes
- Read-only mode for displaying an average score
- Fully custom appearance via user-supplied SVG symbols for the on, off, and
  half states

### Stepper / Wizard

A stepper guides the user through a multi-step process — an installation, a
checkout, an onboarding flow — and shows at a glance which steps are done,
active, and still ahead.

Shown in the demo:

- A horizontal wizard navigated forward and backward
- Step descriptions and an error state for a failed step
- Vertical orientation
- Dot markers with non-linear navigation, where the user may jump between steps
  freely

### Chip / Tag Input

Chips are compact tokens that represent small pieces of information — tags,
filters, email recipients.

Shown in the demo:

- Chips in filled and outlined styles
- Closable chips with a remove button
- Selectable filter chips that toggle like switches
- The tag input field: a text box in which confirmed entries turn into chips
  that wrap across multiple lines

### Badge

Badges are small, attention-grabbing indicators for counts and statuses — the
unread counter on a mail icon is the classic example.

Shown in the demo:

- Colored status pills with text
- Numeric counters that overflow gracefully to "99+"
- Minimal status dots
- Overlay badges anchored to the corner of an icon

## 4. Conclusion — Basic UI Elements

That's the Basic UI layer of UltraCanvas: twenty-four production-ready components,
all fully implemented, all rendered by the framework itself, all pixel-identical on
Windows, Linux, macOS, and UltraOS. Each follows the same API pattern — create the
element, configure it through properties or the fluent builder, attach a callback —
and each is backed by source code and documentation directly inside the demo app.

These components are the building blocks. In the next sections, we'll go beyond
the basics: advanced text editing, tree and list views, charts, diagrams, vector
graphics, and multimedia — all on the same unified UltraCanvas API.
