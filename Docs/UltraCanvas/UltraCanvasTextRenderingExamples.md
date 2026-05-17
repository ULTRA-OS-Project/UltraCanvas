# UltraCanvas Text Rendering Settings Documentation

## Overview

UltraCanvas exposes the Cairo/Pango text rendering pipeline through a small set of global, process-wide settings: **antialiasing mode**, **font hinting style**, and **hint metrics**. These three settings control how every glyph is rasterized — from "no antialiasing, no hinting" (pixel-blocky, fastest) to "subpixel antialiasing with full hinting" (sharpest body text on LCD displays).

This document is a guide to the three settings and to the demo that exposes them as live drop-downs (`UltraCanvasTextRenderingExamples.cpp`).

**Version:** 1.0.0
**Header:** `libspecific/Cairo/RenderContextCairo.h`
**Namespace:** `UltraCanvas`

## Header Include

```cpp
#include "UltraCanvasDropdown.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasContainer.h"
#include "../../UltraCanvas/libspecific/Cairo/RenderContextCairo.h"
// Cairo's cairo_antialias_t, cairo_hint_style_t, cairo_hint_metrics_t come in via Cairo's own headers.
```

## API Reference

All three settings are static methods on `RenderContextCairo`. They mutate global Cairo `cairo_font_options_t` state used by every render context the next time text is drawn — there is no per-element override.

```cpp
class RenderContextCairo {
public:
    // Antialiasing — controls how glyph edges are smoothed.
    static void              SetTextAntialias(cairo_antialias_t mode);
    static cairo_antialias_t GetTextAntialias();

    // Hint Style — controls grid-fitting strength.
    static void               SetTextHintStyle(cairo_hint_style_t style);
    static cairo_hint_style_t GetTextHintStyle();

    // Hint Metrics — controls whether font metrics are snapped to integer pixels.
    static void                 SetTextHintMetrics(cairo_hint_metrics_t metrics);
    static cairo_hint_metrics_t GetTextHintMetrics();
};
```

After mutating any of the three, call `RequestRedraw()` on the affected element (or its container) so the change is visible.

## Antialias Modes

`cairo_antialias_t` values in the order the demo presents them:

| Index | Value                       | Description                                                                 |
|-------|-----------------------------|-----------------------------------------------------------------------------|
| 0     | `CAIRO_ANTIALIAS_DEFAULT`   | Backend default (typically `GRAY` on Linux)                                 |
| 1     | `CAIRO_ANTIALIAS_NONE`      | No antialiasing — strictly black/white pixels                               |
| 2     | `CAIRO_ANTIALIAS_GRAY`      | Standard grayscale antialiasing                                             |
| 3     | `CAIRO_ANTIALIAS_SUBPIXEL`  | Subpixel (LCD) antialiasing — sharpest on RGB stripe displays               |
| 4     | `CAIRO_ANTIALIAS_FAST`      | Hint to the backend: choose speed over quality                              |
| 5     | `CAIRO_ANTIALIAS_GOOD`      | Hint: balanced quality                                                      |
| 6     | `CAIRO_ANTIALIAS_BEST`      | Hint: highest quality                                                       |

## Hint Styles

`cairo_hint_style_t` values control how aggressively glyph outlines are snapped to the pixel grid before rasterization.

| Index | Value                       | Description                                                                 |
|-------|-----------------------------|-----------------------------------------------------------------------------|
| 0     | `CAIRO_HINT_STYLE_DEFAULT`  | Backend default                                                             |
| 1     | `CAIRO_HINT_STYLE_NONE`     | No hinting — preserves outline shape (best for animation/zoom)              |
| 2     | `CAIRO_HINT_STYLE_SLIGHT`   | Vertical-only hinting — modern macOS/Quartz behavior                        |
| 3     | `CAIRO_HINT_STYLE_MEDIUM`   | Moderate grid-fitting                                                       |
| 4     | `CAIRO_HINT_STYLE_FULL`     | Aggressive grid-fitting — crispest body text, can distort shapes at large sizes |

