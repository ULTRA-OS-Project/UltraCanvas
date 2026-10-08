- **Screen readers can use the common widgets, and VoiceOver reaches
  UltraCanvas on macOS.**
  - **Widgets describe themselves** (`UltraCanvasAccessibility.h` 1.2): new
    roles `RadioButton`, `Switch`, `ComboBox`, `Slider`, `SpinButton`,
    `ProgressBar`, `Toolbar`, `TabList`, `Tree`, `Group`; and on
    `UltraCanvasUIElement` `GetAccessibleDescription` (the tooltip by
    default), `GetAccessibleToggleState`, `GetAccessibleRange` /
    `SetAccessibleValue`, `GetAccessibleValueText` /
    `SetAccessibleValueText`, `GetAccessibleActionName` /
    `DoAccessibleAction`, `NotifyAccessibility`, and
    `SetAccessibleName` / `SetAccessibleDescription` to name any element
    (an icon button, a field labelled by another element). New events
    `ValueChanged` and `StateChanged`.
  - Implemented by button (press; a toggle button its pressed state; an icon
    button is named by its tooltip), checkbox / radio / switch (checked
    state, toggle / select), label, text input (named by its placeholder,
    its text as value, settable, never in password mode), dropdown (the
    shown item), slider and spinner (value, range, step, settable), busy
    indicator, group box, tabbed container (named after the open tab),
    toolbar, list and tree view, image and menu.
  - **AT-SPI** gains the `Value` and `Action` interfaces, checkable / checked
    / indeterminate / pressed states, descriptions, the new roles, and
    reads a text field's value through `Text`. **UI Automation** gains the
    Invoke, Toggle, SelectionItem, RangeValue and Value patterns, HelpText,
    the new control types and property-changed events.
  - **NSAccessibility bridge** (`OS/MacOS/UltraCanvasMacOSAccessibility`):
    a window's content view hands VoiceOver its elements, one
    `NSAccessibilityElement` per element, with roles, labels, help, values,
    actions, focus, frames, the text attributes VoiceOver reads (UTF-16
    ranges converted) and change notifications. `UltraCanvasMacOSWindow`
    gains `GetContentView()`. Compile-checked by the macOS CI build; not yet
    tried with VoiceOver.
  - Tests: new `WidgetAccessibilityTest` (41 checks, headless);
    `AtspiBridgeTest` now presses a button, ticks a checkbox, sets a slider
    and reads a text field through libatspi.
