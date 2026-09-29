- **Menu: the checkbox / radio indicator sits level with its label.** It was
  drawn one pixel above the row's centre line while the label was drawn on
  it, so the box floated above the text. The label is now centred on the row
  by its cap height rather than its line box, which holds the ascender and
  descender space too and so put a mixed-case label a shade lower than the
  indicator. The radio dot is a filled disc rather than a stroked ring, and
  the mark greys with a disabled item.
- **`DrawFilledRectangle` / `DrawFilledCircle` keep the border inside the
  shape.** A stroke is centred on its path, so a 1px outline on a rectangle
  with whole-pixel edges was smeared over two rows of pixels on each side
  and read as a grey haze, on every checkbox, input and menu indicator in
  the framework. The path is now inset by half the border width
  (`IRenderContext::InsetForStroke`, usable on its own), so the outline
  sits on whole pixels with its outer edge on the rectangle's edge and its
  centre unchanged; a rounded corner keeps its outer radius. A circle's
  ring likewise stays within its radius instead of overhanging it by half
  a stroke.
- **Menu: a Radio item's outline is a circle.** It was the same square box a
  Checkbox item gets, so the two item kinds could not be told apart until one
  was checked. `MenuStyle::radioShape` chooses: `MenuRadioShape::Round` (the
  default, as `UltraCanvasRadio` draws it) or `MenuRadioShape::Square`, the
  old look. UltraMail's "Show emails" filter menu uses the default and so
  gets the circle.
