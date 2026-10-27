- **A slider that shows no value centres its bar across its box.** The bar
  was always drawn at the foot of a horizontal slider's box (the left of a
  vertical one) to leave room for a value above or beside it - even with
  `SliderValueDisplay::NoDisplay`, the default, where nothing is drawn
  there. A slider in a row of labels taller than its 16 px handle therefore
  sat below their line (ArtCreator's *Quality*, the image-export and
  save-settings dialogs, the font viewer, the media viewer's adjustments).
  It is now centred; a slider that shows a value keeps the bar at the edge.
