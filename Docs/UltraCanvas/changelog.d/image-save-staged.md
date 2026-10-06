- **A failed image save no longer leaves an empty file - or destroys the one
  it was saving over.** A libvips writer opens its file, truncating one that
  is there, before it has encoded a byte. When the encoder then failed - AVIF
  on a libheif built without an AV1 encoder ("heifsave: Unsupported
  compression"), a source that does not decode, a full disk - the save left a
  0-byte file, and saving over an existing image that way lost it.
  `PixelFX::FileIO`'s savers (`Save`, `SaveWithOptions` and the per-format
  ones) now write through `WriteFileAtomically`: the image is encoded into a
  temporary file in the same folder and moved over the target only when it is
  complete, so a failure leaves no file and the old one as it was. The media
  viewer's Save image as goes through them; `UCRasterDocument::SaveToFile`
  (UltraPaint) staged its own write around `FileIO::Save` and now relies on
  it. `UltraCanvasQRCode::ExportToImage` and its SVG export
  (`QRCodeUtils::ExportToSVG`), which wrote straight to the target, are
  staged the same way. New test: `Tests/SaveFileTypeTest.cpp`.
