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
