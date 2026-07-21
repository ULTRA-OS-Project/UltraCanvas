# UltraCanvas Colour Picker Documentation

## Overview

The **UltraCanvasColorPicker** is a comprehensive, self-contained colour selection
widget for the UltraCanvas Framework. It combines an HSV colour wheel (ring or
bar layout), a saturation/value area, preview swatches, a hex input field, a
model selector (HSV / HSL / RGB — tab bar **or** dropdown) and editable channel
sliders (thin or thick style) with a dedicated alpha channel — all rendered
natively with the framework's `IRenderContext` primitives.

**File Location**: `include/UltraCanvasColorPicker.h`
**Implementation**: `core/UltraCanvasColorPicker.cpp`
**Version**: 1.2.0
**Last Modified**: 2026-07-20
**Author**: UltraCanvas Framework

## Features

- ✅ **Hue ring** — drag around the wheel to pick the hue (Ring wheel style).
- ✅ **Hue bar** — alternative `Bar` wheel style: the SV rectangle grows to the
  maximum available area (with margin) and the full hue range is shown as a
  thick bar underneath it, without the ring.
- ✅ **Saturation / Value area** — drag inside the square (Ring) or the
  maximised rectangle (Bar).
- ✅ **Preview swatches** — foreground (current) vs. background (previous) colour,
  with a swap symbol tucked into the corner between the two swatches. Click the
  background swatch to revert.
- ✅ **Full-surface hover preview** — hovering either swatch floods the whole
  widget background with that colour (opaque) so it can be judged on a large
  area; moving the pointer away restores the normal UI background.
- ✅ **Screen colour picker ("eyedropper")** — the button right of the background
  swatch arms a picking mode. The mouse button used on the icon selects the
  target swatch — **left (Select) mouse** targets the foreground colour, **right
  (Adjust) mouse** the background colour. The pointer becomes an eyedropper
  cursor and the **target swatch live-previews the pixel under the pointer** as
  the mouse moves, so you can see the colour before committing; the next click
  commits that pixel into the target swatch, and **Esc** cancels (discarding the
  preview). Hosts can override the behaviour (e.g. for real whole-screen
  sampling) via `onScreenColorPick`.
- ✅ **Hex input** — click to edit, accepts `#RGB`, `#RRGGBB` and `#RRGGBBAA`.
- ✅ **Full inline text editing** — the hex and numeric value fields are real
  single-line editors: click places the caret, drag selects, double-click
  selects all, with the usual keyboard shortcuts (arrows / Home / End with
  Shift-selection, Ctrl+A/C/X/V, Ctrl/Shift+Insert, Ctrl+Delete = cut,
  Backspace/Delete, Tab / Shift+Tab to hop between fields, Enter commits,
  Esc cancels). Input is filtered per field (hex digits + `#`, digits, or
  digits + `.`/`-`).
- ✅ **Model selector** — either a **tab bar** (default) or a **dropdown**
  switches the channel editors between **HSV**, **HSL** and **RGB**. The two
  presentations are mutually exclusive (`SetModeSelector`).
- ✅ **Hover tooltips** — resting the pointer over a model choice (HSV / HSL /
  RGB) or a channel / hex label (H, S, V, A, L, R, G, B, Hex) shows a short
  tooltip explaining what it is and its value range.
- ✅ **Channel sliders** — three colour channels plus alpha, each with a live
  gradient track and an editable numeric value box. Two presentations
  (`SetSliderStyle`): **Thin** (thin track, overhanging handle) or **Thick**
  (a thick bar that fully encloses the control point, Slider-demo style).
- ✅ **Value steppers** — optional `<` and `>` arrows inside each value field
  step the value by one unit (`SetShowValueSpinners`).
- ✅ **Collapsible sliders** — optionally hide the four sliders behind a
  disclosure (dropdown-icon) row; configurable whether they start expanded or
  collapsed (`SetSlidersCollapsible`).
- ✅ **Alpha channel** — checkerboard-backed alpha slider with rounded ends (the
  checkerboard is clipped to the rounded track so it matches the slider shape).
- ✅ **UI scaling** — `SetUIScale(0.6f)` renders the whole picker at 60% (all
  metrics and fonts), for embedding a smaller variant.
- ✅ **Compact mode** — hide the wheel/swatches for an inline sliders-only editor.
- ✅ **Callbacks** — `onColorChanging` (continuous) and `onColorChanged` (final).

## Class Definition

