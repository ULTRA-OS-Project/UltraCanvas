# UltraCanvasTickerBoard Documentation

## Overview

`UltraCanvasTickerBoard` is the **ticker board**: a strip of items - a label,
a value, a change - crawling across a band without end. It is the stock
ticker along the bottom of a news channel, the headline crawl, the departure
board in a lobby and the LED sign over a shop door, in one element with two
faces:

- **Text** sets the strip in a font on a plain band.
- **DotMatrix** lights it on a fixed grid of round LED dots, seven rows high,
  glyphs five dots wide, and steps the text across the grid one column at a
  time the way a real sign does.

The board moves by itself from its first frame, pauses while the pointer
rests on it, shows an item's tooltip, reports a click on an item, and lets a
live feed re-price an item in place without the strip jumping.

For a *still* LED readout of one value use
[`UltraCanvasGaugeDiagramElement`](UltraCanvasGaugeExamples.md) in
`GaugeMode::Digital`; this element exists to move.

**Namespace:** `UltraCanvas`
**Header:** `include/Plugins/Diagrams/UltraCanvasTickerBoard.h`
**Base Class:** `UltraCanvasUIElement`
**Version:** 1.0.0
**Last Modified:** 2026-10-13
**Author:** UltraCanvas Framework

## Class Hierarchy

```
UltraCanvasUIElement
    └── UltraCanvasTickerBoard
```

## Features

- **Items with three fields:** a label, a value and a change, each drawn in
  its own colour; any field may be empty, and an item with only a label is a
  crawling message
- **Trend colouring:** the change is drawn in `upColor`, `downColor` or
  `flatColor` by the item's `TickerTrend`, with a triangle before it
  (`showArrows`)
- **Two faces:** `TickerBoardFace::Text` and `TickerBoardFace::DotMatrix`,
  switchable at run time with `SetFace()`
- **Dot-matrix font:** the classic 5x7 glyph set for ASCII, proportional
  (blank edge columns dropped), with Latin-1 letters drawn as their base
  letter and the arrows, bullet, degree, euro and pound signs added
- **Self-driven motion:** a timer advances the strip at `pixelsPerSecond`
  from the first frame; `Stop()` halts it where it is, `Start()` resumes;
  `pauseOnHover` holds it still under the pointer
- **Seamless loop:** the strip repeats without a gap; a strip shorter than
  the band is drawn as many times as the band needs
- **Live updates:** `UpdateItem()` replaces one item and keeps the strip's
  position, so a feed can re-price while the board rolls
- **Interaction:** per-item tooltips, `onItemClick`, `onItemHover`,
  `GetItemAt()`
- **Quote helper:** `MakeTickerQuote()` formats a price and a change into an
  item with its sign, percentage and trend

## Data Model

```cpp
struct TickerItem {
    std::string label;          // "AAPL", "Platform 4", a headline
    std::string value;          // "189.30" - already formatted
    std::string change;         // "+1.25 (+0.66%)" - drawn in the trend colour
    TickerTrend trend = TickerTrend::Flat;
    Color labelColor = Color(0, 0, 0, 0);   // transparent = the style's colour
    Color valueColor = Color(0, 0, 0, 0);
    std::string tooltip;        // shown while the pointer rests on the item
};
```

`TickerItem` has three constructors: default, `TickerItem(label)` for a
message, and `TickerItem(label, value, change = "", trend = Flat)`.

## Basic Usage

```cpp
#include "Plugins/Diagrams/UltraCanvasTickerBoard.h"

auto board = CreateTickerBoard("Prices", 0, 0, 800, 44, TickerBoardFace::Text);
board->AddItem(MakeTickerQuote("UCNV", 189.30, 1.25));   // "189.30", "+1.25 (+0.66%)", Up
board->AddItem(MakeTickerQuote("VFX", 42.18, -0.64));    // "42.18", "-0.64 (-1.49%)", Down
board->AddItem(TickerItem("Platform 4", "12:45"));       // a label and a value
board->onItemClick = [](size_t index) {
    // open the quote at `index`
    (void)index;
};
container->AddChild(board);
```

The board starts rolling on its first frame; there is nothing to start.

### A crawling message

```cpp
auto sign = CreateTickerBoard("Sign", 0, 0, 800, 64, TickerBoardFace::DotMatrix);
sign->SetText("WELCOME - Next departure: Platform 4 at 12:45");
```

`SetText()` replaces the items with one label-only item.

### Re-pricing while it rolls

```cpp
// Called from a feed callback: the strip keeps its position.
board->UpdateItem(0, MakeTickerQuote("UCNV", 190.05, 2.00));
```

`SetItems()`, `AddItem()`, `RemoveItem()` and `ClearItems()` relay the whole
strip; `UpdateItem()` is the one to call from a feed.

## Motion

```cpp
board->Stop();                    // halts where it is
board->Start();                   // resumes
board->SetRunning(false);         // the same pair, as a flag
board->SetSpeed(120.0f);          // pixels per second
board->SetDirection(TickerDirection::Rightward);
board->SetOffset(0.0);            // the first item's label at the left edge
```

Distance is computed from elapsed time, not counted per frame, so a late
timer never slows the crawl down; the DotMatrix face rounds the position to
whole columns and redraws only when a column boundary is crossed. While
`pauseOnHover` is set (the default), the board holds still from `MouseEnter`
to `MouseLeave` and the pause is not caught up afterwards.

## Style

