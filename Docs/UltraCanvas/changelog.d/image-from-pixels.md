- **A picture from pixels in memory.** `UCImage::FromBgra(pixels, width,
  height, stride, opaque)` and `UCImage::FromRgba(pixels, width, height)`
  make an image from rows already in memory - a screen capture, a frame a
  program made - with no encoder or decoder on the way: it is drawn, scaled
  to any fit, saved through libvips and asked for its transparency like a
  loaded one. UltraSnip shows the frozen screen and the snip taken from it
  this way, where before each went through a PNG and back
  (`ImageFromPixelsTest`).
