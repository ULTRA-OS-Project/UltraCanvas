# UltraCanvasScrollbar Documentation

## Overview

**UltraCanvasScrollbar** is a standalone scrollbar control with full interaction support: thumb dragging, track paging, mouse-wheel scrolling, and optional arrow buttons. It works in either vertical or horizontal orientation and is configured through a single `ScrollbarStyle` struct that controls dimensions, colours (with separate normal / hover / pressed states), corner radius / end shape, an optional custom image (PNG or SVG) thumb handle, and scrolling behaviour. Several built-in style presets are provided, and a `std::function<void(int)> onScrollChange` callback reports live position changes.

**Version:** 1.0.0
**Header:** `include/UltraCanvasScrollbar.h`
**Namespace:** `UltraCanvas`
**Base Class:** `UltraCanvasUIElement`

## Features

- **Two Orientations**: Vertical and horizontal, selectable at construction or via `SetOrientation`
- **Style Presets**: `Default`, `Modern`, `Minimal`, `Classic` (with arrow buttons), and `DropDown`
- **Full Colour Control**: Separate track and thumb colours for normal, hover, and pressed states
- **Corner Radius / End Shape**: Independent `thumbCornerRadius` and `trackCornerRadius` for square, soft, or round (pill) ends
- **Custom Image Handle**: Draw the thumb from a PNG or SVG image via `thumbImagePath` (scaled with its aspect ratio preserved)
- **Arrow Buttons**: Optional up/down (left/right) arrow buttons via `arrowButtonSize`
- **Scroll Operations**: Line, page, top/bottom, wheel, and arbitrary delta scrolling helpers
- **Auto-Hide**: Optionally hide the scrollbar when content fits the viewport
- **Live Callback**: `onScrollChange(int position)` fires on every position change

## Header Include

```cpp
#include "UltraCanvasScrollbar.h"
```

## Class Reference

### Constructors

```cpp
// Full constructor: id, position, size, orientation.
UltraCanvasScrollbar(const std::string& id, float x, float y, float w, float h,
                     ScrollbarOrientation orient = ScrollbarOrientation::Vertical);

// Size only (position resolved by layout).
UltraCanvasScrollbar(const std::string& id, float w, float h,
                     ScrollbarOrientation orient = ScrollbarOrientation::Vertical);

// Id and orientation only.
UltraCanvasScrollbar(const std::string& id,
                     ScrollbarOrientation orient = ScrollbarOrientation::Vertical);

// Orientation only.
explicit UltraCanvasScrollbar(ScrollbarOrientation orient = ScrollbarOrientation::Vertical);
```

### Factory Functions

```cpp
std::shared_ptr<UltraCanvasScrollbar> CreateScrollbar(
    const std::string& id, float x, float y, float w, float h,
    ScrollbarOrientation orientation = ScrollbarOrientation::Vertical);

std::shared_ptr<UltraCanvasScrollbar> CreateVerticalScrollbar(
    const std::string& id, float x, float y, float width, float height);

std::shared_ptr<UltraCanvasScrollbar> CreateHorizontalScrollbar(
    const std::string& id, float x, float y, float width, float height);
```

### Enumerations

#### ScrollbarOrientation

```cpp
enum class ScrollbarOrientation {
    Vertical,
    Horizontal
};
```

### ScrollbarStyle Structure

The complete appearance and behaviour of a scrollbar is described by `ScrollbarStyle`. All fields have sensible defaults; assign only the ones you need.

