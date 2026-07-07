# UltraCanvas Stepper Documentation

## Overview

**UltraCanvasStepper** is a progress / wizard control: a sequence of numbered or
iconed steps that shows how far the user is through a multi-step flow
(Step 1 → 2 → 3), with **completed / active / upcoming / error / disabled**
states and connector lines between markers.

This is the progress-indicator "Stepper" (like Material UI's Stepper or Ant
Design's Steps). It is **distinct from the value −/+ stepper**, which ships as
`CreateStepper()` on `UltraCanvasSpinner`.

**File Location**: `include/UltraCanvasStepper.h`
**Version**: 1.0.0
**Author**: UltraCanvas Framework

## Design choices (all configurable)

| Choice | Options |
|--------|---------|
| **Orientation** | `Horizontal` (labels below) · `Vertical` (labels beside) |
| **Labels** | Title (+ optional description), or marker-only (`SetShowLabels(false)`) |
| **Marker style** | `NumberedCircle` · `IconCheck` · `Dot` · `Custom` (SVG/image per step) |
| **Navigation** | `Linear` (go back to done steps) · `NonLinear` (click any) · `Display` (read-only) |

## States

Step state is derived from the current step, with per-step overrides:

- **Completed** — index `< currentStep` (marker shows a check by default)
- **Active** — index `== currentStep`
- **Upcoming** — index `> currentStep`
- **Error** — `SetStepError(i, true)` (red marker, `!` glyph)
- **Disabled** — `SetStepDisabled(i, true)` (greyed, not clickable)

## Quick Start

```cpp
#include "UltraCanvasStepper.h"
using namespace UltraCanvas;

// Horizontal numbered wizard
auto wiz = CreateStepperWizard("checkout", 20, 20, 620, 70,
                               {"Account", "Profile", "Payment", "Review", "Done"});
wiz->onStepChanged = [](int i) { showPage(i); };

// Drive it from your Next / Back buttons
nextBtn->onClick = [wiz]{ wiz->NextStep(); };   // advances, marking prior steps completed
backBtn->onClick = [wiz]{ wiz->PrevStep(); };

// Flag a failed step
wiz->SetStepError(2, true);

// Vertical, with descriptions
auto v = CreateVerticalStepper("setup", 20, 20, 320, 300);
v->SetSteps({ {"Select campaign", "Choose target"},
              {"Create ad group", "Pick keywords"},
              {"Launch", "Review & publish"} });
```

## Factory Functions

| Function | Result |
|----------|--------|
| `CreateStepper(id, x, y, w, h)` | Empty horizontal stepper |
| `CreateStepperWizard(id, x, y, w, h, {titles...})` | Horizontal, pre-filled with titles |
| `CreateVerticalStepper(id, x, y, w, h)` | Vertical stepper |

## Custom (SVG) markers

```cpp
stepper->SetMarkerStyle(StepMarkerStyle::Custom);
stepper->SetStepIcon(0, "media/icons/cart.svg");
stepper->SetStepIcon(1, "media/icons/ship.svg");
```

Icons load via `UCImage::Get` (so SVG renders as crisp vectors). The step's
state is conveyed by an accent ring around the icon plus the connector and label
colours.

## Key API

```cpp
// Steps
int  AddStep(const std::string& title, const std::string& description = "");
void SetSteps(const std::vector<StepItem>& steps);
void ClearSteps();
int  GetStepCount() const;
StepItem* GetStep(int index);
void SetStepError(int index, bool error);
void SetStepDisabled(int index, bool disabled);
void SetStepIcon(int index, const std::string& iconPath);
StepState GetStepState(int index) const;

// Current step
void SetCurrentStep(int index, bool runCallback = false);
int  GetCurrentStep() const;
bool NextStep(bool runCallback = true);   // to next enabled step
bool PrevStep(bool runCallback = true);   // to previous enabled step
bool IsComplete() const;

// Design choices
void SetOrientation(StepperOrientation);
void SetMarkerStyle(StepMarkerStyle);
void SetNavigation(StepperNavigation);
void SetShowLabels(bool);
StepperStyle& GetStyle();

// Callbacks
std::function<void(int)> onStepChanged;   // new current step
std::function<void(int)> onStepClicked;   // a marker/label was clicked
```

## Keyboard

| Key | Action |
|-----|--------|
| `Left` / `Up`   | Previous enabled step |
| `Right` / `Down`| Next enabled step |
| `Home`          | First step |
| `End`           | Last step |

(Keys are ignored in `Display` navigation mode.)

## Notes

- In `Linear` mode a step is clickable only when its index is `<= currentStep`
  (you can go back to a completed step; advance with `NextStep()`), while
  `NonLinear` makes every enabled step clickable.
- Advancing the current step automatically marks earlier steps *completed* — the
  completed state is derived from the index, so you don't track it separately.
- With marker-only mode (`SetShowLabels(false)`) plus `Dot` markers you get a
  compact progress indicator.
