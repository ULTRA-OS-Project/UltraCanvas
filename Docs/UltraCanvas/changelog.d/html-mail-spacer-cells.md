- **HTML mail keeps its spacing: table cells ignore `min-height` and
  `max-height`.** Stripe - and with it the receipts and invoices of every
  service that bills through it - spaces its mail with cells like
  `<td height="32" style="font-size:1px; line-height:1px; max-height:1px">`.
  Browsers ignore `max-height` on a table cell (CSS 2.1 leaves it undefined
  there); `HTML::ElementBuilder` applied it, so every such gap shrank to 1px:
  a receipt lost the space above its header and inside its cards, and its
  text sat packed together - which read as text drawn too large, although the
  sizes were right. A cell now ignores both limits, as Chromium and Firefox
  do; a cell made `display: block` (mail columns stacked on a phone) is a
  block and keeps them. `HTMLTableLayoutTest` expected the opposite for a
  percentage `min-height` / `max-height` on a cell and now expects what a
  browser lays out.
- **An inline element's `line-height` sets its line's height.** A line was
  as tall as the block's `line-height`, or the font's, and an inline
  element's own was dropped: Stripe's amount - 36px text on 40px lines - and
  the 14px caption above it on 20px lines, each in a cell that sets none,
  were laid out at the fonts' heights, the amount touching the caption. A
  line is now as tall as the tallest `line-height` of the inline boxes on
  it, and at least the block's own; text where nothing sets a `line-height`
  keeps the font's own line spacing, as before.