```cpp
TickerBoardStyle s = board->GetStyle();
s.face = TickerBoardFace::DotMatrix;
s.direction = TickerDirection::Leftward;
s.pixelsPerSecond = 110.0f;
s.pauseOnHover = true;
s.backgroundColor = Color(12, 10, 8, 255);
s.labelColor = Color(255, 170, 0, 255);   // amber
s.upColor = Color(56, 214, 122, 255);
s.downColor = Color(244, 84, 84, 255);
s.separator = "\xE2\x80\xA2";             // a bullet between items; "" for a gap only
s.cornerRadius = 6.0f;
board->SetStyle(s);
```

| Field | Face | Meaning |
|---|---|---|
| `face` | - | `Text` or `DotMatrix` |
| `direction` | - | `Leftward` (items enter at the right) or `Rightward` |
| `pixelsPerSecond` | - | crawl speed; DotMatrix moves in whole columns |
| `frameIntervalMs` | - | timer period, 33 ms by default |
| `pauseOnHover` | - | hold still while the pointer is on the board |
| `backgroundColor`, `cornerRadius` | - | the band |
| `labelColor`, `valueColor` | - | the label and value fields (an item may override both) |
| `upColor`, `downColor`, `flatColor` | - | the change field and its arrow, by trend |
| `separatorColor`, `separator` | - | the glyph between items; empty for none |
| `showArrows` | - | a triangle before the change when the trend is not `Flat` |
| `fontFamily`, `fontSize` | Text | the font; empty family is the UI default, size 0 is sized from the band |
| `labelWeight`, `valueWeight` | Text | weights of the label and of the other fields |
| `fieldGap`, `itemGap` | Text | px between fields, and either side of the separator |
| `dotPitch` | DotMatrix | dot centre to centre; 0 fits seven rows to the band height |
| `dotFill` | DotMatrix | dot diameter as a share of the pitch (0.3..1) |
| `showOffDots`, `offDotColor` | DotMatrix | the unlit grid behind the text |
| `glyphGapColumns`, `fieldGapColumns`, `itemGapColumns` | DotMatrix | blank columns between glyphs, fields and items |

A font family may be a comma-separated fallback list
(`"DSEG14 Classic,Monospace"`), so a seven-segment face is one style field
away when such a font is installed.

## Interaction

```cpp
board->onItemClick = [](size_t index) { (void)index; };   // left button released on an item
board->onItemHover = [](size_t index) { (void)index; };   // SIZE_MAX when the pointer left every item
size_t under = board->GetItemAt(120);                      // board-local x, from the last frame drawn
```

An item's `tooltip` is shown through `UltraCanvasTooltipManager` while the
pointer rests on it and hidden when it leaves. The separator belongs to no
item: between items `GetItemAt()` returns `SIZE_MAX`.

## Accessibility

The board reads as a label (`AccessibleRole::Label`) whose value text is the
items in order - `"UCNV 189.30 +1.25 (+0.66%); VFX 42.18 -0.64 (-1.49%)"` -
so a screen reader gets the content once, not the animation.

## Quote formatting

```cpp
TickerItem q = MakeTickerQuote("UCNV", 189.30, 1.25);          // decimals 2, with percent
TickerItem r = MakeTickerQuote("BTC", 64123.5, -812.25, 1, false);
```

`MakeTickerQuote(symbol, price, change, decimals = 2, withPercent = true)`
formats the value at `decimals` places, prefixes a positive change with `+`,
appends the percentage against the previous price (`price - change`) when
asked, and sets the trend from the sign of the change. The numbers are
formatted for a reader, in the application's locale.

## API Reference

```cpp
void SetItems(const std::vector<TickerItem>& newItems);
size_t AddItem(const TickerItem& item);
void UpdateItem(size_t index, const TickerItem& item);
void RemoveItem(size_t index);
void ClearItems();
const std::vector<TickerItem>& GetItems() const;
size_t GetItemCount() const;
void SetText(const std::string& message);

void Start();
void Stop();
void SetRunning(bool running);
bool IsRunning() const;
void SetOffset(double pixels);
double GetOffset() const;
double GetStripLength() const;   // one pass of the strip in px, from the last frame drawn

TickerBoardStyle& GetStyle();
void SetStyle(const TickerBoardStyle& s);
void SetFace(TickerBoardFace face);
void SetDirection(TickerDirection direction);
void SetSpeed(float pixelsPerSecond);

size_t GetItemAt(int localX) const;
```

```cpp
std::function<void(size_t index)> onItemClick;
std::function<void(size_t index)> onItemHover;
```

Factory and helper, free functions in `namespace UltraCanvas`:

```cpp
std::shared_ptr<UltraCanvasTickerBoard> CreateTickerBoard(
        const std::string& identifier, float x, float y, float w, float h,
        TickerBoardFace face = TickerBoardFace::Text);
TickerItem MakeTickerQuote(const std::string& symbol, double price, double change,
                           int decimals = 2, bool withPercent = true);
```

## Demo

DemoApp, *Diagrams → Ticker Board*
(`Apps/DemoApp/UltraCanvasTickerBoardExamples.cpp`): a market ticker in a
font, an amber LED sign, dot-matrix quotes re-priced every 1.5 s in place, a
rightward headline crawl on a light band, and buttons that pause, re-speed,
reverse and re-face the first board while it rolls.

## See Also

- [`UltraCanvasUIElements.md`](UltraCanvasUIElements.md) - the element catalogue
- [`UltraCanvasGaugeExamples.md`](UltraCanvasGaugeExamples.md) - `GaugeMode::Digital`, the still LED readout
- [`UltraCanvasBusyIndicator`](UltraCanvasUIElements.md) - the other self-animating element, built on the same timer
