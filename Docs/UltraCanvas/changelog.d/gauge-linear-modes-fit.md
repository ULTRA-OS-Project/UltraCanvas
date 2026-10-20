- **The gauge's bar modes fit their box.** `UltraCanvasGaugeDiagramElement`
  drew five of its six linear modes for the box they wanted: an LED strip was
  44 px tall whatever it was given, MultiPointer kept 80 px empty on the right
  and WithArrow 60, the title and the value line overlapped the bar in a short
  box, and a narrow MultiPointer drew its bar left of the element. Vertical
  LinearBar judged the room for its title on the width (drawing nothing at
  320 x 60 and a dot at 320 x 160) and rounded its ends with half its length,
  which drew past the element. All six now share one layout: the full card
  where there is room, then a tight card, then the card without the mode's
  marks, then the bar alone - nothing outside the element, in either
  orientation, and a clip to the element as the guarantee.
  - LinearSegmented runs vertically (it ignored the orientation).
  - LinearWithArrow's value label sits under its arrow (it was a line lower,
    often outside the element), carries the unit, and stays inside at the
    ends; a narrow vertical one drops its min/max labels before its arrow.
  - MultiPointer's labels stay inside the element, 3 px clear of its edge
    (flush with it, cairo 1.16 painted their last pixel outside), and one
    that would cover another is left out; the tuner's station labels no longer cover the title
    or the value line.
  - The value follows the range: it was clamped only when set, so
    `SetValue(150)` then `SetMaxValue(200)` showed 100, and a range narrowed
    afterwards left the value outside it, drawn past the track. A range,
    pointer or threshold outside min..max sits at the end of the scale.
  - `Tests/GaugeLinearModesTest.cpp`: every mode, both orientations, seven
    sizes from a card to an 8 px strip.
