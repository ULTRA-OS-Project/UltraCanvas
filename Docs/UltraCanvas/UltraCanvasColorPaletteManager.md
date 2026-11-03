# UltraCanvas Color Palette Manager Documentation

<!-- doc-check: std::shared_ptr<UltraCanvasContainer> card; std::shared_ptr<UltraCanvasColorPicker> picker; std::string savedLine; void SaveSetting(const std::string& value); -->

## Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Keeping the palettes](#keeping-the-palettes)
- [The palettes](#the-palettes)
- [Selection and popups](#selection-and-popups)
- [Style](#style)
- [Callbacks](#callbacks)

## Overview

**UltraCanvasColorPaletteManager** is the set of colour palettes that sits
under a [colour picker](UltraCanvasColorPicker.md): one or more rows of
swatches, each row a palette, with a small button at the end of the rows to
add more. It is what UltraPaint and ArtCreator show under the picker in their
Colour card.

- With **one** palette, its row ends in a **+** button.
- With **several**, every row ends in a **−** that takes that palette away,
  and the **last** row has the **+** under its **−**.
- **+** opens the **Palette manager** (the gallery): a popup with a title bar
  it is dragged about the window by, listing ready-made schemes grouped by
  kind (basics, moods, design systems, retro machines, indexed / GIF), each
  with a preview of its colours, the number of colours and its own **+** that
  adds it as a new row. A scheme already among the rows shows a check mark.
  Clicking a preview adds it too.
- **Add palette** at its foot opens the **palette creator** over it: a name,
  a picker, *Add colour* / *Remove* and the colours added so far - a click on
  one of them lets the picker change it. *Create* keeps the palette among the
  user's own, listed first in the Palette manager under **My palettes** with
  a **×** that deletes it, and adds it as a row.
- A **left click** on a swatch picks its colour (`onColorSelected`); one
  swatch is marked across all the rows.
- A **right click** on a swatch opens a small **foreground-only** colour
  picker next to it that sets that one entry of the palette, live, as it is
  dragged. *Revert* puts the colour back; *Done*, Escape or a click outside
  keep it.
- The pointer on a swatch **floods the rows' background** with its colour and
  reports it (`onColorHovered`, then `onColorHoverEnded`), so the host can
  flood its picker too with `UltraCanvasColorPicker::SetSurfacePreview` - the
  same preview the picker gives its own foreground and background swatches.

Palettes run from a few colours to a whole **256-colour GIF palette**; large
ones wrap into a compact block (`UltraCanvasColorSwatchBar::SetWrap`) instead
of shrinking to hairlines; a 256-colour one is eight full lines of 32. The
element sets its own height to its rows', so in
a flex column what is below it moves as rows come and go.

It is built from framework elements only: the rows are
[`UltraCanvasColorSwatchBar`](UltraCanvasColorSwatchBar.md)s, the buttons
`UltraCanvasButton`s, the captions `UltraCanvasLabel`s, the creator's name an
`UltraCanvasTextInput` and the pickers `UltraCanvasColorPicker`s with
`SetShowBackgroundColor(false)`.

**File Location**: `include/UltraCanvasColorPaletteManager.h`
**Implementation**: `core/UltraCanvasColorPaletteManager.cpp`
**Version**: 1.1.0
**Author**: UltraCanvas Framework

## Quick Start

```cpp
#include "UltraCanvasColorPaletteManager.h"
using namespace UltraCanvas;

// Under the picker, as wide as it.
auto palettes = CreateColorPaletteManager("palettes", 0, 0, 250);
palettes->layoutItem.SetFlexGrow(0).SetFlexShrink(0);
palettes->onColorSelected = [](const Color& c) {
    // the host's foreground colour, e.g. picker->SetForegroundColor(c, false)
    (void)c;
};
// The swatch under the pointer floods the picker above as well.
palettes->onColorHovered = [p = picker.get()](const Color& c) { p->SetSurfacePreview(c); };
palettes->onColorHoverEnded = [p = picker.get()]() { p->ClearSurfacePreview(); };
card->AddChild(palettes);
```

## Keeping the palettes

The rows are the host's to keep. `onPalettesChanged` fires after every change
(a row added or removed, an entry set), and the palettes go into one line of
a settings file and back:

```cpp
palettes->onPalettesChanged = [m = palettes.get()]() {
    SaveSetting(UltraCanvasColorPaletteManager::SerializePalettes(m->GetPalettes()));
};

std::vector<NamedColorPalette> saved;
if (UltraCanvasColorPaletteManager::ParsePalettes(savedLine, saved)) palettes->SetPalettes(saved);
```

The line is `Name|RRGGBB,RRGGBB,...;Name|...`. `ParsePalettes` returns false,
and leaves its output alone, for text that holds no palette. A preset saved
with fewer colours than it has now - the presets grew to fill their rows -
comes back with all of them, unless one of its entries was changed.

The user's own palettes are kept the same way, in a line of their own:

```cpp
palettes->onUserPalettesChanged = [m = palettes.get()]() {
    SaveSetting(UltraCanvasColorPaletteManager::SerializePalettes(m->GetUserPalettes()));
};

std::vector<NamedColorPalette> own;
if (UltraCanvasColorPaletteManager::ParsePalettes(savedLine, own)) palettes->SetUserPalettes(own);
```

## The palettes

```cpp
struct NamedColorPalette {
    std::string name;        // shown in the gallery and as the row's tooltip
    std::string category;    // the gallery's group
    std::vector<Color> colors;
};

void SetPalettes(const std::vector<NamedColorPalette>& palettes);
const std::vector<NamedColorPalette>& GetPalettes() const;
int  GetPaletteCount() const;
void AddPalette(const NamedColorPalette& palette);   // a row at the end
void RemovePalette(int index);                       // never the last one
bool HasPalette(const std::string& name) const;
void SetEntryColor(int palette, int entry, const Color& c, bool notify = true);

static NamedColorPalette DefaultPalette();                       // six greys and twelve colours
static const std::vector<NamedColorPalette>& PresetPalettes();   // what the gallery offers

// The user's own, made with the palette creator ("My palettes")
void SetUserPalettes(const std::vector<NamedColorPalette>& palettes);   // no callback
const std::vector<NamedColorPalette>& GetUserPalettes() const;
std::string AddUserPalette(const NamedColorPalette& palette);   // kept and added as a row; its name
void RemoveUserPalette(const std::string& name);                // a row of it stays
std::string UniquePaletteName(const std::string& wanted) const; // "wanted", "wanted 2", ...
static const char* UserPaletteCategory();                       // "My palettes"
```

Every preset has at least sixteen colours, which fill a row under a picker
and in the gallery. There is always at least one row: setting no palettes, or a list of empty
ones, leaves `DefaultPalette()`.

The gallery's schemes:

| Group | Palettes |
|---|---|
| Basics | Basic (18), Greys (16), Rainbow (32) |
| Moods | Pastel, Earth, Skin tones, Sunset, Ocean, Forest, Neon (16 each) |
| Design systems | Material (19), Nord (16), Solarized (16) |
| Retro | Commodore 64 (16) |
| Indexed (GIF) | Web-safe 216, GIF 256 (RGB 3-3-2), GIF 256 greys |

## Selection and popups

```cpp
void SelectColor(const Color& c);   // marks the first swatch with it, any row
void ClearSelection();

void OpenPaletteGallery(const Rect2Df& anchorWindow);   // what "+" does
void OpenEntryEditor(int palette, int entry, const Point2Di& windowPos);   // a right click
void OpenPaletteCreator(const Rect2Df& anchorWindow);   // what "Add palette" opens
void ClosePopups();
```

A popup opens below its anchor (window coordinates), right-aligned with it,
or above it when the window has no room below. Each has a title bar - "Palette
manager", "Colour 3 of Basic", "New palette" - that the user drags it about
the window by. *Add palette* opens the creator over the Palette manager, which
stays open under it.

## Style

```cpp
ColorPaletteManagerStyle s = palettes->GetManagerStyle();
s.swatchSelectedBorder = Color(37, 99, 235, 255);
s.editorPickerStyle = picker->GetStyle();   // the entry editor looks like the host's picker
palettes->SetManagerStyle(s);
```

`ColorPaletteManagerStyle` holds the swatch outlines, the largest swatch size
(the manager makes a row's swatches smaller and closer the more colours it
has), the gaps, the micro buttons' size and colours, the popups' colours and
corner radius, the accent of the gallery's add buttons, and the entry
editor's picker style and scale.

## Callbacks

```cpp
std::function<void(const Color&)> onColorSelected;   // a swatch was clicked
std::function<void()>             onPalettesChanged; // added, removed, an entry set
std::function<void()>             onUserPalettesChanged; // one made or deleted
std::function<void(const Color&)> onColorHovered;    // the pointer onto a swatch
std::function<void()>             onColorHoverEnded; // and off the rows again
```
