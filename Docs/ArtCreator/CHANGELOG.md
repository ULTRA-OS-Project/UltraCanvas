#### 2026-10-07 *0.6.7*
- **Save as and Export keep the file type you chose.** Both added an
  extension of their own to a name typed without one - `.xar` on Save as,
  `.pdf` on Export - whatever type was chosen, so "drawing" with SVG chosen
  was saved as Xara, after the dialog had asked about replacing "drawing"
  rather than that file. The save dialog now gives the name the chosen
  type's extension itself (UltraCanvas 0.9.176, PR #694), so ArtCreator saves
  the name it hands back as it is. A name without an extension saved under
  "All files" is refused with a message saying it needs one.

#### 2026-10-06 *0.6.6*
- **SVG files styled with CSS open in their colours.** A drawing that sets
  its fills, strokes, corner radii and text styles from a `<style>` block
  (diagram tools and hand-written SVG do) opened with every such shape black
  and the status bar saying "SVG import: `<style>` is not supported,
  skipped": an architecture diagram's light grey page came in as a black
  one. The style sheet now applies. The fix is in the framework's SVG reader
  (`Docs/UltraCanvas/changelog.d/svg-import-style-sheets.md`).
- **Arrowheads on imported SVG lines.** Lines that end in an SVG marker -
  the arrows connecting the boxes of a diagram - opened as bare lines, with
  "`<marker>` is not supported, skipped" in the status bar. The marker is
  now drawn: each line and its arrowheads come in as one group, so the arrow
  moves as one object, and ungrouping it gives the arrowhead as an editable
  shape. Also in the framework's SVG reader
  (`Docs/UltraCanvas/changelog.d/svg-import-markers.md`).

#### 2026-10-06 *0.6.5*
- **Copy and paste work with other programs.** Copy and Paste only ever
  used ArtCreator's own in-app list of objects: nothing copied here could be
  pasted anywhere else, and a picture or a file copied anywhere else pasted
  nothing here.
  - *Copy* now also puts a picture of the selection on the system clipboard,
    a pixel a point and cropped to what it draws, so UltraPaint, Paint Shop
    Pro, a word processor or a chat can paste it (UltraCanvas
    `RasterizeVectorElements`).
  - *Paste* takes what was copied last. A picture another program put on the
    clipboard becomes an image object in the middle of the page, shrunk to
    the page if it is larger. So does an image file copied in UltraFiler,
    Explorer or Finder, and a drawing file (SVG, CorelDRAW, Xara, ...) comes
    in as one group with its gradients and symbols. ArtCreator's own copy
    still pastes as the editable objects, offset as before - it recognises
    the picture it put on the clipboard byte for byte.
  - On Windows this needs the framework's clipboard fix in the same release
    (`Docs/UltraCanvas/changelog.d/windows-clipboard-images.md`): before it,
    no picture crossed the Windows clipboard in either direction.

#### 2026-10-02 *0.6.4*
- **The selection box fits the selected shape.** Selecting a part of an SVG
  drawn with relative curves (`astronaut.svg`) drew a box much larger than
  the shape and in the wrong place, near the top left of the page; it now
  fits the shape. The fix is in the framework's path bounds (see the
  UltraCanvas changelog), so moving, aligning and snapping use the right
  box too.
- **Display quality slider** in the main toolbar: *Outlines* (every shape as
  a thin line, nothing filled - for finding and picking shapes), *Fills +
  lines* (colours, no antialiasing) and *Antialiased* (full quality, the
  default).

#### 2026-09-29 *0.6.3*
- **Ctrl-C and SIGTERM exit in order.** The signal handler called
  `RequestExit()` (which logs and runs a callback) and then `std::exit`,
  running the static destructors under live threads. It now makes the one
  call a handler may, `UltraCanvasApplicationBase::RequestExitFromSignal()`,
  and the main loop turns it into the same shutdown as a closed window.

#### 2026-09-28 *0.6.2*
- **The version is in the window title** — `ArtCreator 0.6.2` — so a screenshot or a
  bug report says which build it came from. The number is this changelog's
  first line, as everywhere else (`cmake/UltraCanvasVersion.cmake`). With a drawing open it reads `drawing.svg - ArtCreator x.y.z`.

