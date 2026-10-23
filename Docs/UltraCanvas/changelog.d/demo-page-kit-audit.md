- **DemoApp: one page look, and an audit that finds cut text.**
  - `UltraCanvasDemoPageKit.h` is the Level Meter page's look for every page:
    a title and a description, the examples in white rounded cards, captions
    and notes in grey - and every label sized to its text (it wraps and is as
    tall as its lines), rows that wrap, the page as tall as its content and
    scrolled by the page area. The Toolbar page is on it.
  - `UltraCanvasDemo --audit-layout` opens every page, lays it out and lists
    each label or button whose text does not fit and each element a parent
    cuts off with no scrollbar to reach it; `--pseudo-loc` first makes every
    text ~40% longer, as a translation is (and with `--component` shows the
    page that way); `--audit-page <id>` audits one page.
  - The *IODeviceManager* entry is DeviceExplorer's own panel
    (`Apps/DeviceExplorer/ui`) and reads *Partially implemented*: printers,
    scanners and cameras are found and shown, other categories and platform
    backends are planned. *VideoFX* reads *Fully implemented*: every stage of
    its roadmap but keyframes is done and UltraVideo is built on it.
- **UltraCanvasToolbar: a label is as wide as its text** (it was 80 px, which
  cut a longer translation), **and `AddDropdownButton` shows its initial
  text** - the item reading so is selected; the dropdown opened empty.
- **DemoApp: slim gray scrollbars.** The demo-wide scrollbar style is an 8 px
  light gray track with a mid-gray round-ended thumb that darkens on hover and
  while dragged (it was a 12 px blue one).
- **UltraCanvasButton: a new text fits the width at once.** `SetText` widens
  (or narrows back) a button to its text when it can measure, instead of at
  its next measure: the parent reads that width to break a wrapping row into
  lines first, and laid the row out a line short with the old one, so the
  button's new text ("Resume" for "Pause", a longer translation) was cut.
- **Longer translations in two framework panels.** SmartHomePanel's device
  count ("30 of 30 devices") is as wide as its text, 124 px at least, instead
  of cut at 124; the menu configurator's action buttons, each as wide as its
  text, make their pane scroll sideways when they outgrow it.
- **DemoApp: a page is never narrower than its fixed-size content.** The page
  kit gives the page, and every card and row on the way to a chart, diagram
  or text button, that content's width as its minimum; a card sharing a row
  wraps under its neighbour instead of cutting the chart, and a window
  narrower than a 1000 px chart scrolls the page sideways. Labels break inside
  a word only when the word is longer than the line (a code sample).
  `--audit-layout` reports no finding on any of the 166 pages.
