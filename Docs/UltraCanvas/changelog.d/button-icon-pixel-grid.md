- **Button icons are drawn on whole pixels.** `UltraCanvasButton` centred its
  icon in the content box, and where that left an odd number of pixels
  around it - or the button was drawn under a translation of a fraction of a
  pixel - the icon landed half a pixel off and was resampled across two
  pixels: every 1-2 px line in a toolbar icon blurred into grey, and the
  dashes of a selection icon ran together (UltraPaint's tool palette). The
  icon's corner (and the split button's secondary icon's) is now snapped to
  the nearest whole device pixel through the current transform; its size is
  kept. New `ButtonIconPixelTest` draws a square icon centred at a half
  pixel, and under a 0.3 px translation, and finds it on whole pixels with
  no grey edge.
