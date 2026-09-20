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
