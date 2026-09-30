- **HTML mail tables line up, and mail "buttons" are drawn.** A partner-proposal mail in
  UltraMail showed every table row with its own column widths, the Login / Upgrade /
  Profile / Photos buttons as white text on white (or not at all), `5&acute;8"` literally,
  and its text a third too large; Thunderbird showed a tidy table with blue buttons.
  - CSSLayout has a table layout: `DisplayType::Table` (`core/CSSLayout/TableLayout.cpp`,
    `Layout::SetTable` / `SetTableSpacing`) - the browsers' automatic table layout, with
    columns shared by every row, colspan / rowspan, min/max-content column widths, px and
    % widths, border-spacing, and cells stretched to their rows. See *Table layout* in
    `Docs/CSSLayout.md`.
  - The HTML reader builds tables on it (it gave each row its own flex row before), reads
    `cellspacing` / `cellpadding` / `valign` / `nowrap` / `<tr align>` / `<table align>` /
    `border`, `border-collapse` / `border-spacing` / `border-radius` / `white-space:
    nowrap` and `<nobr>`, centres a cell's content vertically by default, and places a
    table narrower than its line by its alignment.
  - An inline-block with a box of its own (background, border, padding, width) and an
    inline table are shrink-to-fit boxes on the line beside their text - the mail button
    idiom (`<a style="display:inline-block"><table style="display:inline"><td
    style="background:...">`). An inline element around a block (`<a href><div>`,
    `<font><table>`) is looked through, its formatting and link carried into the block.
  - Borders keep their colour and radius (only the width reached the element before, so
    they were invisible).
  - Every HTML 4 named entity is decoded (`&acute;`, `&eth;`, `&alpha;`, `&hearts;`, ...).
  - `a:link` / `:any-link` selectors match links; other pseudo-classes still drop the rule.
  - `inherit` works for colour, font and text properties, so `<a style="color: inherit">`
    keeps its paragraph's grey instead of turning default blue (Anthropic's sign-in mail).
  - CSS pixel sizes are converted to the label's points: text was drawn 33% too large,
    and a 15px `<span>` came out smaller than the 12px text around it. The eBook viewer
    converts its system font size (points) to px, so its default size is unchanged.
  - `UltraCanvasLabel` publishes a real min-content width (its widest unbreakable run).
