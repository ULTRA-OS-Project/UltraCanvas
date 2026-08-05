# UltraCanvas API — Video Script
## Episode: Complex UI Elements

Narration text for the UltraCanvas API video series, based on the UltraCanvas
Demo application (`Apps/DemoApp`). Style: technical, spoken narration.
`[SCREEN: …]` lines are visual cues for the editor, not narration.

---

## 1. Introduction — The UltraCanvas Idea

[SCREEN: UltraCanvas logo, then the Demo application opening]

UltraCanvas is a modern, cross-platform C++20 UI and rendering framework.
The idea behind it is simple: one codebase, one API — running natively on
Windows, Linux, macOS, BSD, WebAssembly, and ULTRA OS. All
platform-specific code — X11 and Cairo on Linux, Direct2D on Windows, Core
Graphics on macOS — is cleanly isolated behind a unified API for windowing,
rendering, and event handling.

On top of this rendering core, UltraCanvas provides a comprehensive UI
toolkit: from simple buttons and labels all the way to charts, diagrams,
document viewers, and complete editing engines. Every component is created
programmatically in C++, arranged with flexible layout containers, and wired
up with simple callback functions — no separate markup language, no code
generators, just clean C++ code.

The best way to explore the framework is the UltraCanvas Demo application.
It presents every component in a tree view on the left — and by the way,
that navigation tree is itself an UltraCanvas element. For every entry, the
header links directly to the demo source file and the component
documentation, so everything you see on screen, you can immediately read as
code.

---

## 2. The "Complex UI Elements" Section

[SCREEN: Expanding the "Complex UI Elements" node in the demo tree]

In a previous episode we looked at the Basic UI Elements — buttons, labels,
text inputs, and the other building blocks of an interface. Today we move
one level up, to the section called "Complex UI Elements".

These are the components that go beyond a single control. They manage
structured data, they have their own editing and rendering engines, and they
follow a model-view architecture where your data, its presentation, and the
selection behavior are cleanly separated.

The section contains four elements: the Advanced Text Area, the Tree View,
the Spreadsheet engine, and the List View. Let's look at each of them in the
demo application.

---

## 3. The Elements

### 3.1 Advanced Text Area

[SCREEN: Selecting "Advanced Text Area"; three code editors with C++, Python and Pascal code]

The Advanced Text Area is a full multi-line text editor with a built-in
syntax highlighting tokenizer. It is the component you would reach for when
building a code editor, a script console, or a configuration editor inside
your application.

The demo page shows three independent editor instances, each highlighting a
different language. The first one displays C++ code using the built-in dark
code style — a single call to ApplyDarkCodeStyle is all it takes. The second
shows Python with the standard light theme. And the third one renders Pascal
with a completely custom theme: the demo defines its own TextAreaStyle,
assigning individual colors and font attributes to every token class —
keywords in bold blue, comments in italic green, strings in red, numbers in
teal, and so on. This shows how deep the styling API goes: every visual
aspect, from the background to individual token styles, is under your
control.

All three editors show line numbers and highlight the current line, and they
are fully editable — with selection, copy and paste, and undo and redo. At
the bottom of the page, a row of controls manipulates all three editors live
through the API: you can increase and decrease the font size, toggle the
line number gutter, switch syntax highlighting on and off, or clear the
content entirely.

### 3.2 Tree View

[SCREEN: Selecting "Tree View"; three tree examples side by side]

The Tree View displays hierarchical data — folder structures, categories,
object trees — with expandable nodes, icons, and selection callbacks. As
mentioned earlier, the demo application's own navigation is built with this
very element, so you are looking at it in production the whole time.

The demo page presents three configurations. The first is a file-explorer
style tree: a "My Computer" root with drives, folders, and files, each node
carrying its own icon. A selection callback fires whenever a node is chosen.
Two checkboxes below let you toggle behavioral options at runtime —
automatically expanding the selected node, and automatically selecting the
first child when a node opens.

The second tree demonstrates multi-selection: with the selection mode set to
Multiple, you can Control-click to select several nodes at once.

The third example is the most advanced: a debugger-style "Variables" panel.
It uses the Modern columnar display mode, where every node shows Name, Type,
and Value in aligned columns, and group headers — Line, Loop, and function —
render as full-width section bars. A checkbox switches live between the
Classic single-text layout and the Modern column layout, and a second
checkbox changes the sort order from alphabetic to last-access — exactly
what an IDE debugger needs.

### 3.3 Spreadsheet Engine

[SCREEN: Selecting "Spreadsheet engine"; the grid with the monthly sales report]

The Spreadsheet engine is exactly what the name promises: a complete,
editable spreadsheet grid with cells, formulas, and number formatting —
ready to be embedded wherever your application needs tabular data entry or
reporting.

When the demo opens, the element loads a real OpenDocument spreadsheet file:
a monthly sales and chargeback report. The grid demonstrates per-column
widths, a taller bold header row, and proper number formats — currency
values rendered with a Euro symbol and percentages with two decimal places.
The totals row is computed with live SUM formulas, and here is a nice
detail: when you select a totals cell, the range of cells that formula
covers is highlighted in the grid, just like in a desktop spreadsheet
application.

The toolbar above the grid exercises the file interface. The Open button
loads spreadsheet files in ODS, Excel, and CSV formats through the
UltraCanvas file loader. The Import CSV button goes one step further: it
opens a text-import dialog with a live preview, where you choose the
character set, the field separators, the starting row, and number
recognition — and the grid is filled with exactly those settings. And the
Save button writes the sheet back out as ODS, Excel, CSV, or TSV, with an
export options dialog for the text formats covering separator, quoting,
charset, and line endings.

### 3.4 List View

[SCREEN: Selecting "List View"; four list examples and the status feedback panel]

The List View presents item collections — and it is a good showcase of the
model-view-delegate architecture used throughout UltraCanvas. A model
supplies the data, a delegate controls how each row is drawn, and a
selection object defines how the user can select items. You combine these
three pieces to get exactly the list you need.

The demo page shows four variations. The first is a simple single-selection
list — ten items backed by a simple list model, each with a tooltip.
Clicking or double-clicking an item fires a callback, and the demo prints
the event into a status panel in the corner, so you can watch the callback
API respond in real time.

The second is a multi-column list with a header row: file names, types,
sizes, and modification dates, with per-column alignment, grid lines, and
multi-selection via Control-click — essentially the core of a file manager
view, defined in a few lines of code.

The third example focuses on styling: alternating row colors, a custom
purple selection theme, and hover highlighting, all configured through a
ListViewStyle structure together with a customized delegate.

And the fourth uses a delegate with icons: a list of programming languages,
each row rendered with its own icon, custom icon size, spacing, and row
height.

---

## 4. Conclusion

[SCREEN: Zooming back out to the demo tree, briefly scrolling over the categories]

That was the Complex UI Elements section of UltraCanvas: a syntax-
highlighting text editor, a hierarchical tree view with columnar display
modes, a full spreadsheet engine with formulas and file import and export,
and a model-view-based list component.

Together with the Basic UI Elements from the previous episode, these
components cover the complete foundation of a desktop-class application —
and they all follow the same principles: one C++ API on every platform,
programmatic construction, callback-driven events, and styling that goes as
deep as you want to take it.

Everything shown here is available in the UltraCanvas Demo application, with
a direct link from every page to its source code and documentation. In the
next episode, we will continue through the component tree — from bitmap and
vector graphics to charts, diagrams, and beyond.

Thanks for watching — and happy coding with UltraCanvas.
