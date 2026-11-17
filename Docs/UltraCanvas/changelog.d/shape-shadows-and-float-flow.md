- **Drawn shapes throw their drop shadow.** A page frame drawn as a white
  rectangle had none. `RichShape::shadowColor`, `shadowOffsetXPt` /
  `shadowOffsetYPt`, `shadowOpacity` and `shadowBlurPt`: read from ODF
  `draw:shadow` and DrawingML `a:outerShdw`, written back by the ODT and DOCX
  writers, and drawn under the shape by `UltraCanvasRichTextEdit` (a soft edge
  as a few faint outlines). A line throws none, and a .doc's shadow is not
  read.
- **Text went on 9 pt too low under a picture.** Text that may not pass a
  picture (wrap "none" / top and bottom) continued a fixed 9 pt below it -
  Word's distance at a picture's sides, not above and below it, where Word
  and Writer keep none. And a paragraph starting a hair above such a picture
  ran through it; one whose first line does not fit above the picture now
  goes below. A paragraph that only anchors a picture is a line of its own
  font, not of the view's (it was 2.5 pt too tall in a 10 pt letter).
