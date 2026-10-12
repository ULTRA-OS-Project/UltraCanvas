# UltraCanvas Checkbox and Radio Button Documentation

## Overview

This page documents the framework's two choice controls and the helper that
makes radio buttons exclusive:

- **`UltraCanvasCheckbox`**: a box with a label that is ticked and cleared
  independently of any other control. It can also show a third,
  *indeterminate* state (a dash), for a "select all" box whose items are only
  partly selected.
- **`UltraCanvasRadio`**: a round radio button with a label.
- **`UltraCanvasRadioGroup`**: a plain object (not a UI element) that makes a
  set of radios exclusive, so that checking one clears the others, and
  reports which one is selected.

Both controls derive from `UltraCanvasLabeledToggleBase`, which holds the
label, the checked state, the callbacks, mouse and keyboard handling and
content sizing. The on/off **switch** is a third class on the same base,
`UltraCanvasSwitch`, documented in [UltraCanvasSwitch.md](UltraCanvasSwitch.md).
The checkbox no longer has a switch or radio style: those are the separate
classes.

**Version:** 2.1.0
**Last Modified:** 2026-10-07
**Author:** UltraCanvas Framework
**Headers:** `include/UltraCanvasCheckbox.h`, `include/UltraCanvasRadio.h`, `include/UltraCanvasLabeledToggleBase.h`
**Implementation:** `core/UltraCanvasCheckbox.cpp`, `core/UltraCanvasRadio.cpp`, `core/UltraCanvasLabeledToggleBase.cpp`
**Demo:** `Apps/DemoApp/UltraCanvasCheckboxExamples.cpp`

## Features

- **Checkbox states**: Unchecked, Checked and (optionally) Indeterminate, with
  a click cycling through them
- **Three box shapes**: square (`Standard`) or rounded corners (`Rounded`,
  `Material`)
- **Radio buttons** with a ring and a centre dot; clicking the selected radio
  changes nothing
- **Exclusive selection** through `UltraCanvasRadioGroup`, with a single
  `onSelectionChanged` notification per change
- **Callbacks** for every state change, fired for clicks and for changes made
  in code alike
- **Content sizing**: the natural size is the indicator plus the label
- **Styling**: colours for every state, indicator size, border, corner radius,
  label font and focus ring
- **Disabled look**: a disabled control greys its indicator, mark and label
  and ignores input

## Which control to use

| Need | Use |
|---|---|
| An option that is on or off, independent of the others | `UltraCanvasCheckbox` |
| A parent box over a list of items, partly selected | `UltraCanvasCheckbox` with the Indeterminate state |
| One choice out of a few, all visible at once | `UltraCanvasRadio` in an `UltraCanvasRadioGroup` |
| A setting that takes effect immediately (on/off) | `UltraCanvasSwitch` |
| One choice out of many, or a list that changes | a dropdown (`UltraCanvasDropdown`) |

## Class Hierarchy

```cpp
namespace UltraCanvas {
    class UltraCanvasLabeledToggleBase : public UltraCanvasUIElement { /* label, state, callbacks */ };
    class UltraCanvasCheckbox : public UltraCanvasLabeledToggleBase { /* box, checkmark, dash */ };
    class UltraCanvasRadio : public UltraCanvasLabeledToggleBase { /* ring, dot */ };
    class UltraCanvasRadioGroup { /* not an element */ };
}
```

## Shared Toggle API

Everything in this section is inherited by both the checkbox and the radio
(and the switch).

### CheckedState

```cpp
enum class CheckedState {
    Unchecked,
    Checked,
    Indeterminate  // Only checkbox uses this; radio/switch clamp to Unchecked.
};
```

### State and label

```cpp
class UltraCanvasLabeledToggleBase : public UltraCanvasUIElement {
public:
    void SetChecked(bool checked);                 // Checked or Unchecked
    bool IsChecked() const;                        // true only for Checked (not Indeterminate)

    virtual void SetCheckState(CheckedState state);
    CheckedState GetCheckState() const;

    virtual void Toggle();                         // Checked <-> Unchecked (the checkbox overrides it)

    void SetText(const std::string& labelText);    // the label beside the indicator
    std::string GetText() const;
};
```

`SetChecked` is `SetCheckState(Checked)` or `SetCheckState(Unchecked)`.
Setting the state the control already has does nothing: no callback, no
redraw.

