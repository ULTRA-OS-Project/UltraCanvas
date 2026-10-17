- **The colour picker folds its colour definitions away and can follow its
  content's height** (picker 1.8.0).
  - The design selector offers a fourth design, the **colour + brightness
    sliders** (`HueLightnessSliders`), with its own icon
    (`media/icons/colorpicker/sliders.svg`). The two sliders end short of the
    selector, so all of the colour range stays left of it rather than under
    it, and the selector stays put while they are dragged.
  - A collapsible picker (`SetSlidersCollapsible`) hides the **HSV / HSL /
    RGB tab bar** with the channel sliders behind the disclosure row, which
    now comes first; the tabs used to stay out on their own when the sliders
    folded away. `onSlidersExpandedChanged` reports the user opening or
    folding the section, as `onWheelStyleChanged` reports a design chosen,
    for a host that remembers it (UltraPaint does).
  - `SetAutoHeight(true)` makes the widget take the height its content needs
    and keep it as the content changes - the section opened or closed,
    another design chosen, the alpha row, the UI scale - in the layout too,
    so in a toolbox column what is below the picker moves up when it folds.
    Before, the widget kept the size it was given and the colour + brightness
    sliders left the freed space empty at its bottom. Off by default.
  - The DemoApp shows the colour + brightness sliders in a toolbox column with
    a palette under it. `ColorPickerAutoHeightTest` and
    `ColorPickerDesignSelectorTest` cover it.