```cpp
namespace UltraCanvas {
    enum class ColorPickerModel        { HSV, HSL, RGB };
    enum class ColorPickerModeSelector { TabBar, Dropdown };  // mutually exclusive
    enum class ColorPickerSliderStyle  { Thin, Thick };
    enum class ColorPickerWheelStyle   { Ring, Bar };

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

        // Presentation choices
        void SetModeSelector(ColorPickerModeSelector s); // tab bar OR dropdown
        void SetSliderStyle(ColorPickerSliderStyle s);   // thin / thick bars
        void SetWheelStyle(ColorPickerWheelStyle s);     // hue ring / hue bar
        void SetShowValueSpinners(bool show);            // < > arrows in value box
        void SetSlidersCollapsible(bool collapsible, bool expanded = true);
        void SetSlidersExpanded(bool expanded);
        void SetUIScale(float scale);                    // 0.6 = 60% sized picker

        void SetShowColorWheel(bool show);               // compact mode when false
        void SetShowAlpha(bool show);

        // Foreground = current swatch, Background = previous swatch.
        Color GetForegroundColor() const;
        void  SetForegroundColor(const Color& c, bool notify = true);
        Color GetBackgroundColor() const;
        void  SetBackgroundColor(const Color& c);

        // Built-in eyedropper control. `foreground` selects the target swatch
        // (true = foreground, false = background) for the preview and sample.
        bool IsScreenPickActive() const;
        void StartScreenPick(bool foreground = true);
        void CancelScreenPick();

        std::function<void(const Color&)> onColorChanged;
        std::function<void(const Color&)> onColorChanging;
        std::function<void(bool foreground)> onScreenColorPick; // host override
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

### Presentation variants

```cpp
// Dropdown model selector instead of the tab bar (never both):
picker->SetModeSelector(UltraCanvas::ColorPickerModeSelector::Dropdown);

// Thick channel bars that enclose the control point (Slider-demo style):
picker->SetSliderStyle(UltraCanvas::ColorPickerSliderStyle::Thick);

// Replace the hue ring with a maximised SV rectangle + hue bar underneath:
picker->SetWheelStyle(UltraCanvas::ColorPickerWheelStyle::Bar);

// < and > stepper arrows inside the numeric value fields:
picker->SetShowValueSpinners(true);

// Hide the four sliders behind a disclosure icon, collapsed by default:
picker->SetSlidersCollapsible(true, false);

// A 60% sized picker (pair the scale with a smaller widget size):
auto small = UltraCanvas::CreateColorPicker("small", color, 20, 20, 174, 282);
small->SetUIScale(0.6f);
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
| Drag on the hue ring / hue bar | Sets hue |
| Drag inside the SV area | Sets saturation & value |
| Hover a swatch | Floods the whole widget surface with that colour for a big-area preview |
| Click background (previous) swatch | Reverts to the background colour |
| Click the corner swap symbol | Swaps foreground and background colours |
| Click the eyedropper button | Arms picking: pointer becomes an eyedropper cursor |
| …then left (Select) click | Samples the pixel under the pointer into the foreground colour |
| …then right (Adjust) click | Samples the pixel under the pointer into the background colour |
| …Esc | Cancels picking |
| Click hex / value box | Begins inline editing with the caret at the click position |
| Drag in an edited field | Selects text; double-click selects all |
| Arrows / Home / End (+Shift) | Move the caret / extend the selection |
| Ctrl+A / Ctrl+C / Ctrl+X / Ctrl+V | Select all / copy / cut / paste (Ctrl+Insert copy, Shift+Insert paste, Ctrl+Delete cut) |
| Tab / Shift+Tab while editing | Commits and moves to the next / previous field |
| Enter / Esc while editing | Commits / cancels the edit |
| Click `<` / `>` in a value box | Steps the value by one unit (when spinners are enabled) |
| Click a tab or the dropdown | Switches HSV / HSL / RGB channel editors |
| Click the disclosure arrow | Shows/hides the channel sliders (when collapsible) |
| Drag a channel/alpha slider | Adjusts that component |

## Screen colour picker (eyedropper)

Without a host callback the picker runs its **built-in mode**: clicking the
eyedropper button changes the mouse pointer to an eyedropper cursor
(`media/lib/cursor/color-picker.png`, crosshair fallback) and installs a
window-level event filter. The next click anywhere in the window samples the
rendered pixel under the pointer via `UltraCanvasWindowBase::GetPixelColor()`:
the **left (Select)** button writes it into the foreground colour, the
**right (Adjust)** button into the background colour. Escape cancels.

Hosts that can sample the whole screen (outside the application window) can
take over by setting `onScreenColorPick`; the button then only raises the
callback and the host writes the pixel back:

```cpp
picker->onScreenColorPick = [picker](bool foreground) {
    Color pixel = SampleScreenPixelUnderCursor();   // host-provided
    if (foreground) picker->SetForegroundColor(pixel);
    else            picker->SetBackgroundColor(pixel);
};
```
