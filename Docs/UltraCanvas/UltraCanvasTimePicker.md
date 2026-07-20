# UltraCanvas Time Picker Documentation

## Overview

**UltraCanvasTimePicker** is the time-of-day sibling of `UltraCanvasDatePicker`.
It presents a compact combobox-style field that shows the selected time as
editable text and pops up either a small panel of **hour / minute (/ second)
spinners** plus an optional **AM/PM** selector, or a **circular clock-face
dial** (`TimePickerPopupStyle::Clock`, Windows-10 style). The spinner popup
fields are built from `UltraCanvasSpinner`, so hour/minute selection reuses the
same arrow-driven control.

**File Location**: `include/UltraCanvasTimePicker.h`
**Version**: 1.1.0
**Author**: UltraCanvas Framework

## Features

- ✅ **12- and 24-hour formats** (with AM/PM selector in 12-hour mode)
- ✅ **Optional seconds** field
- ✅ **Two popup styles**: spinner panel or circular **clock-face dial**
- ✅ **Configurable minute / second step** (e.g. 5- or 15-minute increments)
- ✅ **Min / max constraints** — values are clamped to an allowed window
- ✅ **Four input paths**: popup, direct typing, mouse wheel, keyboard
- ✅ **In-field caret editing** — click to place the cursor, `Left`/`Right`/
  `Home`/`End`/`Del`/`Backspace` edit at the caret
- ✅ **"Now" / "Clear"** shortcuts in the popup footer
- ✅ Custom **format string** override, placeholder text, full styling
- ✅ Popup auto-positions above/below the field to stay on-screen

## The `UCTime` value type

A lightweight, timezone-free time-of-day value (sibling of `UCDate`):

```cpp
struct UCTime {
    int hour = 0;     // 0–23
    int minute = 0;   // 0–59
    int second = 0;   // 0–59
    bool present = false;   // false = empty/unset

    UCTime();                       // empty
    UCTime(int h, int m, int s = 0);

    bool IsEmpty() const;
    bool IsValid() const;
    int  ToSecondOfDay() const;
    static UCTime FromSecondOfDay(int s);   // wraps within a day
    UCTime AddMinutes(int n) const;
    UCTime AddSeconds(int n) const;
    int  Hour12() const;            // 1–12
    bool IsPM() const;
    static UCTime Now();

    std::string Format(const std::string& fmt) const;
    static bool Parse(const std::string& text, bool is24h, bool hasSeconds, UCTime& out);
    // relational operators compare by ToSecondOfDay()
};
```

### Format tokens (`UCTime::Format`)

| Token | Meaning | Example (14:05:09) |
|-------|---------|--------------------|
| `HH` / `H` | 24-hour, padded / unpadded | `14` / `14` |
| `hh` / `h` | 12-hour, padded / unpadded | `02` / `2` |
| `mm` / `m` | minute | `05` / `5` |
| `ss` / `s` | second | `09` / `9` |
| `tt` / `t` | AM/PM · A/P | `PM` / `P` |

Anything else is copied literally (e.g. the `:` in `HH:mm`).

## Quick Start

```cpp
#include "UltraCanvasTimePicker.h"
using namespace UltraCanvas;

// 24-hour HH:mm
auto tp = CreateTimePicker24h("start", 20, 20, 160, 28);
tp->SetTime(UCTime(9, 30));
tp->onTimeChanged = [](const UCTime& t) {
    if (!t.IsEmpty()) printf("%s\n", t.Format("HH:mm").c_str());
};

// 12-hour with AM/PM
auto tp12 = CreateTimePicker12h("meeting", 20, 60, 180, 28);

// With seconds + 15-minute step + business-hours constraint
auto tp2 = CreateTimePicker24h("slot", 20, 100, 190, 28, /*showSeconds*/ true);
tp2->SetMinuteStep(15);
tp2->SetMinTime(UCTime(9, 0));
tp2->SetMaxTime(UCTime(17, 0));

// Circular clock-face dial popup
auto tpc = CreateClockTimePicker("alarm", 20, 140, 190, 28, /*use24h*/ true);
// ...or switch any picker: tp->SetPopupStyle(TimePickerPopupStyle::Clock);
```

