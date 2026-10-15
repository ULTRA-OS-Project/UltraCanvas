- **The colour picker can offer its designs in a dropdown.**
  `UltraCanvasColorPicker::SetShowWheelStyleSelector(true)` (picker 1.6.0)
  puts a small dropdown in the top-right corner that switches between the
  three designs, each shown by its icon: colour circle (`Ring`), colour
  slider + square (`Bar`) and colour rectangle (`HueLightnessField`). Off by
  default. On the rectangular designs it covers the corner of the colour
  area, so it hides while a drag on the area or a colour bar runs and comes
  back on release; beside the circle it stays. `onWheelStyleChanged` reports
  the user's choice. The icons are `media/icons/colorpicker/{ring,bar,field}.svg`,
  and the DemoApp's full picker shows the selector.
  `ColorPickerDesignSelectorTest` covers it.
- **A dropdown can show the selected item's icon alone.**
  `DropdownStyle::buttonShowsText = false` leaves the text off the button and
  centres the icon beside the arrow; the list still names every item. The
  button also draws an icon now when the item has no text.
