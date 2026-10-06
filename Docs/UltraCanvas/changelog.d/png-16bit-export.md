- **A 16-bit picture saved as an 8-bit PNG keeps its colours.** `pngsave`
  narrows 16-bit samples to the bit depth it is given by clipping them, so a
  16-bit PNG (as ImageMagick, scanners and some screenshot tools write them)
  that `UCImage::Save` wrote as an ordinary PNG - a thumbnail, an export -
  came out white. A 16-bit RGB or grey image is now converted to 8-bit sRGB
  or grey first, as the JPEG and WebP writers already do on their own.