## Hint Metrics

`cairo_hint_metrics_t` controls whether font metrics (advances, ascent, descent) are quantized to integer pixel values.

| Index | Value                            | Description                                                                  |
|-------|----------------------------------|------------------------------------------------------------------------------|
| 0     | `CAIRO_HINT_METRICS_DEFAULT`     | Backend default                                                              |
| 1     | `CAIRO_HINT_METRICS_OFF`         | Fractional metrics — smoother kerning, better for animated/transformed text  |
| 2     | `CAIRO_HINT_METRICS_ON`          | Quantize metrics to integer pixels — crispest at 1:1 scale                   |

## Events / Callbacks

These settings are global — they are not events on a specific element. The standard pattern is:

1. Read current value via `RenderContextCairo::Get*()`
2. Drive it from a `UltraCanvasDropdown` (or any other control)
3. Call `SetText*()` inside the dropdown's `onSelectionChanged` callback
4. Call `RequestRedraw()` on the affected container so the new settings are visible immediately

## Usage Examples

### Reading the current value into a dropdown

```cpp
auto aaDropdown = std::make_shared<UltraCanvasDropdown>("AADropdown", 160, currentY, 200, 30);
aaDropdown->AddItem("Default",  "0");
aaDropdown->AddItem("None",     "1");
aaDropdown->AddItem("Gray",     "2");
aaDropdown->AddItem("Subpixel", "3");
aaDropdown->AddItem("Fast",     "4");
aaDropdown->AddItem("Good",     "5");
aaDropdown->AddItem("Best",     "6");
aaDropdown->SetSelectedIndex(static_cast<int>(RenderContextCairo::GetTextAntialias()));
```

### Wiring the antialias dropdown

```cpp
aaDropdown->onSelectionChanged = [mainContainer](int index, const DropdownItem& item) {
    cairo_antialias_t modes[] = {
        CAIRO_ANTIALIAS_DEFAULT, CAIRO_ANTIALIAS_NONE,     CAIRO_ANTIALIAS_GRAY,
        CAIRO_ANTIALIAS_SUBPIXEL, CAIRO_ANTIALIAS_FAST,    CAIRO_ANTIALIAS_GOOD,
        CAIRO_ANTIALIAS_BEST
    };
    RenderContextCairo::SetTextAntialias(modes[index]);
    mainContainer->RequestRedraw();
};
```

### Hint style dropdown

```cpp
auto hsDropdown = std::make_shared<UltraCanvasDropdown>("HSDropdown", 160, currentY, 200, 30);
hsDropdown->AddItem("Default", "0");
hsDropdown->AddItem("None",    "1");
hsDropdown->AddItem("Slight",  "2");
hsDropdown->AddItem("Medium",  "3");
hsDropdown->AddItem("Full",    "4");
hsDropdown->SetSelectedIndex(static_cast<int>(RenderContextCairo::GetTextHintStyle()));

hsDropdown->onSelectionChanged = [mainContainer](int index, const DropdownItem& item) {
    cairo_hint_style_t styles[] = {
        CAIRO_HINT_STYLE_DEFAULT, CAIRO_HINT_STYLE_NONE,   CAIRO_HINT_STYLE_SLIGHT,
        CAIRO_HINT_STYLE_MEDIUM,  CAIRO_HINT_STYLE_FULL
    };
    RenderContextCairo::SetTextHintStyle(styles[index]);
    mainContainer->RequestRedraw();
};
```

### Hint metrics dropdown

```cpp
auto hmDropdown = std::make_shared<UltraCanvasDropdown>("HMDropdown", 160, currentY, 200, 30);
hmDropdown->AddItem("Default", "0");
hmDropdown->AddItem("Off",     "1");
hmDropdown->AddItem("On",      "2");
hmDropdown->SetSelectedIndex(static_cast<int>(RenderContextCairo::GetTextHintMetrics()));

hmDropdown->onSelectionChanged = [mainContainer](int index, const DropdownItem& item) {
    cairo_hint_metrics_t metrics[] = {
        CAIRO_HINT_METRICS_DEFAULT, CAIRO_HINT_METRICS_OFF, CAIRO_HINT_METRICS_ON
    };
    RenderContextCairo::SetTextHintMetrics(metrics[index]);
    mainContainer->RequestRedraw();
};
```

