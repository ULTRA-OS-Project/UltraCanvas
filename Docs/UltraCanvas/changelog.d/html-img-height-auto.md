- **HTML reader: pictures with `height="auto"` are shown.** Mail templates
  (Beefree, Braze - Kickstarter's newsletters among them) write
  `width="580" height="auto"` on every `<img>`. The resolver read the
  attribute `auto` as 0px, so each picture was laid out zero pixels tall and
  the message showed none of them, even after its remote images had loaded.
  `width` / `height="auto"` on `<img>`, `<table>`, `<td>` and `<th>` is now no
  size at all, as `auto` already was in CSS: the picture keeps its own aspect
  ratio. Test: `HTMLReaderTest` (`TestImageAutoAttributes`).
