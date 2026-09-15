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
