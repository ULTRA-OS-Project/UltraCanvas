- **`UCRasterDocument::ScaleImage` takes the resampling method and a
  sharpening pass.** `ScaleImage(width, height, UCScaleImageOptions)`:
  `method` is a `UCResampleMethod` (`Nearest`, `Bilinear`, `Bicubic`,
  `Mitchell`, `Lanczos2`, `Lanczos3` - the default, as before), and `sharpen`
  (0 to 1) is an unsharp mask after the resample, with a radius that follows
  the enlargement. It works on the colour of the premultiplied pixels and
  joins the alpha band back untouched, so an edge against transparency gets
  no fringe. `Nearest` is done by the new `UCRasterLayer::ResampleNearest`, an
  exact copy of pixels with or without libvips, and is never sharpened. The
  two-argument call is unchanged.
