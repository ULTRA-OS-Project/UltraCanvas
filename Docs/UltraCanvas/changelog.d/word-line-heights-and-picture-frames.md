- **A paragraph's last line is as tall as its others, so a Word document's
  page is as long as in Word and Writer.** Lines are spaced by a factor of
  their fonts' height - ascent, descent and the line gap the font's designer
  gave it - but the text layout ends at the last line's descent, so every
  paragraph lacked one line's share: a one-line paragraph in Palatino (1.2 em
  a line, a fifth of it gap) was laid out 1.0 em tall at single spacing and
  1.0 instead of 1.38 at Word's 1.15. A one-page form in Palatino ended
  about 20 mm higher than in Word, and the signature lines anchored to an
  empty paragraph landed on the captions under them. The rich text
  view (`UltraCanvasRichTextEdit`, so UltraOffice and UltraTexter) now gives
  the last line of a paragraph and of a table cell the same room below its
  text, and an empty paragraph a line of its paragraph mark's font; the line
  an empty line of a set height is scaled from, a cell's stated multiple and
  an "at least" line height are measured the same way (the last no longer
  from 1.2 times the font size). `LayoutLineExtent::fontHeight` gives each
  laid-out line that height (`UltraCanvasRenderContext.h` 2.14.0).
- **A picture fills the frame its document gives it.** A picture alone in its
  paragraph - a .docx's `wp:inline` on a line of its own, an ODT `draw:frame`
  the same - became a picture block, which was drawn in its own proportions
  in the middle of its frame: a 4:1 logo in a 4.5:1 frame came out 10 mm
  narrower than in Word. It is stretched to the frame now, as pictures in a
  line and floating ones already were; the picture dialog's "keep
  proportions" still decides what a resize in the editor does. Such a picture
  also keeps its paragraph's indents, alignment and spacing from a .docx,
  read and written - a letterhead's logo set out into the margin by a
  negative indent is drawn there in page view.
- **A .docx's text starts right below a header taller than the top margin,**
  as in Word: the view kept Writer's 0.25 cm between the two for every
  document, and a page whose header pushed the text down ran 2.5 mm low. The
  room is the document's now (`RichPageSetup::furnitureSpacingPt`, 0 from a
  .docx; unset, Writer's 0.25 cm as before).
