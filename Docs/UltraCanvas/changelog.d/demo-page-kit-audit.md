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
