- **UltraCanvasMediaViewer 1.18.0: a transparent picture or a drawing gets a
  backdrop it can be seen on.** Dark line art in the vector view sat on the
  viewer's near-black pane, and a white logo on the white backdrop - both
  close to invisible. The new `TransparentImageBackground::Auto`, the viewer's
  default, samples what is shown (a transparent picture's pixels, or the
  drawing rasterized at 64 px) and picks white, mid grey or dark grey -
  whichever gives the most of the artwork a 4.5:1 contrast
  (`UltraCanvasMediaViewer::ContrastingBackdropFor`). The vector view now
  takes its backdrop from the strip too (it had none), and the strip opens
  with an auto swatch; a colour picked there still holds for the following
  files. 3D models are unchanged: every model is drawn in one light grey on
  a dark ground, which already contrasts.
- **The media viewer's zoom box shows the zoom.** It stayed blank whatever the
  zoom was; it now reads *Fit*, the step it is at, or the percentage between
  steps (`137%`), for pictures, PDFs, e-books and drawings.
- **UltraCanvasColorSwatchBar 1.4.0: an auto swatch** (`SetShowAutoEntry`,
  `SelectAuto`, `onAutoSelected`), drawn half light and half dark before the
  checkered one.
- **UltraCanvasDropdown 1.2.0: `SetPlaceholderText`**, what the button reads
  while no item is selected.
