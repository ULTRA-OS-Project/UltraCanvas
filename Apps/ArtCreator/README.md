# ArtCreator

A vector drawing editor of the Xara Designer / ArtWorks class, built on the
UltraCanvas framework's vector editing layer. The application is the tools,
panels, dialogs and commands; everything it edits through is framework
code, so an icon editor or a diagram designer can reuse the same parts.

| Layer | What | Where |
|---|---|---|
| Model | `VectorStorage::VectorDocument` — layers, groups, shapes, paths, text, styles, gradients | `UltraCanvas/include/DataFormats/UltraCanvasVectorStorage.h` |
| Editing | `VectorEdit` — selection, history, hit testing, transform / arrange / group / align / convert | `UltraCanvas/include/DataFormats/UltraCanvasVectorEdit.h` |
| Nodes | `UltraCanvasBezierPath` — the shape editor's node model | `UltraCanvas/include/UltraCanvasBezierPath.h` |
| View | `UltraCanvasVectorCanvas` — page, rulers, guides, grid, snapping, handles, tool hooks | `UltraCanvas/include/UltraCanvasVectorCanvas.h` |
| Ramp | `UltraCanvasGradientEditor` | `UltraCanvas/include/UltraCanvasGradientEditor.h` |
| Files | the Vector plugin's converters (`ULTRACANVAS_PLUGIN_VECTOR`) | `UltraCanvas/Plugins/Vector/` |

Docs: [`Docs/UltraCanvas/UltraCanvasVectorCanvas.md`](../../Docs/UltraCanvas/UltraCanvasVectorCanvas.md)
(the layer), [`Docs/Research/ArtCreatorVectorCanvasProposal.md`](../../Docs/Research/ArtCreatorVectorCanvasProposal.md)
(the investigation and the phases still to come), [`Docs/ArtCreator/CHANGELOG.md`](../../Docs/ArtCreator/CHANGELOG.md)
(the version - its first line is the version, per `AGENTS.md`).

## Layout

```
┌ menu bar ─────────────────────────────────────────────────────────────┐
├ toolbar: new open save | undo redo | zoom- zoom+ page 100% | group … | mirror ┤
├──────┬────────────────────────────────────────────┬────────────────────┤
│ tool │  rulers                                     │ Colour (fill/line) │
│ pal- │ ┌──────────────────────────────────────┐    │ swatches           │
│ ette │ │ pasteboard, page, grid, guides,       │    │ Fill (gradient     │
│      │ │ the drawing, selection handles        │    │   ramp)            │
│      │ │                                       │    │ Tool Options       │
│      │ └──────────────────────────────────────┘    │ Layers             │
├──────┴────────────────────────────────────────────┴────────────────────┤
│ status: pointer | zoom | selection | hint                              │
└────────────────────────────────────────────────────────────────────────┘
```

## Tools

| Key | Tool | What it does |
|---|---|---|
| V | Selector | Click / shift-click / marquee; drag to move (ctrl constrains); handles scale (shift: keep aspect, ctrl: about the centre); click a selected shape again for rotate (corners, ctrl: 15° steps) and skew (edges) with a movable centre; arrows nudge (shift: 10 px); double-click a shape to edit its nodes |
| F | Shape Editor | Drag nodes and handles; click a line to select it, double-click it to add a node; Delete removes nodes; options: corner / smooth / symmetric, line ⇄ curve, close / open, reverse. A rectangle, ellipse or polygon becomes an editable shape on first edit |
| P | Pen | Click for corner nodes, drag for curves; click the first node to close; Enter or double-click finishes, Escape cancels, Backspace removes the last node |
| N | Freehand | Draw by hand; the stroke is simplified and smoothed on release, and closed when it ends near its start |
| L | Straight Line | Drag; shift constrains to 45° |
| R | Rectangle | Drag; shift for a square, ctrl from the centre; corner radius option |
| E | Ellipse | Drag; shift for a circle, ctrl from the centre |
| Q | Quick Shape | Drag from the centre; polygon or star, sides and inner radius |
| T | Text | Click; a dialog asks for the text, font, size, bold, italic |
| G | Fill | Drag across a shape for a linear (or radial) gradient from the fill colour; drag the gradient's ends to adjust; flat / no fill buttons |
| Y | Transparency | Click a shape and drag right for a flat level, or drag across it for a linear / radial / conical ramp; the mix (stained glass, bleach, contrast, ...) and the level are in the options |
| W | Shadow | Click a shape for a wall shadow and drag it into place; kind (wall / floor / glow), blur, darkness and colour in the options |
| K | Feather | Click a shape to fade its edges, drag right for a wider fade; radius in the options |
| C | Contour | Click a shape for a contour; drag right for an outward width, left for inward; steps, the colour run (fade / rainbow / alt rainbow / constant) and the colour (from the line or fill colour) in the options |
| J | Bevel | Click a shape for a bevel, drag right for a wider rim; Xara's fifteen profiles, light angle and tilt, contrast, inner / outer in the options |
| B | Blend | Drag from one shape to another to blend them (from or onto a blend adds the shape to it); steps and the colour run in the options |
| M | Mould | Click a shape (or the selection it belongs to) to put it in an envelope or a perspective; drag the corner squares and an envelope's curve handles; reset shape / remove mould |
| Z | Zoom | Click to zoom in, shift-click out, drag a rectangle to fill the view with it |
| H | Push | Drag the view (space + drag does the same with any tool) |

