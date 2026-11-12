# UltraCanvas Settings Page Kit

## Contents

- [Overview](#overview)
- [The window](#the-window)
- [A page](#a-page)
- [The controls](#the-controls)
- [Usage](#usage)
- [Related](#related)

## Overview

**UltraCanvasSettingsPageKit** is the look of an application's settings
window, once: the window with the tree of pages on the left and the chosen
page on the right, the page's *Restore defaults* and *Close* at the foot; a
page's skeleton - the title, the caption, the controls, the notes block at
the foot - and the controls in the window's one type scale. UltraFiler's
settings window set the style; UltraSnip's is built on the kit, and so is
the next application's, so every settings window reads as one family and a
change to the look lands everywhere.

**File Location**: `include/UltraCanvasSettingsPageKit.h`
**Version**: 1.2.0
**Author**: UltraCanvas Framework

Everything is in `namespace UltraCanvas::SettingsPageKit`.

## The window

`UltraCanvasSettingsWindow` is the skeleton. An application's dialog state
derives from it (or holds one), adds its sections, nodes, pages and resets,
and calls `Finish`:

| Call | Meaning |
|---|---|
| `Create(parent, title, options)` | The window (not shown yet), the empty tree with a hidden root, the page area and the bottom bar; Escape closes it. False when the window could not be made. `SettingsWindowOptions`: `width` and `height` (700 x 560), `treeWidth` (180; wider for a long page list), `sidebarHeader` (an element above the tree: UOS-Settings' logo), `dialog` (true: a dialog of `parent`; false: an application's own main window, as UOS-Settings is) and `closeLabel` ("Close", for a translated window). |
| `AddSection(id, text)` | A top-level section of the tree (Display, Handling, ...). A section has no page of its own: selecting it moves on to its first page. |
| `AddTreeNode(parentId, id, text)` | A node under any node; its id is the page id. |
| `AddPage(pageId, page)` | A page under its node id. |
| `resets[pageId] = PageReset{label, width, action}` | The page's *Restore defaults* button in the bottom bar: labelled and wired for it while the page is up, hidden for a page without one. |
| `Finish(startPage, show = true)` | After the nodes and pages: hides the root (which promotes the sections to the top level), wires the selection, shows `startPage` and, unless `show` is false, the window. The sections start closed. |
| `ShowPage(nodeId)`, `SelectPage(pageId)` | Show a page; select its tree row (its section opened) and show it. |
| `onPageShown`, `onClosed` | Called after a page is shown (a page of live figures re-reads them) and when the window has closed (`closed` is set first). |
| `window`, `tree`, `pageArea`, `pages`, `restoreButton`, `shownPage`, `closed`, `IsOpen()` | The parts, for what the calls above do not cover. |

## A page

`MakePage(id, title, caption, scrolls = false)` returns `PageParts`: the
whole `page`, the `body` the controls go into, and the `notes` block at
the foot. `AddNote(parts, id, text)` adds a paragraph to the notes,
`AddBodyCaption(parts, id, text)` a caption introducing a second group of
controls. A page that `scrolls` (a long list of formats) puts its notes
under the caption, before the list.

## The controls

| Call | Makes |
|---|---|
| `MakeLabel(id, text, fontSize, color)`, `MakeText(id, text, width, fontSize, color)` | A single-line label that hugs its text; a paragraph wrapped at `width`. |
| `MakeChoice(id, text, checked, width)` | One choice of a radio group (`UltraCanvasRadioGroup::AddRadioButton` groups them). |
| `MakeCheckbox(id, text, width, checked, onChange)` | A checkbox reporting its state. |
| `MakeIntSlider(id, min, max, value, onChange)`, `MakeSliderValueLabel(id)`, `MakeSliderRow(id, caption, slider, value)` | A whole-number slider reporting while it moves, its value label, and the row holding both. |
| `MakeButton(id, label, width, onClick, iconFile)`, `MakeButtonRow(id)` | The window's button (an icon from `media/icons/` as a mask), and a row of them. |
| `MakeChoiceRow(id)`, `MakeLabelledRow(id, caption, control)` | A row of small choices that wraps; "caption [control]" on one line. |
| `MakeColorBox(id, color, tooltip, onClick)`, `SetColorBoxColor(box, color)` | A box showing a configured colour, the same shade in every state. |
| `IconPath(fileName)` | `media/icons/<fileName>` resolved. |

The type scale and colours are the theme's, read as functions of the
namespace for a control a page builds itself: `TitleFontSize()`,
`TextFontSize()`, `NoteFontSize()` and `TreeFontSize()` are the theme's
Heading, Body and Caption sizes (the desktop's interface font size and the
ratios of [`UltraCanvasTheme.md`](UltraCanvasTheme.md)); `TextColor()`,
`NoteTextColor()`, `NoteBackground()`, `NoteAccent()`, `WindowBackground()`,
`TreeBackground()`, `PageBackground()`, `ButtonFace()`, `ButtonHover()`
and `Rule()` are its roles, so a settings window is dark on a dark desktop
and a window built after the theme changed takes the new look. The page
geometry (`kTextWidth`, `kControlHeight`, ...) stays constant.

## Usage

<!-- doc-check: UltraCanvasWindowBase* parent; struct MySettings { bool dark = false; int rate = 20; void Save() {} } settings; -->
```cpp
using namespace UltraCanvas::SettingsPageKit;

struct Dialog : public UltraCanvasSettingsWindow {
    std::shared_ptr<UltraCanvasSlider> rateSlider;
    std::shared_ptr<UltraCanvasLabel> rateValue;
};
auto d = std::make_shared<Dialog>();
if (!d->Create(parent, "MyApp - Settings")) return;
d->AddSection("look", "Look");
d->AddTreeNode("look", "look/colours", "Colours");

PageParts page = MakePage("set-colours", "Colours", "How the window is drawn:");
page.body->AddChild(MakeCheckbox("set-dark", "Dark", kTextWidth, settings.dark, [](bool on) {
    settings.dark = on;
    settings.Save();
}));
d->rateValue = MakeSliderValueLabel("set-rate-value");
d->rateSlider = MakeIntSlider("set-rate", 5, 60, settings.rate, [d = d.get()](int rate) {
    settings.rate = rate;
    d->rateValue->SetText(std::to_string(rate) + " fps");
    settings.Save();
});
page.body->AddChild(MakeSliderRow("set-rate-row", "Frame rate:", d->rateSlider, d->rateValue));
AddNote(page, "set-colours-note", "Every change applies at once and is saved.");
d->AddPage("look/colours", page.page);
d->resets["look/colours"] = PageReset{"Restore defaults", 170, [d = d.get()]() {
    settings.dark = false;
    settings.rate = 20;
    d->rateSlider->SetValue(20.0f);
    settings.Save();
}};
d->Finish("look/colours");
```

The dialog state is kept alive by its owner (a `static std::shared_ptr`)
while the window is up; `closed` says when it has gone. UltraFiler
(`Apps/UltraFiler/UltraFilerSettingsDialog.cpp`) and UltraSnip
(`Apps/UltraSnip/ui/UltraSnipSettingsDialog.cpp`) are the two windows
built on it, and so is UOS-Settings (`Apps/UOSSettings/ui/UOSSettingsWindow.cpp`),
as a main window with the ULTRA OS logo above its tree:

<!-- doc-check: std::shared_ptr<UltraCanvasImageElement> logo; -->
```cpp
using namespace UltraCanvas::SettingsPageKit;
UltraCanvasSettingsWindow shell;
SettingsWindowOptions options;
options.width = 860;
options.height = 580;
options.sidebarHeader = logo;
options.dialog = false;
shell.Create(nullptr, "UOS-Settings 1.0", options);
```

## Related

- `UltraCanvasAppSettings.h` - the file the settings are kept in
- `UltraCanvasTreeView`, `UltraCanvasRadio`, `UltraCanvasCheckbox`, `UltraCanvasSlider` - the elements the kit makes
