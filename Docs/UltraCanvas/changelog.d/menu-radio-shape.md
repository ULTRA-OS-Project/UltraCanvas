- **Menu: the checkbox / radio indicator sits level with its label.** It was
  drawn one pixel above the row's centre line while the label was drawn on
  it, so the box floated above the text. The outline now also sits on whole
  pixels instead of being smeared over two, and the radio dot is a filled
  disc rather than a stroked ring; the mark greys with a disabled item.
- **Menu: `MenuStyle::radioShape` chooses the outline of a Radio item.**
  `MenuRadioShape::Square` (the default, the same box a Checkbox item gets) or
  `MenuRadioShape::Round`, a circle as `UltraCanvasRadio` draws it.
