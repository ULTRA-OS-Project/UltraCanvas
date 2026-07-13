# UltraCanvas Spinner / SpinBox Control Documentation

## Overview

The **UltraCanvasSpinner** is the UltraCanvas control for selecting a value with
**up/down (or left/right) arrow buttons**. It is the framework's equivalent of a
*SpinBox* (Qt), *NumericUpDown* (WinForms), *NSStepper* (Cocoa),
*GtkSpinButton* (GTK) or an ARIA `spinbutton`.

A value can be changed four ways:

- Clicking the **increment / decrement arrow buttons**
- The keyboard **arrow keys** (`Up`/`Down`, `Left`/`Right`), plus `PageUp`/`PageDown` and `Home`/`End`
- The **mouse wheel** while hovering the control
- **Typing** a value directly into the editable field (numeric modes)

**File Location**: `include/UltraCanvasSpinner.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Features

### Value Modes (`SpinnerValueType`)
- ✅ **Integer** – whole-number stepping
- ✅ **Decimal** – fixed-precision floating stepping (configurable decimal places)
- ✅ **List** – cycle through an ordered list of arbitrary string values

### Layouts (`SpinnerLayout`)
- ✅ **UpDownRight** – editable field with stacked ▲/▼ buttons on the right (classic spin box)
- ✅ **SidesHorizontal** – `[dec] value [inc]` stepper with buttons flanking the field

### Button Glyphs (`SpinnerButtonGlyph`)
- ✅ **Triangle** – filled triangle pointing up/down or left/right
- ✅ **Chevron** – two-stroke chevron
- ✅ **PlusMinus** – `+` / `−` (ideal for the horizontal stepper)

### Behaviour
- ✅ Min / max range, step, and page-step
- ✅ Optional **wrap-around** at the ends
- ✅ **Prefix / suffix** strings (e.g. `$`, ` s`, `°`)
- ✅ **Custom formatter** callback for arbitrary display text (month names, etc.)
- ✅ Inline **text editing** with commit (`Enter`) / cancel (`Esc`); focusing /
  clicking the field selects the current value so typing replaces it
- ✅ Optional **value dropdown** (`SetDropdownEnabled(true)`) — a combobox-style
  popup of every available value, opened by clicking the field
- ✅ Buttons render disabled at the range limits (unless wrapping)
- ✅ Full state feedback: hover, pressed, focused, disabled
- ✅ Event callbacks: value changed / changing, selection changed, editing finished

## Class Definition

```cpp
namespace UltraCanvas {
    class UltraCanvasSpinner : public UltraCanvasUIElement {
        // Spinner / spin-button component implementation
    };
}
```

## Quick Start

```cpp
#include "UltraCanvasSpinner.h"
using namespace UltraCanvas;

// Integer spinner: range 0–100, start 20, step 5
auto qty = CreateIntSpinner("qty", 20, 20, 120, 28, 0, 100, 20, 5);
qty->onValueChanged = [](double v) { /* use (int)v */ };

// Decimal spinner with a unit suffix
auto dur = CreateDecimalSpinner("dur", 20, 60, 140, 28,
                                0.0, 5.0, 1.5, 0.25, /*decimals*/ 2);
dur->SetSuffix(" s");

// List spinner: cycle arbitrary values (wraps around)
auto size = CreateListSpinner("size", 20, 100, 160, 28,
                              {"Small", "Medium", "Large", "X-Large"});
size->SetWrap(true);
size->onSelectionChanged = [](int idx, const std::string& text) { /* ... */ };

// Horizontal stepper:  [−] value [+]
auto stepper = CreateStepper("count", 20, 140, 140, 28, 1, 99, 1, 1);
```

## Factory Functions

| Function | Result |
|----------|--------|
| `CreateSpinner(id, x, y, w, h)` | Bare spinner (Integer mode, 0–100) |
| `CreateIntSpinner(id, x, y, w, h, min, max, initial, step)` | Integer spinner |
| `CreateDecimalSpinner(id, x, y, w, h, min, max, initial, step, decimals)` | Fixed-precision decimal spinner |
| `CreateListSpinner(id, x, y, w, h, items)` | Value-cycling list spinner |
| `CreateStepper(id, x, y, w, h, min, max, initial, step)` | Horizontal `[−] value [+]` stepper |

## Key API

```cpp
// Value
void   SetRange(double min, double max);
void   SetValue(double value);          // clamps/wraps + snaps to step grid
double GetValue() const;
int    GetIntValue() const;
void   SetStep(double s);
void   SetPageStep(double s);           // PageUp / PageDown amount
void   SetDecimals(int d);              // >0 switches to Decimal mode
void   SetValueType(SpinnerValueType);
void   SetWrap(bool);
void   StepUp(double steps = 1.0);
void   StepDown(double steps = 1.0);

// List mode
void        SetListItems(const std::vector<std::string>& items);
void        AddListItem(const std::string& item);
void        SetSelectedIndex(int index);
int         GetSelectedIndex() const;
std::string GetSelectedText() const;

// Display
void SetPrefix(const std::string&);
void SetSuffix(const std::string&);
void SetPlaceholder(const std::string&);
void SetTextAlignment(TextAlignment);
void SetFormatter(std::function<std::string(double)>);
void SetEditable(bool);

// Value dropdown (optional combobox-style picker)
void SetDropdownEnabled(bool);          // click the field to pick from a popup list
bool IsDropdownEnabled() const;
void OpenValueDropdown();
void CloseValueDropdown();

// Layout / style
void SetLayout(SpinnerLayout);
void SetButtonGlyph(SpinnerButtonGlyph);
void SetButtonThickness(float);
SpinnerVisualStyle& GetStyle();

// Callbacks
std::function<void(double)> onValueChanged;
std::function<void(double)> onValueChanging;
std::function<void(int, const std::string&)> onSelectionChanged; // List mode
std::function<void()> onEditingFinished;
```

## Keyboard Reference

| Key | Action |
|-----|--------|
| `Up` / `Right` | Increment by one step |
| `Down` / `Left` | Decrement by one step |
| `PageUp` / `PageDown` | Step by the page amount |
| `Home` / `End` | Jump to minimum / maximum |
| digits / `.` / `-` | Begin typing a value (editable numeric modes) |
| `Enter` | Commit the typed value |
| `Esc` | Cancel the edit and revert |

## Notes

- In **List** mode `GetValue()` returns the selected index as a `double`; use
  `GetSelectedIndex()` / `GetSelectedText()` for convenience.
- When `SetWrap(true)`, stepping past an end wraps to the opposite end; otherwise
  the value clamps and the corresponding button renders disabled.
- Text editing is available for Integer and Decimal modes when `IsEditable()` is
  true (the default). Set `SetEditable(false)` for arrow-only controls such as a
  month picker driven by a custom formatter. When a field is focused (or clicked)
  its current value is "selected", so the first keystroke replaces it rather than
  appending.
- `SetDropdownEnabled(true)` turns the spinner into a combobox: a single click on
  the field opens a popup listing every available value (the list items in List
  mode, or the `min..max` grid stepped by `step` in numeric modes). Editable
  numeric fields can still be typed into via a **double-click**. Numeric grids
  larger than 512 entries are not listed (the arrows / typing remain available).
