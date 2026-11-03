- **`UltraCanvasTextFormatBar`: one text formatting bar for every program
  that sets text.** Font, Size, Bold, Italic, Underline, Strikethrough, Align
  left / centre / right, Line spacing and Letter spacing in one row; it edits
  a `TextFormat` and reports each change with the attribute that changed
  (`onFormatChanged`), hands the keyboard back after a control
  (`onControlDone`), hides what a host cannot draw (`SetAttributeVisible`),
  takes the host's size unit (`SetSizeUnit`) and its theme (`ApplyTheme`; the
  icons, in `media/icons/textformat/`, take the ink). `TextFormatCharDelta` and
  `TextFormatAtCaret` connect it to an `UltraCanvasRichTextEdit` typed on a
  canvas. ArtCreator's text bar is this element now; UltraPaint and UltraVideo
  follow.
- **A dropdown row can be drawn in a font of its own** -
  `DropdownItem::fontFamily` - so a font list shows each font in itself
  without an element or a drawing callback of its own. Loading a font takes a
  few milliseconds the first time (measured: 2.6 ms on average, 6 ms for a
  colour emoji font), which would freeze a list of a computer's hundreds of
  fonts for a second or more as it opened; the dropdown prepares those rows
  lazily instead, in slices of about 6 ms every 15 ms once it is on screen,
  the rows its list opens on first, and shapes a row not prepared yet as it
  scrolls into view.
- **Text layers keep a letter spacing** - `RasterTextContent::letterSpacingPx`,
  drawn through `RichTextEditStyle::letterSpacing` - saved as
  `"letterSpacingPx"` in a raster project and in a version 2 `UCTF` text
  frame in a *.ucd (UCD 4.7.3). A text without letter spacing is still
  written as version 1, which every earlier reader takes. `RasterEditingTest`
  checks it.
