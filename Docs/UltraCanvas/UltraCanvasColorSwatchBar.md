# UltraCanvas Color Swatch Bar Documentation

<!-- doc-check: void SetFillColor(const Color& c); std::shared_ptr<UltraCanvasMediaViewer> viewer; -->

## Overview

**UltraCanvasColorSwatchBar** is a strip of colour swatches: one click picks a
colour. It is for the places where a full
[`UltraCanvasColorPicker`](UltraCanvasColorPicker.md) is too much furniture —
the row of backdrop colours under a transparent image in the media viewer, a
quick fill palette in an editor, a highlight colour in a toolbar.

The bar **sizes its swatches to the space it is given**: each one grows towards
`swatchSize` and shrinks towards `minSwatchSize` so a full palette still fits a
narrow preview pane, and the row is centred in whatever space is left over.
That is why this is an element rather than a row of buttons — a fixed row of
buttons cannot adapt, and a palette that overflows its pane is a palette with
colours nobody can reach.

**File Location**: `include/UltraCanvasColorSwatchBar.h`
**Implementation**: `core/UltraCanvasColorSwatchBar.cpp`
**Version**: 1.2.0
**Author**: UltraCanvas Framework

## Features

- ✅ Any list of colours (`SetColors`), with ready-made palettes: grayscale,
  colours, or both
- ✅ Optional leading **checkered** swatch standing for "no colour — show the
  transparency pattern"
- ✅ Swatch size adapts to the element's width **and** height; the row is centred
- ✅ Hover outline, selected outline, per-swatch tooltip (`#RRGGBB`)
- ✅ `onColorSelected` / `onCheckeredSelected` callbacks; right (Adjust) click
  raises `onColorAdjustSelected` — the background colour of a
  foreground/background picker
- ✅ Optional **wrapping** (`SetWrap`) for large palettes — up to a 256-colour
  GIF palette as a compact block; `HeightForWidth` sizes the element
- ✅ `SetColorAt` edits one entry; `onColorContextClicked` reports a right
  click with the entry's index, for a host that edits it in a popup
- ✅ `onColorHovered` reports the entry under the pointer, for a host that
  previews it (the palette manager floods its rows and the picker with it)
- ✅ Never takes the keyboard focus, so a host keeps its own arrow-key handling

## Quick Start

```cpp
#include "UltraCanvasColorSwatchBar.h"
using namespace UltraCanvas;

// Greys first, then colours — the palette a backdrop chooser wants.
auto bar = CreateColorSwatchBar("fill-colors", 0, 0, 320, 28);
bar->onColorSelected = [](const Color& c) { SetFillColor(c); };
container->AddChild(bar);
```

A backdrop chooser adds the checkerboard entry — `CreateBackdropSwatchBar` does
both steps. Here it sets the backdrop an `UltraCanvasMediaViewer` draws under
transparent images:

```cpp
auto backdrop = CreateBackdropSwatchBar("backdrop", 0, 0, 0, 28);
backdrop->onColorSelected = [&](const Color& c) {
    viewer->SetTransparentBackground(TransparentImageBackground::SolidColor);
    viewer->SetTransparentColor(c);
};
backdrop->onCheckeredSelected = [&]() {
    viewer->SetTransparentBackground(TransparentImageBackground::Checkered);
};
```

## Palettes

```cpp
static std::vector<Color> GrayscalePalette(int steps = 6);  // white → black
static std::vector<Color> ColorPalette();                   // red → brown, 12 colours
static std::vector<Color> DefaultPalette();                 // grayscale + colours
```

Any other list works just as well:

```cpp
bar->SetColors({ Colors::White, Color(245, 240, 225, 255), Colors::Black });
```

Replacing the palette keeps the selection **only** if the selected colour is in
the new list; otherwise nothing is marked.

## Selection

```cpp
void  SelectColor(const Color& c, bool runCallback = false);  // by value, alpha ignored
void  SelectCheckered(bool runCallback = false);
void  ClearSelection();
bool  HasColorSelected() const;
bool  IsCheckeredSelected() const;
int   GetSelectedIndex() const;      // -1 == none
Color GetSelectedColor() const;      // ask HasColorSelected() first
```

