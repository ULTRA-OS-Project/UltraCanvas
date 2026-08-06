# UltraCanvasTooltipManager Documentation

**Version:** 2.2.0  
**Last Modified:** 2026-08-06  
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasTooltipManager` is the framework-wide tooltip system. It shows a
single shared tooltip for the element under the pointer, handles show/hide
delays, positions the tooltip inside the window, and renders it during window
composition. Any element with a non-empty tooltip text (set via
`UltraCanvasUIElement::SetTooltip`) gets a tooltip automatically — application
code normally never calls the manager directly.

The default look is a modern dark tooltip: dark neutral background, light
text, 6 px rounded corners and a soft drop shadow. A light preset is available
via `TooltipStyle::Light()`.

## Basic Usage

### Including the Header

```cpp
#include "UltraCanvasTooltipManager.h"
```

### Automatic tooltips

```cpp
auto button = CreateButton("Save", 101, 10, 10, 120, 40, "Save");
button->SetTooltip("Save the current document");
// The application shows/hides the tooltip automatically on hover.
```

### Showing a tooltip manually

```cpp
// e.g. from a chart hover handler; the tooltip text may contain Pango markup
UltraCanvasTooltipManager::UpdateAndShowTooltip(window, text, cursorPosition);

// with a custom style
TooltipStyle style = TooltipStyle::Light();
style.fontSize = 12.0f;
UltraCanvasTooltipManager::UpdateAndShowTooltip(window, text, cursorPosition, style);

UltraCanvasTooltipManager::HideTooltip();             // honors hideDelay
UltraCanvasTooltipManager::HideTooltipImmediately();  // no delay
```

## TooltipStyle Reference

```cpp
struct TooltipStyle {
    // Appearance (defaults = dark theme, see presets below)
    Color backgroundColor = Color(45, 45, 48, 245);  // dark neutral, slightly translucent
    Color borderColor = Color(255, 255, 255, 36);    // subtle light hairline
    Color textColor = Color(242, 242, 242, 255);
    Color shadowColor = Color(0, 0, 0, 90);          // peak (core) shadow alpha

    // Typography
    std::string fontFamily = "Sans";
    float fontSize = 11.0f;

    // Layout
    int paddingLeft = 10, paddingRight = 10;
    int paddingTop = 7, paddingBottom = 7;
    int maxWidth = 450;        // text wraps beyond this width
    int borderWidth = 1;
    float cornerRadius = 6.0f;

    // Shadow: soft drop shadow below the tooltip.
    bool hasShadow = true;
    Point2Di shadowOffset = Point2Di(0, 3);
    int shadowBlur = 10;       // spread in px; 0 = legacy hard-edged shadow

    // Behavior
    unsigned int showDelay = 300;  // ms before showing
    unsigned int hideDelay = 200;  // ms before hiding
    int offsetX = 10, offsetY = 10;  // offset from cursor
    bool followCursor = false;
};
```

### Presets

```cpp
static TooltipStyle TooltipStyle::Dark();   // the default dark theme
static TooltipStyle TooltipStyle::Light();  // white background, dark text
```

### Soft shadow

The renderer has no Gaussian-blur primitive, so the soft shadow is
approximated by stacking `shadowBlur` translucent rounded rectangles; the
per-layer alpha is derived so the fully overlapped core reaches
`shadowColor.a`. Set `shadowBlur = 0` to get the old single hard-edged
shadow rectangle, or `hasShadow = false` to disable the shadow entirely.

## Manager API Reference

```cpp
// Show/update (honors showDelay unless the tooltip is already visible)
static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string& text,
                                 const Point2Di& position);
static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string& text,
                                 const Point2Di& position, const TooltipStyle& style);

// Show with no delay
static void UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const std::string& text,
                                            const Point2Di& position);
static void UpdateAndShowTooltipImmediately(UltraCanvasWindowBase* win, const std::string& text,
                                            const Point2Di& position, const TooltipStyle& style);

static void HideTooltip();              // honors hideDelay
static void HideTooltipImmediately();

static void SetEnabled(bool enable);    // globally enable/disable tooltips
static bool IsEnabled();
static bool IsVisible();
static bool IsPending();                // show requested, delay running

static const std::string& GetCurrentText();
static Point2Di GetTooltipPosition();   // top-left of the tooltip body
static Size2Di GetTooltipSize();        // size of the tooltip body
static void UpdateTooltipPosition(const Point2Di& cursorPosition);

// Rendering (called by the window compositor)
static IRenderContext* Render(UltraCanvasWindowBase* win);
static Point2Di GetCompositePosition(); // GetTooltipPosition() minus shadow margins
```

## Rendering Integration Notes

- `Render()` draws into an off-screen surface sized to the tooltip body plus
  the soft-shadow margins; the surface is cached until the text, style, or
  target window changes.
- The window compositor must blend the surface with
  `CompositeToSurface(nativeSurface, GetCompositePosition())` — an OVER
  blend, not a raw flush — because the shadow margins are translucent.
- Tooltip text is laid out with Pango markup; the text passed in is wrapped
  in a `<span>` carrying `fontSize`/`fontFamily`, so tooltip strings may
  themselves contain markup (e.g. `<b>`, `<span foreground="...">`).

## Dependencies

- `UltraCanvasWindow`: target window and composition
- `UltraCanvasApplication`: hover tracking and show/hide timers
- `IRenderContext`: rendering context interface
