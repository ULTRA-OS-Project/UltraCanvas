- **CorelDRAW files open complete: libcdr vendored and patched.** libcdr
  (0.1.7, the engine LibreOffice also uses) lost two things
  `media/vector/CDR/detailed.cdr` depends on, and LibreOffice shows the same
  broken result: bitmap transparency masks (drop shadows became black boxes,
  a cut-out logo overlay an opaque white sheet over everything) and
  PowerClip contents (the cards' leaves, waves and gloss - parsed, never
  drawn). The patched copy lives in `UltraCanvas/third_party/libcdr` (MPL
  2.0; `README.md` and `ultracanvas.patch` document both fixes, meant for
  upstream) and is built by the CDR plugin when librevenge, lcms2, ICU,
  zlib and the Boost headers are present; the system libcdr remains the
  fallback. CI installs the Boost, lcms2 and ICU headers.
  - libcdr reads the 8-bit mask CorelDRAW stores after a bitmap (colour
    model 99) and re-encodes the bitmap as RGBA PNG.
  - libcdr reads loda argument `0x1f45` (PowerClip) and draws the clipped
    vect after its frame: an SVG image over the contents' box whose
    `clipPath` is the frame outline.
- **The SVG importer reads `<clipPath>` and SVG images.** `clip-path`
  references become `VectorStyle::ClipPath` over `VectorClipPath`
  definitions; an `<image>` whose href is `data:image/svg+xml` is read as an
  editable group placed by its box and `preserveAspectRatio`, its
  definitions renamed so nested documents cannot collide.
- **The editing canvas resolves definitions.** `UltraCanvasVectorCanvas`
  drew layers through `VectorRenderer::RenderLayer` without the document, so
  clip paths (and gradients or symbols referenced by id) resolved to
  nothing and drew unclipped. `VectorRenderer::SetDocument()` is new; the
  canvas sets it around the layers.
- **A fully transparent paint draws nothing.** `RenderContextCairo` set no
  source for a colour with alpha 0 and no pattern, leaving cairo's previous
  one - black by default - so a `fill-opacity="0"` shape was filled black
  (CorelDRAW writes stripes of them; one became a black bar across a card).
