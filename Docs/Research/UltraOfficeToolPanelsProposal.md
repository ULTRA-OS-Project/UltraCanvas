# UltraOffice — tool panels at the left instead of toolbars at the top (Proposal)

## Decisions

Taken on 2026-11-27; they replace the suggestions of §8 where they differ.

1. **The panels are the default; the toolbars remain a setting.** One
   toolbar for the general functions stays at the top in both layouts:
   File, Undo, Redo, Save, then the other general functions, and **the view
   group last** (navigator, pages, the fits, zoom) at the toolbar's right
   end. The view group is never a panel. The tool panels at the left are
   the default layout; the command tabs (Home, Insert …) with their toolbar
   remain as a setting (*Tools: panels at the left / toolbar at the top*).
2. **Values are edited in the panels.** Line spacing, space before and
   after, indents, a shape's size and position, colours and the like are
   shown with their labels and changed in place; the dialogs stay for what a
   panel does not hold.
3. **Panels can be reordered from the start**, by dragging a panel's
   header, and the order is saved (per kind, in `UltraOffice/settings.json`)
   for the next start, with which panels are open.

Phase 1 (§6), the tool table, is built: `Apps/UltraOffice/UltraOfficeTools.h`
holds each kind's tools, the command tabs' bars and the view group are built
from it, and `ToolsByPanel` gives the tools by panel, in the default order or
a saved one, for the panel stack.

## 1. Recommendation

Present UltraOffice's tools by default as a **column of tool panels at the
left of the document** instead of the command tabs (Home, Insert, Layout,
Review …) and the one toolbar row each shows: one panel per topic
(Clipboard, Font, Paragraph, Lists, Insert, Table, Picture, Review …), each a
collapsible `UltraCanvasGroupBox` in its **Header** frame style. The column
scrolls, panels open and close independently and remember it, the user
reorders them by dragging their headers and the order is kept, and panels
that only make sense for the selection (Table, Picture, Chart, Shape) appear
when they apply. Values are edited in the panels. One toolbar stays at the
top for the general functions (File, Undo, Redo, Save), with the view group
last, at its right end; the navigator stays at the right. The command tabs
with their toolbars stay too, as a setting (*Tools: panels at the left /
toolbar at the top*), so nobody loses the layout they know.

Why:

| | Toolbars at the top (today) | Panels at the left (proposed) |
|---|---|---|
| What is on screen | one tab's tools at a time; the rest one click away, behind tab names | every topic at once, each open or closed as the user likes |
| Room for each tool | a 34 px icon, a label only where it fits; long lists squeezed (font 130 px) | a labelled row per control; lists full width (font list 230 px, the size beside it) |
| A wide screen | the toolbar's width is mostly empty; the page's height is short | the spare width goes to tools; the page keeps the full height |
| Context | the same tools whatever is selected | Table / Picture / Chart / Shape panels appear with their selection |
| Settings that are values | dropdowns in a row (style, font, size) | values shown with their labels: spacing 1.15, indent 1.25 cm, colour swatches |
| Learning | tools hidden behind tab names | everything visible, grouped by topic |

It is how design and page-layout programs (Xara, Affinity, Scribus, the
properties sidebar of LibreOffice) present many settings, and it suits the
landscape screens UltraOffice mostly runs on: a 1920 × 1080 window gives the
page its full height and still has 300 px at the left for tools.

