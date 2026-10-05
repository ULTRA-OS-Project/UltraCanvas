- **Colour picker: a square, pixel-exact saturation/value area for the Bar
  style.** `UltraCanvasColorPicker::SetSVAreaShape` takes
  `ColorPickerSVAreaShape::Fill` (the stretched rectangle, still the default),
  `Square` (1:1, as large as fits) or `PixelExact` (1:1 at
  `PixelExactSVSide` = 256 x 256 pixels). 8-bit channels have 256 steps, so
  at 256 pixels each column is one step of saturation and each row one step of
  value: a smaller area skips values, a larger one repeats them. A square area
  is centred with the hue bar at its width, and `PreferredHeightForWidth`
  sizes a collapsible PixelExact picker for its expanded sliders, so opening
  them does not shrink the area below exact.
- **Colour picker: the SV area's first and last pixels now are 0 and 100 %.**
  The pointer mapped column `i` to `i / width`, so the last column inside the
  area gave 99.6 % and the extremes were reachable only by dragging past the
  edge, while the gradient was drawn half a pixel off what a click picked.
  Column `i` now maps to `i / (width - 1)`, the gradients run from the centre
  of the first pixel to the centre of the last, and the marker sits on the
  centre of the picked pixel.
- **DemoApp colour picker page rearranged.** The first row now holds the full
  picker, the collapsible-sliders picker with a 256 x 256 pixel-exact SV
  square, and the hue x lightness field with collapsible sliders; the 60 %
  scaled picker moved to the last row.
