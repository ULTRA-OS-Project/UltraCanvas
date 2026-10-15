- **The colour picker's saturation/value marker can be dragged again.**
  `UltraCanvasColorPicker` (1.5.1) hit-tested only the area, while the marker
  is centred on the colour's pixel, so at an edge or corner most of it lay
  outside the area and a press on it did nothing. A press within the marker's
  radius of the area is now taken (one beside the area picks that edge), and
  a press on the marker grabs it where it was hit instead of jumping the
  colour to the pointer; the hue x lightness field does the same. Every drag
  (area, ring, bars, channel sliders) now captures the mouse, as the slider
  does: a drag that left the picker stopped short of the edge, and a release
  outside it was lost, so the drag went on under a pointer with no button
  down. `ColorPickerDragTest` covers it.