```cpp
struct ScrollbarStyle {
    // Dimensions
    int trackSize       = 16;   // Thickness of the bar
    int thumbMinSize    = 20;   // Minimum thumb length
    int arrowButtonSize = 0;    // 0 = no arrow buttons

    // Track colors
    Color trackColor       = Color(240, 240, 240, 255);
    Color trackHoverColor  = Color(235, 235, 235, 255);
    Color trackBorderColor = Color(220, 220, 220, 255);

    // Thumb colors
    Color thumbColor        = Color(192, 192, 192, 255);
    Color thumbHoverColor   = Color(160, 160, 160, 255);
    Color thumbPressedColor = Color(128, 128, 128, 255);
    Color thumbBorderColor  = Color(170, 170, 170, 255);

    // Arrow button colors (if enabled)
    Color arrowColor                = Color(96, 96, 96, 255);
    Color arrowHoverColor           = Color(64, 64, 64, 255);
    Color arrowPressedColor         = Color(32, 32, 32, 255);
    Color arrowBackgroundColor      = Color(240, 240, 240, 255);
    Color arrowBackgroundHoverColor = Color(220, 220, 220, 255);

    // Appearance options
    int  thumbCornerRadius = 0;
    int  trackCornerRadius = 0;
    bool showTrackBorder   = false;
    bool showThumbBorder   = false;

    // Custom handle (thumb) image. When non-empty, the thumb is drawn from
    // this image (PNG or SVG) instead of the solid rounded rectangle. The image
    // keeps its aspect ratio: it is scaled to fit the thumb rect and centered
    // within it, never stretched. SVG handles scale crisply.
    std::string  thumbImagePath;
    // Optional orientation-specific handle for horizontal bars; falls back to
    // thumbImagePath when empty.
    std::string  thumbImagePathHorizontal;

    // Behavior
    bool autoHide            = false;
    int  scrollSpeed         = 20;    // Pixels per scroll step (one "line")
    int  wheelScrollLines    = 3;     // Line steps per wheel notch (Windows-like)
    int  pageScrollRatio     = 90;    // Percentage of viewport for page scroll
    // Wheel, arrow-button and track-page scrolls glide to their target
    // (ease-out) instead of jumping; thumb drags and programmatic
    // SetScrollPosition() stay immediate. On by default.
    bool smoothScrolling     = true;
    int  smoothScrollDuration = 150;  // milliseconds

    // Preset styles
    static ScrollbarStyle Default();
    static ScrollbarStyle Modern();
    static ScrollbarStyle Minimal();
    static ScrollbarStyle Classic();
    static ScrollbarStyle DropDown();
};
```

### Style Presets

```cpp
ScrollbarStyle::Default();   // 16px track, solid grey thumb, square ends
ScrollbarStyle::Modern();    // 12px track, rounded thumb, auto-hide
ScrollbarStyle::Minimal();   // 8px translucent track, rounded thumb, auto-hide
ScrollbarStyle::Classic();   // 16px track with borders and arrow buttons
ScrollbarStyle::DropDown();  // 10px translucent track, rounded thumb
```

### Orientation

```cpp
void SetOrientation(ScrollbarOrientation orient);
ScrollbarOrientation GetOrientation() const;
bool IsVertical() const;
bool IsHorizontal() const;
```

### Style

```cpp
void SetStyle(const ScrollbarStyle& newStyle);
const ScrollbarStyle& GetStyle() const;
ScrollbarStyle& GetStyleRef();
```

### Scroll Parameters

```cpp
// Set the visible (viewport) size and the total scrollable content size.
void SetScrollDimensions(int viewportSize, int contentSize);
void SetViewportSize(int size);
void SetContentSize(int size);

int GetViewportSize() const;
int GetContentSize() const;
```

### Scroll Position

```cpp
bool SetScrollPosition(int position);      // Returns true if the position changed
int  GetScrollPosition() const;
int  GetScrollPositionPercent() const;     // Position as a fraction of the max
int  GetMaxScrollPosition() const;
```

### Scroll Operations

```cpp
bool ScrollBy(int delta);
bool ScrollToTop();
bool ScrollToBottom();
bool ScrollToStart();
bool ScrollToEnd();
bool ScrollLineUp();      // Moves by style.scrollSpeed
bool ScrollLineDown();
bool ScrollPageUp();      // Moves by a fraction of the viewport (pageScrollRatio)
bool ScrollPageDown();
// delta is a signed wheel-notch count (+/-1 per notch); scrolls
// scrollSpeed * wheelScrollLines pixels per notch.
bool ScrollByWheel(int delta);
// Animated scrolling; successive calls accumulate on the pending target so
// fast wheel notches chain into one glide. Immediate when smoothScrolling
// is off. IsScrollAnimating() reports a scroll animation in flight.
bool SmoothScrollBy(int delta);
bool SmoothScrollTo(int targetPosition);
```

Line, page and wheel scrolls animate when `smoothScrolling` is enabled (the
default); `ScrollBy`/`ScrollTo*`/`SetScrollPosition` are immediate and cancel
any running animation.

### Scrollability and Visibility

```cpp
bool IsScrollable() const;       // True when there is content beyond the viewport
bool ShouldBeVisible() const;    // Honors autoHide + IsVisible()
```

### State Access

```cpp
const ScrollbarScrollState&       GetScrollState() const;
const ScrollbarInteractionState&  GetInteractionState() const;
bool IsDragging() const;

Rect2Di GetTrackRect() const;
Rect2Di GetThumbRect() const;
```

### Callback

```cpp
// Fires whenever the scroll position changes. The argument is the new position.
std::function<void(int)> onScrollChange;
```

