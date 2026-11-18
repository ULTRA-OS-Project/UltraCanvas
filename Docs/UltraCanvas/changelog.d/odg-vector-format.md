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
- **CI: the element-modules rows keep their apt packages between runs.**
  `element-modules.yml` downloaded every package on every run, so a slow
  mirror (64 kB/s on 2026-11-18) ran its dependency step out of its 45
  minutes. It now restores and saves them through `scripts/ci-apt.sh
  cache-in` / `cache-out` as `build.yml` and `wayland-build.yml` do,
  falling back to wayland-build's cache of the same image until main has
  saved its own.