### Building a preview that re-renders on change

The demo builds a set of labels at varying point sizes so the effect of each setting is visible across the typographic range:

```cpp
struct SampleDef { float fontSize; std::string label; std::string text; };

std::vector<SampleDef> samples = {
    {10, "10pt", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMm 0123456789"},
    {12, "12pt", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMm 0123456789"},
    {14, "14pt", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJjKkLlMm"},
    {18, "18pt", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFf 0123456789"},
    {24, "24pt", "The quick brown fox jumps over the lazy dog."},
    {36, "36pt", "The quick brown fox jumps over the lazy dog."},
};

for (const auto& sample : samples) {
    auto textLabel = std::make_shared<UltraCanvasLabel>(
        "SampleText" + sample.label, 80, currentY, 900,
        static_cast<long>(sample.fontSize * 2.5));
    textLabel->SetText(sample.text);
    textLabel->SetFontSize(sample.fontSize);
    textLabel->SetWrap(TextWrap::WrapWord);
    mainContainer->AddChild(textLabel);

    currentY += static_cast<long>(sample.fontSize * 2.5) + 10;
}
```

A separate Bold sample loop demonstrates that hinting and antialiasing also interact strongly with font weight at small sizes:

```cpp
std::vector<SampleDef> boldSamples = {
    {12, "12pt Bold", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFfGgHhIiJj 0123456789"},
    {18, "18pt Bold", "The quick brown fox jumps over the lazy dog. AaBbCcDdEeFf 0123456789"},
    {24, "24pt Bold", "Pack my box with five dozen liquor jugs."},
};

for (const auto& sample : boldSamples) {
    auto textLabel = std::make_shared<UltraCanvasLabel>(
        "BoldSample" + sample.label, 110, currentY, 870,
        static_cast<long>(sample.fontSize * 2.5));
    textLabel->SetText(sample.text);
    textLabel->SetFontSize(sample.fontSize);
    textLabel->SetFontWeight(FontWeight::Bold);
    textLabel->SetWrap(TextWrap::WrapWord);
    mainContainer->AddChild(textLabel);
}
```

## Best Practices

1. **Match hinting to your use case.** Static UI body text on LCDs: `HINT_STYLE_FULL` + `ANTIALIAS_SUBPIXEL` + `HINT_METRICS_ON`. Animated or zooming text: `HINT_STYLE_NONE` + `ANTIALIAS_GRAY` + `HINT_METRICS_OFF` — preserves outlines and prevents glyph "jumps" between frames.
2. **`ANTIALIAS_NONE` is rarely what you want.** Even at 8pt it produces blocky text on modern displays. Use only for retro/pixel-art effects.
3. **Subpixel antialiasing is display-dependent.** It assumes RGB-stripe LCDs at 1:1 scale — on rotated or non-LCD displays it can introduce color fringing.
4. **`HINT_METRICS_ON` snaps advances to integer pixels.** This makes static text crisp but causes kerning to "stair-step" when text is animated or rendered at fractional scales.
5. **Always `RequestRedraw()` after mutating these settings** — the global state is changed immediately, but cached layouts inside labels won't repaint until something marks them dirty.

## Thread Safety

The setters are not thread-safe. Call them only from the UI thread.

## See Also

- [UltraCanvasLabel](UltraCanvasLabelExamples.md) — Text display widget that consumes these settings
- [UltraCanvasDropdown](UltraCanvasDropdown.md) — Used in the demo for live selection
- [UltraCanvasRenderContext](UltraCanvasRenderContext.md) — General 2D rendering pipeline
