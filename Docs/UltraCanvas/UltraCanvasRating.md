# UltraCanvas Rating Documentation

## Overview

**UltraCanvasRating** is a row of symbols (stars by default) for entering or
displaying a score. It supports whole- and half-step values, hover preview,
read-only display, and — as a first-class feature — **user-supplied vector (SVG)
files** for the three per-symbol states: **on** (filled), **off** (empty) and
**half**.

**File Location**: `include/UltraCanvasRating.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Features

- ✅ Configurable symbol count and value (`0 .. maxRating`)
- ✅ **Whole or half steps** (`SetAllowHalf`)
- ✅ Built-in vector shapes: **Star**, **Circle**, **Square**
- ✅ **Custom SVG / image symbols** for on / off / half (`SetCustomSymbols`)
- ✅ Half state synthesised automatically when no half file is supplied
- ✅ Hover preview, click-to-set, click-again-to-clear
- ✅ Read-only display mode
- ✅ Keyboard control (Left/Right/Up/Down, Home, End)
- ✅ Per-state colours (built-ins) and optional mask tinting (custom)
- ✅ `onRatingChanged` and `onHoverChanged` callbacks

## Symbol sources

### Built-in shapes

`SetSymbol(RatingSymbol::Star | Circle | Square)` draws the symbol as a vector
directly. The filled portion uses `onColor` (or `hoverColor` while hovering); the
empty portion uses `offColor`. Half values are rendered by clipping the filled
shape to its left half.

### Custom vector (SVG) files

```cpp
rating->SetCustomSymbols("heart-on.svg", "heart-off.svg", "heart-half.svg");
```

- `onPath` / `offPath` are required; `halfPath` is optional.
- Files are loaded with `UCImage::Get(path)` (so any format UltraCanvas can load
  works — **SVG renders as crisp vectors at any symbol size**).
- **If `halfPath` is empty**, the half state is synthesised by drawing the `off`
  image and then the `on` image clipped to its left half — so half ratings work
  for any symbol without a dedicated half asset.
- Set `style.tintCustom = true` to treat the images as monochrome **masks** and
  tint them with the state colours (via `DrawMask`); leave it `false` (default)
  to draw full-colour SVG icons as-authored.
- If the `on`/`off` images fail to load, the control falls back to the current
  built-in shape, so it always renders something.

## Quick Start

```cpp
#include "UltraCanvasRating.h"
using namespace UltraCanvas;

// Whole-star rating, 3 of 5
auto stars = CreateRating("score", 20, 20, 200, 36, /*max*/ 5, /*value*/ 3);
stars->onRatingChanged = [](float v) { save((int)v); };

// Half-star rating
auto half = CreateHalfRating("score2", 20, 60, 200, 36, 5, 3.5f);

// Custom SVG hearts (on / off / half)
auto hearts = CreateVectorRating("love", 20, 100, 240, 40,
                                 "heart-on.svg", "heart-off.svg", "heart-half.svg", 5);

// Read-only display
auto ro = CreateHalfRating("shown", 20, 140, 200, 36, 5, 4.5f);
ro->SetReadOnly(true);
```

## Factory Functions

| Function | Result |
|----------|--------|
| `CreateRating(id, x, y, w, h, maxRating, value)` | Whole-step star rating |
| `CreateHalfRating(id, x, y, w, h, maxRating, value)` | Half-step star rating |
| `CreateVectorRating(id, x, y, w, h, onPath, offPath, halfPath, maxRating)` | Rating from vector/image files |

## Key API

```cpp
// Value
void  SetMaxRating(int count);
void  SetValue(float v, bool runCallback = false);
float GetValue() const;
void  SetAllowHalf(bool);
void  SetReadOnly(bool);
void  SetAllowClear(bool);          // click current value to clear (default true)

// Symbol
void SetSymbol(RatingSymbol);       // Star | Circle | Square | Custom
void SetCustomSymbols(const std::string& onPath,
                      const std::string& offPath,
                      const std::string& halfPath = "");

// Appearance
void SetSymbolSize(float);
void SetSpacing(float);
void SetColors(const Color& on, const Color& off);
RatingStyle& GetStyle();            // onColor/offColor/hoverColor/borderColor/tintCustom
float GetPreferredWidth() const;

// Callbacks
std::function<void(float)> onRatingChanged;   // committed value
std::function<void(float)> onHoverChanged;    // live hover value (-1 when the cursor leaves)
```

## Keyboard

| Key | Action |
|-----|--------|
| `Right` / `Up`   | Increase by one step (0.5 or 1) |
| `Left` / `Down`  | Decrease by one step |
| `Home`           | Clear (0) |
| `End`            | Maximum |

## Notes

- Half detection on click/hover is based on the cursor's position within a
  symbol: the left half yields `x.5`, the right half yields `x` whole (only when
  `allowHalf` is enabled).
- Clicking the symbol equal to the current value clears the rating to 0 when
  `allowClear` is true — click the same star again to reset.
- `GetPreferredWidth()` returns `maxRating * symbolSize + (maxRating-1) * spacing`.