#### 2026-09-26 *0.6.1*
- **Right click sets the line colour.** A right click on a palette swatch
  under the colour picker now sets the line (background) colour of the
  picker and the selection; it used to do nothing. A right-button drag on
  the colour wheel or sliders with a shape selected no longer leaves the
  background swatch showing the fill colour, and a right-button eyedropper
  sample now reaches the selection's line colour too, and the swap arrow
  swaps the selection's fill and line instead of leaving both swatches the
  old fill colour. The
  framework side is in `Docs/UltraCanvas/changelog.d/colorpicker-right-click-background.md`.
- **The colour wheel fills the panel width.** The picker is sized to the
  height at which its hue ring spans the side panel, as in UltraPaint,
  instead of a fixed 300 px that left a small wheel. It is no longer
  stretched to the panel's content box either, so the hex field and
  channel values sit in the visible part beside the scrollbar instead of
  running under it.
- **SVG files open complete and in place.** Most shapes of an optimised
  SVG went missing, drawings with a transformed top-level group or an
  offset viewBox landed off the page (or not at all), and text was too
  large and hung below its line. The causes were in the framework's shared
  vector code (see
  `Docs/UltraCanvas/changelog.d/svg-import-and-vector-fileloader.md`).
  Text placed with the text tool now sits on the clicked point as its
  baseline, and at the size chosen.