### Rendering and Events

```cpp
void SetBounds(const Rect2Df& b) override;
void Render(IRenderContext* ctx, const Rect2Df& dirtyRect) override;
bool OnEvent(const UCEvent& event) override;
bool HandleMouseWheel(const UCEvent& event);
```

## Examples

The following examples mirror the variants shown in the demo
(`Apps/DemoApp/UltraCanvasScrollbarExamples.cpp`). In each case a scrollbar is
made *scrollable* by giving it a content size larger than its viewport so the
thumb is draggable and the wheel works.

### 1. Preset Styles

Build a vertical scrollbar from one of the built-in presets. The track size of
the chosen preset is used as the bar's thickness.

```cpp
auto style = ScrollbarStyle::Modern();
const float barH = 150.0f;

auto sb = std::make_shared<UltraCanvasScrollbar>(
    "sbModern", 60.0f, 140.0f, static_cast<float>(style.trackSize), barH,
    ScrollbarOrientation::Vertical);
sb->SetStyle(style);
sb->SetScrollDimensions(static_cast<int>(barH), 460);  // viewport < content
sb->SetScrollPosition(120);
sb->onScrollChange = [](int pos) {
    std::cerr << "Modern scrollbar position: " << pos << std::endl;
};
container->AddChild(sb);
```

Other presets are used identically:

```cpp
sbDefault->SetStyle(ScrollbarStyle::Default());
sbMinimal->SetStyle(ScrollbarStyle::Minimal());
sbClassic->SetStyle(ScrollbarStyle::Classic());   // adds arrow buttons
sbDropdown->SetStyle(ScrollbarStyle::DropDown());
```

### 2. Colour Options

Every colour state can be set independently. Start from `Default()` (or a fresh
`ScrollbarStyle`) and override the track and thumb colours. Round ends are
obtained by setting the corner radii to half the track thickness.

```cpp
ScrollbarStyle blue;                       // trackSize 16
blue.trackColor        = Color(225, 232, 245, 255);
blue.thumbColor        = Color(90, 140, 220, 255);
blue.thumbHoverColor   = Color(70, 120, 200, 255);
blue.thumbPressedColor = Color(50, 100, 180, 255);
blue.thumbCornerRadius = blue.trackSize / 2;   // pill thumb
blue.trackCornerRadius = blue.trackSize / 2;

auto sb = CreateVerticalScrollbar("sbBlue", 60.0f, 380.0f, 16.0f, 150.0f);
sb->SetStyle(blue);
sb->SetScrollDimensions(150, 460);
container->AddChild(sb);
```

### 3. Corner Radius / End Shape

`thumbCornerRadius` and `trackCornerRadius` independently control the end shape,
ranging from square (r = 0) through soft to fully round (pill). The thumb and
track radii do not have to match.

```cpp
auto makeShape = [](int thumbR, int trackR) {
    ScrollbarStyle s;                          // trackSize 16
    s.thumbColor       = Color(150, 160, 175, 255);
    s.thumbHoverColor  = Color(120, 130, 150, 255);
    s.thumbCornerRadius = thumbR;
    s.trackCornerRadius = trackR;
    return s;
};

// Square ends.
auto sbSquare = CreateVerticalScrollbar("sbSquare", 60.0f, 620.0f, 16.0f, 150.0f);
sbSquare->SetStyle(makeShape(0, 0));
sbSquare->SetScrollDimensions(150, 460);

// Soft corners.
auto sbSoft = CreateVerticalScrollbar("sbSoft", 180.0f, 620.0f, 16.0f, 150.0f);
sbSoft->SetStyle(makeShape(3, 3));
sbSoft->SetScrollDimensions(150, 460);

// Round / pill thumb on a flat (square) track.
auto sbPillFlat = CreateVerticalScrollbar("sbPillFlat", 300.0f, 620.0f, 16.0f, 150.0f);
sbPillFlat->SetStyle(makeShape(8, 0));
sbPillFlat->SetScrollDimensions(150, 460);
```

### 4. Custom SVG Handle

Set `thumbImagePath` to a PNG or SVG file to draw the thumb from an image
instead of the solid rounded rectangle. The image always keeps its aspect
ratio: it is scaled to fit the thumb rect and centered within it, so a long
thumb shows a centered grip rather than a stretched one. SVG handles scale
crisply, so the same handle can ride bars of different widths.

