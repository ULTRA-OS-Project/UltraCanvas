- **The media viewer's Details panel is a group box with a close button, and
  shows a GIF's palette.** The panel's title (*Image information*, *Document
  information* ...) is now the caption of an `UltraCanvasGroupBox` instead of
  the first heading of its Markdown text, and an **x** at the caption's right
  end closes it (as Escape and the Details button do). The text is a size
  smaller (11 px, headings a step above it rather than document-sized). A GIF
  gets a *Palette* row (its colour table's size, global or the first frame's)
  and the colour table itself as a block of swatches under the text; the
  entry under the pointer is named by index and `#RRGGBB`. UltraFiler and
  UltraViewer both show this panel.
- **`UltraCanvasGroupBox::SetCloseButton`**: an optional "x" at the right end
  of the title bar, hover-highlighted with a tooltip
  (`SetCloseButtonTooltip`); a click runs `onCloseClicked` and the host
  decides what closing means. Colours: `closeIconColor`,
  `closeIconHoverColor`, `closeHoverBackgroundColor` in `GroupBoxVisualStyle`.
- **Markdown tables in `UltraCanvasTextArea`: the header fill now meets the
  grid.** The column dividers were drawn in whatever paint was current - the
  text colour - on whole-pixel coordinates, so they smeared over two pixel
  columns, and the rule under the header sat in the middle of the separator
  row, leaving a band of background between the header's fill and the line.
  Every rule is now one pixel in `tableBorderColor` on a pixel centre, the
  rule under the header is drawn at the separator row's top edge against the
  header fill, and without a line-number gutter the separator row is only a
  small gap rather than a whole text line.
