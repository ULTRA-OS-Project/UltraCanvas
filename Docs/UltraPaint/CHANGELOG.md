#### 2026-09-12 *0.2.0*
- **Dropping an image asks what to do with it.** A file dragged onto the
  canvas no longer replaces the open image without warning: the drop opens a
  small dialog naming the file and its size with **Merge image** and **Open
  new window** (and Cancel). Merge lands it as a new layer, centred, with the
  Move tool selected so it can be dragged into place; Open new window gives it
  an editor of its own and leaves the current image alone.
- **UltraPaint is multi-window.** *File > New Window* (`Ctrl+Alt+N`), a window
  per "Open new window" drop, and the application exits with the last window
  rather than the first. *Quit* now counts the unsaved images across every
  window instead of asking about the one in front.
- **A bitmap bigger than the canvas offers to fit.** Merging a 4000 px photo
  into an 800 px canvas used to be possible only by cropping it invisibly;
  the drop dialog offers *Scale to fit the canvas when merging*, ticked when
  the image does not fit, and resamples through PixelFX.
- **Vector drawings open as bitmaps.** Dropping or opening an SVG, PDF, EPS
  or - where the application has the Vector plugin - a DXF / DWG / EMF / WMF /
  XAR asks for the raster size first, starting at the drawing's natural size,
  with a page picker for a multi-page PDF. It is rendered at that size rather
  than scaled up from a thumbnail, keeps no file path (so *Save* asks where to
  put it instead of overwriting the drawing with pixels), and merges into the
  open image at whatever size fits the canvas. Backed by the new
  `UltraCanvasVectorRaster` (UltraCanvas 0.8.38).
- **File > Import Image...** does the same as a drop from the file dialog:
  merge into this image, or open it in a new window.
- Dropping several files at once asks once and applies the answer to all of
  them; files UltraPaint cannot read are ignored rather than reported one by
  one. The Open dialog's filter now lists the vector formats too, and a
  loader's error is trimmed to its reason instead of showing libvips' whole
  log.

#### 2026-09-12 *0.1.1*
- **Adjust > Colour to Alpha...** - pick a colour and make it transparent.
  The dialog carries the framework colour picker (whose eyedropper samples the
  colour straight off the canvas, so the colour need not be known in advance),
  a **Tolerance** slider measured the same way the Magic Wand and the Fill
  tool measure it, a **Softness** slider that ramps the edge instead of
  cutting it, **Transparency %** and **Remove colour fringe**.
- **Transparency is a percentage, not just on or off:** at 100% the colour is
  gone, at 40% it keeps 60% of its opacity - the same control fades a colour
  back as well as removing it. Alpha is only ever scaled down, so pixels that
  were already transparent stay transparent, and a layer with no transparency
  gains it.
- It previews live on the canvas, respects the selection and lands as a single
  undo entry, like every other adjustment. Backed by the new
  `PixelFX::Colour::ColourToAlpha` (UltraCanvas 0.8.33).
- The Magic Wand with *Contiguous* off plus `Del` still does the hard-edged
  version; this is the one to reach for on anti-aliased artwork, where that
  route leaves jagged edges and a fringe of the colour it removed.
- **Dragging a slider in any parameter dialog now updates the preview.** The
  value label moved and the canvas did not: the framework slider only reported
  a committed value when it was not being dragged, so Brightness / Contrast,
  Levels, the blurs, Curves and the rest showed the result of the value the
  dialog opened with until it was closed. Fixed in UltraCanvas 0.8.33; every
  dialog in the app picks it up.

#### 2026-09-06 *0.1.0*
- **First release of UltraPaint**, the UltraCanvas bitmap editor, built on the
  new raster-editing layer of the framework (`UCRasterDocument`,
  `UCRasterLayer`, `UCRasterSelection`, the brush engine and
  `UltraCanvasPaintSurface`, all in UltraCanvas 0.3.107) with PixelFX
  (libvips) doing the whole-image work.
- **Tools:** move, rectangle / ellipse / lasso / magic-wand selection with
  add / subtract / intersect and feathering, crop, eyedropper, pencil,
  paintbrush, airbrush, eraser, clone stamp, smudge, dodge, burn, flood fill
  (tolerance, contiguous / global, sample merged), linear / radial / reflected
  gradient, line / rectangle / ellipse (outline and fill, anti-aliased),
  text, zoom and pan. `[` and `]` change the brush size; Space+drag pans with
  any tool.
- **Layers:** add, duplicate, delete, reorder, merge down, flatten; per-layer
  visibility, lock, opacity and eleven blend modes; a layers panel and a
  properties dialog.
- **Adjustments and filters** (Adjust and Filter menus, generated from the
  PixelFX-backed catalogue in `UltraPaintFilters.cpp`, each with a live
  preview dialog): brightness / contrast, hue / saturation / lightness, gamma,
  levels, curves (the framework Curves dialog), posterize, threshold,
  solarize, invert, desaturate, sepia, equalize, auto contrast; gaussian /
  box / motion blur, median, sharpen, unsharp mask; Sobel, Laplacian, Canny,
  emboss, find edges; add noise, despeckle; pixelate, oil paint; erode,
  dilate. Every one respects the selection.
- **Files:** opens every raster format libvips loads (PNG, JPEG, WebP, AVIF,
  HEIC, TIFF, GIF, BMP, JXL, PSD, camera RAW, …), saves through the same
  pipeline (Save As by extension, Export with the framework's format options
  dialog), and keeps layered work in its own `.ucraster` project file (a ZIP
  of `document.json` plus one PNG per layer).
- **Editing:** unlimited undo / redo under a memory budget, cut / copy /
  copy merged / paste as new layer / paste as new image (system clipboard as
  PNG plus an in-app clipboard), fill with foreground / background, scale
  image, canvas size with anchor, crop to selection, flip / rotate, select
  all / none / invert / from layer alpha, feather / grow / shrink.
- Versioned separately from the framework from the start: this file is the
  single source of `ULTRAPAINT_VERSION`.
