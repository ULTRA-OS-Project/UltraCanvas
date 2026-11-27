# UltraCanvas Tool Panel Stack Documentation

## Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Panels](#panels)
- [Context panels](#context-panels)
- [Order](#order)
- [Saved state](#saved-state)
- [Rail mode and flyouts](#rail-mode-and-flyouts)
- [Keyboard](#keyboard)
- [Tool rows](#tool-rows)
- [Following the theme](#following-the-theme)
- [Accessibility](#accessibility)
- [Notes](#notes)

## Overview

`UltraCanvasToolPanelStack` is a column of **tool panels** at the side of a
document: one panel per topic (Clipboard, Font, Paragraph, Table ...), each an
`UltraCanvasGroupBox` in its `Header` frame style that opens and closes by
itself, stacked top to bottom in a column that scrolls. It shows every topic
at once, where a row of command tabs shows one toolbar at a time, and a panel
shows a value with its label (spacing 1.15, before 0 pt) where a toolbar shows
a button. It is the framework half of
[the UltraOffice tool panels proposal](../Research/UltraOfficeToolPanelsProposal.md).

- **Panels** are added with their content and open or close with a click on
  the header, `SetPanelOpen`, or Enter / Space on a focused header.
- **Context panels** (Table, Picture, Chart) are hidden until the host shows
  them for the selection; one comes in at the top, open, framed in the accent,
  and the panel under the pointer stays where it is on the screen.
- **The order** is the user's: drag a panel by its header (a line shows where
  it goes), use *Move up* / *Move down* in the header's menu, or Ctrl+Up /
  Ctrl+Down on a focused header. The order and the open panels are saved as
  one string in the host's settings.
- **Rail mode** folds the column into a 44 px strip of icons for a narrow
  window; a click opens a panel as a flyout beside the strip.
- **The rows** a panel is built from are in `UltraCanvasToolRows.h`: a
  wrapping row of 28 × 28 icon tools and toggles, labelled values (spinner,
  dropdown, colour swatches) on a form grid, full-width lists and the font
  picker - all editable, each with a getter and a setter, and `Sync()` to show
  the document's state again.

Built from framework elements only: the group box, `UltraCanvasContainer`'s
scrolling, `UltraCanvasButton` for the rail, the window's popup stack for the
flyout and `UltraCanvasMenu` for the header's menu.

**File Location**: `include/UltraCanvasToolPanelStack.h`, `include/UltraCanvasToolRows.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

---

## Quick Start
```cpp
#include "UltraCanvasToolPanelStack.h"
#include "UltraCanvasToolRows.h"
using namespace UltraCanvas;

struct Selection { bool bold = false; double spacing = 1.15; bool inTable = false; } doc;

auto font = CreateToolPanelContent("font");
auto fontTools = CreateToolButtonRow("font-tools");
fontTools->AddToggle("bold", "media/icons/textformat/bold.svg", "Bold", "Ctrl+B",
                     [&] { return doc.bold; }, [&](bool on) { doc.bold = on; });
font->AddChild(fontTools);

auto paragraph = CreateToolPanelContent("paragraph");
auto values = CreateToolValueRows("paragraph-values");
values->AddSpinner("spacing", "Spacing", 0.5, 4.0, 0.05, 2,
                   [&] { return doc.spacing; }, [&](double v) { doc.spacing = v; });
paragraph->AddChild(values);

auto table = CreateToolPanelContent("table");

auto panels = CreateToolPanelStack("tools");
ToolPanelOptions fontOptions;
fontOptions.iconPath = "media/icons/textformat/bold.svg";
panels->AddPanel("font", "Font", font, fontOptions);
ToolPanelOptions paragraphOptions;
paragraphOptions.helpText = "More: Paragraph (Ctrl+Shift+P)";
paragraphOptions.dialogAction = [] { /* the Paragraph dialog */ };
paragraphOptions.dialogTooltip = "Paragraph";
panels->AddPanel("paragraph", "Paragraph", paragraph, paragraphOptions);
ToolPanelOptions tableOptions;
tableOptions.context = true;
tableOptions.chip = "in a table";
panels->AddPanel("table", "Table", table, tableOptions);

// After every change of the selection:
panels->SetPanelShown("table", doc.inTable);
panels->Sync();
```

<!-- doc-check: std::string savedText; -->
```cpp
// The order and the open panels, kept in the settings:
panels->onOrderChanged = [](const std::vector<std::string>& order) { (void)order; /* save */ };
panels->onPanelOpenChanged = [](const std::string& id, bool open) { (void)id; (void)open; /* save */ };
std::string toSave = panels->SaveState().ToString();     // "order=table,font,paragraph;open=font,paragraph;rail=0"
panels->RestoreState(ToolPanelStackState::FromString(savedText));
```

## Panels

```cpp
std::shared_ptr<UltraCanvasGroupBox> AddPanel(const std::string& id, const std::string& title,
                                              std::shared_ptr<UltraCanvasContainer> content,
                                              const ToolPanelOptions& options = ToolPanelOptions());
bool RemovePanel(const std::string& id);
bool HasPanel(const std::string& id) const;
std::shared_ptr<UltraCanvasGroupBox> GetPanel(const std::string& id) const;
std::shared_ptr<UltraCanvasContainer> GetPanelContent(const std::string& id) const;
void SetPanelTitle(const std::string& id, const std::string& title);
void SetPanelChip(const std::string& id, const std::string& chip);
void SetPanelOpen(const std::string& id, bool open);
bool IsPanelOpen(const std::string& id) const;
```

| `ToolPanelOptions` | |
|---|---|
| `iconPath` | the panel's icon in the rail (without one, the title's first letter) |
| `helpText` | the header's info icon and its tooltip; empty: none |
| `dialogAction` | the header's "…" button, which opens the topic's dialog; empty: none |
| `dialogTooltip` | the "…" button's tooltip, the dialog's name; empty: "Options" |
| `context` | a context panel: hidden until `SetPanelShown(id, true)` |
| `open` | open at the start (a context panel opens whenever it is shown) |
| `chip` | a count or a context at the header's right: "8", "in a table" |

A panel is a group box in the `Header` frame style, collapsible, with its
header focusable and draggable (see
[UltraCanvasGroupBoxExamples.md](UltraCanvasGroupBoxExamples.md)); `AddPanel`
returns it for a host that wants more of its own. A closed panel's content is
hidden, so Tab does not walk into controls nobody sees. `SetPanelOpen` does
not call `onPanelOpenChanged`: that reports the user's clicks and keys.

## Context panels

```cpp
void SetPanelShown(const std::string& id, bool shown);
bool IsPanelShown(const std::string& id) const;
bool IsContextPanel(const std::string& id) const;
```

A context panel comes and goes with the selection (the caret in a table, a
picture selected). Shown, it comes in **at the top** of the column, **open**,
with an accent border, a light-accent header and an accent chip. The column
does not jump under the pointer: the panel under the pointer keeps its place
on the screen and the column scrolls by the panel that came or went instead.
With the pointer elsewhere and the column scrolled, the first panel in view
keeps its place; with the column at its top, nothing is held, so a panel that
comes in at the top is seen.

## Order

```cpp
std::vector<std::string> GetOrder() const;
void SetOrder(const std::vector<std::string>& order);
bool MovePanel(const std::string& id, int delta);
```

`GetOrder` names every panel top to bottom: the context panels first (shown or
not), then the others. `SetOrder` puts the panels it names in that order; an
id it does not know is skipped, and a panel it does not name keeps its default
place - after the panel added before it - so a panel a new version adds turns
up where it belongs in a saved order. The context panels stay above the
others, whatever the order says.

The user reorders by:

- **dragging a header** - a press that moves four pixels is a drag; the
  panel is veiled, a line in the accent with a dot at each end shows where it
  goes, and the column scrolls when the pointer nears its top or bottom edge.
  A press released in place still opens or closes the panel;
- **the header's menu** - a right-click, Shift+F10 or the Menu key: *Open* /
  *Close*, *Move up* (Ctrl+Up), *Move down* (Ctrl+Down);
- **Ctrl+Up / Ctrl+Down** on a focused header.

Each calls `onOrderChanged` with the new order. A panel moves past the shown
panels of its kind; a plain panel never goes above the context panels.
`SetOrder` and `MovePanel` called by the host do not call `onOrderChanged`.

## Saved state

```cpp
struct ToolPanelStackState {
    std::vector<std::string> order;    // every panel's id, top to bottom
    std::vector<std::string> open;     // the ids of the open panels
    bool railMode = false;
    std::string ToString() const;
    static ToolPanelStackState FromString(const std::string& text);
};
ToolPanelStackState SaveState() const;
void RestoreState(const ToolPanelStackState& state);
```

`ToString` writes one line, `order=clipboard,font;open=font;rail=0`, with `%`,
`,`, `;` and `=` in an id escaped as `%XX`. `RestoreState` sets the order,
opens and closes the panels the state names (a panel added since keeps its
default), and the rail choice; an empty state (an empty or unreadable string)
changes nothing. A context panel's open state is not saved: it opens whenever
it is shown.

## Rail mode and flyouts

```cpp
void SetRailMode(bool rail);
void SetAutoRailBelowWidth(float windowWidth);   // 0: never
bool IsRailMode() const;                         // chosen, or the window narrow enough
bool IsRailModeChosen() const;
bool OpenFlyout(const std::string& id);
void CloseFlyout();
bool IsFlyoutOpen() const;
std::string GetFlyoutPanel() const;
std::shared_ptr<UltraCanvasButton> GetRailButton(const std::string& id) const;
```

In rail mode the stack is a 44 px strip with a 32 px icon button per shown
panel (its tooltip and accessible name are the panel's title). A click on an
icon opens that panel as a **flyout** beside the strip: the panel itself moves
into a popup on the window's popup stack, open and as tall as it needs (it
scrolls when the window is shorter), and the icon is highlighted. Escape
closes one level - a dropdown or the font list open inside the flyout first,
then the flyout - a click outside closes it, a click on another icon switches
to that panel and on its own icon closes it. When the flyout closes, the panel
goes back into the column as it was. `SetAutoRailBelowWidth(1100)` folds the
column while the window is narrower than 1100 px and unfolds it when it is
wider again; `SetRailMode(true)` is the user's own choice and is saved.

## Keyboard

| Key | On | Does |
|---|---|---|
| the host's F6 | | `FocusFirst()`: the first shown panel's header (in rail mode, its icon) |
| Up / Down | a header or a rail icon | the previous / next one |
| Home / End | a header or a rail icon | the first / last one |
| Enter / Space | a header | opens or closes the panel |
| Tab | a header | the panel's controls when it is open, the next header when it is closed |
| Ctrl+Up / Ctrl+Down | a header | moves the panel up / down |
| Shift+F10, Menu | a header | the header's menu |
| Escape | a flyout | closes it (one popup per press) |

A click on a header does not take the keyboard focus, so the document keeps
its caret; `FocusPanel(id)` and `GetFocusedPanel()` set and read it.

## Tool rows

`UltraCanvasToolRows.h` holds the rows a panel's content is made of; put them
in `CreateToolPanelContent(id)`, a column with 6 px between rows.

```cpp
struct ToolRowButton {
    std::string id, iconPath, name, shortcut, text;
    std::function<void()> onClick;
    std::function<bool()> isChecked;          // a toggle: pressed while it says so
    std::function<void(bool)> setChecked;
    std::function<bool()> isEnabled;
};
```

| Row | Adds | Shows |
|---|---|---|
| `UltraCanvasToolButtonRow` | `AddButton`, `AddToggle`, `AddButton(ToolRowButton)`, `AddGap` | a wrapping row of 28 × 28 icon buttons (`Ghost` look), the tooltip "Bold (Ctrl+B)" |
| `UltraCanvasToolValueRows` | `AddSpinner` (a suffix such as "pt"), `AddDropdown`, `AddColorSwatches`, `AddRow` for a control of your own | "caption [control]" on a form grid whose caption column is as wide as the widest caption; a caption of more than 14 letters (`kCaptionBreakLength`) is set on two lines, so it does not narrow the controls until their values are cut off |
| `UltraCanvasToolValueRows` | `AddListRow`, `AddFontPicker`, `AddWideRow` | a list across both columns: the paragraph style, the font in `UltraCanvasFontPicker` |

Every value is editable: the setter runs when the user changes it (a toggle
clicked, a spinner stepped or typed, a list chosen, a swatch clicked, a font
chosen). `Sync()` reads every getter again and shows the value **without**
calling a setter, so a host mirrors the document after every change of the
selection; `UltraCanvasToolPanelStack::Sync()` calls it on every row of every
panel (`SyncToolRows(container)` for any other container).

```cpp
auto paragraphTools = CreateToolButtonRow("paragraph-tools");
ToolRowButton justify;
justify.id = "justify";
justify.iconPath = "media/icons/office/justify.svg";
justify.name = "Justify";
justify.shortcut = "Ctrl+J";
justify.isChecked = [] { return false; };
justify.setChecked = [](bool on) { (void)on; };
paragraphTools->AddButton(justify);
paragraphTools->AddGap();
paragraphTools->AddButton("indent", "media/icons/office/indent.svg", "Increase indent", "", [] {});

auto pageValues = CreateToolValueRows("page-values");
pageValues->AddDropdown("orientation", "Orientation", {"Portrait", "Landscape"},
                        [] { return 0; }, [](int index) { (void)index; });
pageValues->AddColorSwatches("page-colour", "Colour", UltraCanvasColorSwatchBar::ColorPalette(),
                             [] { return Colors::White; }, [](const Color& c) { (void)c; });
pageValues->AddFontPicker("page-font", [] { return std::string("Ubuntu"); },
                          [](const FontPickerChoice& choice) { (void)choice; });
```

## Following the theme

`ToolPanelStackStyle::FromTheme` takes the theme's roles: the ground under the
column and the rail, the panel surface for the panels with the well for their
header strips, hairlines, ink and muted ink, the ground for a count's chip;
a context panel framed in the accent over the soft accent with an accent
chip; the accent for the drop line, the focus colour for the header's ring.
The element roles `tool-panel.header` and `tool-panel.context-header`
override the two header strips. Under the theme policy a new stack follows
the theme by itself; `SetStyle` by hand stops that. The shape: 286 px
column, 44 px rail with 32 px icons, 6 px padding, gaps and corner radius,
8 px inside a panel.

```cpp
auto panels = CreateToolPanelStack("tools");
ToolPanelStackStyle look = panels->GetStyle();
look.columnWidth = 300.0f;
look.cornerRadius = 8.0f;
panels->SetStyle(ToolPanelStackStyle::FromTheme(UltraCanvasTheme::Current(), look));
panels->SetFollowTheme(true);
```

## Accessibility

The stack is a `Toolbar`; each panel a `Group` named by its title, whose
action opens or closes it; each rail icon a button named by its panel's
title; each tool a button named by its `name`, a toggle with its state.

## Notes

- The stack sets its own width: the column width (`SetColumnWidth`, 286 px by
  default, the width it is built with) or the rail's. A host with a splitter
  calls `SetColumnWidth` and keeps the width in its settings.
- Panels are taken by id; an id may hold any character, the saved string
  escapes the separators.
- Tests: `Tests/ToolPanelStackElementTest.cpp` (a window test under Xvfb in
  CI); `ULTRACANVAS_SCREENSHOT_DIR=<dir>` writes its screenshots.
- Demo: the *Tool Panel Stack* page of the demo application
  (`Apps/DemoApp/UltraCanvasToolPanelStackExamples.cpp`).