## Factory Functions

| Function | Result |
|----------|--------|
| `CreateTimePicker(id, x, y, w, h)` | Default (24-hour) picker |
| `CreateTimePicker24h(id, x, y, w, h, showSeconds)` | 24-hour picker |
| `CreateTimePicker12h(id, x, y, w, h, showSeconds)` | 12-hour AM/PM picker |
| `CreateClockTimePicker(id, x, y, w, h, use24h, showSeconds)` | Picker with the clock-dial popup |

## Key API

```cpp
// Value
void   SetTime(const UCTime& t, bool runCallbacks = false);
UCTime GetTime() const;
void   Clear(bool runCallbacks = false);
void   SetNow(bool runCallbacks = true);

// Config
void SetUse24HourFormat(bool);
void SetShowSeconds(bool);
void SetMinuteStep(int);
void SetSecondStep(int);
void SetTimeFormat(const std::string& fmt);   // overrides the derived format
void SetPlaceholder(const std::string&);
void SetAllowTextInput(bool);
void SetMinTime(const UCTime&);
void SetMaxTime(const UCTime&);

// Popup
void SetPopupStyle(TimePickerPopupStyle);  // Spinners (default) or Clock
void OpenPopup();
void ClosePopup();
bool IsPopupOpen() const;

// Style
void SetStyle(const TimePickerStyle&);

// Callbacks
std::function<void(const UCTime&)> onTimeChanged;
std::function<void()> onPopupOpened;
std::function<void()> onPopupClosed;
```

## The clock-face dial (`TimePickerPopupStyle::Clock`)

`SetPopupStyle(TimePickerPopupStyle::Clock)` (or `CreateClockTimePicker`)
replaces the spinner panel with `UltraCanvasTimeClockView`, a round dial:

- The **header** shows the pending time (`--:--` while empty). Click a
  component (hour / minute / second, or **AM/PM** in 12-hour mode) to make it
  active on the face below.
- The **hour face** in 24-hour mode has two rings — outer `00, 13–23`, inner
  `12, 01–11`; 12-hour mode shows a single `12, 1–11` ring.
- The **minute/second face** is labelled every 5 and snaps to the configured
  minute/second step; click or drag to select, and the mouse wheel nudges the
  active component.
- Picking an hour advances to minutes (then seconds if enabled); picking the
  last component closes the popup. The `Now` / `Clear` footer matches the
  spinner popup.
- Colors come from the `clock*` fields of `TimePickerStyle`
  (header, face, numbers, selection bubble, hover).

`UltraCanvasTimeClockView` is a regular element with `onTimeChanged` /
`onAccepted` callbacks, so it can also be embedded outside the picker.

## Keyboard & Mouse

| Input | Action |
|-------|--------|
| Click clock button / `Up` / `Down` | Open the popup (spinners or clock dial) |
| Spinner arrows / wheel / keys | Adjust hour, minute, second, AM/PM |
| Type into the field | Enter a time (e.g. `9:30`, `2:15 pm`) |
| Click inside the text | Place the caret at the clicked character |
| `Left` / `Right` / `Home` / `End` | Move the caret while editing |
| `Backspace` / `Del` | Delete before / at the caret |
| `Enter` | Commit typed text |
| `Esc` | Close popup / cancel edit |
| Mouse wheel over field | Nudge the time by the minute step |

## Notes

- In 12-hour mode the spinner popup shows an hour spinner (1–12) plus an
  **AM/PM** list spinner; the derived format is `hh:mm tt`.
- Changing `SetUse24HourFormat`/`SetShowSeconds` while the popup is open closes it
  so it is rebuilt with the new field set on the next open.
- Constraints are applied whenever the value changes (popup, typing or wheel);
  if a constraint moves the value, the popup is re-synced to match.
- `UCTime` is timezone-free wall-clock time; `UCTime::Now()` reads local time.
