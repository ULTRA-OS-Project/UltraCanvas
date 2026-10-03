- **CSSLayout: floats in block layout.** `LayoutItem::floatSide`
  (`SetFloat(FloatSide::Left / Right)`) puts a block child at the left or
  right edge of its parent's content box, as high as it fits: beside the
  floats already there when there is room, else below them. The blocks after
  it are narrowed by the floats beside their top edge, or moved below them
  when they would get less than their min-content width (a table, a long
  word). The parent grows to hold its floats. Approximation: a browser
  narrows only the line boxes beside a float, so a paragraph that starts
  beside a short float stays narrow to its end here. Only a `Block` parent
  honours `floatSide`. Test: `HTMLTableLayoutTest`.
- **HTML reader: a table without a width is as wide as its content.** It
  filled its line, so the 30px logo table of a mail header was drawn 138px
  wide, and a mail's left-hand button was centred across the whole line.
  Test: `HTMLTableLayoutTest` ("a table without a width is shrink-to-fit").
- **HTML reader: a list marker starts the item's first block.** In
  `<li><div>text</div></li>` the bullet stood on a line of its own above the
  text. Test: `HTMLTableLayoutTest` ("a list marker starts the item's first
  block").
- **HTML reader: `vertical-align: top / bottom` on inline-block boxes.** Two
  side-by-side mail columns were centred on each other instead of starting
  level. Test: `HTMLTableLayoutTest` ("vertical-align: top on side-by-side
  boxes").
