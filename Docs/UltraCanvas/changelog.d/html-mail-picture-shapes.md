- **A picture in an HTML mail is no longer stretched out of shape.** Since
  the HTML reader learnt `object-fit` (default `fill`: the picture is
  stretched to its box), every `<img>` whose box the reader got wrong was
  drawn distorted. LinkedIn's mails showed both:
  - **An `<img>` with a height and no width** (LinkedIn's header icons,
    `height="25"`) took the picture's own width - a 50px-high 2x icon shown
    25 high stayed 50 wide - and was stretched across it, twice as wide as
    it should be. Its width is now the picture's shape at that height, as
    in a browser (CSS 2.1 10.3.2).
  - **An `<img>` with a width and a height** that a narrower column shrinks
    (LinkedIn's logo, 101x37 in an 84px link) kept its full height and was
    squeezed. It now shrinks in its own shape
    (`UltraCanvasImageElement::SetBoxAspectRatio`, new).
  - **The picture is stretched only into a box the author gave another
    shape** - a width and a height, or min / max sizes that break the
    ratio, as `<img width="600" height="1">` rules need. Every other box has
    the picture's shape, so it is fitted keeping its proportions: the same
    as `fill` when the box is right, and an undistorted picture if the
    layout ever hands it one of another shape.
- **A table cell with a percentage width laid its row out at half its
  width.** While a row's height was measured, a `width="50%"` cell took its
  own 50% again of the column width it had been given, so its content was
  measured at half the cell's width: text wrapped onto too many lines and
  made the row too tall, and a `width:100%` picture came out half as high,
  so the row was too short and the picture hung out of it over the text
  below (LinkedIn's "Install LinkedIn Widgets" footer). The table now marks
  its cells `CSSLayout::Element::widthSetByParent` (new): an Exact width
  from it is the cell's used width - the block, flex, grid and table
  layouts all honour the flag - so a row is as tall as its content at the
  width it is drawn at.
- **`height="100%"` on the content of a table cell without a height is
  auto,** as in browsers (CSS 2.1 10.5): the table inside such a cell keeps
  its rows together, centred by the cell, in a row the picture beside it
  makes taller, instead of spreading them over the whole row. A cell that
  sets a height is still what the percentage is a share of.
