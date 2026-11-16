- **A picture in a text frame vanished while the frame's words were edited.**
  A letterhead's sender block is a text frame with the logo anchored in it.
  Nothing floats inside a frame, so `UltraCanvasRichTextEdit` draws such a
  picture in the line (`LayoutTextFrame`) - but once a double-click opened the
  frame (`EditFrame`), its paragraphs were laid out as body paragraphs, the
  picture's placeholder was squeezed to nothing for a float that no pass
  places there, and the logo disappeared with the words moving up into its
  place. A frame being edited now keeps its pictures in the line, where they
  are drawn, selected and resized as before the double-click.
  `RichTextEditElementTest` opens such a frame and checks that the words stay
  below the picture and that a click still finds it.
- **A frame anchored in a text frame lost its words while the outer frame was
  edited.** Drawn, the inner frame's paragraphs stand before the paragraph
  that anchors it (`WithFramesInlineIn`); edited, its placeholder took no room
  and nothing drew them. They are now shown in the same place for as long as
  the outer frame is open (`PlacedFloat::wordsOnly`) - shown, not edited: they
  are the inner frame's, and stay as they were when the editing ends.
