- **`ImageJxlTest`: JPEG XL is now under test.** The framework claims `.jxl`
  for loading and saving (the format inventory, the export encoder, the Save
  dialog's JPEG XL settings), but nothing checked that a file it writes reads
  back. The new headless test writes a lossless and a lossy `.jxl` with
  `ExportVImage`, reads each back through `UCImageRaster` and compares the
  pixels (lossless exact, the transparent area kept), checks that the quality
  option reaches the encoder, and saves a loaded PNG as `.jxl` with
  `UCImageRaster::Save` - the calls the demo app's image performance page
  makes. It reports SKIP on a libvips built without libjxl, which includes
  Ubuntu 22.04's (CI's Linux runner); Ubuntu 24.04's passes it.
