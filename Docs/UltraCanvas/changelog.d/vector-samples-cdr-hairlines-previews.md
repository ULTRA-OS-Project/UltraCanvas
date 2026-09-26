- **Every vector sample in the repository opens, and looks like its source.**
  Checked against independent references - each file's embedded preview
  (Xara, CorelDRAW), ezdxf (DXF), LibreOffice (CDR) - through
  `UltraCanvasFileLoader::LoadVectorDocument`:
  - **CorelDRAW files are read.** `CDRConverter::CanImport()` is true when
    the CDR plugin is built: libcdr's parse becomes SVG through librevenge's
    generator and the SVG importer turns that into the document, so a `.cdr`
    arrives as editable shapes (the three samples: 50, 400 and 725 objects).
    `ImportFromString` / `ImportFromStream` spool to a temporary file for
    libcdr. What libcdr drops stays dropped - in `detailed.cdr` the bitmaps'
    transparency and four card images' rotation, exactly as in LibreOffice.
    Windows builds still have no CDR plugin (no libcdr there).
  - **The readable and writable extensions are no longer hand-written
    lists.** `UltraCanvasVectorFormatsPlugin` keeps one converter table;
    `GetSupportedExtensions()` / `GetSaveExtensions()` collect the extensions
    each converter declares where `CanImport()` / `CanExport()` is true, and
    `CreateConverterForExtension()` picks from the same table. A reader that
    depends on an optional plugin appears exactly when it is built. New on
    the read list as a result: `cdr`, `svgz`, and Xara's `web`.
  - **`.svgz` reads.** `SVGConverter::Import` goes through
    `UltraCanvasFileLoader::LoadFile`, which inflates gzip transparently.
  - **Hairlines stay visible.** `VectorRenderOptions::MinStrokePixels`
    (default 1): no stroke is drawn thinner than one device pixel, as in a
    CAD viewer. A 0.25 pt pen on a plan 10,000 units wide shown at 7 %
    faded to nothing; the AI samples' 0.26 pt cutting outlines read as grey
    haze.
  - **A DXF whose declared extents are absurdly small or large is scaled
    like one without.** `millennium-falcon.dxf` declares 58 x 42 metres;
    kept as points that was a 2 cm page under 1 pt pens - solid black.
    Declared extents between 200 and 20,000 units are still kept as they are.
  - **Embedded images draw.** `VectorRenderer` passed an image's
    `data:...;base64,` source to `DrawImage` as a file path, so every
    embedded image (SVG, and every bitmap in a CorelDRAW file) drew nothing.
    It is decoded once, cached (cleared by `ClearCaches`) and drawn.
  - **Previews are the right size.** `RenderVectorDocumentPixmap` put the
    fit scale on the context and passed it again as `PixelRatio`, which the
    renderer applies on top: every Filer thumbnail and media-viewer preview
    of a drawing was drawn at the fit squared - a large drawing shrunk into
    a corner, a small one enlarged and cropped.
  - The AI documentation gains a *render trap* note: a `.ai` saved without
    PDF compatibility is a blank page to Ghostscript, poppler, ImageMagick
    and every PDF viewer, correctly, so they are no reference for it.