```cpp
const std::string vHandle =
    NormalizePath(GetResourcesDir() + "media/icons/scrollbar-handle-v.svg");

ScrollbarStyle handleV;
handleV.trackSize        = 14;
handleV.thumbMinSize     = 44;
handleV.trackColor       = Color(238, 240, 243, 255);
handleV.trackCornerRadius = 7;
handleV.thumbImagePath   = vHandle;            // SVG handle

auto sb = std::make_shared<UltraCanvasScrollbar>(
    "sbHandleV", 60.0f, 862.0f, static_cast<float>(handleV.trackSize), 150.0f,
    ScrollbarOrientation::Vertical);
sb->SetStyle(handleV);
sb->SetScrollDimensions(150, 460);
container->AddChild(sb);
```

The same style can be reused on a wider bar; the SVG scales to fit:

```cpp
ScrollbarStyle handleVWide = handleV;
handleVWide.trackSize    = 20;
handleVWide.thumbMinSize = 52;
// ... attach to a 20px-wide scrollbar
```

### 5. Horizontal Orientation

Construct the scrollbar with `ScrollbarOrientation::Horizontal` (or use
`CreateHorizontalScrollbar`). For a horizontal bar the *height* is the track
thickness and the width is the scrollable length.

```cpp
const float width = 300.0f;

ScrollbarStyle round = ScrollbarStyle::Default();
round.thumbColor        = Color(90, 140, 220, 255);
round.thumbCornerRadius = round.trackSize / 2;   // round ends
round.trackCornerRadius = round.trackSize / 2;

auto sb = std::make_shared<UltraCanvasScrollbar>(
    "sbHRound", 60.0f, 1130.0f, width, static_cast<float>(round.trackSize),
    ScrollbarOrientation::Horizontal);
sb->SetStyle(round);
sb->SetScrollDimensions(static_cast<int>(width),
                        static_cast<int>(width * 2.5f));   // content > viewport
sb->SetScrollPosition(80);
sb->onScrollChange = [](int pos) {
    std::cerr << "Horizontal position: " << pos << std::endl;
};
container->AddChild(sb);
```

### 6. Reacting to Scroll Changes

`onScrollChange` is the primary integration point. It receives the new position
on every change, whether from dragging, paging, the wheel, or a programmatic
`SetScrollPosition`.

```cpp
auto sb = CreateVerticalScrollbar("sbContent", 60.0f, 140.0f, 16.0f, 200.0f);
sb->SetScrollDimensions(200, 800);

std::weak_ptr<UltraCanvasScrollbar> sbWeak = sb;
sb->onScrollChange = [sbWeak](int pos) {
    auto s = sbWeak.lock();
    if (!s) return;
    // Offset your content by `pos`, then ...
    // s->GetScrollPositionPercent() gives the fraction scrolled.
};
container->AddChild(sb);
```

## Best Practices

- **Always set scroll dimensions.** A scrollbar only becomes interactive once
  `SetScrollDimensions(viewportSize, contentSize)` is given a content size
  larger than the viewport (`IsScrollable()` returns `true`).
- **Match the cross-axis size to `trackSize`.** For a vertical bar set the
  width to `style.trackSize`; for a horizontal bar set the height. The demo
  derives the bar thickness directly from the chosen style.
- **Round ends with half the track size.** Set `thumbCornerRadius` and
  `trackCornerRadius` to `trackSize / 2` for a pill shape; they can differ to
  put a rounded thumb on a flat track.
- **Prefer SVG for custom handles.** SVG `thumbImagePath` handles scale crisply
  across different bar widths; the handle is scaled with its aspect ratio
  preserved and centered in the thumb, never stretched.
- **Use `autoHide` for overlay scrollbars.** Presets `Modern` and `Minimal`
  enable it so the bar hides when content fits; check `ShouldBeVisible()`.
- **Capture by `weak_ptr` in callbacks.** When an `onScrollChange` lambda needs
  the scrollbar itself, capture a `std::weak_ptr` to avoid reference cycles.
- **Use the scroll helpers** (`ScrollLineDown`, `ScrollPageUp`, `ScrollToTop`,
  etc.) instead of recomputing positions by hand; they honour `scrollSpeed` and
  `pageScrollRatio` from the style.

## See Also

- [`Apps/DemoApp/UltraCanvasScrollbarExamples.cpp`](../../Apps/DemoApp/UltraCanvasScrollbarExamples.cpp) - Full runnable demo of every variant above
- [`include/UltraCanvasScrollbar.h`](../../UltraCanvas/include/UltraCanvasScrollbar.h) - Public API header
- [UltraCanvasContainer](UltraCanvasContainer.md) - Hosting container
- [UltraCanvasLabel](UltraCanvasLabel.md) - Companion text element
