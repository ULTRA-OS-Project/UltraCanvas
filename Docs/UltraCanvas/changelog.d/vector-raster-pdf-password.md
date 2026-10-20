- **A password-protected PDF can be rasterized.** `UltraCanvasVectorRaster`
  read a PDF through libvips' `pdfload` with no way to give it a password, so
  a protected PDF failed like a damaged one. `VectorRasterOptions::password`
  carries it, `InspectVectorFile(path, password)` takes it, and
  `VectorSourceInfo::passwordRequired` / `passwordWrong` say when one is needed
  or the one given does not open the file - decided from the file's
  `/Encrypt` dictionary behind the loader's refusal, since poppler says
  *"Document is encrypted"* for both. `PixelFX::FileIO::LoadPdf` takes the
  password too. UltraPaint asks for it on import.
- **On a libvips older than 8.13 the password opens the PDF too.** `pdfload`
  gained its `"password"` in libvips 8.13; Ubuntu 22.04's 8.12 ignores it
  (*"no property named 'password'"*) and opens every PDF with none, so the
  right password was reported as the wrong one. With such a libvips, CMake
  finds poppler-glib - which that libvips' `pdfload` is built on, and which
  `libvips-dev` depends on - and `LoadPdf` opens a protected PDF through it,
  rendering the page as `pdfload` does. A newer libvips is used as before.
