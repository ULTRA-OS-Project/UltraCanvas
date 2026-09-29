- **Menu: the checkbox / radio indicator sits level with its label.** It was
  drawn one pixel above the row's centre line while the label was drawn on
  it, so the box floated above the text. The outline now also sits on whole
  pixels instead of being smeared over two, and the radio dot is a filled
  disc rather than a stroked ring; the mark greys with a disabled item.
- **Menu: a Radio item's outline is a circle.** It was the same square box a
  Checkbox item gets, so the two item kinds could not be told apart until one
  was checked. `MenuStyle::radioShape` chooses: `MenuRadioShape::Round` (the
  default, as `UltraCanvasRadio` draws it) or `MenuRadioShape::Square`, the
  old look. UltraMail's "Show emails" filter menu uses the default and so
  gets the circle.