The wheel zooms about the pointer, shift + wheel pans, `+` / `-`, Ctrl+0
fits the page, Ctrl+1 is 100 %. Drag a guide out of a ruler; alt-drag a
guide to move it; drop it back on a ruler to delete it.

The **Line** panel on the right holds the line gallery: the width, an
arrowhead for each end (triangle, open arrow, circle, square, diamond,
bar, and Xara's eight stock arrowheads drawn with Xara's own shapes and
sizes) and their size, a width profile (taper to either end, both, bulge)
and a brush (dots, dashes, hearts stamped along the path). It applies to
the selection and to every line drawn afterwards.

## Menus and shortcuts

- **File**: New (Ctrl+N; page presets, mm / cm / in / pt / px, orientation),
  New Window (Ctrl+Alt+N), Open (Ctrl+O), Save (Ctrl+S), Save As
  (Ctrl+Shift+S), Export (Ctrl+E), Document Setup, Quit (Ctrl+Q).
- **Edit**: Undo / Redo (Ctrl+Z / Ctrl+Y), Cut / Copy / Paste (in-app
  clipboard, pastes offset), Duplicate (Ctrl+D), Delete, Select All / None.
- **Arrange**: Bring to Front (Ctrl+F), Forward, Backward, Send to Back
  (Ctrl+B), Group (Ctrl+G), Ungroup (Ctrl+U; dissolves clip views,
  blends and moulds too), Mirror Horizontally / Vertically (also the two
  toolbar buttons: the selection flips about the centre of its bounds),
  align to selection / page, distribute, Combine
  Shapes (Add, Subtract, Intersect, Slice - the front shape cuts the
  others and is removed; Add and Intersect keep the back shape's style),
  Apply ClipView (Ctrl+K: the front shape becomes the keyhole the others
  show through), Remove ClipView, Blend or Mould, Convert to Editable
  Shapes.
- **Object**: No Fill, No Line, line widths.
- **Layer**: New (Ctrl+Shift+N), Delete, Move Up / Down, Show / Hide; the
  layers panel toggles visible / locked and picks the active layer.
- **View**: zoom, rulers, grid, guides, snap to grid / guides / objects /
  page, clear guides.

## Files

Opens SVG, XAR (compressed too, through the XAR plugin's reader), EMF,
WMF, DXF and DWG; saves XAR (the native format: shapes, fills with every
stop, text, transparency ramps and mixes, shadows and feathers, clip
views, contours, blends, moulds and bevels all round trip as Xara's own
records; Xara's own arrowheads as Xara arrowheads, the rest of the line
gallery as plain shapes that read back as strokes), SVG (shapes, fills,
text - no effects), DXF, EMF, WMF; exports PDF, AI, EPS and CDR too. What
a reader or writer had to drop is reported in the status bar. Without
the Vector plugin (`-DULTRACANVAS_PLUGIN_VECTOR=ON`) the editor draws but
has no file formats.

## Source map

| File | Contents |
|---|---|
| `main.cpp` | Bootstrap, arguments, plugin registration, the first window |
| `ArtCreatorWindow.{h,cpp}` | Window composition, the open-window registry, menus, panels, status bar, shortcuts, file open / save, every command |
| `ArtCreatorTools.{h,cpp}` | The `ArtTool` base, the 15 tools, the line gallery's named choices, the option-widget helpers |
| `ArtCreatorDialogs.{h,cpp}` | New Drawing / Document Setup and Text |
| `ArtCreator.desktop` | The freedesktop launcher |
| `../../media/icons/artcreator/` | Tool and command icons; `../../media/appicon/ArtCreator.{png,svg}` the app icon |