## 2. Layout

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ Start | Report.docx ● | Budget.xlsx | +                                      │  tabs
│ File  ↶ ↷ 💾                         [nav][pages][↔][▣][▥ 60%▾][－][119%▾][＋] │  command row + view group
├───────────────────────┬──────────────────────────────────────┬───────────────┤
│ ▾ Clipboard         ⓘ │                                      │ Search…       │
│   ✂ Cut  ⧉ Copy  📋 Paste│                                    │ ▾ Headings 8  │
│ ▾ Style               │          ┌──────────────┐            │   Sales       │
│   [Body text       ▾] │          │              │            │   Costs       │
│ ▾ Font                │          │    page      │            │ ▸ Tables 2    │
│   [Palatino L…   ▾][12▾]│        │              │            │ ▸ Pictures 1  │
│   B I U S  x² x₂  A▾ ▒▾│         │              │            │               │
│ ▾ Paragraph           │          │              │            │               │
│   ≡ ≡ ≡ ≡   ⇤ ⇥       │          └──────────────┘            │               │
│   Spacing  [1.15 ▾]   │                                      │               │
│   Before [0 pt] After [10 pt]                                │               │
│ ▸ Lists               │                                      │               │
│ ▸ Insert              │                                      │               │
│ ▾ Table  (in a table) │                                      │               │
│   + row ↑ ↓  + col ← →│                                      │               │
│   [Properties…]       │                                      │               │
│ ▸ Review              │                                      │               │
│ ▸ Page                │                                      │               │
├───────────────────────┴──────────────────────────────────────┴───────────────┤
│ Paragraph 3 of 29   Page view                                                │  status line
└──────────────────────────────────────────────────────────────────────────────┘
   tool panels (left)            document                        navigator (right)
