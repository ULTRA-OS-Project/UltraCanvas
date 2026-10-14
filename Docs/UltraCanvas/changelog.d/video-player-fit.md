- **`UltraCanvasVideoPlayerElement` 0.1.5: the picture placed as a host's
  frame surface places it.** `VideoPlayerStyle::videoFit` chooses `Contain`
  (fitted whole, letterboxed, the default and the old behaviour), `Cover`
  (filling the element, cropped) or `Pixels` (the frame's own pixels at
  `zoomPercent`), and `videoInset` keeps the picture that far from the
  element's edges; a picture larger than the element is clipped to it. A
  viewer that swaps its own frame surface for the player while a file
  plays can give the player the surface's inset and fit, so the picture
  stays where it was. 0.1.6: `showSafeArea` draws a dashed title-safe
  guide over the picture, `safeAreaInset` (5 %) in from each edge, in
  `safeAreaColor`; `zoomReferenceHeight` makes a `Pixels` zoom relative to
  a picture of that height, so a proxy file zooms as the original would.
