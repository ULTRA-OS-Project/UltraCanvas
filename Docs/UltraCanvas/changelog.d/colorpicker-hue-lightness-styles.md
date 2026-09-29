- ColorPicker: two new wheel styles that pick colour and intensity together at
  full saturation. `ColorPickerWheelStyle::HueLightnessField` is a single
  field with the hue running top to bottom and the lightness from black
  through the pure colour to white; `HueLightnessSliders` is a colour (hue)
  bar plus an intensity bar from white through the colour to black. The hue
  bar and the new intensity bar share one gradient-bar renderer, whose handle
  now carries the marker outline so it stays visible on white.
- DemoApp: the Colour Picker page gains a row showing both styles, and the
  field with collapsible sliders.
