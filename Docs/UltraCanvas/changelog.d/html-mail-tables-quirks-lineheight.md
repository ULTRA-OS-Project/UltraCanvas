- **HTML mail like Yahoo's renders as in Thunderbird: quirks mode, table placement,
  overflow, line-height; cell percentage heights.**
  - The parser keeps the `<!DOCTYPE>` (`Document::doctype`) and decides quirks mode
    from it as browsers do (`Document::quirksMode`, `IsQuirksDoctype`). In quirks mode
    - no standards doctype, most HTML mail - a table does not inherit `text-align`:
    `<td align="center">` around a mail's 420px content tables centres the tables,
    not every line of text in them.
  - A table is placed by its container's alignment, not by its own `text-align`
    (`<table style="text-align:left">` in a centring cell is still centred).
  - A cell's children with a width of their own (`<div style="width:250px">`,
    `<table width="420">`) keep it instead of being stretched across the cell,
    placed by the cell's `align`.
  - HTML boxes draw content that is wider than they are (CSS `overflow: visible`):
    `ContainerStyle::clipChildren = false`; `overflow: hidden` / `auto` / `scroll`
    clip. Before, a 280px paragraph in a 250px box lost its last words.
  - `line-height` reaches the text: `LabelStyle::lineHeightPx` gives each line that
    height (Pango's absolute line height), a line holding a taller inline image still
    grows. A length or percentage inherits as px, a number as a factor of each
    element's font, `normal` restores the font's own.
  - A table cell (and a box with bottom padding or border) keeps its last child's
    bottom margin.
  - Table cells take percentage `height`, `min-height` and `max-height`, resolved
    against the table's set height; a table's extra height goes to the rows nothing
    set the height of.
