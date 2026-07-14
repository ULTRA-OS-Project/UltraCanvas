# UltraCanvas Colour Picker Documentation

## Overview

The **UltraCanvasColorPicker** is a comprehensive, self-contained colour selection
widget for the UltraCanvas Framework. It combines an HSV colour wheel, a
saturation/value square, preview swatches, a hex input field, a model selector
(HSV / HSL / RGB) and editable channel sliders with a dedicated alpha channel —
all rendered natively with the framework's `IRenderContext` primitives.

**File Location**: `include/UltraCanvasColorPicker.h`
**Implementation**: `core/UltraCanvasColorPicker.cpp`
**Version**: 1.1.0
**Last Modified**: 2026-07-12
**Author**: UltraCanvas Framework

## Features

- ✅ **Hue ring** — drag around the wheel to pick the hue.
- ✅ **Saturation / Value square** — drag inside the inscribed square.
- ✅ **Preview swatches** — foreground (current) vs. background (previous) colour,
  with a swap action. Click the background swatch to revert.
- ✅ **Full-surface hover preview** — hovering either swatch floods the whole
  widget background with that colour (opaque) so it can be judged on a large
  area; moving the pointer away restores the normal UI background.
- ✅ **Screen colour picker ("eyedropper")** — a button right of the background
  swatch samples a screen pixel: **left mouse** into the foreground colour,
  **right mouse** into the background colour (wired via `onScreenColorPick`).
- ✅ **Hex input** — click to edit, accepts `#RGB`, `#RRGGBB` and `#RRGGBBAA`.
- ✅ **Model selector** — dropdown and tab row switch the channel editors between
  **HSV**, **HSL** and **RGB**.
- ✅ **Channel sliders** — three colour channels plus alpha, each with a live
  gradient track and an editable numeric value box.
- ✅ **Alpha channel** — checkerboard-backed alpha slider.
- ✅ **Compact mode** — hide the wheel/swatches for an inline sliders-only editor.
- ✅ **Callbacks** — `onColorChanging` (continuous) and `onColorChanged` (final).

## Class Definition

```cpp
namespace UltraCanvas {
    class UltraCanvasColorPicker : public UltraCanvasUIElement {
    public:
        UltraCanvasColorPicker(const std::string& id, float x, float y, float w, float h);

        Color   GetColor() const;
        void    SetColor(const Color& color, bool notify = true);
        uint8_t GetAlpha() const;
        void    SetAlpha(uint8_t a, bool notify = true);
        void    SetHSV(float h, float s, float v, bool notify = true);

        ColorPickerModel GetModel() const;
        void             SetModel(ColorPickerModel m);   // HSV / HSL / RGB

        void SetShowColorWheel(bool show);               // compact mode when false
        void SetShowAlpha(bool show);

        // Foreground = current swatch, Background = previous swatch.
        Color GetForegroundColor() const;
        void  SetForegroundColor(const Color& c, bool notify = true);
        Color GetBackgroundColor() const;
        void  SetBackgroundColor(const Color& c);

        std::function<void(const Color&)> onColorChanged;
        std::function<void(const Color&)> onColorChanging;
        std::function<void(bool foreground)> onScreenColorPick; // eyedropper
    };
}
```

## Basic Usage

```cpp
#include "UltraCanvasColorPicker.h"

// Factory with an initial colour
auto picker = UltraCanvas::CreateColorPicker("picker", UltraCanvas::Colors::Red,
                                             20, 20, 290, 470);

picker->onColorChanged = [](const UltraCanvas::Color& c) {
    printf("Selected: %s\n", c.ToHexStringWithAlpha().c_str());
};

container->AddChild(picker);
```

### Compact (sliders-only) variant

```cpp
auto compact = UltraCanvas::CreateColorPicker("compact", color, 20, 20, 260, 150);
compact->SetShowColorWheel(false);   // hide the wheel + swatches
```

## Colour Space Helpers

The header also exposes free helper functions so callers can round-trip colours
without losing hue information on greys:

```cpp
void  RGBToHSV(const Color& c, float& h, float& s, float& v);
void  RGBToHSL(const Color& c, float& h, float& s, float& l);
Color HSLToRGB(float h, float s, float l, uint8_t a = 255);
// HSV -> RGB is provided by the existing global Color HSV(h, s, v, a) helper.
```

## Interaction Summary

| Action | Result |
|--------|--------|
| Drag on the hue ring | Sets hue |
| Drag inside the SV square | Sets saturation & value |
| Hover a swatch | Floods the whole widget surface with that colour for a big-area preview |
| Click background (previous) swatch | Reverts to the background colour |
| Click the swap arrow | Swaps foreground and background colours |
| Left-click the eyedropper | Requests a screen pixel sample into the foreground colour |
| Right-click the eyedropper | Requests a screen pixel sample into the background colour |
| Click hex / value box | Begins inline editing (Enter commits, Esc cancels) |
| Click a tab or the dropdown | Switches HSV / HSL / RGB channel editors |
| Drag a channel/alpha slider | Adjusts that component |

## Screen colour picker (eyedropper)

The eyedropper button next to the background swatch raises the `onScreenColorPick`
callback. Actual screen sampling is platform specific, so the host supplies the
pixel and writes it back into the picker:

```cpp
picker->onScreenColorPick = [picker](bool foreground) {
    Color pixel = SampleScreenPixelUnderCursor();   // host-provided
    if (foreground) picker->SetForegroundColor(pixel);
    else            picker->SetBackgroundColor(pixel);
};
```