### Callbacks

```cpp
std::function<void(CheckedState oldState, CheckedState newState)> onStateChanged;
std::function<void()> onChecked;
std::function<void()> onUnchecked;
std::function<void()> onIndeterminate;
```

On every real change, `onStateChanged` runs first, then the one of
`onChecked` / `onUnchecked` / `onIndeterminate` that matches the new state.
They fire for a click **and** for a change made in code (`SetChecked`,
`SetCheckState`, `Toggle`, a group's `SelectButton`), so code that writes a
stored setting back into a control and also saves the setting from the
callback has to tell the two apart, for instance with a "restoring" flag.

`onStateChanged` is the one to use when the handler needs the new value:

```cpp
auto notify = std::make_shared<UltraCanvasCheckbox>("Notify", 10, 10, 260, 24, "Notify me of new mail");
notify->onStateChanged = [](CheckedState oldState, CheckedState newState) {
    bool enabled = (newState == CheckedState::Checked);
    // ... store `enabled` ...
};
```

On a radio that belongs to an `UltraCanvasRadioGroup`, `onChecked` is the
group's: see [onChecked belongs to the group](#onchecked-belongs-to-the-group).

### Shared visual fields

The label, its font and the focus ring are set through a
`LabeledToggleVisualStyle`, which each control's own visual style carries as
its `base` member (`CheckboxVisualStyle::base`, `RadioVisualStyle::base`):

```cpp
struct LabeledToggleVisualStyle {
    // Text appearance
    Color textColor = Colors::TextDefault;
    Color textHoverColor = Colors::TextDefault;
    Color textDisabledColor = Colors::TextDisabled;

    // Layout
    int textSpacing = 6;  // Space between indicator and label

    // Text styling
    std::string fontFamily = "Arial";
    float fontSize = 12.0f;
    FontWeight fontWeight = FontWeight::Normal;

    // Focus ring
    bool hasFocusRing = true;
    Color focusRingColor = Color(0, 120, 215, 128);
    float focusRingWidth = 2.0f;
};
```

The label is drawn in `textHoverColor` while the pointer is over the control,
so a custom `textColor` usually wants the same `textHoverColor`.

### Sizing and placement

The constructors follow the framework's element conventions:

- **x, y**: when either is positive the control is placed absolutely at
  (x, y); `-1, -1` (or `0, 0`) leaves it in the parent's layout flow.
- **w, h**: a positive value fixes that axis; `-1` or `0` leaves it to the
  content. The content width is 8 px, the indicator, `textSpacing`, the
  label's width and 8 px again (no spacing without a label); the height is
  the taller of the indicator and the label line, plus 8 px.

Measuring the label needs the window's render context. A control that is laid
out before it is in a window (a dialog built and laid out before it is shown)
has nothing to measure against yet, so such dialogs give their toggles an
explicit size, as UltraFiler's and UltraMail's settings dialogs do:

```cpp
auto box = std::make_shared<UltraCanvasCheckbox>("ShowHidden", "Show hidden files");
box->size.width  = CSSLayout::Dimension::Px(320);
box->size.height = CSSLayout::Dimension::Px(26);
```

## UltraCanvasCheckbox

### Constructors and factory

```cpp
class UltraCanvasCheckbox : public UltraCanvasLabeledToggleBase {
public:
    // Placed (positive x, y) or in flow (-1, -1), with a fixed or content size
    UltraCanvasCheckbox(const std::string& identifier,
                        float x, float y, float w, float h,
                        const std::string& labelText = "");

    // In the layout flow, with a fixed size
    UltraCanvasCheckbox(const std::string& identifier,
                        float w, float h,
                        const std::string& labelText = "");

    // In the layout flow, content-sized
    UltraCanvasCheckbox(const std::string& identifier, const std::string& labelText);
    explicit UltraCanvasCheckbox(const std::string& labelText = "");

    static std::shared_ptr<UltraCanvasCheckbox> CreateCheckbox(
            const std::string& identifier,
            float x, float y, float w, float h,
            const std::string& text = "",
            bool checked = false);
};
```

There is no numeric id argument and no free `CreateCheckbox` function: the
factory is the static member. A zero `w` or `h` passed to it leaves that axis
content-sized.

```cpp
// Placed at (20, 20), fixed 200 x 24
auto terms = std::make_shared<UltraCanvasCheckbox>("Terms", 20, 20, 200, 24, "I agree to the terms");

// In a flex column, sized to its label
auto autosave = std::make_shared<UltraCanvasCheckbox>("AutoSave", "Save automatically");

// Factory: placed, content-sized, initially checked
auto wrap = UltraCanvasCheckbox::CreateCheckbox("WordWrap", 20, 60, 0, 0, "Word wrap", true);
```

### The three states

```cpp
class UltraCanvasCheckbox : public UltraCanvasLabeledToggleBase {
public:
    void Toggle() override;                           // what a click does

    void SetAllowIndeterminate(bool allow);           // let a click reach Indeterminate
    bool GetAllowIndeterminate() const;

    void SetIndeterminate(bool indeterminate);        // only while indeterminate is allowed
    bool IsIndeterminate() const;
};
```

A click (or `Toggle()`) moves the box on as follows:

| Current state | Indeterminate not allowed (default) | `SetAllowIndeterminate(true)` |
|---|---|---|
| Unchecked | Checked | Checked |
| Checked | Unchecked | Indeterminate |
| Indeterminate | Unchecked | Unchecked |

Setting the state in code:

- `SetCheckState(CheckedState::Indeterminate)` shows the dash whatever
  `SetAllowIndeterminate` says. This is the usual way for a "select all" box:
  code sets the dash when only some items are selected, and the user's clicks
  still go only between checked and unchecked.
- `SetIndeterminate(true)` sets Indeterminate and `SetIndeterminate(false)`
  sets **Unchecked** (not the state before), and both do nothing unless
  indeterminate is allowed.
- `IsChecked()` is false for an indeterminate box; test
  `GetCheckState()` or `IsIndeterminate()` for the dash.

### Appearance

#### CheckboxStyle

```cpp
enum class CheckboxStyle {
    Standard,   // Default square checkbox
    Rounded,    // Rounded corners
    Material    // Material Design (rounded with material colors)
};
```

`Standard` draws square corners and ignores `cornerRadius`; `Rounded` and
`Material` round the box by `cornerRadius`. `Material` does not change any
colour by itself: set `boxColor` and `checkmarkColor` as well (the demo uses
blue `Color(33, 150, 243, 255)` with a white checkmark).

#### CheckboxVisualStyle

```cpp
struct CheckboxVisualStyle {
    LabeledToggleVisualStyle base;

    // Box appearance
    Color boxColor = Colors::ButtonFace;
    Color boxBorderColor = Colors::ButtonShadow;
    Color boxHoverColor = Colors::SelectionHover;
    Color boxPressedColor = Color(204, 228, 247, 255);
    Color boxDisabledColor = Colors::ControlDisabled;
    Color boxBorderDisabledColor = Colors::ControlDisabledBorder;

    // Checkmark appearance
    Color checkmarkColor = Colors::TextDefault;
    Color checkmarkHoverColor = Colors::TextDefault;
    Color checkmarkDisabledColor = Colors::TextDisabled;

    // Layout
    float boxSize = 16.0f;
    float borderWidth = 1.0f;
    float cornerRadius = 2.0f;
    float checkmarkThickness = 2.0f;
};
```

The box takes `boxHoverColor` under the pointer and `boxPressedColor` while
the mouse button is held on it. A disabled box uses the two disabled colours,
which are lighter than the normal ones, so a control that cannot be used
recedes. `checkmarkThickness` is the line width of the tick and the height of
the indeterminate dash.

#### Appearance methods

```cpp
class UltraCanvasCheckbox : public UltraCanvasLabeledToggleBase {
public:
    void SetStyle(CheckboxStyle newStyle);
    CheckboxStyle GetStyle() const;

    void SetVisualStyle(const CheckboxVisualStyle& s);
    CheckboxVisualStyle& GetVisualStyle();
    const CheckboxVisualStyle& GetVisualStyle() const;

    void SetBoxSize(float size);
    float GetBoxSize() const;

    // Box fill, tick colour and label colour (base.textColor; not the hover colour)
    void SetColors(const Color& box, const Color& checkmark, const Color& text);
    void SetFont(const std::string& family, float size, FontWeight weight = FontWeight::Normal);
    void SetFontSize(float size);
};
```

`SetVisualStyle`, `SetStyle`, `SetBoxSize`, `SetFont` and `SetFontSize`
re-measure and redraw the control. Writing through the reference that
`GetVisualStyle()` returns does neither, which is fine before the control is
first laid out; on a control that is already shown, copy the style, change
the copy and hand it to `SetVisualStyle`.

## UltraCanvasRadio

A radio draws a ring and, when checked, a dot in it. On its own a radio only
checks itself; put the radios of one question in an
[`UltraCanvasRadioGroup`](#ultracanvasradiogroup) so that checking one clears
the others.

### Constructors and factory

```cpp
class UltraCanvasRadio : public UltraCanvasLabeledToggleBase {
public:
    UltraCanvasRadio(const std::string& identifier,
                     float x, float y, float w, float h,
                     const std::string& labelText = "");
    UltraCanvasRadio(const std::string& identifier,
                     float w, float h,
                     const std::string& labelText = "");
    UltraCanvasRadio(const std::string& identifier, const std::string& labelText);
    explicit UltraCanvasRadio(const std::string& labelText = "");

    // Content-sized; a positive x, y places it, -1, -1 leaves it in the flow
    static std::shared_ptr<UltraCanvasRadio> Create(
            const std::string& identifier,
            float x, float y,
            const std::string& text = "",
            bool checked = false);
};
```

The factory is `UltraCanvasRadio::Create`. There is no `CreateRadioButton`,
and no radio style on the checkbox.

```cpp
auto low  = UltraCanvasRadio::Create("QualityLow", 20, 20, "Low quality");
auto high = UltraCanvasRadio::Create("QualityHigh", 20, 50, "High quality", true);
auto inFlow = std::make_shared<UltraCanvasRadio>("QualityAuto", "Automatic");
```

### Behaviour

- A click checks the radio. Clicking a radio that is already checked changes
  nothing: a radio is cleared by checking another one in its group, or in
  code.
- `SetCheckState(CheckedState::Indeterminate)` is turned into Unchecked;
  radios have only two states.
- `Toggle()`, inherited from the base, *does* clear a checked radio. To select
  a radio in code use `SetChecked(true)` or the group's `SelectButton`.

### RadioVisualStyle

```cpp
struct RadioVisualStyle {
    LabeledToggleVisualStyle base;

    // Outer circle (the ring)
    Color outerColor = Colors::ButtonFace;
    Color outerBorderColor = Colors::ButtonShadow;
    Color outerHoverColor = Colors::SelectionHover;
    Color outerPressedColor = Color(204, 228, 247, 255);
    Color outerDisabledColor = Colors::ControlDisabled;
    Color outerBorderDisabledColor = Colors::ControlDisabledBorder;

    // Inner dot (visible when checked)
    Color innerDotColor = Colors::TextDefault;
    Color innerDotDisabledColor = Colors::TextDisabled;

    // Layout
    float boxSize = 16.0f;       // Diameter of outer circle
    float borderWidth = 1.0f;
    float dotInsetRatio = 0.3f;  // Inner dot radius = (boxSize/2) * (1 - 2*dotInsetRatio)
};
```

```cpp
class UltraCanvasRadio : public UltraCanvasLabeledToggleBase {
public:
    void SetVisualStyle(const RadioVisualStyle& s);
    RadioVisualStyle& GetVisualStyle();
    const RadioVisualStyle& GetVisualStyle() const;

    void SetBoxSize(float size);    // ring diameter
    float GetBoxSize() const;
};
```

The radio has no `SetFont` or `SetColors`: its label font and colours are in
`base`. The settings dialogs style their radios like this:

```cpp
auto radio = UltraCanvasRadio::Create("ViewHtml", -1, -1, "Show HTML", true);
RadioVisualStyle style = radio->GetVisualStyle();
style.base.fontSize       = 13.0f;
style.base.textColor      = Color(30, 30, 30, 255);
style.base.textHoverColor = Color(30, 30, 30, 255);
radio->SetVisualStyle(style);
```

## UltraCanvasRadioGroup

```cpp
class UltraCanvasRadioGroup {
public:
    UltraCanvasRadioGroup() = default;
    ~UltraCanvasRadioGroup();

    void AddRadioButton(std::shared_ptr<UltraCanvasRadio> button);
    void RemoveRadioButton(std::shared_ptr<UltraCanvasRadio> button);
    void SelectButton(std::shared_ptr<UltraCanvasRadio> button);
    std::shared_ptr<UltraCanvasRadio> GetSelectedButton() const;
    void ClearSelection();

    std::function<void(std::shared_ptr<UltraCanvasRadio>)> onSelectionChanged;
};
```

### How the selection works

- **`AddRadioButton`** appends the radio (a null pointer is ignored) and
  installs the group's handler in the radio's `onChecked`. From then on a
  click on the radio, or `SetChecked(true)` on it in code, selects it in the
  group. A radio that is already checked when it is added becomes the
  selection, and any member checked before it is cleared: the last checked
  radio added wins, as in an HTML radio group. `onSelectionChanged` is not
  called for it.
- **`SelectButton`** makes a member the selection: it checks it, clears
  every other member and calls `onSelectionChanged` with it, once. A radio
  that is not in the group, or a null pointer, is ignored; selecting the
  radio that is already selected and checked does nothing and reports
  nothing.
- **`GetSelectedButton`** returns the selected radio, or null.
- **`ClearSelection`** clears every member and calls
  `onSelectionChanged(nullptr)`, so a handler must allow for a null radio.
- **`RemoveRadioButton`** takes the radio out of the group and takes the
  group's handler back from it (see [Lifetime](#lifetime-and-ownership)). If
  it was the selection, the group has none afterwards. The radio keeps its
  checked state, and `onSelectionChanged` is not called.

**The initial selection.** Create the initial choice checked
(`UltraCanvasRadio::Create(..., true)`) and add it: the group adopts it
without reporting a change. Or call `SelectButton` on it after adding the
radios; that one is reported, so assign `onSelectionChanged` after it if
building the dialog should not count as a change.

A group serves one question, and a radio belongs to one group: adding it to a
second group hands its clicks to the second one.

### onChecked belongs to the group

The group listens to its radios through their `onChecked` callback, so on a
grouped radio that slot is taken:

- an `onChecked` assigned **before** `AddRadioButton` is replaced by the
  group's handler;
- an `onChecked` assigned **after** it replaces the group's handler, and the
  radio then no longer clears the others when clicked.

React to the choice with the group's `onSelectionChanged`. A radio's own
`onStateChanged` and `onUnchecked` stay free for the application.

### Lifetime and ownership

The group is **not an element and is not owned by its radios**. The
handler it installs in a radio's `onChecked` refers to the group, and to the
radio, by plain pointers, so a radio keeps neither its group nor itself
alive. The group does hold `shared_ptr`s to its radios: it keeps them alive,
not the other way round. The rules:

- **Keep the group alive for as long as the selection should work**: make it
  a member of the window, dialog or container that holds the radios (a plain
  member, as in UltraFiler, UltraMail and UltraCanvasStart, or a
  `std::shared_ptr` member, as in the demo). A group that is a local variable
  of the function building the page is destroyed when that function returns.
- **If the group goes first**, its destructor takes back the `onChecked`
  handler it installed on each radio. A radio clicked afterwards only checks
  itself: the others are not cleared, and nothing reports the change. This is
  safe (no handler is left pointing at the destroyed group), but the radios
  are no longer exclusive.
- **`RemoveRadioButton` takes the handler back too**, so a removed radio is
  an independent radio again.
- **Only the group's own handler is taken back.** An `onChecked` that the
  application assigned after `AddRadioButton` (which replaced the group's)
  stays when the group goes or the radio is removed.
- **Moving a group hands its radios' clicks to the new object**, and the
  moved-from group is left with no radios. A group can therefore be returned from a
  function or kept in a `std::vector`, which moves its elements when it
  grows. Move-assigning into a group first takes back the handlers of the
  radios it held before.
- **A copy lists the same radios, but their clicks stay with the original.**
  The copy has the same radios, selection and `onSelectionChanged`, but a
  click still reaches only the original group, and once the original is
  destroyed the radios stop being exclusive even though the copy still lists
  them. Hold a group by value in one place, or by reference or pointer, rather
  than copying it. Copy-assigning into a group first takes back the handlers
  of the radios it held before.

The radios themselves are owned like any element, by the container they are
added to (`AddChild`). The group is only a second owner.

## Mouse and Keyboard

- **Mouse**: pressing the button anywhere on the control (indicator or label)
  shows the pressed colour, and releasing it over the control activates it: a
  checkbox moves to its next state, a radio checks itself. Releasing outside
  cancels. A quick second click counts as a click. The indicator takes its
  hover colour, and the label `textHoverColor`, while the pointer is over the
  control.
- **Keyboard**: checkboxes, radios and switches take the keyboard focus like
  buttons: Tab reaches them and a press focuses them. Space activates the
  focused control, and the focus ring (`base.hasFocusRing`, a rectangle
  around the box or a circle around the ring) is drawn while it has the
  focus. Enter is left to the window, so in a dialog it still presses the
  default button. `SetAcceptsFocus(false)` keeps a control out of the Tab
  order.
- **Disabled or hidden**: a control for which `SetDisabled(true)` was called,
  or that is not visible, ignores all input. A disabled control still
  accepts `SetChecked` from code and draws its state in the disabled colours.

## Usage Examples

### Basic checkbox enabling a button

```cpp
auto agree = std::make_shared<UltraCanvasCheckbox>("AgreeTerms", 20, 20, 240, 24, "I agree to the terms");
auto okButton = CreateButton("OkButton", 20, 60, 100, 30, "Continue");
okButton->SetDisabled(true);

agree->onStateChanged = [okButton](CheckedState, CheckedState now) {
    okButton->SetDisabled(now != CheckedState::Checked);
};

container->AddChild(agree);
container->AddChild(okButton);
```

### "Select all" with an indeterminate parent

The parent shows a dash while only some items are checked. Code sets the
dash; the parent's own clicks only check or clear everything, because
`SetAllowIndeterminate` is left off. A flag stops the two directions from
feeding each other.

```cpp
// Raw pointers: the boxes' callbacks refer to each other, and shared_ptrs
// both ways would keep every box alive for good. The container owns them.
std::vector<UltraCanvasCheckbox*> rows;
for (int i = 0; i < 3; ++i) {
    auto row = std::make_shared<UltraCanvasCheckbox>("Item" + std::to_string(i),
            50, 50 + 26.0f * i, 220, 24, "Item " + std::to_string(i + 1));
    container->AddChild(row);
    rows.push_back(row.get());
}

auto selectAll = std::make_shared<UltraCanvasCheckbox>("SelectAll", 20, 20, 240, 24, "Select all");
container->AddChild(selectAll);
auto syncing = std::make_shared<bool>(false);

// Parent to items: a click on "Select all" checks or clears every item.
selectAll->onStateChanged = [rows, syncing](CheckedState, CheckedState now) {
    if (*syncing || now == CheckedState::Indeterminate) return;
    *syncing = true;
    for (auto* row : rows) row->SetChecked(now == CheckedState::Checked);
    *syncing = false;
};

// Items to parent: all, none, or some.
for (auto* row : rows) {
    row->onStateChanged = [rows, syncing, parent = selectAll.get()](CheckedState, CheckedState) {
        if (*syncing) return;
        size_t checked = 0;
        for (auto* r : rows) checked += r->IsChecked() ? 1 : 0;
        *syncing = true;
        parent->SetCheckState(checked == 0 ? CheckedState::Unchecked
                            : checked == rows.size() ? CheckedState::Checked
                            : CheckedState::Indeterminate);
        *syncing = false;
    };
}
```

A click on the parent while it shows the dash clears it, and with it every
item (the table in [The three states](#the-three-states)).

### Tri-state cycling

With indeterminate allowed, the user's clicks go through all three states,
for a "yes / no / don't change" setting:

```cpp
auto bold = std::make_shared<UltraCanvasCheckbox>("ApplyBold", 20, 20, 220, 24, "Bold");
bold->SetAllowIndeterminate(true);
bold->SetIndeterminate(true);            // start at "leave as it is"
bold->onIndeterminate = []() { /* keep each selection's own weight */ };
bold->onChecked = []() { /* make everything bold */ };
bold->onUnchecked = []() { /* make everything regular */ };
```

### Custom styling

```cpp
auto styled = std::make_shared<UltraCanvasCheckbox>("Styled", 20, 20, 240, 32, "Custom styled");
styled->SetStyle(CheckboxStyle::Rounded);

CheckboxVisualStyle style = styled->GetVisualStyle();
style.boxSize = 20.0f;
style.cornerRadius = 4.0f;
style.boxColor = Color(240, 240, 255, 255);
style.boxBorderColor = Color(100, 100, 200, 255);
style.checkmarkColor = Color(0, 150, 0, 255);
style.base.fontSize = 14.0f;
style.base.fontWeight = FontWeight::Bold;
styled->SetVisualStyle(style);

// The shortcuts for the common cases
auto large = std::make_shared<UltraCanvasCheckbox>("Large", 20, 60, 260, 40, "Large");
large->SetBoxSize(28.0f);
large->SetFont("Arial", 16.0f, FontWeight::Bold);
large->SetColors(Color(255, 240, 240, 255),   // box
                 Color(200, 0, 0, 255),       // tick
                 Color(60, 60, 60, 255));     // label
```

### A radio group in a dialog class

The group is a member of the dialog, so it lives exactly as long as the
radios it serves. This is the pattern of the UltraFiler, UltraMail and
UltraCanvasStart settings pages.

```cpp
class ExportDialog {
public:
    void Build(UltraCanvasContainer* body) {
        pngRadio  = UltraCanvasRadio::Create("ExportPng", -1, -1, "PNG");
        jpegRadio = UltraCanvasRadio::Create("ExportJpeg", -1, -1, "JPEG");
        svgRadio  = UltraCanvasRadio::Create("ExportSvg", -1, -1, "SVG");

        formatGroup.AddRadioButton(pngRadio);
        formatGroup.AddRadioButton(jpegRadio);
        formatGroup.AddRadioButton(svgRadio);
        formatGroup.SelectButton(pngRadio);    // the initial choice

        // Assigned after the initial choice, so building is not a change.
        // `this` is captured raw: the dialog owns the group.
        formatGroup.onSelectionChanged = [this](std::shared_ptr<UltraCanvasRadio> selected) {
            if (!selected) return;             // ClearSelection reports nullptr
            format = selected == svgRadio  ? "svg"
                   : selected == jpegRadio ? "jpeg"
                                           : "png";
        };

        body->AddChild(pngRadio);
        body->AddChild(jpegRadio);
        body->AddChild(svgRadio);
    }

    // Shows a stored choice. SelectButton reports it through
    // onSelectionChanged unless it already was the selection.
    void Show(const std::string& stored) {
        formatGroup.SelectButton(stored == "svg"  ? svgRadio
                               : stored == "jpeg" ? jpegRadio
                                                  : pngRadio);
    }

private:
    std::shared_ptr<UltraCanvasRadio> pngRadio, jpegRadio, svgRadio;
    UltraCanvasRadioGroup formatGroup;         // as long-lived as the dialog
    std::string format = "png";
};
```

### A radio group on a page built by a function

When a function builds the page and returns it, the page must own the group.
The demo does this with a small container subclass:

```cpp
class ThemePage : public UltraCanvasContainer {
public:
    ThemePage() : UltraCanvasContainer("ThemePage", 0, 0, 400, 120) {}
    UltraCanvasRadioGroup themeGroup;   // lives as long as the page and its radios
};

std::shared_ptr<UltraCanvasContainer> BuildThemePage() {
    auto page = std::make_shared<ThemePage>();
    auto light = UltraCanvasRadio::Create("ThemeLight", 20, 20, "Light", true);
    auto dark  = UltraCanvasRadio::Create("ThemeDark", 20, 50, "Dark");
    auto status = CreateLabel("ThemeStatus", 200, 20, 180, 24, "Theme: Light");

    page->themeGroup.AddRadioButton(light);
    page->themeGroup.AddRadioButton(dark);
    page->themeGroup.SelectButton(light);
    page->themeGroup.onSelectionChanged =
            [label = status.get()](std::shared_ptr<UltraCanvasRadio> selected) {
        if (selected) label->SetText("Theme: " + selected->GetText());
    };

    page->AddChild(light);
    page->AddChild(dark);
    page->AddChild(status);
    return page;
}
```

The mistake this avoids, a group that dies with the function that built it:

```cpp
void BuildSizeChoice(UltraCanvasContainer* page) {
    UltraCanvasRadioGroup sizeGroup;                 // destroyed at the closing brace
    auto small = UltraCanvasRadio::Create("SizeSmall", 20, 20, "Small", true);
    auto large = UltraCanvasRadio::Create("SizeLarge", 20, 50, "Large");
    sizeGroup.AddRadioButton(small);
    sizeGroup.AddRadioButton(large);
    page->AddChild(small);
    page->AddChild(large);
}   // the group takes its handlers back here: each radio now only checks itself
```

### Groups in a container

Groups can live in a `std::vector`. When the vector grows it moves them, and
each radio's clicks follow its group to the new place:

```cpp
std::vector<UltraCanvasRadioGroup> questions;
for (int q = 0; q < 3; ++q) {
    questions.emplace_back();     // may move the earlier groups: still fine
    auto yes = UltraCanvasRadio::Create("Q" + std::to_string(q) + "Yes", 20, 20 + 60.0f * q, "Yes");
    auto no  = UltraCanvasRadio::Create("Q" + std::to_string(q) + "No", 120, 20 + 60.0f * q, "No");
    questions.back().AddRadioButton(yes);
    questions.back().AddRadioButton(no);
    container->AddChild(yes);
    container->AddChild(no);
}
```

As above, the vector itself must outlive the radios' use, typically as a
member of the window.

## Rendering Details

Each control draws, in this order:

1. **The indicator**, at 4 px from the left and centred vertically:
   - checkbox: the box, filled and bordered in the colours of the current
     state, with square corners for `Standard` and `cornerRadius` for the
     other two styles; then the tick (three points joined by a line of
     `checkmarkThickness`) when checked, or a horizontal dash when
     indeterminate;
   - radio: the ring, and the dot when checked.
2. **The label**, `textSpacing` px to the right of the indicator, centred
   vertically, in the colour for the current state.
3. **The focus ring**, when the control has the focus and `hasFocusRing` is
   set.

## Best Practices

1. Give every checkbox and radio a label: a click on the label activates the
   control, which makes it a much larger target than the 16 px indicator.
2. Make the group a member of whatever owns the radios, never a local
   variable of a builder function.
3. Create the initial radio checked, or select it with `SelectButton` before
   assigning `onSelectionChanged`.
4. Do not assign `onChecked` on a grouped radio; use the group's
   `onSelectionChanged`.
5. A toggle's callback must not hold a `std::shared_ptr` to the toggle
   itself or to a container above it; capture those raw. Two toggles whose
   callbacks refer to each other (the "select all" example) capture each
   other raw too.
6. For a setting that applies immediately, prefer `UltraCanvasSwitch`; a
   checkbox suits options confirmed with OK or Apply.

## See Also

- [UltraCanvasSwitch](UltraCanvasSwitch.md) - the on/off switch, on the same toggle base
- [UltraCanvasGroupBox](UltraCanvasGroupBoxExamples.md) - frames a set of radios or checkboxes, and can be checkable itself
- [UltraCanvasUIElements](UltraCanvasUIElements.md) - which element to use for what
- Demo: `Apps/DemoApp/UltraCanvasCheckboxExamples.cpp`

## Version History

- **2.1.0** (2026-10-07): the toggles take the keyboard focus (Space
  activates, Enter is left to the dialog); a radio added to a group already
  checked becomes its selection.
- **2.0.0** (2026-10-07): Rewritten for the current API. The checkbox is one
  class of three on `UltraCanvasLabeledToggleBase`; the radio
  (`UltraCanvasRadio`, factory `UltraCanvasRadio::Create`) and the switch
  (`UltraCanvasSwitch`) are separate classes, and the checkbox's `Switch` and
  `Radio` styles, `CreateSwitch`, `CreateRadioButton` and `SetAutoSize` are
  gone. Constructors and `CreateCheckbox` take `float` geometry and no numeric
  id. The label, font and focus-ring fields moved to
  `LabeledToggleVisualStyle` (`CheckboxVisualStyle::base`). Added the radio
  visual style, sizing, the click cycle of the three states, and
  `UltraCanvasRadioGroup`: how the selection works, that `onChecked` is the
  group's, and its lifetime rules (it is not owned by its radios; going
  first, removing a radio, moving and copying).
- **1.1.0** (2024-12-19): One checkbox class with Standard, Rounded, Switch,
  Radio and Material styles, `CreateSwitch` and `CreateRadioButton`
  factories, and a radio group of checkboxes.