```

- **The column** is about 260 px wide (160 to 400, set with the splitter, kept
  in the settings), scrolls vertically when the open panels are taller than
  the window, and has its own background (the theme's panel colour) so the
  document's grey desk stays a separate area.
- **A panel** is an `UltraCanvasGroupBox` with `GroupBoxFrameStyle::Header`,
  `SetCollapsible(true)`, the topic as its title and, where a topic has a
  dialog, an info icon whose help text names it ("More: Paragraph… (Ctrl+Shift+P)")
  and a "…" button at the right of the header that opens that dialog.
- **Inside a panel**, controls sit in rows:
  - icon buttons in a wrapping row (B I U S x² x₂), 28 × 28 px, tooltips with
    the shortcuts;
  - value controls with a label in front (Spacing, Before, After, Indent), using
    the elements the dialogs already use (spinner, dropdown, colour swatch), so
    a value is seen and changed in place instead of through a dialog;
  - full-width lists (paragraph style, font with the font picker, number
    format).
- **The command row** keeps File, Undo, Redo, Save and the other general
  functions, and the view group last, at its right end, as today; the command
  tab buttons (Home, Insert …) are gone in this mode, since the panels
  replace them.
- **Narrow windows:** below about 1100 px the column folds into an **icon
  rail** (40 px, one icon per panel); a click on an icon opens that panel as a
  flyout over the document's edge, and Escape or a click elsewhere closes it.
  The rail is also a choice of its own for people who want the page wide.

## 3. The panels per kind

Every tool of today's command tabs has a place; the order is the order of
use. *Context* panels show only while their condition holds, at the top of
the column, opened.

### 3.1 Documents

| Panel | Contents | Shown |
|---|---|---|
| Clipboard | Cut, Copy, Paste, Paste as text | always |
| Style | paragraph style list, Styles… | always |
| Font | font picker, size, B I U S, super/subscript, text colour, highlight | always |
| Paragraph | alignment (4), indent less/more, line spacing, space before/after, first-line indent, Paragraph… | always |
| Lists | bullets, numbering, level up/down, list style | always |
| Insert | table (grid picker), picture, link, page break, footnote, contents, caption, cross-reference | always |
| Page | page size, orientation, margins (presets), columns, Page setup… | always |
| Review | comment, previous/next, all comments, show comments; track changes, previous/next change, accept, reject, accept/reject all | always |
| *Table* | rows/columns insert and delete, merge/split, header row, borders, cell shading, Table properties… | caret in a table |
| *Picture* | size (width/height, keep proportions), wrapping, position, alt text, Picture… | a picture selected |
| *Chart* | type (the chart gallery), title, legend, data… | a chart selected |

### 3.2 Spreadsheets

| Panel | Contents | Shown |
|---|---|---|
| Clipboard | Cut, Copy, Paste, Paste values | always |
| Cell | font, size, B I, text and fill colour, borders | always |
| Alignment | horizontal (3), vertical (3), wrap, merge | always |
| Number | format list, decimals +/−, percent, currency | always |
| Data | sort A–Z / Z–A, filter, fill down/right, SUM, conditional formatting, validation | always |
| Insert | chart (gallery), pivot table, picture, link, comment | always |
| Page | print area, landscape, fit to width, gridlines, headings | always |
| *Chart* | type, title, legend, properties, delete | a chart selected |
| *Pivot* | the pivot field list (`UltraCanvasSpreadsheetPivotPanel`) moves into a panel here | a pivot cell active |

### 3.3 Presentations

| Panel | Contents | Shown |
|---|---|---|
| Clipboard | Cut, Copy, Paste | always |
| Slide | new slide, layout list, duplicate, delete, hide, background | always |
| Insert | text box, shapes (grid), picture, table, chart | always |
| Text | font, size, B I U S, colour, alignment, bullets, numbering, level | text or a text shape selected |
| Arrange | order (4), group/ungroup, align (6), distribute (2) | shapes selected |
| Shape | fill, line colour and width, size and position (x, y, w, h), rotation | a shape selected |
| *Picture* | crop, reset crop, size | a picture selected |
| *Table* | rows and columns, cell fill | a table selected |
| *Chart* | type, data… | a chart selected |
| Transitions | transition list, duration, advance, apply to all | always |
| Show | from the beginning, from this slide, presenter view | always |

## 4. Behaviour

- **State is remembered** per kind in `UltraOffice/settings.json`: which
  panels are open, the column width, rail or column, and the order when the
  user moves panels (dragging a panel's header up or down), from the first
  release of the panels on. A saved order names the panels by key
  (`PanelKey`); a panel it leaves out - one added later - takes its default
  place.
- **Context panels** come and go with the selection without moving the panel
  under the pointer: they are inserted above the fixed panels and the column
  keeps its scroll position relative to the panel the pointer is over.
- **The format state** (bold on, alignment centred, spacing 1.15) is shown in
  the panels exactly as the toolbar shows it today, from the same getters of
  the tool table (a toggle's `isOn`, a list's `selected`, a value's `get`);
  in a panel more of it is visible (values, not just toggles).
- **Keyboard:** F6 moves the focus between the tab strip, the panels and the
  document; inside the column, Up/Down go from panel header to panel header,
  Enter or Space opens and closes a panel, Tab walks its controls. Every
  shortcut keeps working whichever layout is chosen.
- **Accessibility:** each panel is a group with its title as name (the group
  box's accessible role), its controls in reading order.
- **One source of truth:** the hosts describe their tools once — command id,
  icon, label, tooltip, kind (button, toggle, list, value), the panel or tab
  it belongs to, and a condition — and both layouts are built from that list.
  Today each host builds its toolbars by hand (`AddTool`, `AddActionGroup`,
  `AddFontList` in `UltraOfficeDocumentHost.cpp` and its siblings); turning
  that into a table is the first step and makes the two layouts cheap to keep
  in step.

## 5. What is built where

| Layer | New or changed | Notes |
|---|---|---|
| Framework | **`UltraCanvasToolPanelStack`** (new element): a scrolling vertical stack of collapsible group boxes, with open/closed state get/set (for saving), context panels shown and hidden without a jump, the icon-rail mode with flyouts, F6 / arrow-key navigation, theme roles | built on `UltraCanvasGroupBox` (Header, collapsible, info icon, close/"…" button), `UltraCanvasContainer` scrolling, the popup stack for flyouts; documented with a catalogue row and a demo page |
| Framework | **`ToolRow`** helpers: a wrapping row of icon toggles and a labelled value row (label + spinner / dropdown / swatch) | small; the dialogs' form rows (`UltraCanvasFormLayout`) are reused for labelled values |
| Framework | `UltraCanvasGroupBox`: a header action button ("…"), a drag handle for reordering (from the first release of the panels) | small additions |
| App | the tool table per host (§4) and a builder for each layout | the refactor that makes both layouts possible |
| App | the setting *Tools: toolbar at the top / panels at the left / icon rail* (Settings → General), and panel state in settings.json | |
| App | context conditions (in a table, picture selected, …) from the hosts' existing selection state | |

## 6. Phases and size

| Phase | Scope | Size (new or changed lines) |
|---|---|---|
| 1 | The tool table per host, the current toolbars built from it (no visible change); tests that every tool is in the table (**built**: `UltraOfficeTools.h`, `UltraOfficeToolTableTest`) | ~900 app |
| 2 | `UltraCanvasToolPanelStack` and the row helpers, with reordering by dragging a header, element tests and a demo page | ~1,500 framework |
| 3 | Panels for documents (§3.1), the default layout, the toolbar a setting; value rows (spacing, indents) wired to the editor; open panels and their order saved | ~750 app |
| 4 | Spreadsheets and presentations (§3.2, §3.3); context panels; the pivot field list moved into a panel | ~900 app |
| 5 | Icon rail and flyouts, keyboard navigation, accessibility roles | ~500 framework, ~100 app |

**The table (phase 1).** A row (`ToolEntry`) has the command id (also its
widget's id), icon, label, tooltip with the shortcut, kind (`Button`,
`Toggle`, `ActionGroup`, `List` - a choice, the font list or a percentage -,
`Value` - a number with its unit and range, or a colour -, `Custom`), the
panel, the command tab it is on today with its group on that bar (a
separator between groups), a context (`AlwaysShown`, `InTable`,
`PictureSelected`, `ChartSelected`, `ShapeSelected`, `TextSelected`,
`TableSelected`, `PivotCell`) and its callbacks: `execute`, and the state
getters `isOn`, `isEnabled`, a list's `selected` / `currentFont` /
`currentPercent`, a value's `get` and `set`. The view group's rows are on
the view tab and in no panel. Rows on no tab are the panels' own: the values
the toolbars leave to the dialogs.

About **4,600 lines**, half framework. Phase 1 is worth doing whatever is
decided: it removes the hand-built toolbars' duplication and gives one place
to add a tool.

## 7. Risks and answers

- **Width taken from the page.** A 260 px column leaves a 1920 px window
  1,300 px for the page at "page width" — more than an A4 page needs at 150 %.
  Below 1,100 px the rail (40 px) takes over. The navigator at the right can
  stay closed by default.
- **Two layouts to keep in step.** Answered by the single tool table (§4):
  a tool is added once.
- **Long columns.** Panels are closed by default except Font and Paragraph
  (documents), Cell and Number (spreadsheets), Slide and Text (presentations);
  context panels open themselves and close when their selection goes.
- **Familiarity.** The panels are the default (see *Decisions*), so the
  toolbar layout stays one setting away: the Start tab says once where the
  tools went and offers the toolbar ("Use the toolbar at the top"), and the
  setting switches at once, either way.

## 8. Decisions for the user

All four were decided on 2026-11-27 (*Decisions*, at the top):

1. **Default layout:** the panels are the default; the toolbar with its
   command tabs is a setting. (The suggestion had been the other way round.)
2. **Where the view group lives** in panel mode: in the one top toolbar, as
   its last group at the right end, after File, Undo, Redo, Save and the
   other general functions; never a panel.
3. **Value rows** (spacing, indents, sizes) are editable in the panels.
4. **Panel order** is reorderable by dragging from the start, and the order
   is saved for the next start. (The suggestion had been a fixed order for
   the first release.)

## 9. References

- [`UltraOfficeDesignProposal.md`](UltraOfficeDesignProposal.md) — the
  application's design and phases.
- [`../UltraCanvas/UltraCanvasGroupBoxExamples.md`](../UltraCanvas/UltraCanvasGroupBoxExamples.md)
  — the group box: Header frame style, collapsible, info icon, close button.
- [`../UltraCanvas/UltraCanvasCollapsiblePanel.md`](../UltraCanvas/UltraCanvasCollapsiblePanel.md),
  [`../UltraCanvas/UltraCanvasSidebarNavigation.md`](../UltraCanvas/UltraCanvasSidebarNavigation.md)
  — existing side elements to reuse or compare.
- `Apps/UltraOffice/UltraOfficeDocumentHost.cpp`, `UltraOfficeSpreadsheetHost.cpp`,
  `UltraOfficePresentationHost.cpp` — today's toolbars, which the tool table
  replaces.
