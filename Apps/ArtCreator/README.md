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
├ toolbar: new open save | undo redo | zoom- zoom+ page 100% | group … | mirror | quality ┤
├──────┬────────────────────────────────────────────┬────────────────────┤
│ tool │  rulers                                     │ Icon (icon mode)   │
│ pal- │ ┌──────────────────────────────────────┐    │ Colour (fill/line) │
│ ette │ │                                       │    │ swatches           │
│      │ │ pasteboard, page, grid, guides,       │    │ Fill (gradient     │
│      │ │ the drawing, selection handles        │    │   ramp)            │
│      │ │                                       │    │ Tool Options       │
│      │ └──────────────────────────────────────┘    │ Layers             │
├──────┴────────────────────────────────────────────┴────────────────────┤
│ status: pointer | zoom | selection | hint                              │
└────────────────────────────────────────────────────────────────────────┘
```

## Tools

The palette groups its 19 tools as UltraPaint's does: tools of a kind share
one button, which shows the tool last picked from its group. Resting the
pointer on it folds out a bar with the whole group, and a click there picks
a tool; a small triangle in a button's corner marks a group. A tool's key
picks it as before and shows it in its group's button. The groups, top to
bottom: *Select* (Selector, Shape Editor), *Draw* (Pen, Freehand, Straight
Line), *Shapes* (Rectangle, Ellipse, Quick Shape), Text, *Fill* (Fill,
Transparency), *Effects* (Shadow, Feather, Contour, Bevel), *Blend and
Mould*, *View* (Zoom, Push).

| Key | Tool | What it does |
|---|---|---|
| V | Selector | Click / shift-click / marquee; drag to move (ctrl constrains); handles scale (shift: keep aspect, ctrl: about the centre); click a selected shape again for rotate (corners, ctrl: 15° steps) and skew (edges) with a movable centre; arrows nudge (shift: 10 px); double-click a shape to edit its nodes |
| F | Shape Editor | Drag nodes and handles; click a line to select it, double-click it to add a node; Delete removes nodes; options: corner / smooth / symmetric, line ⇄ curve, close / open, reverse. A rectangle, ellipse or polygon becomes an editable shape on first edit |
| P | Pen | Click for corner nodes, drag for curves; click the first node to close; Enter or double-click finishes, Escape cancels, Backspace removes the last node |
| N | Freehand | Draw by hand; the stroke is simplified and smoothed on release, and closed when it ends near its start |
| L | Straight Line | Drag; shift constrains to 45° |
| R | Rectangle | Drag; shift for a square, ctrl from the centre; corner radius option |
| E | Ellipse | Drag; shift for a circle, ctrl from the centre |
| Q | Quick Shape | Drag from the centre. Options: Shape (Polygon — a regular n-gon, 8 corners for an octagon — or Star), Corners (3–24), Depth (how far a star's inner corners are pulled in). Shift stands it upright: a star's point up, an even polygon on a flat side |
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
  New Window (Ctrl+Alt+N), New Icon (16 to 256 px, see *Icon mode*), Open (Ctrl+O), Save (Ctrl+S), Save As
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
  Shapes, Snap to Pixels (Ctrl+Shift+P, in icon mode).
- **Object**: No Fill, No Line, line widths in points (hairline to 8 pt)
  and in pixels (1 to 4 px: the icon's pixels in icon mode, 96 DPI pixels
  otherwise).
- **Layer**: New (Ctrl+Shift+N), Delete, Move Up / Down, Show / Hide; the
  layers panel toggles visible / locked and picks the active layer.
- **View**: zoom, rulers, grid, guides, snap to grid / guides / objects /
  page, clear guides, Icon Size (icon mode for any drawing, or off).
- **Display quality** (the *Quality* slider at the end of the toolbar):
  *Outlines* - every shape as a thin line, nothing filled, clips and effects
  off, for finding and picking shapes in a dense drawing; *Fills + lines* -
  the colours without antialiasing; *Antialiased* - full quality (default).

## Icon mode

An icon is drawn for the size it is shown at - 16 px in a menu, 24 px on a
toolbar, up to the 256 px of a macOS Dock or taskbar icon - and is sharp
there only where its straight edges land on whole pixels. Icon mode is for
drawing it so.

- **Starting**: File > New Icon makes a square page of 16, 20, 24, 28, 32,
  40, 48, 64, 96, 128 or 256 px with one document unit a pixel, as icons
  are saved (an SVG of `width="24" viewBox="0 0 24 24"`). Opening a square
  SVG of one of those sizes starts icon mode at that size; View > Icon Size
  (or the Icon panel's Size) checks any drawing at an icon size, a pixel
  then being the page's width divided by it.
- **The grid**: a line every pixel once the view is close enough to tell
  them apart, and a darker one every 4 px (8 from 40 px, 16 from 128 px).
  The rulers and the status bar count in the icon's pixels, and the arrow
  keys nudge by one.
- **Snapping** (the panel's *Snap*): the pointer goes to the pixel grid,
  onto pixel centres while the line being drawn - or, for the selector and
  the shape editor, the selection's line - is an odd number of pixels
  wide, onto pixel edges otherwise. A 1 px line on a pixel centre covers
  one row of pixels fully; on an edge it would cover two half grey. New
  lines are made a whole number of pixels wide; Object > 1 px to 4 px set
  a width in the icon's pixels.
- **Snap to Pixels** (Arrange, Ctrl+Shift+P, or the panel's button) does
  the same to what is already drawn - the selection, or everything on the
  visible, unlocked layers: line widths become whole pixels, points go to
  pixel centres or edges by that rule, circles and ellipses by the edges of
  their bounds, and a curve too small to show as one (the round end of a
  bar a pixel wide) becomes straight. Text, pictures and rotated shapes are
  left as they are. It is `VectorEdit::SnapToPixels`, a framework operation.
- **The preview** (the top of the right panel): the page drawn at the
  icon's size, as Export writes it, on a light and a dark ground, and
  enlarged up to eight times with every *soft* pixel - one the drawing
  covers by more than 10 % and less than 90 % - outlined in red; the
  caption counts them. Curves and slants always leave some; a straight
  edge should leave none. *Mark soft* tints the same pixels on the page
  once they are 4 screen pixels wide.
- **Export** writes a bitmap at the icon's size unless another is chosen;
  its file name starts as a `.png`.

## Files

Opens SVG, XAR (compressed too, through the XAR plugin's reader), EMF,
WMF, DXF and DWG; saves XAR (the native format: shapes, fills with every
stop, text, transparency ramps and mixes, shadows and feathers, clip
views, contours, blends, moulds and bevels all round trip as Xara's own
records; Xara's own arrowheads as Xara arrowheads, the rest of the line
gallery as plain shapes that read back as strokes), SVG (shapes, fills,
text - no effects), DXF, EMF, WMF; exports PDF, AI, EPS and CDR too, and the
page as a bitmap at any size - PNG, JPEG, WebP, AVIF, HEIC, TIFF, GIF, QOI
or a Windows icon, each one this build can write (AVIF and HEIC need
libheif's encoders): the Export dialog's Settings for those types open on
the page at 96 DPI and take any other size, and the page is drawn at that
size - as sharp at 8000 pixels as at 800 - with the format's save settings
(JPEG quality, PNG compression ...). A PNG keeps what the page leaves
transparent; a JPEG is laid on white; an icon holds the page fitted into
16, 32, 48 and 256 pixel squares. What a reader or writer had to
drop is reported in the status bar. Without
the Vector plugin (`-DULTRACANVAS_PLUGIN_VECTOR=ON`) the editor draws but
has no file formats.

One ArtCreator runs per user. `ArtCreator a.svg b.xar` opens one window
each; starting it again while it runs hands the drawings to the running copy,
which opens each in a new window of its own and brings it to the front (a
drawing already open is never replaced), and a start without drawings just
brings ArtCreator forward (`UltraCanvasSingleInstance`). `--help` and
`--version` answer on their own.

## Source map

| File | Contents |
|---|---|
| `main.cpp` | Bootstrap, arguments, plugin registration, the first window |
| `ArtCreatorWindow.{h,cpp}` | Window composition, the open-window registry, menus, panels, status bar, shortcuts, file open / save, every command |
| `ArtCreatorTools.{h,cpp}` | The `ArtTool` base, the 15 tools, the line gallery's named choices, the option-widget helpers |
| `ArtCreatorDialogs.{h,cpp}` | New Drawing / Document Setup and Text |
| `ArtCreatorIcon.{h,cpp}` | Icon mode's sizes, the page drawn at the icon's size with its soft pixels, and the preview element |
| `ArtCreator.desktop` | The freedesktop launcher |
| `../../media/icons/artcreator/` | Tool and command icons in one shade, each set drawn on the pixel grid of the size it is shown at, so its lines land on whole pixels: the 24 px set (toolbar and panel buttons), `28/` (the tool palette, 28 px icons in 36 px buttons) and `16/` (every menu entry). The toolbar's file, edit and zoom buttons use `../../media/icons/menu/`; `../../media/appicon/ArtCreator.{png,svg}` is the app icon |
