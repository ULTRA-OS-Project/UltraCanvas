- **Type a title on the frame.** A double-click on a title in the viewer, or
  on its span on the title lane, lays the words over the picture where the
  title shows, at the viewer's scale, in its font and colour; the title leaves
  the frame meanwhile. Escape, Ctrl+Enter or a click outside puts them in the
  video as one undo step (*Edit Title*).
- **The text bar** over the viewer, shown with a title - the framework's
  `UltraCanvasTextFormatBar`, as in ArtCreator and UltraPaint, in the
  program's theme: the font (every installed font, each shown in itself),
  size in pixels of the project's frame, bold, italic, underline,
  strikethrough, alignment, line spacing and letter spacing. While a title is
  typed they go to the selected words, else to the whole title; otherwise to
  the selected title, one undo step (a held arrow is one).
- **Titles are drawn by UltraCanvas**, the engine that types them, into a
  picture laid over the frame - for the viewer, the draft and the export, at
  the export's own height - instead of FFmpeg's text filter, which could set
  none of the above. Their shadow and band are drawn into the picture as the
  filter drew them. Typing a title's text in the inspector makes it plain
  again. A saved project keeps the words, the font, the alignment and the
  spacings.
- The titles are drawn as the engine's text renderer
  (`VideoFXUltraCanvasText`, shared with the `videofx` tool): the project's
  text overlays carry each title's formatting, so `videofx render
  trip.ultravideo` draws the titles exactly as UltraVideo shows them.
