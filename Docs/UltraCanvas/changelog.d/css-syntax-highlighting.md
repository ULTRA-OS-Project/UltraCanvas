- **CSS is highlighted by structure, not by a word list.** The text area — and
  with it the Filer / media-viewer preview of a `.css` file — coloured CSS
  with the generic tokenizer, which split `background-color` at the hyphen,
  knew a few dozen property names, took the `//` in `url(http://…)` for a
  comment that swallowed the rest of the line, and could not tell a selector
  from a declaration. A dedicated CSS scanner now colours tags, `.classes`,
  `#ids` and `:pseudo`s in selectors; properties (including `--custom` and
  `-vendor-` ones) before the `:`; values, `var()`/`calc()`/`url()` calls,
  numbers with their units, `#hex` colours and `!important` after it; and
  `@media`/`@font-face` preludes. A minified stylesheet on one line is scanned
  exactly; in a multi-line file each line starts from a guess based on its own
  braces and semicolons. The unused CSS keyword tables and the invalid `//`
  comment were removed from the CSS rules.
