- **`RenderFileThumbnail`: a file's picture outside the Filer.** A free
  function in `UltraCanvasFilerWidget.h` makes the picture a Filer tile
  shows - the image, a PDF's first page, a book's cover, a 3D model, a font
  specimen, a video's poster frame - and for a text document a small page of
  its first lines. The thumbnail workers' decode switch is now this one
  function, so the Filer and its other callers (UltraDesktop's Sticker board)
  agree on every format.
- `DesktopScreenInfo::number`: Windows' own number for a monitor (n of
  `\\.\DISPLAYn`, as its display settings show it); 0 where the system numbers
  none.
