- **More kinds of DOCX charts are drawn as what they are**: a pie or
  doughnut as slices (in the colours the file gives its points), a bar chart
  as bars lying down with the categories up the left, and stacked or
  per-cent columns one on the other. They were all drawn as columns side by
  side.
- **A document's own bullets look like a word processor's.** A list read
  from a document carries its bullet (`RichDocBlock::bulletText`); it was
  drawn like the view's own - a text font's small bullet in the grey marker
  colour - and came out as a faint dot. It is now drawn in the text's colour
  and half as large again, the size of the symbol font's bullet Word and
  Writer use. `DrawBulletMark` (UltraCanvasDrawnMarks.h) takes a `scale`.
