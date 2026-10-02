- **HTML mail columns stack in a narrow pane, as on a phone.** Mail templates
  lay their articles out side by side and, below a width (`@media
  (max-width:620px) { .stack .column { display:block } }`), make each `<td>`
  a block so the columns stack. The HTML reader kept every `<td>` a table
  cell whatever its `display`, so a narrow reading pane showed two squeezed
  columns. Now the `display:block` cells next to each other in a row share
  one anonymous cell and stack in it, as in a browser.
  Test: `HTMLTableLayoutTest` ("mail columns stack in a narrow pane").
- **A stretched flex item keeps its `max-width`.** `align-items: stretch`
  widened an item past its max (or below its min) cross size; CSS Flexbox
  clamps it (§9.4 step 11). In HTML mail, a `<div style="max-width:280px">`
  holding a `width:100%` picture in a wider table cell was measured 280px
  tall but drawn stretched, the picture spilling over the text below.
- **`align="center"` / `"right"` on a container places its narrowed blocks.**
  `<td align="center"><div style="max-width:280px">` centres the div, as in
  browsers (also `<div align>` and `<center>`); before it sat at the left.
  Test: `HTMLTableLayoutTest` ("align=center places a max-width block").
