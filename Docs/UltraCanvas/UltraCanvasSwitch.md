# UltraCanvas Switch Component Documentation

## Overview

The **UltraCanvasSwitch** is a toggle switch: a pill-shaped track with a circular
thumb that snaps between the two ends. It supports horizontal and vertical
orientation, optional thumb icons (built-in checkmark or custom images), and
state texts that can be drawn inside the track, next to it, or as two permanent
labels with the switch between them (`Backup [switch] Auto-Save`).

**Version:** 1.3.0
**Last Modified:** 2026-08-04
**Author:** UltraCanvas Framework
**Header:** `include/UltraCanvasSwitch.h`
**Implementation:** `core/UltraCanvasSwitch.cpp`

## Features

- **Two-state toggle**: `Checked` / `Unchecked` (`Indeterminate` is clamped to
  `Unchecked`)
- **Orientation**: horizontal (OFF left, ON right) or vertical (ON top)
- **Thumb icons**: plain circle, built-in checkmark, or custom `UCImage` per
  state, optionally tinted as a mask
- **State texts**: hidden, inside the track, outside the track, or one text on
  each side of the track
- **Main label**: inherited from `UltraCanvasLabeledToggleBase`, drawn after the
  indicator
- **Keyboard**: Space / Enter activate when focused; focus ring follows the
  track shape
- **Content sizing**: the natural size is measured from track + texts + label

## Class Definition

```cpp
namespace UltraCanvas {
    class UltraCanvasSwitch : public UltraCanvasLabeledToggleBase { ... };
}
```