`SelectColor` with a colour the palette does not hold clears the selection
instead of adding it — which is what a host wants when the value came from
somewhere else (a colour picker dialog, a saved setting): no swatch is marked,
and the strip still works.

Selection is a highlight, not a filter: clicking always reports through the
callback, even on the swatch that is already selected.

## Layout and style

```cpp
struct ColorSwatchBarStyle {
    Color background, border, hoverBorder, selectedBorder;
    Color checkerLight, checkerDark;
    float swatchSize, minSwatchSize, spacing, padding;
    float borderWidth, selectedBorderWidth, cornerRadius, checkerCell;
};

void SetStyle(const ColorSwatchBarStyle& s);
void SetSwatchSize(float size);
float GetPreferredWidth() const;        // every swatch at its preferred size
float GetEffectiveSwatchSize() const;   // what they are drawn at right now
```

Give the element the height you want the strip to be (`28` suits a 20 px
swatch) and let the layout stretch it across the width. The swatch edge is
`min(swatchSize, width that fits every swatch, height − 2·padding)`, floored at
`minSwatchSize`, so:

| Space | What happens |
|---|---|
| Wide | Swatches reach `swatchSize`; the row is centred |
| Narrow | Swatches shrink towards `minSwatchSize`; below that the gaps close and the swatches keep shrinking, so every colour stays reachable |
| Short | Swatches shrink to the height; the row stays a single line |

### Wrapping

A palette too large for one row wraps instead of shrinking to hairlines:

```cpp
auto gif = std::make_shared<UltraCanvasColorSwatchBar>("gif", 230, 10);
std::vector<Color> colors(256, Colors::Black);   // a GIF's palette
gif->SetColors(colors);
gif->SetWrap(true);
gif->SetElementSize(Size2Df(230, gif->HeightForWidth(230)));
```

In wrap mode the swatches stay at least `minSwatchSize`; the bar takes as many
lines as that needs, shares the colours out evenly over them (16 on two lines
are 8 + 8), and makes each swatch as large as the line's width allows up to
`swatchSize`. When a line or two more than the fewest divides the colours
exactly, it takes that many, so no line is short: 256 colours with room for
37 a line are 8 lines of 32. The block starts at the top-left corner. `HeightForWidth(w)` is
the height all the lines take at width `w`.
`UltraCanvasColorPaletteManager` ([doc](UltraCanvasColorPaletteManager.md))
uses it for its palette rows.

The background is transparent by default — set `style.background` when the strip
needs to read as a bar of its own.

## Callbacks

```cpp
std::function<void(const Color&)> onColorSelected;        // left click
std::function<void(const Color&)> onColorAdjustSelected;  // right click
std::function<void()>             onCheckeredSelected;
// right click with the entry's index and the window position; when set it
// takes the right click instead of onColorAdjustSelected
std::function<void(int index, const Point2Di& windowPos)> onColorContextClicked;
// The pointer came onto a colour swatch (its index), or left the bar (-1);
// crossing the gap between two swatches reports nothing.
std::function<void(int index)> onColorHovered;
```

A right click does not move the selection outline (that tracks left clicks)
and is ignored when `onColorAdjustSelected` is unset. Pair the bar with a
colour picker so left sets the foreground and right the background:

```cpp
bar->onColorSelected       = [picker](const Color& c) { picker->SetForegroundColor(c); };
bar->onColorAdjustSelected = [picker](const Color& c) { picker->SetBackgroundColor(c, true); };
```

Fast repeated clicks (delivered as double-clicks) select just like single ones.

Programmatic selection stays silent unless `runCallback` is passed, so a host
can mirror an external change without hearing its own echo.

## Used by

- **[`UltraCanvasMediaViewer`](UltraCanvasMediaViewer.md)** — the backdrop strip
  under a transparent image (and with it the UltraFiler preview pane).
