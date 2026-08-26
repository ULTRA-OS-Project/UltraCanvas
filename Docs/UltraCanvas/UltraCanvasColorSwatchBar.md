# UltraCanvas Color Swatch Bar Documentation

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
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Features

- ✅ Any list of colours (`SetColors`), with ready-made palettes: grayscale,
  colours, or both
- ✅ Optional leading **checkered** swatch standing for "no colour — show the
  transparency pattern"
- ✅ Swatch size adapts to the element's width **and** height; the row is centred
- ✅ Hover outline, selected outline, per-swatch tooltip (`#RRGGBB`)
- ✅ `onColorSelected` / `onCheckeredSelected` callbacks
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
both steps:

```cpp
auto backdrop = CreateBackdropSwatchBar("backdrop", 0, 0, 0, 28);
backdrop->onColorSelected    = [&](const Color& c) { view->SetBackdropColor(c); };
backdrop->onCheckeredSelected = [&]()              { view->SetBackdropCheckered(); };
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

The background is transparent by default — set `style.background` when the strip
needs to read as a bar of its own.

## Callbacks

```cpp
std::function<void(const Color&)> onColorSelected;
std::function<void()>             onCheckeredSelected;
```

Programmatic selection stays silent unless `runCallback` is passed, so a host
can mirror an external change without hearing its own echo.

## Used by

- **[`UltraCanvasMediaViewer`](UltraCanvasMediaViewer.md)** — the backdrop strip
  under a transparent image (and with it the UltraFiler preview pane).
