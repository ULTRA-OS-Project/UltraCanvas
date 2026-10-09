- **`HTML::ExtractPlainText` reads a word split by formatting as one word.**
  It put a space wherever a tag was, so `wor<b>ld</b>` came out as
  "wor ld" - and a keyword filter, which is one of the things the function is
  for, could be fooled by `<b>via</b>gra`. An inline element (`<b>`, `<i>`,
  `<span>`, `<a>`, `<font>` ...) now leaves no space, as on screen; a block,
  `<br>` and a picture still separate words, and so does the place a
  `<style>` or `<script>` was (that ran words together before). A no-break
  space now counts as a space when whitespace is collapsed. With this,
  UltraMail's threat scan and EmailCleaner drop their own tag strippers and
  entity tables and leave the html-reuse baseline (six entries).
- **`HTML::ExtractPlainText(const Node&)`: the text of a parsed element**, by
  the same rules - what a link or a cell says, read from the DOM instead of
  from a slice of the source.
- **The Filer's text preview and UltraCloud's WebDAV client decode entities
  through `HTML::DecodeEntities`.** The preview's own decoder knew six names
  and turned every numeric reference into a blank (`&#8364;` showed as a
  space, not "€"); the WebDAV client knew the five XML entities and left a
  numeric one such as Nextcloud's `&#x27;` in the file name. Both had their
  own tables; three more html-reuse baseline entries go.
