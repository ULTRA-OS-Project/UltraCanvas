# UltraCanvas Badge Documentation

## Overview

**UltraCanvasBadge** is a small count / status indicator. It works two ways:

- **Standalone** — a coloured pill with a number or short label, or a minimal
  status dot.
- **Overlay** — anchored to a corner of another element (the classic
  notification count on a bell / inbox icon); it straddles the corner and
  follows the anchor.

**File Location**: `include/UltraCanvasBadge.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Features

- ✅ **Count** mode with a max cap (e.g. `150` → `"99+"`) and show-zero suppression
- ✅ **Text / status** mode (short label)
- ✅ **Dot** mode (minimal status indicator, no text)
- ✅ Colour **variants**: Neutral · Primary · Successful · Warning · Danger · Info
  (or a fully custom colour)
- ✅ **Overlay anchoring** to any corner of a sibling element, with a white
  separator ring
- ✅ Auto-sizes to content; optional `onClick`

> The green variant is spelled **`Successful`** (not `Success`) because `Success`
> is an X11 macro (`#define Success 0`) — the same rename the framework uses for
> `DialogType::Successful` and `AlertSeverity::Successful`.

## Quick Start

```cpp
#include "UltraCanvasBadge.h"
using namespace UltraCanvas;

// Status pill
auto s = CreateBadge("s1", 20, 20, "Active", BadgeVariant::Successful);

// Count badge (danger red, caps at 99+)
auto c = CreateCountBadge("c1", 20, 60, 12);
c->SetMaxCount(99);      // 100 -> "99+"
c->SetShowZero(false);   // count 0 renders nothing (default)

// Status dot
auto d = CreateDotBadge("d1", 20, 100, BadgeVariant::Warning);

// Overlay badge on an icon tile (both are children of the same container)
auto badge = CreateCountBadge("bell-badge", 0, 0, 3);
badge->AnchorTo(bellIcon, BadgeCorner::TopRight, /*dx*/ -4, /*dy*/ 4);
badge->SetCount(unreadCount);   // updates live; 0 hides it
```

## Factory Functions

| Function | Result |
|----------|--------|
| `CreateBadge(id, x, y, text, variant)` | Text/status pill |
| `CreateCountBadge(id, x, y, count, variant = Danger)` | Number badge (99+ cap) |
| `CreateDotBadge(id, x, y, variant = Successful)` | Status dot |

## Key API

```cpp
// Content
void SetText(const std::string&);      // status/text mode
void SetCount(int);                    // count mode
void SetMaxCount(int);                 // cap, e.g. 99 -> "99+"
void SetShowZero(bool);                // show a 0 count instead of hiding
void SetDot(bool);                     // dot mode (no text)
bool IsBadgeVisible() const;           // false when a 0 count / empty text hides it

// Appearance
void SetVariant(BadgeVariant);         // Neutral|Primary|Successful|Warning|Danger|Info
void SetColor(const Color&);           // custom colour override
BadgeStyle& GetStyle();

// Overlay
void AnchorTo(const std::shared_ptr<UltraCanvasUIElement>& anchor,
              BadgeCorner corner = BadgeCorner::TopRight,
              float offsetX = 0.0f, float offsetY = 0.0f);
void SetOverlayRing(bool);             // white separator ring (default on when anchored)

std::function<void()> onClick;
```

## Notes

- **Anchoring** requires the badge and its anchor to be siblings in the same
  container (so their bounds share a coordinate space). The badge sets a high
  z-index automatically so it draws above the anchor, sizes itself to its content,
  and re-positions to the chosen corner each frame — so it tracks a moving or
  resizing anchor.
- A count of `0` with show-zero off renders nothing (`IsBadgeVisible()` returns
  false); the element stays in place so it reappears when the count returns.
- The Warning variant uses dark text on amber for contrast; all other variants
  use white text.
- For an *interactive* pill with a remove button or selection, use
  `UltraCanvasChip` instead — a Badge is a passive indicator.
