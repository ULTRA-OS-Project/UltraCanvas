- **UCD v2: the container is implemented, and layered images are its first
  content type.** `UltraCanvasUCDContainer.h` reads and writes the UltraCanvas
  Document container (`Docs/UltraCanvas/UCD-FileFormat-v2.md`): the signature
  and fixed header, the raw thumbnail, length-prefixed sections, an `INDX`
  written first, zlib sections written and zlib / gzip read (miniz, no
  libvips), unknown sections listed rather than refused, every refusal named
  (a v1 file, a later major version, a truncated file, a damaged stream, an
  encrypted section). The spec gains revision **2.1**: layered images (section
  4.7: the `LAYR` stack, one `BMAP` per layer, `UCTF` text frames that keep a
  text layer's words as text), the `INDX` layout, and the deflate / gzip stream
  formats pinned down; its SuperVault note no longer says AES.
  `UCRasterDocument` saves and opens them (`SaveUCD` / `LoadUCD`, `*.ucd`
  through `SaveToFile` / `LoadFromFile`; `IsProjectFile` covers both layered
  formats). FileLoader's inventory has a **`MediaFormatCategory::LayeredImage`**
  (`ucd`, and `ucraster` with libvips), filed by UltraFiler as images; a
  `.ucd`'s thumbnail is an embedded preview (`ExtractEmbeddedPreviewBytes`), so
  UltraFiler thumbnails it, and the media viewer reads UCD headers through the
  container (its Details name the key source the spec defines, not an AES
  cipher). Docs: `UltraCanvasUCDContainer.md`, `UltraCanvasPaintSurface.md`
  (*Layered files*).