### Inheritance
- **Base class:** `UltraCanvasLabeledToggleBase` (shared state, layout, events,
  main label — also the base of `UltraCanvasCheckbox`'s successor widgets)
- **Namespace:** `UltraCanvas`

## Enumerations

### SwitchOrientation

```cpp
enum class SwitchOrientation {
    Horizontal,   // OFF on the left, ON on the right
    Vertical      // ON at the top, OFF at the bottom
};
```

### SwitchThumbIconStyle

```cpp
enum class SwitchThumbIconStyle {
    Plain,         // Plain circle thumb (default)
    Check,         // Built-in check stroke when ON, plain circle when OFF
    CustomImage    // Uses thumbIconOn / thumbIconOff; null icon falls back to a plain circle
};
```

### SwitchStateLabelPosition

```cpp
enum class SwitchStateLabelPosition {
    Hidden,        // No state text
    InsideTrack,   // Drawn inside the track, on the side opposite the thumb
    OutsideTrack,  // Drawn next to the track, between the track and the main label
    BothSides      // Both texts always visible, one on each side of the track
};
```

`Hidden`, `InsideTrack` and `OutsideTrack` show a single text — `onText` when
the switch is ON, `offText` when it is OFF. `BothSides` shows both at once and
highlights the one matching the current state.

## Visual Style

`SwitchVisualStyle` groups all appearance settings; `visualStyle.base` carries
the shared label/focus/font fields from `LabeledToggleVisualStyle`.

| Field | Default | Purpose |
|---|---|---|
| `trackOffColor` / `trackOnColor` | LightGray / `(76,175,80)` | Track fill per state |
| `trackBorderColor`, `borderWidth` | ButtonShadow, `1.0f` | Track and thumb outline |
| `trackDisabledColor` | `(220,220,220)` | Track fill while disabled |
| `thumbColor`, `thumbBorderColor`, `thumbDisabledColor` | white, ButtonShadow, `(240,240,240)` | Thumb circle |
| `orientation` | `Horizontal` | Long-axis direction |
| `trackWidth` / `trackHeight` | `36` / `18` | Long axis / short axis (swapped when vertical) |
| `trackCornerRadius` | `-1` | `<0` means pill (short axis / 2) |
| `thumbInset` | `2.0f` | Gap between thumb and track edge |
| `thumbIconStyle`, `thumbIconOn/Off`, `thumbIconUseAsMask` | `Plain`, null, false | Thumb icon source |
| `thumbIconOnColor` / `thumbIconOffColor` | green / gray | Check stroke and mask tint |
| `thumbIconStrokeWidth`, `thumbIconInset` | `2.0f`, `3.0f` | Check style geometry |
| `stateLabelPosition` | `Hidden` | Where state text is drawn |
| `onText` / `offText` | `"ON"` / `"OFF"` | The two state texts |
| `onTextColor` / `offTextColor` | white / `(120,120,120)` | Colors for Inside/OutsideTrack |
| `stateLabelFontFamily`, `stateLabelFontSize`, `stateLabelFontWeight` | Arial, `10`, Bold | Inside/OutsideTrack font |
| `stateLabelTrackPadding`, `stateLabelOutsidePadding` | `4.0f`, `6.0f` | Inside/OutsideTrack gaps |

### BothSides fields

Used only when `stateLabelPosition == SwitchStateLabelPosition::BothSides`:

| Field | Default | Purpose |
|---|---|---|
| `sideLabelFontFamily` / `sideLabelFontSize` | Arial / `12.0f` | Side label font |
| `sideLabelActiveWeight` / `sideLabelInactiveWeight` | Normal / Normal | Weight per state (e.g. set the active one to Bold) |
| `sideLabelActiveColor` | `Colors::TextDefault` | Text matching the current state |
| `sideLabelInactiveColor` | `(150,150,150)` | The other text |
| `sideLabelDisabledColor` | `Colors::TextDisabled` | Both texts while disabled |
| `sideLabelPadding` | `8.0f` | Gap between the track and each text |
| `sideLabelClickSelects` | `true` | Clicking a text selects that state; clicking the track toggles |

Both labels are measured at both weights, so switching state never shifts the
track sideways when the active and inactive weights differ.

## API

### Factories

```cpp
static std::shared_ptr<UltraCanvasSwitch> Create(
        const std::string& identifier, float x, float y,
        const std::string& text = "", bool checked = false);

// Switch flanked by two texts (offLabel — track — onLabel), no main label.
static std::shared_ptr<UltraCanvasSwitch> CreateWithSideLabels(
        const std::string& identifier, float x, float y,
        const std::string& offLabel, const std::string& onLabel,
        bool checked = false);
```

### Methods

```cpp
void SetChecked(bool checked);            // from the base class
bool IsChecked() const;
void Toggle();

void SetVisualStyle(const SwitchVisualStyle& style);
SwitchVisualStyle& GetVisualStyle();      // mutable, for one-off tweaks

void SetTrackSize(float width, float height);          // long axis, short axis
void SetOrientation(SwitchOrientation orientation);
void SetThumbIcons(UCImagePtr onIcon, UCImagePtr offIcon, bool useAsMask = false);

void SetStateLabels(const std::string& onText, const std::string& offText,
                    SwitchStateLabelPosition position);
void SetSideLabels(const std::string& offLabel, const std::string& onLabel);
```

`SetSideLabels` puts `offLabel` on the OFF side of the track (left when
horizontal, bottom when vertical) and `onLabel` on the ON side, and switches
`stateLabelPosition` to `BothSides`.

### Callbacks

```cpp
std::function<void(CheckedState oldState, CheckedState newState)> onStateChanged;
std::function<void()> onChecked;
std::function<void()> onUnchecked;
```

## Examples

### Basic switch with a label

```cpp
auto sw = UltraCanvasSwitch::Create("AutoSave", 30, 100, "Auto-Save", true);
sw->onStateChanged = [](CheckedState, CheckedState newState) {
    std::cout << "Auto-Save " << (newState == CheckedState::Checked ? "on" : "off") << std::endl;
};
container->AddChild(sw);
```

### Two texts with the switch between them

```cpp
// Renders as:  Backup  (=O)  Auto-Save
auto mode = UltraCanvasSwitch::CreateWithSideLabels(
        "SaveMode", 30, 140, "Backup", "Auto-Save", /*checked=*/true);
mode->SetTrackSize(44.0f, 22.0f);
mode->onStateChanged = [](CheckedState, CheckedState newState) {
    UseAutoSave(newState == CheckedState::Checked);
};
container->AddChild(mode);
```

The text matching the current state uses `sideLabelActiveColor`, the other
`sideLabelInactiveColor`. Clicking either text selects that side directly;
clicking the track toggles. Set `sideLabelClickSelects = false` to make every
click a plain toggle.

To emphasise the active side with weight as well as color:

```cpp
auto& style = mode->GetVisualStyle();
style.sideLabelActiveWeight = FontWeight::Bold;
style.sideLabelActiveColor = Color(20, 20, 20, 255);
style.sideLabelInactiveColor = Color(150, 150, 150, 255);
style.sideLabelPadding = 10.0f;
```

An existing switch can be converted at any time:

```cpp
auto sw = UltraCanvasSwitch::Create("Mode", 30, 180);
sw->SetSideLabels("Manual", "Automatic");
```

### ON/OFF text inside the track

```cpp
auto power = UltraCanvasSwitch::Create("Power", 30, 220, "Power", true);
power->SetTrackSize(60.0f, 24.0f);
power->SetStateLabels("ON", "OFF", SwitchStateLabelPosition::InsideTrack);
```

### Thumb icons

```cpp
auto wifi = UltraCanvasSwitch::Create("WiFi", 30, 260, "Wi-Fi", true);
wifi->GetVisualStyle().thumbIconStyle = SwitchThumbIconStyle::Check;   // built-in checkmark

auto bt = UltraCanvasSwitch::Create("Bluetooth", 30, 300, "Bluetooth", true);
bt->SetTrackSize(48.0f, 26.0f);
bt->SetThumbIcons(UCImage::Load("media/icons/check.png", false),
                  UCImage::Load("media/icons/x.png", false));
```

### Vertical orientation

```cpp
auto volume = UltraCanvasSwitch::Create("Volume", 30, 340, "Volume", true);
volume->SetOrientation(SwitchOrientation::Vertical);
volume->SetTrackSize(40.0f, 18.0f);   // long axis 40, short axis 18 — ON is at the top
```

## Layout Notes

- Leave `size.width` / `size.height` as `Auto` (what the factories do) so the
  widget content-sizes to track + texts + label; an explicit size wins as usual.
- The hit area covers the whole widget, so the main label and the side labels
  are clickable.
- `BothSides` sizing measures the real texts through the element's render
  context; before a surface exists it falls back to a conservative estimate,
  which is corrected at the first layout pass.

## Related Components

- `UltraCanvasCheckbox` — checkbox / radio styles on the same toggle base
- `UltraCanvasLabeledToggleBase` — shared state, layout and event plumbing

## See Also

- [UltraCanvasCheckbox.md](UltraCanvasCheckbox.md)
- Demo: `Apps/DemoApp/UltraCanvasCheckboxExamples.cpp` (section "Switch Style
  Toggles")
