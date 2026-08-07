# UltraCanvasTooltipManager Documentation

**Version:** 2.2.0  
**Last Modified:** 2026-08-06  
**Author:** UltraCanvas Framework

## Overview

`UltraCanvasTooltipManager` is the framework-wide tooltip system. It shows a
single shared tooltip for the element under the pointer, handles show/hide
delays, positions the tooltip inside the window, and renders it during window
composition. Any element with a tooltip — plain text via
`UltraCanvasUIElement::SetTooltip` or structured content via
`SetTooltipContent` — gets a tooltip automatically; application code normally
never calls the manager directly.

The default look is a modern dark tooltip: dark neutral background, light
text, 6 px rounded corners and a soft drop shadow. A light preset is available
via `TooltipStyle::Light()`.

Tooltip content comes in two forms:

- a **plain string**, optionally containing Pango markup for inline styling
  (`"<b>bold</b>"`, `"<i>italic</i>"`, colored `<span>`s), and
- a **`TooltipContent`** value: an ordered list of structured blocks — bold
  title, aligned label/value rows with optional color swatch, bullet list
  items, free markup text and separators — laid out natively by the manager
  (defined in `UltraCanvasTooltipTypes.h`, included by the manager header).

## Basic Usage

### Including the Header

```cpp
#include "UltraCanvasTooltipManager.h"
```

### Automatic tooltips

```cpp
auto button = CreateButton("Save", 10, 10, 120, 40, "Save");
button->SetTooltip("Save the current document");           // plain text
button->SetTooltip("Press <b>Ctrl+S</b> to save");          // inline markup

// Structured tooltip — takes precedence over the plain-text tooltip
TooltipContent content;
content.AddTitle("Batch S10")
       .AddRow("temperature", "209.79 °C")
       .AddRow("pressure", "2.27 bar")
       .AddSeparator()
       .AddText("<i>Updated 2 s ago</i>");
button->SetTooltipContent(content);
// button->ClearTooltipContent() reverts to the plain-text tooltip.
```

### Showing a tooltip manually

```cpp
// e.g. from a chart hover handler; both forms are accepted
UltraCanvasTooltipManager::UpdateAndShowTooltip(window, text, cursorPosition);
UltraCanvasTooltipManager::UpdateAndShowTooltip(window, content, cursorPosition);

// with a custom style
TooltipStyle style = TooltipStyle::Light();
style.fontSize = 12.0f;
UltraCanvasTooltipManager::UpdateAndShowTooltip(window, text, cursorPosition, style);

UltraCanvasTooltipManager::HideTooltip();             // honors hideDelay
UltraCanvasTooltipManager::HideTooltipImmediately();  // no delay
```

## TooltipContent Reference

```cpp
TooltipContent content;
content.AddTitle("Sales 2026-08")                       // bold, titleFontSize
       .AddRow("Europe", "1 204 t")                     // label/value table row
       .AddRow(Color(66,133,244,255), "Asia", "1 890 t") // row with color swatch
       .AddBullet("wraps with a hanging indent")        // list item
       .AddSeparator()                                  // thin hairline
       .AddText("free text, <b>markup allowed</b>");    // Pango markup line
content.SetStyle(TooltipStyle::Light());                // optional per-tooltip style
```

Layout rules:

- **Rows** form a two-column table: labels (muted `secondaryTextColor`) are
  left-aligned in a shared column; values are right-aligned to the tooltip's
  right edge. Labels keep their natural width up to 55 % of the available
  space. If any row carries a swatch, all labels shift so the columns align.
- **Escaping:** text passed to `AddTitle`, `AddRow` and `AddBullet` is treated
  as plain data — Pango markup characters (`&`, `<`, `>`) are escaped
  automatically, so arbitrary labels/values can never corrupt the rendering.
  Only `AddText` (and plain-string tooltips) interpret markup.
- **Style override:** `SetStyle()` attaches a `TooltipStyle` used when this
  tooltip is shown (per-element/per-chart theming). An explicit style passed
  to `UpdateAndShowTooltip` wins over the content's override.

Element integration (`UltraCanvasUIElement`):

```cpp
void SetTooltipContent(const TooltipContent& content);
const std::shared_ptr<TooltipContent>& GetTooltipContent() const;
void ClearTooltipContent();
bool HasTooltip() const;   // structured content or non-empty plain text
```

## TooltipStyle Reference

Defined in `UltraCanvasTooltipTypes.h` (re-exported by the manager header):

```cpp
struct TooltipStyle {
    // Appearance (defaults = dark theme, see presets below)
    Color backgroundColor = Color(45, 45, 48, 245);  // dark neutral, slightly translucent
    Color borderColor = Color(255, 255, 255, 36);    // subtle light hairline
    Color textColor = Color(242, 242, 242, 255);
    Color secondaryTextColor = Color(168, 168, 174, 255); // row labels, muted
    Color separatorColor = Color(255, 255, 255, 46); // AddSeparator() hairline
    Color shadowColor = Color(0, 0, 0, 90);          // peak (core) shadow alpha

    // Typography
    std::string fontFamily = "Sans";
    float fontSize = 11.0f;
    float titleFontSize = 12.0f;   // AddTitle() blocks (rendered bold)

    // Layout
    int paddingLeft = 10, paddingRight = 10;
    int paddingTop = 7, paddingBottom = 7;
    int maxWidth = 450;        // text wraps beyond this width
    int borderWidth = 1;
    float cornerRadius = 6.0f;
    int columnGap = 14;        // gap between label and value columns
    int rowSpacing = 2;        // vertical gap between content blocks

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
// Show/update (honors showDelay unless the tooltip is already visible).
// Every overload also exists with TooltipContent instead of std::string;
// the no-style TooltipContent overloads honor content.styleOverride.
static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string& text,
                                 const Point2Di& position);
static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const std::string& text,
                                 const Point2Di& position, const TooltipStyle& style);
static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const TooltipContent& content,
                                 const Point2Di& position);
static void UpdateAndShowTooltip(UltraCanvasWindowBase* win, const TooltipContent& content,
                                 const Point2Di& position, const TooltipStyle& style);

// Show with no delay (same std::string / TooltipContent overload pairs)
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
