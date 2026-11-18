- **OpenDocument Drawing (`.odg`, `.otg`, `.fodg`) is a vector format, read
  and written like SVG and XAR.** LibreOffice Draw's files open as an editable
  `VectorDocument` through `UltraCanvasFileLoader::LoadVectorDocument`, the
  media viewer and the Filer, and any drawing saves as one - the new
  `ODGConverter` (`Plugins/Vector/UltraCanvasODGConverter.h`) joins the Vector
  plugin's matrix, so every application that registers the plugin offers it
  in its Open and Save dialogs with no list edited.
  - Reading takes the package, its template and the flat XML form, and the
    first page of the drawing (other pages are reported). LibreOffice draws
    most of its shapes - the basic shapes, stars, arrows, flowchart and
    callout shapes - as `draw:custom-shape`s whose outline is a program, not
    a path: the reader evaluates their `draw:enhanced-geometry` (every
    enhanced-path command, the equations and their functions, the
    modifiers, stretch points and mirroring), so they arrive as editable
    paths where LibreOffice draws them. Styles resolve through the default
    style, the parent chain and the automatic styles: fills, gradients,
    hatches, bitmap fills, transparency, dashes, arrowheads, shadows; text
    in shapes and text boxes keeps its paragraphs and spans; layers, the
    master page's objects, the page size and background and the document's
    title and author are read.
  - Writing produces a package LibreOffice opens as it was drawn: one layer
    per model layer, transforms folded into the geometry, text as text
    boxes, pictures embedded, gradients, dashes, arrowheads and
    transparency as named styles.
  - Checked against LibreOffice 24.2 both ways: its own files (two samples
    in `media/vector/ODG`, 57 shapes of every kind) render here as
    LibreOffice renders them, and what the writer produces - from those
    files and from the SVG samples - renders in LibreOffice as the source
    did. `Tests/ODGConverterTest.cpp` holds the round trip, the samples and
    the custom-shape geometry; the flat form joins `VectorLocaleTest`.
- **A vector shape can be filled even-odd.** `VectorStyle::ShapeFillRule`
  (non-zero by default) is honoured by the renderer; a ring, a donut or a
  letter converted to curves read from an ODG drawing keeps its hole, since
  LibreOffice fills every shape that way.
- `.odg`, `.otg` and `.fodg` are vector files in the graphics plugin
  system's format table, the Filer's type names and the Save dialog's type
  names.
- **The Filer and the media viewer show an `.odg` / `.otg` from the
  thumbnail it carries** (`Thumbnails/thumbnail.png`, which LibreOffice
  writes) where no Vector plugin is registered to draw it - the same
  embedded-preview path as Xara, CorelDRAW and EPS files
  (`UltraCanvasEmbeddedPreview.h`). The ODG writer stores one as well: the
  page drawn 256 pixels on its long side, so a drawing saved here has a
  preview in LibreOffice's dialogs, desktop file managers and every
  application that has no reader for it.
