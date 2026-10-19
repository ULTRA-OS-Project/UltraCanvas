- **A password-protected PDF can be rasterized.** `UltraCanvasVectorRaster`
  read a PDF through libvips' `pdfload` with no way to give it a password, so
  a protected PDF failed like a damaged one. `VectorRasterOptions::password`
  carries it, `InspectVectorFile(path, password)` takes it, and
  `VectorSourceInfo::passwordRequired` / `passwordWrong` say when one is needed
  or the one given does not open the file - decided from the file's
  `/Encrypt` dictionary behind the loader's refusal, since poppler says
  *"Document is encrypted"* for both. `PixelFX::FileIO::LoadPdf` takes the
  password too. UltraPaint asks for it on import.