- **Open and Save go through the FileLoader.** ArtCreator no longer calls
  the Vector plugin's converters itself: `UltraCanvasFileLoader::
  LoadVectorDocument` / `SaveVectorDocument` do, with the same formats and
  the same reader and writer notes in the status bar.
- **Ellipses and circles draw again, and no longer blank the window.** The
  first step of every ellipse drag has a zero-size box; drawing its
  preview put the window's Cairo context into a permanent error state, so
  nothing was drawn or created afterwards and the side panel went blank
  (Quick Shape looked broken too when tried next). Fixed in the framework's
  Cairo context (see
  `Docs/UltraCanvas/changelog.d/cairo-flat-ellipse.md`).
- **Quick Shape: polygon or star, and the options in view.** A *Shape*
  dropdown (Polygon / Star) replaces the Star checkbox, *Corners* (3–24)
  sets the number of corners or points - 8 with Polygon is an octagon -
  and *Depth* the star's inner radius. With Shift a star points up and a
  polygon with an even number of corners stands on a flat side. The Tool
  Options now sit directly under the colour swatches, above the Fill ramp,
  so they show without scrolling.
- **CorelDRAW files open, CAD drawings are readable.** Open now lists
  `.cdr` (where the framework's CDR plugin is built - not yet on Windows),
  `.svgz` and Xara's `.web`, from the formats the framework reports rather
  than a list of its own. Thin CAD lines stay one pixel wide at any zoom,
  and a DXF declared in metres (the millennium-falcon sample) no longer
  opens as a black blot. CorelDRAW drop shadows and cut-out overlays keep
  their transparency instead of covering the drawing. See
  `Docs/UltraCanvas/changelog.d/vector-samples-cdr-hairlines-previews.md`.
- **CorelDRAW drawings look like CorelDRAW's own preview.** Drop shadows,
  cut-out overlays and PowerClipped artwork (content clipped into a frame)
  now appear, through the framework's patched libcdr; clip paths in any
  imported drawing are applied on the canvas.

#### 2026-09-22 *0.6.0*
- **Mirror.** Two toolbar buttons after Send to Back, and *Mirror
  Horizontally* / *Mirror Vertically* on the Arrange menu, flip the
  selection about the centre of its bounds: left-right, or top-bottom. It
  is a scale of -1 on one axis through the editing layer, so it composes
  into each element's transform like a drag of the selection handles,
  undoes as one step and keeps the selection where it was. Icons
  `media/icons/artcreator/mirror-h.svg` and `mirror-v.svg`.

#### 2026-09-21 *0.5.1*
- **The app icon needs no font.** `media/appicon/ArtCreator.svg` kept the
  letter as live text in the Salina face, so every renderer without that
  font (GitHub's PNG export, the build environment, most desktops' icon
  themes) drew a different "A" from the one in the artwork. The glyph is
  now an outline path traced from the uploaded 256 px render, which is
  what `media/appicon/ArtCreator.png` (the window icon, the desktop entry
  and the Windows `.exe` icon) holds; the four tiles and their colours are
  unchanged. Rendering the SVG reproduces the PNG to within antialiasing.

#### 2026-09-20 *0.5.0*
- **One version number, one place.** This changelog's first line is now
  the only place ArtCreator's version lives: the build reads it
  (`cmake/UltraCanvasVersion.cmake`) and passes it to the sources as
  `ARTCREATOR_VERSION`. The `"0.0.0"` fallbacks in `main.cpp` and
  `ArtCreatorWindow.cpp` are gone; a build without the definition now
  fails at compile time instead of an About dialog and `--version`
  reporting 0.0.0.
- **`--help` and `--version` answer on stdout.** They wrote to the
  diagnostic stream, which a Release build keeps off unless
  `ULTRACANVAS_DEBUG_LOG` is set, so a shell saw nothing. Unknown arguments
  go to stderr.

#### 2026-09-19 *0.3.0*
- **Depth, phase 5 of the proposal (first slice).** Four tools after
  Feather in the palette and three commands, on the framework's new
  containers, effects and polygon booleans (UltraCanvas 0.9.16):
  - **Contour (C)**: click a shape for a contour; drag right for an
    outward width, left for an inward one; steps, the colour run (fade,
    rainbow, alt rainbow, constant) and the colour - from the line or the
    fill colour - in the options.
  - **Bevel (J)**: click a shape for a bevel and drag right for a wider
    rim; Xara's fifteen profiles, the light's angle and tilt, contrast
    and inner / outer in the options.
  - **Blend (B)**: drag from one shape to another to blend them; dragging
    from or onto a blend adds the shape to it; steps and the colour run
    in the options.
  - **Mould (M)**: click a shape (or the selection it belongs to) to put
    it in an envelope or a perspective, then drag the corner squares and
    an envelope's curve handles; reset the shape or remove the mould.
  - **Arrange > Combine shapes**: Add, Subtract, Intersect and Slice, as
    in Xara (the front shape subtracts from or slices the others and is
    removed; Add and Intersect keep the back shape's style).
  - **Arrange > Apply ClipView (Ctrl+K)**: the front shape of the
    selection becomes the keyhole the others show through; **Remove
    ClipView, Blend or Mould** (and Ungroup) dissolve them again.
- **XAR carries all of it.** Clip views, contours, blends, moulds and
  bevels are saved as Xara's own controller records and read back as
  themselves; a Xara drawing's own ones open as editable objects.
- Not in this release (the rest of phase 5): text on path and text areas,
  pages and spreads, the colour gallery with linked shades, symbols, the
  photo tool, live effects, trace-to-vector, editable brushes.

#### 2026-09-18 *0.2.0*
- **Xara-class effects, phase 4 of the proposal.** Three additions to the
  palette and one panel, all on the framework's new model fields
  (`VectorElement::Effects`, `VectorStyle::Transparency`, the line
  gallery on `StrokeData`) and its raster effect cache:
  - **Shadow (W)**: click a shape for a wall shadow and drag it into
    place; the options set the kind (wall, floor - squashed and sheared
    from the bottom edge - or glow), the penumbra, the darkness, the
    colour from the line colour, or remove it.
  - **Feather (K)**: click a shape to fade its edges, drag right for a
    wider fade; radius in the options.
  - **Transparency (Y)** grew shapes and mixes: drag across a shape for a
    linear, radial or conical ramp (its ends are shown and the far level
    is the slider), and pick the mix - stained glass, bleach, contrast,
    saturation, darken, lighten, brightness, luminosity, hue - which the
    renderer paints as the matching blend mode. Flat transparency with
    the normal mix stays plain opacity.
  - **Line panel**: width, an arrowhead for each end (triangle, open
    arrow, circle, square, diamond, bar, and Xara's eight stock
    arrowheads - straight, angled, rounded, spot, diamond, feather,
    feather 2, hollow diamond - drawn with Xara's own shapes and sizes)
    and their size, a width profile
    (taper to either end, both, bulge) and a brush (dots, dashes, hearts
    stamped along the path, in the line colour). It applies to the
    selection and to every line, rectangle, ellipse and path drawn
    afterwards.
- **XAR is the native format.** Save As offers XAR first and names new
  drawings `untitled.xar`: shadows, feathers, transparency ramps with
  their mixes and gradients with every stop round trip through it (the
  framework's XAR converter now reads through the XAR plugin's parser,
  so compressed Xara files open too). SVG remains for shapes, fills and
  text without the effects. The line gallery round-trips as well:
  Xara's stock arrowheads are saved as Xara arrowheads with their size
  (and a Xara drawing's arrowheads open as the same kinds at the same
  size, at either end); the other arrowheads,
  width profiles and brushes are saved as plain shapes - the brush as its
  stamped copies - marked with a Xara user value, so Xara shows them as
  drawn and ArtCreator reads them back as the stroke they were.
- **The app icon is the uploaded artwork.** `media/appicon/ArtCreator.svg`
  is the four-tile "A"; the PNG the window icon, the desktop entry and
  the Windows `.exe` icon are made from is re-rendered from it.
- Not in this release (phase 5 of the proposal): bevel, contour, blend,
  mould, ClipView, path booleans, text on path, pages, editable brushes,
  a live-effects gallery.

#### 2026-09-15 *0.1.0*
- **First release of ArtCreator, a vector drawing editor of the Xara
  Designer / ArtWorks class on the UltraCanvas framework's vector editing
  layer** (`VectorStorage::VectorDocument`, `VectorEdit`,
  `UltraCanvasVectorCanvas`, `UltraCanvasBezierPath`,
  `UltraCanvasGradientEditor` - framework 0.8.51 - and the Vector plugin's
  converters for the file formats). The investigation behind it is
  `Docs/Research/ArtCreatorVectorCanvasProposal.md`; the application is
  the tools, panels, dialogs and commands over that layer.
- **Tools** (the palette, with single-letter keys): Selector (V - click,
  shift-click, marquee; drag to move; handles scale, shift keeps the
  aspect, ctrl scales about the centre; a second click on the selection
  shows rotate and skew handles with a movable centre; arrows nudge;
  double-click a shape to edit its nodes), Shape Editor (F - nodes and
  handles, drag a line, double-click to add a node, Delete removes,
  corner / smooth / symmetric, line ⇄ curve, close / open, reverse; a
  rectangle, ellipse or polygon is converted to an editable shape on
  first edit), Pen (P - click for corners, drag for curves, click the
  first node to close), Freehand (N - smoothed on release, closes when
  ended near its start), Straight Line (L), Rectangle (R - corner
  radius), Ellipse (E), Quick Shape (Q - polygon or star, sides, inner
  radius), Text (T - dialog: font, size, bold, italic), Fill (G - drag a
  linear or radial gradient across a shape from the fill colour, drag its
  ends afterwards; flat and no fill), Transparency (Y - click a shape and
  drag, or the slider), Zoom (Z - click, shift-click, drag a rectangle),
  Push (H).
- **Panels**: colour picker (foreground = fill, background = line; with a
  selection, changes apply to it and are undoable), swatch bar, the fill's
  gradient ramp (stops added, moved and removed on the strip; a flat fill
  becomes a linear gradient), tool options, layers (visible, locked,
  active; add, delete, move up / down).
- **Menus**: File (New with page presets and units, New Window, Open, Save,
  Save As, Export, Document Setup, Quit), Edit (undo / redo, cut / copy /
  paste / duplicate / delete, select all / none), Arrange (z-order, group
  / ungroup, align to selection or page, distribute, convert to editable
  shapes), Object (no fill / no line, line widths), Layer, View (zoom,
  rulers, grid, guides, snapping to grid / guides / objects / page), Help.
- **Files**: opens SVG, XAR, EMF, WMF, DXF and DWG; saves SVG (the format
  that keeps everything), XAR, DXF, EMF and WMF; exports PDF, AI, EPS and
  CDR as well. Reader and writer notes appear in the status bar. A build
  without `ULTRACANVAS_PLUGIN_VECTOR` still draws but says so on Open and
  Save.
- Multi-window: File > New Window, and every extra path on the command
  line, gets a window of its own; the application exits with the last.
- Not yet: pages, text on a path, Xara's feather / shadow / bevel / contour
  / blend / mould effects, arrowheads and brushes, the system clipboard,
  bitmap export - see the proposal's phases 4 and 5.
