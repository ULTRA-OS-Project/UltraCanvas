#### 2026-08-09 *0.3.36*
- **UltraCanvasFilerWidget** *(1.13.0)*: content previews are now **selectable
  per file kind**. The context menu grew a `Display > Preview` submenu with a
  checkbox for each of Bitmaps, Vector graphics, 3D, PDF, Text, Docs,
  Spreadsheets and Videos — all enabled by default — mirrored in code by
  `SetPreviewType()` / `SetPreviewTypes(mask)` / `IsPreviewTypeEnabled()` /
  `GetPreviewTypes()` over the new `FilerPreviewType` bitmask. Switching a kind
  off drops its entries back to the plain type glyph immediately *and* stops
  the widget from opening those files at all, which is what makes a folder of
  huge photos, videos or PDFs on a slow volume browsable.
  Three kinds gained a real preview producer, all running on the existing
  viewport-driven thumbnail workers so no preview ever blocks a frame:
  **PDF** files render their first page through the PDF plugin (outlined as a
  sheet of paper, since a page is white on a white widget), **STL** models are
  rasterized in software as a shaded three-quarter view (the GL viewer needs a
  window and a current context, which a background decode has neither of), and
  **text, documents and spreadsheets** preview as a miniature page of their own
  content — plain text and source code read directly, HTML stripped of its
  tags, RTF of its control words, ODT / DOC / DOCX through the shared
  rich-document reader, and ODS / XLSX / CSV / TSV laid out as a cell grid.
  Page-shaped previews are only drawn from roughly a 40 px box up, so the icon
  column of a Details or List row keeps its glyph and a folder listing does not
  read every document in it. `FilerFileCategory` gained `Model3D` and the type
  map learned the common 3D extensions (stl, obj, ply, 3ds, 3mf, gltf, glb,
  dae, fbx) plus `tsv`, so those files sort and colour as models / text instead
  of "File".

#### 2026-08-08 *0.3.35*
- Change Linux packager script, drop .appimage
- Change GitHub build to produce package with all dependent libs

#### 2026-08-08 *0.3.34*
- **UltraCanvasFilerWidget** *(1.12.0)*: a dragged file can leave the widget
  again. The drag was handed to the native OS drag the moment the cursor
  crossed the widget's border, and that is where it visibly died: the badge is
  drawn by the widget and therefore clipped to it, the OS drag draws nothing of
  its own while the cursor is still over the application's own window (XDND
  refuses a drop back onto the window that started it), and on Windows and
  macOS `StartNativeFileDrag()` had no implementation at all, so the gesture
  was dropped on the floor. Crossing the border now keeps the drag running: the
  badge travels over the whole window on the new window drag overlay, a release
  over another element hands it the files as a `Drop` event (a second Filer
  pane, a folder tree, any drop-aware widget), and only leaving the *window*
  turns the set into the native OS drag. A platform without one keeps the
  window-wide drag alive instead of losing the gesture.
- **UltraCanvasWindowBase** *(2.2.0)*: new `SetDragOverlay(owner, windowRect,
  renderer)` / `ClearDragOverlay(owner)` — window-level content painted above
  every element, for widgets that drag something across the whole window and
  cannot paint outside their own bounds. Moving it repaints the rectangle it
  leaves and the one it enters; the first owner keeps it until it clears it.
- **Windows backend**: native file drags out of a window are implemented
  (`UltraCanvasWindowsWindow::StartNativeFileDrag`) with a CF_HDROP
  `IDataObject` plus an `IDropSource` driven by `DoDragDrop`, the counterpart of
  the `IDropTarget` that was already there. Files can now be dragged from a
  Filer widget into Explorer or any other application, and the accepted effect
  (copy / move) is reported back so a move rescans the source folder. macOS
  still has no drag-and-drop backend in either direction.
- **UCTextLayout** *(1.1.2)*: a `TextWrap::WrapNone` layout no longer wraps onto
  a second line when it is also given an explicit height. Pango has no "never
  wrap" flag — a no-wrap layout is one that Pango is told to ellipsize, and the
  layout *height* decides when that kicks in: `-1` (the default) means "ellipsize
  the first line of each paragraph", but a positive height means "ellipsize once
  that many pixels are used up", so a box two lines tall lets the text word-wrap
  once before anything is ellipsized. Widgets set an explicit height purely to
  centre the glyphs vertically (`VerticalAlignment::Middle`), which silently
  turned single-line text into two lines whenever the box was at least twice the
  line height. `SetExplicitHeight` now keeps that value for the vertical
  alignment maths only and leaves Pango's own height at `-1` while the layout is
  in no-wrap mode; `SetWrap` re-applies it, since it may be called after the
  height is set. `GetExplicitHeight` reports the requested height rather than
  Pango's.
- **UltraCanvasBreadcrumb / UltraCanvasLabel**: fixed as a result — the filer's
  and media viewer's path strips kept long folder names such as
  `UCDemo-Windows-0.3.24-x86_64 (1)` on one ellipsized line instead of breaking
  them at the space and drawing two cramped lines inside a one-line strip.
  Whether the break happened depended on the exact font line height against the
  strip's height, which is why it showed on Windows and not on Linux. Long names
  are still capped at `BreadcrumbStyle::maxItemTextWidth` (200px by default; set
  it to `0` for no per-item limit).

#### 2026-08-08 *0.3.33*
- **UltraFiler — Extras > Open prompt**: a new menu bar entry starts the
  operating system's command line program in the folder of the active tab.
  The launch lives in `Apps/UltraFiler/UltraFilerPrompt` *(1.0.0)*, which
  detaches the process (`fork` + `setsid` + `execvp` behind a reaped
  intermediate child on POSIX, `ShellExecuteExW` on Windows), so closing the
  file manager never takes the terminal with it and no zombie is left behind.
  Without configuration the platform default is detected at run time:
  `%COMSPEC%` on Windows, Terminal.app (started with `open -a <bundle>
  <folder>`) on macOS, `$TERMINAL` or the first installed terminal emulator on
  Linux; when nothing is found the failure is reported in an alert instead of
  silently doing nothing.
- **UltraFiler — settings**: the settings window gained an *Extras > Open
  prompt* page holding the application that menu entry starts
  (`extras.prompt.application` in the config file). The folder button next to
  the path field opens the file dialog filtered to this platform's
  applications, **Save app** persists the chosen program, **Use system
  default** clears the setting again and **Test** starts the program in the
  field to check the path. The dialog's buttons now come from one
  `MakeButton` helper instead of per-button styling.
- **UltraCanvasCircleDiagram** *(1.0.0)*: new hub-and-spoke circle diagram
  infographic — a centre hub, a backbone ring, and equally sized labelled node
  discs threaded onto that ring, each with a fan of satellites on leader lines.
  It is the node-on-ring member of the circular family: every existing circular
  element subdivides the ring into sectors, while this one threads discrete
  discs onto it, so a node's radius is independent of ring thickness and it can
  carry children outside the ring. Presentation-only (no viewport, dragging,
  inline editing or undo); interaction is hover highlighting, tooltips and
  `onNodeClick` / `onSatelliteClick`. Structure and colour are independent
  presets — `CircleDiagramDesign` (`SatelliteWheel`, `BandedWheel`, `Custom`)
  and `CircleDiagramPaletteKind` (seven themes plus `Custom`) — and both work
  at any node count, because each palette is a hue ramp sampled at N points
  rather than a fixed list. Every layout quantity derives from the per-node arc
  of 360/N: the auto-fitted node radius is a share of the chord between
  neighbours, the satellite fan is narrowed to what one node's wedge can hold
  (shrinking auto-sized satellite discs when K of them will not fit side by
  side), and anything outside the backbone — fans, or labels placed with
  `CircleNodeLabelPlacement::Outside` — is reserved for by shrinking the
  backbone radius so nothing is clipped. Disc labels shrink to fit, testing the
  longest single word as well as the wrapped block, since a word too wide to
  break ellipsizes rather than wrapping. Node discs are all one radius and
  satellites another; `value` is tooltip/callback payload and never scales a
  disc. `SetNodeCount()` clamps to 3–12 rather than degrading silently. Docs in
  `Docs/UltraCanvas/UltraCanvasCircleDiagram.md`, survey and roadmap in
  `Docs/UltraCanvas/CircleDiagramInfographicVariants.md`, demo scene in
  `Apps/DemoApp/UltraCanvasCircleDiagramExamples.cpp`.

#### 2026-08-08 *0.3.32*
- **Version numbers** are now derived from the changelogs at build time, so the
  version the demo app's info window shows can no longer disagree with the
  version in the file name of the build it came from. The packaging scripts
  already parsed `#### YYYY-MM-DD *x.y.z*` off the first changelog line for the
  artefact names; the number compiled *into* the binaries was a separate
  hand-maintained copy that only moved when someone remembered to run
  `set-version.sh`, and it had fallen ten releases behind (the info window
  reported 0.3.21 against a 0.3.31 changelog). The new
  `cmake/UltraCanvasVersion.cmake` reads the same first line at configure time
  and feeds `project(VERSION)`, `ULTRACANVAS_VERSION` and `ULTRATEXTER_VERSION`;
  `UltraCanvas::versionString` and `UltraCanvasTextEditor::version` take their
  value from those defines instead of a literal. Adding a changelog entry
  re-triggers the configure step, so an existing build tree picks the new
  version up rather than baking in the one it was first configured with.
  `set-version.sh` now only writes the two Windows resource files that are read
  from disk by windres (`UltraTexter.rc`, `UltraTexter.manifest`) — a configure
  on any platform warns when those are stale.

#### 2026-08-07 *0.3.31*
- **UltraCanvasGLSurface** *(1.0.1)*: a surface built with a non-zero `(x, y)`
  now keeps that origin. The constructor delegated to the size-only base
  constructor, so the origin was written straight to `finalBounds` without a CSS
  position behind it: the first layout pass re-stacked the surface as an in-flow
  child and the GL content composited in the top-left corner of its container
  while every sibling widget stayed put. It now uses the `(id, x, y, w, h)` base
  constructor, which stamps an AbsoluteUI position for a non-zero origin;
  `(0, 0)` still leaves the surface in flow for flex/grid parents. The doc gained
  a "Positioning" section covering this and the bounds + CSS-box pattern needed
  to move or resize a surface at runtime.
- **DemoApp — OpenGL 3D showcase**: all three tabs (3D Models, Shaders, Zarch)
  share one maximize control, `gldemo::AddMaximizeControl()`. The icon sits in
  the canvas's top-right corner with an 8px margin, and the button, a
  double-click on the canvas or Esc toggles between the normal layout and a
  canvas maximized over the whole tab. Maximizing works now that the zoom writes
  the CSS box as well as the bounds (`gldemo::PlaceElement`) — hiding the chrome
  invalidated the layout, which promptly restored the canvas's original size. A
  new `media/icons/minimise.svg` marks the restore state, the Zarch and Models
  panels match the Shaders tab's column geometry, and the Shaders info text was
  trimmed to what fits its panel.

#### 2026-08-07 *0.3.30*
- **UltraCanvasTooltipManager** *(2.3.0)*: structured tooltips gained
  three-column table rows and definable column alignment.
  `AddRow(label, value, value2)` (and the swatch overload
  `AddRow(color, label, value, value2)`) adds a three-column row next to the
  existing two-column form. Alignment is set per column and separately for
  each arity — `TooltipColumnAlign::Left | Center | Right` via
  `TooltipStyle::columnAlign2` / `columnAlign3` (theme level) or
  `TooltipContent::SetColumnAlignment(...)` (per tooltip, stored outside
  `styleOverride` so `SetStyle()` does not reset it); the defaults reproduce
  the previous look of labels left, values flush right. Two- and three-column
  rows are measured as independent tables, so a two-column "Total" row cannot
  disturb a three-column table above it. Column widths are natural when the
  row fits and otherwise cap the label column at 55 % and share the rest in
  proportion to the natural widths; surplus width is spread over the gaps so
  the last column stays flush with the tooltip's right edge.
- **UltraCanvasTooltipManager**: `TooltipColumnAlign::Decimal` aligns a column
  of numbers on their decimal separator instead of on an edge. A cell's anchor
  is the last `.` or `,` followed by a digit, so a thousands separator never
  wins over the real decimal mark (`1,204.50` and `1.204,50` both resolve) and
  a trailing period cannot steal it; a cell with no separator anchors at its
  end, so integers meet the separator column. The column reserves the widest
  integer part plus the widest fractional part — wider than any single cell —
  so nothing is clipped to stay aligned, and a cell that still does not fit
  falls back to right alignment. Demo tiles cover the three-column table,
  custom alignment, mixed row arity and decimal alignment; the "Title + table"
  tile is now a label / value / unit table with capitalized labels.

#### 2026-08-07 *0.3.29*
- **UltraCanvasSequenceDiagram** *(1.0.0)*: new UML 2 sequence diagram
  feature, layered like the class diagram. `UltraCanvasSequenceModel` is the
  UI-free interaction model — lifelines (object / actor / boundary / control /
  entity / database heads), the seven message forms (sync, async, return,
  create, destroy, lost, found, plus self messages), combined fragments
  (loop / alt / opt / par / break / critical) with guarded operands, and
  anchored notes. Execution bars are computed from the message order
  (`ComputeActivations`), message numbers sequentially or hierarchically
  Visual-Paradigm style (`ComputeMessageNumbers`), and `Validate()` diagnoses
  dangling endpoints, lifecycle misuse, unmatched returns and fragments that
  cross without nesting. `SequenceTextExport` writes PlantUML and Mermaid,
  including activations, fragments, notes and inline create declarations.
  The rendering element solves lifeline spacing from head and label widths,
  attaches arrows to the deepest execution bar, drops created heads onto
  their create row, and offers six themes, zoom/pan/fit, hover + selection
  callbacks, an optional "sd" frame and foot boxes. Six sample models —
  among them the framework's own event pipeline — drive the new DemoApp tab;
  unit tests in `Tests/SequenceModelTest.cpp` (target `SequenceModelTest`).

#### 2026-08-06 *0.3.28*
- **UltraCanvasFilerWidget** *(1.9.0)*: the inline rename editor is now a real
  `UltraCanvasTextInput` overlaid on the item's name instead of a hand-drawn
  append-only field. Typing, caret movement (arrows / Home / End),
  click-to-position, Shift selection and Ctrl+C/X/V/Z all work; the editor
  opens with the base name selected (extension kept, Explorer-style; folders
  select the whole name), commits on Enter and on focus loss (a click
  anywhere else), cancels on Esc. Renaming a search-result entry now renames
  in the entry's own folder instead of resolving against the shown path.
- **UltraCanvasFilerWidget**: rubber-band selection. Dragging from empty
  space draws a selection rectangle; every entry it touches becomes the
  selection, live while the band is dragged, and with Ctrl the rectangle
  adds to the selection held at the press. The band auto-scrolls at the
  viewport edge and Escape abandons it (restoring the previous selection);
  `WantsEscapeKey()` covers it. A plain click on empty space still clears
  the selection (a Ctrl click leaves it alone).
- **UltraCanvasFilerWidget**: video files show their poster frame (grabbed
  via `CaptureVideoThumbnailPixmap` a short way into the clip) as their
  thumbnail in the thumbnail views, the Details/List mini icons, the drag
  badge and the delete-confirmation preview. Decoded on the existing
  background thumbnail workers, so the folder page never waits on a video;
  without a video backend the tile keeps its generic glyph.
- **UltraCanvasTextInput** *(1.3.3)*: typed UTF-8 input is inserted instead
  of dropped — the printable filter on `event.text` kept only ASCII 32..126,
  so multi-byte characters (umlauts, accents, CJK, ...) typed into any text
  field vanished.
- **UltraViewer app** *(1.0.0)*: new universal media viewer application
  (`Apps/UltraViewer`, target `UltraViewer`, `BUILD_ULTRAVIEWER_APP`, default
  ON). One full-window `UltraCanvasMediaViewer` displays bitmaps, vector
  graphics, video and audio (with the player elements' transport controls:
  play / pause / seek / scrub / volume), documents (PDF), e-books,
  spreadsheets (ODS/CSV/TSV), 3D models (STL), text / source / markdown and
  UltraCanvas Document containers (*.ucd). Command line takes a folder (browse
  it), a file (browse its folder with the file shown first), several files
  (exactly that playlist) or nothing (use Open / drag & drop). New app icon
  `media/appicon/UltraViewer.png`.
- **UltraCanvasMediaViewer** *(1.4.0)*: two new media kinds.
  `MediaKind::Book` — e-books (EPUB / FB2 / MOBI / PRC / AZW / AZW3) open in
  an embedded `UltraCanvasEBookViewer` (chapter toolbar, TOC, reflowing
  content); the zoom toolbar drives the reading text scale and PageUp /
  PageDown switch chapters while Left / Right keep browsing the folder.
  `MediaKind::UCDoc` — UltraCanvas Document containers (*.ucd) are recognised
  by the UCD v2 fixed header: the viewer shows the embedded raw HEIC/PNG
  preview thumbnail (readable without parsing the body, as the format
  intends) on the image surface, or a header summary in the text view when
  there is none; the info bar labels the file `UC DOCUMENT` and the details
  popup lists the container fields (type descriptor, version, body encoding,
  compression, encryption, thumbnail). Full rendering arrives with the UCD
  v2 engine. New doc: `Docs/UltraCanvas/UltraCanvasMediaViewer.md`.

#### 2026-08-06 *0.3.27*
- **UltraCanvasMediaViewer** *(1.3.1)*: the `Still` video preview mode shows
  the first frame instead of staying on "Buffering...". The mode relied on the
  load-time preroll frame alone, whose single emission could be flushed away
  while the pipeline was still settling; the `Still` paths now request the
  frame explicitly (a paused seek to 0 re-prerolls and delivers it — the same
  mechanism as a paused scrub) whenever no frame of the current file has been
  shown yet.
- **UltraCanvasVideoPlayerElement** *(0.1.7)*: loading a source resets the
  per-file frame state (shown frame, scrub throttle, time readout), so
  switching files updates the preview instead of keeping the previous video's
  last frame. New `HasVideoFrame()` reports whether a frame of the current
  source has been shown yet.
- **UltraCanvasVideoPlayer** *(0.1.2)* / **GStreamer backend** *(0.1.11)*: a
  freshly opened session no longer receives a spurious playback-rate "change"
  to the default 1.0. Backends apply a rate through a flushing seek, and
  issuing one during the initial preroll discarded the prerolled first frame a
  paused session depends on; the GStreamer backend also skips the seek for any
  unchanged rate (as the MediaFoundation backend already did for 1x).

#### 2026-08-05 *0.3.26*
- **UltraCanvasMediaViewer** *(1.3.0)*: configurable backdrop behind
  transparent images. New `TransparentImageBackground` enum
  (`SolidColor` / `Checkered`) with `SetTransparentBackground()` /
  `GetTransparentBackground()` and `SetTransparentColor()` /
  `GetTransparentColor()` on both the surface and the viewer. The backdrop is
  drawn under the image's displayed rectangle (only its visible part, so a
  high zoom costs nothing extra) — a preset solid colour (default white) or
  the light/dark checkerboard familiar from image editors — and fades with
  the image during slideshow transitions. Transparent pixels now read against
  a defined background instead of the dark canvas colour.
- **UltraFiler app** *(1.3.0)*: menu bar with a *Settings* menu opening the
  new settings window: a tree of settings pages on the left (main pages with
  sub pages — currently *Media Viewer > Transparent Images*) and the selected
  page on the right. The Transparent Images page chooses between the
  checkered pattern and a preset colour (colour picker) for the backdrop
  behind transparent images in the media preview; changes apply live and
  persist to the platform config directory
  (`~/.config/UltraFiler/config.ini` on Linux).
- **UltraCanvasMediaViewer**: new `SetTopBarsVisible()` / `GetTopBarsVisible()`
  — shows/hides everything above the display surface (the folder breadcrumb,
  both toolbar rows and the adjustments panel). For hosts that embed the
  viewer as a plain preview pane and provide their own navigation. Default:
  visible.
- **UltraCanvasTreeView**: new `SetFontSize()` / `GetFontSize()` for the row
  label font size (default 12, previously hardcoded).
  `UltraCanvasColumnsTreeView` uses it for its cell text, column headers and
  group headers too.
- **UltraCanvasFilerWidget**: new `FilerStyle::folderIconScale` (default 1.0)
  — shrinks the folder glyph inside a thumbnail tile's image box, centered,
  so folders can read lighter next to image thumbnails.
- **UltraCanvasFilerWidget**: file-list display for search results. New
  `ShowFileList(paths)` / `IsShowingFileList()` shows an explicit list of
  paths (stat-ed like scanned entries) instead of the folder listing, in the
  current view mode; `SetPath()` returns to the folder display. The Details
  view gains a `Path` column (the entry's containing folder) shown only in
  that mode, so normal folder displays are unchanged. The Open-Path context
  item now sits at the *top* of the menu followed by a separator, and
  `SetOpenPathMenuItemVisible(visible, label)` takes a caption.
- **UltraFiler app** *(1.2.0)*: search field on the right of the path bar —
  searches the current folder recursively for names containing the text
  (case-insensitive, capped at 1000 matches) and shows the matches in the
  tab's current view mode, with the *Path* column after the name in Details
  view and "Open path (in new tab)" as the context menu's first entry.
  Clearing the field or navigating returns to the folder display; each tab
  keeps its own search. Every UI element now uses a single 9 pt font size
  (toolbar, breadcrumb, dropdowns, tabs, folder tree, file panel, status
  bar). The preview pane hides the media viewer's breadcrumb and toolbars —
  the filer provides the navigation, the pane shows only the media. Folder
  icons in the thumbnail views draw at 70% size.

#### 2026-08-04 *0.3.25*
- **UltraCanvasFilerWidget** *(1.7.0)*: entries are properly draggable. A press
  on an item captures the mouse and, past the slop threshold, picks up that
  item — or the whole selection when the press landed inside it. Inside the
  widget the drag is drawn by the widget (a badge with the entry icon and name
  / "N items" following the cursor, the folder under it highlighted) and a drop
  on a folder of the view **moves** the files into it, **Ctrl** drops a copy;
  Escape abandons the drag. Leaving the widget hands the same set to the native
  OS drag as before — which is also what fixes dragging files out: the capture
  makes a fast flick out of the widget start the drag instead of losing the
  move to whatever the cursor passed over. New `SetDragEnabled()` /
  `IsDragEnabled()` turn the gesture off.
- **UltraCanvasFilerWidget**: a drag no longer changes the selection. What a
  plain press would select is applied on the release, so dragging a file does
  not fire `onSelectionChanged` and no longer re-targets a preview pane fed by
  it (UltraFiler loaded the dragged file into the preview mid-drag).
- **UltraCanvasFilerWidget**: the selection now survives a rescan — it is
  remembered by path instead of by row index, so `Refresh()` after a file
  operation no longer leaves the selection pointing at whatever moved into
  those indices. New `onFolderRefreshed` callback fires after every scan of the
  shown folder, so hosts can refresh their folder description.
- **UltraCanvasMediaViewer**: SVG files preview as the rendered image instead
  of their source code. `ClassifyFile()` matched the syntax tokenizer's SVG /
  XPM / XBM languages before the image formats, so markup-based image files
  opened in the read-only text area. New `IsImageFile()` is checked first.
- **UltraFiler app**: the status bar follows the folder listing again — it is
  refreshed from `onFolderRefreshed`, so item counts stay correct after a drop
  or another file operation rescans the view.
- Merge "UltraFiler font size standardization"
- Add UltraFiler app icon

#### 2026-08-02 *0.3.24*
- **UltraCanvasMediaViewer**: selectable video preview behavior. New
  `VideoPreviewMode` (`Autoplay` — full playback with sound, the previous and
  default behavior; `PreviewClip` — the first seconds muted and then paused,
  the `UltraCanvasAlbum` hover-preview style; `Still` — the prerolled first
  frame, paused) with `SetVideoPreviewMode()` / `GetVideoPreviewMode()` and
  `SetVideoPreviewClipSeconds()` (default 5 s). Changing the mode applies to a
  currently shown video too. New `StopPlayback()` stops video / audio playback
  and the pending clip timer — for hosts that hide or detach the viewer, where
  the sound used to keep playing invisibly.
- **UltraCanvasSupportedFormats**: new `CanImagePipelineLoad(extension)` —
  whether the raster/SVG image pipeline behind `UCImage` (libvips + the
  built-in SVG renderer, including the ImageMagick delegate fallback for known
  raster extensions) can decode files with that extension. The existing
  inventory reports plugin-provided formats (CDR, XAR, ...) as loadable, so it
  could not answer "may this file go to the image loader?".
- **UltraCanvasFilerWidget**: thumbnail decoding is now gated on
  `CanImagePipelineLoad`. Vector-category files that only a graphics plugin
  understands (e.g. `.xar`, `.cdr`) were handed to the libvips loader by both
  the thumbnail worker and the delete-confirmation folder preview (which fed it
  *every* file type), producing `VipsForeignLoad "... is not a known file
  format"` warnings and wasted decode attempts; such files now keep their
  category glyph.
- **UltraFiler app** *(1.1.0)*: tabbed browsing — a "+" button on the left of
  the toolbar opens additional tabs, each with its own folder view, history
  and sort/view settings (drag to reorder, closable except the last). The
  preview pane now folds away while nothing previewable is selected, giving
  the folder display the whole width; the Preview toggle enables / disables
  the feature. New "Video" dropdown selecting the preview behavior for video
  files: Autoplay (default), 5 s muted clip, or still image.

#### 2026-08-01 *0.3.23*
- **UltraCanvasDendrogram** *(1.5.1)*: fixed radial leaf labels rendering upside
  down in the upper-right quadrant. The left/right half test was
  `angle > -pi/2 && angle < pi/2`, which assumes angles in `(-pi, pi]`, but
  `DendrogramLayoutEngine::ApplyRadialLayout` produces `[0, 2pi)` — so every leaf
  between `3pi/2` and `2pi` failed the test, took the left-half branch and picked
  up an extra 180-degree rotation. Both the leaf labels and the group arc labels
  now test `cos(rotation)`, which is precisely the condition for "this text is not
  upside down" and does not depend on which angle range the layout uses. Audited
  the other radial components at the same time — Sunburst, Chord, RadialBar,
  CircularInfoGraphic and PolarChart all normalise correctly and were unaffected.

#### 2026-08-01 *0.3.22*
- **UltraCanvasNodeDiagram** *(2.2.0)*: organizational-network features. Node
  size can now be driven by the data - `NodeSizeMode::ByDegree` sizes a node
  from its connection count, `ByValue` from a new `NodeDiagramNode::value`
  field, both through a sqrt transfer so node AREA tracks the quantity rather
  than the diameter. Degrees can count total / incoming / outgoing links and are
  cached, so bulk-loading a graph costs one rebuild instead of one per
  `AddLink`. New `NodeDiagramGroup` cluster containers wrap a set of member
  nodes in an auto-fitted boundary box (solid or dashed, optional fill and
  corner radius, title in any corner) that follows its members through dragging
  and re-layout; boxes draw behind the links and titles after the nodes, both
  sized in screen pixels so they hold up at any zoom. `SetGroupCohesion()` adds
  a per-group centroid attraction to the force-directed layout - without it
  repulsion scatters a cluster and the boxes overlap into mush. New color legend
  overlay (`NodeDiagramLegendConfig`, `BuildLegendFromGroups()`) drawn in screen
  space in any corner. Groups and sizing round-trip through `ToJson()` /
  `FromJson()`, and group boxes are included in `ComputeContentBounds()` so
  `FitView()` and the minimap account for them. New demo tab (Diagrams > Node
  Diagram > Organization): a five-department company network with a control
  column for layout, sizing mode, degree mode, cohesion, link style, node shape,
  theme, and toggles for the boxes, legend, grid, minimap, controls and snap.
- **UltraCanvasDendrogram** *(1.5.0)*: hierarchical edge bundling (Holten 2006).
  `AddRelation()` registers a leaf-to-leaf association that does not follow the
  tree's parent/child structure; each one is routed through the tree path source
  -> lowest common ancestor -> target, relaxed toward the straight chord by
  `SetBundlingStrength()` (beta), and smoothed with a clamped cubic B-spline. A
  radial dendrogram can now show its hierarchy and the cross-links between its
  leaves at the same time without becoming a hairball. Also new: area-proportional
  node dots via `DendrogramNodeSizeMode::ByValue` and `DendrogramNode::nodeValue`,
  normalised against the largest value in the tree - replacing the single global
  `style.leafNodeRadius` as the only leaf size available.
- **UltraCanvasJitterPlotElement** *(1.3.0)*: per-point encodings. A parallel
  vector of size magnitudes plus `JitterPointSizeMode::ByValue` turns a beeswarm
  into a bubble beeswarm, with the packer receiving the real per-point radii so
  mixed sizes pack without overlapping. A parallel vector of color values plus
  `JitterPointColorMode::ByValue` samples any `UltraCanvasColormap` palette,
  including the diverging ones with a configurable midpoint, so a signed quantity
  reads correctly around zero. Fixes: `minScoreFilter` defaulted to `0.0` and
  silently discarded every negative value before rendering; `AddCategoryData()`
  did not invalidate the point-position cache, so a second call left the previous
  points on screen; `RenderJitterPoints()` always drew a plain circle and ignored
  both `SetPointShape()` and the point edge style.
- **UltraCanvasMediaViewer**: the arrow keys now browse the folder as soon as
  the widget is on screen. The widget takes the window keyboard focus when it is
  attached to a window (`SetGrabFocusOnAttach(false)` opts out,
  `FocusForKeyboard()` requests it on demand) and installs a window key filter,
  so Left / Right no longer require a click into the picture first — with no
  focused element at all the key event never reached the widget before. The
  filter only steps in when the keyboard is unowned or held by one of the
  display views; toolbar buttons, sliders and the breadcrumb keep their own key
  handling. Where the active view uses the bare arrows itself (spreadsheet cell
  movement) `Alt+Left` / `Alt+Right` browse instead.
- **UltraCanvasMediaViewer**: text / source / markdown files open display-only
  instead of read-only-but-focusable. The text area used to grab the focus and
  swallow Left / Right for caret movement, which stopped file browsing dead
  (and showed an editing caret in a viewer). The viewer now scrolls the text
  itself with Up / Down / PageUp / PageDown and copies the selection with
  Ctrl+C.
- **UltraCanvasMediaViewer**: opening a single file through the Open dialog, or
  dropping one file onto the widget, now browses the folder that file lives in
  (with that file shown first) instead of building a one-entry playlist that the
  arrow keys and the slideshow had nowhere to move in. Multi-selections are
  still taken as an explicit playlist.
- **UltraCanvasTextArea**: new `SetDisplayOnly()` / `IsDisplayOnly()`. Display-
  only implies read-only and additionally takes the area out of the keyboard
  focus chain — `AcceptsFocus()` returns false, no caret is drawn and no key
  event reaches it — so a hosting widget keeps the arrow keys for its own
  navigation. Mouse wheel / scrollbar scrolling and mouse selection are
  unaffected.
  
#### 2026-08-01 *0.3.21*
- **UltraCanvasSplitPane**: split lines can now carry an optional **handle**.
  `SplitterHandleShape` picks the form - `Square`, `RoundedSquare`, `Round`
  (a circle when square, a capsule when elongated) or `Image` - and
  `SplitterHandleStyle` sets its size across and along the line, corner radius,
  position along the line (0..1), colors, border and grip lines. The splitter
  strip widens to fit the handle while the painted line stays as thin as
  `splitterThickness`, so a 3 px line can carry a 22 px handle.
  `SetSplitterHandleShape()` is the one-liner; the handle drags exactly like the
  rest of the line. The shape enumerator for "no handle" is `NoHandle`, not
  `None`, because `<X11/X.h>` defines `None` as a macro.
- **UltraCanvasSplitPane**: **image handles**. `SplitterHandleStyle::imagePath`
  takes an SVG or raster asset, with `SetSplitterHandleImage()` as the
  one-liner. With `SplitterHandleShape::Image` the asset is the whole handle;
  with any drawn shape it is centred on top of it and the grip is suppressed.
  Leaving `axisLength` at 0 takes the handle's proportions from the asset, so
  the ready-made `media/icons/scrollbar-handle-v.svg` (14x48) renders as a grip
  rather than a squashed square. `imageAsMask` re-tints a monochrome glyph with
  `imageColor`, or with the handle's own normal/hover/active color when that is
  left transparent, so an image handle can react to hover and drag like a drawn
  one.
- **UltraCanvasSplitPane**: **action icons on the split line**. Any number of
  icons per splitter (`AddSplitterIcon`, `SetSplitterIcons`,
  `InsertSplitterIcon`, `RemoveSplitterIcon`, `ClearSplitterIcons`), each with
  an image or text glyph, tooltip, enabled/visible state and its own click
  handler, plus a pane-level `onSplitterIconClicked`. Icons are centred across
  the line and grouped along it, either inside the handle (which auto-sizes to
  wrap them) or at their own `SplitterIconStyle::position`. Pressing an icon
  does not start a drag: the click fires on release over the same icon, the
  cursor turns into a hand, and a disabled icon dims and swallows the press.
  Icons live on the split pane rather than on the splitter objects, so they
  survive pane insertion and removal.
- **UltraCanvasSplitPane**: `SplitPaneStyle::splitterHitMargin` is wired up -
  it now widens the grab strip on each side of the line without thickening the
  painted line. New guide `Docs/UltraCanvas/UltraCanvasSplitPane.md` (the demo
  already pointed at it) and three new demo sections covering handle shapes,
  icons in a capsule handle, and icons on a bare vertical split line.

#### 2026-08-01 *0.3.23*
- **UltraCanvasDendrogram** *(1.5.1)*: fixed radial leaf labels rendering upside
  down in the upper-right quadrant. The left/right half test was
  `angle > -pi/2 && angle < pi/2`, which assumes angles in `(-pi, pi]`, but
  `DendrogramLayoutEngine::ApplyRadialLayout` produces `[0, 2pi)` — so every leaf
  between `3pi/2` and `2pi` failed the test, took the left-half branch and picked
  up an extra 180-degree rotation. Both the leaf labels and the group arc labels
  now test `cos(rotation)`, which is precisely the condition for "this text is not
  upside down" and does not depend on which angle range the layout uses. Audited
  the other radial components at the same time — Sunburst, Chord, RadialBar,
  CircularInfoGraphic and PolarChart all normalise correctly and were unaffected.

#### 2026-07-31 *0.3.22*
- **UltraCanvasNodeDiagram** *(2.2.0)*: organizational-network features. Node
  size can now be driven by the data - `NodeSizeMode::ByDegree` sizes a node
  from its connection count, `ByValue` from a new `NodeDiagramNode::value`
  field, both through a sqrt transfer so node AREA tracks the quantity rather
  than the diameter. Degrees can count total / incoming / outgoing links and are
  cached, so bulk-loading a graph costs one rebuild instead of one per
  `AddLink`. New `NodeDiagramGroup` cluster containers wrap a set of member
  nodes in an auto-fitted boundary box (solid or dashed, optional fill and
  corner radius, title in any corner) that follows its members through dragging
  and re-layout; boxes draw behind the links and titles after the nodes, both
  sized in screen pixels so they hold up at any zoom. `SetGroupCohesion()` adds
  a per-group centroid attraction to the force-directed layout - without it
  repulsion scatters a cluster and the boxes overlap into mush. New color legend
  overlay (`NodeDiagramLegendConfig`, `BuildLegendFromGroups()`) drawn in screen
  space in any corner. Groups and sizing round-trip through `ToJson()` /
  `FromJson()`, and group boxes are included in `ComputeContentBounds()` so
  `FitView()` and the minimap account for them. New demo tab (Diagrams > Node
  Diagram > Organization): a five-department company network with a control
  column for layout, sizing mode, degree mode, cohesion, link style, node shape,
  theme, and toggles for the boxes, legend, grid, minimap, controls and snap.
- **UltraCanvasDendrogram** *(1.5.0)*: hierarchical edge bundling (Holten 2006).
  `AddRelation()` registers a leaf-to-leaf association that does not follow the
  tree's parent/child structure; each one is routed through the tree path source
  -> lowest common ancestor -> target, relaxed toward the straight chord by
  `SetBundlingStrength()` (beta), and smoothed with a clamped cubic B-spline. A
  radial dendrogram can now show its hierarchy and the cross-links between its
  leaves at the same time without becoming a hairball. Also new: area-proportional
  node dots via `DendrogramNodeSizeMode::ByValue` and `DendrogramNode::nodeValue`,
  normalised against the largest value in the tree - replacing the single global
  `style.leafNodeRadius` as the only leaf size available.
- **UltraCanvasJitterPlotElement** *(1.3.0)*: per-point encodings. A parallel
  vector of size magnitudes plus `JitterPointSizeMode::ByValue` turns a beeswarm
  into a bubble beeswarm, with the packer receiving the real per-point radii so
  mixed sizes pack without overlapping. A parallel vector of color values plus
  `JitterPointColorMode::ByValue` samples any `UltraCanvasColormap` palette,
  including the diverging ones with a configurable midpoint, so a signed quantity
  reads correctly around zero. Fixes: `minScoreFilter` defaulted to `0.0` and
  silently discarded every negative value before rendering; `AddCategoryData()`
  did not invalidate the point-position cache, so a second call left the previous
  points on screen; `RenderJitterPoints()` always drew a plain circle and ignored
  both `SetPointShape()` and the point edge style.

#### 2026-07-31 *0.3.21*
- **UltraCanvasTimelineChart**: added the swimlane grouping mode
  (`TimelineLaneMode::Swimlanes`). The same date axis and the same entries, with
  rows given identity: one named band per workstream, a name column on the left
  and a tinted background per row. Rows are declared with `SetSwimlanes()` or
  derived from the distinct `TimelineChartEntry::swimlaneName` values in
  first-appearance order; milestones move inside their band with the label
  beside them; the axis is forced to the top. Each band sub-packs with the same
  packer and the bands share one height budget - rows are compressed before any
  sub-row is dropped, so a busy row can keep three sub-rows while quiet rows
  keep one. New `SwimlaneProgram()` / `ProgramSwimlanes()` samples and a
  Swimlanes demo tab with a row-grouping control.
- **UltraCanvasTimelineChart**: the lane packer now tracks each row's full
  interval list instead of only its right edge. Point events are placed in
  importance order rather than date order, so with a single right edge an event
  earlier than everything already placed could not be inserted and ended up
  overlapping a bar. Affects packed mode as well as swimlanes.
- New **UltraCanvasTimelineChart** element
  (`Plugins/Charts/UltraCanvasTimelineChart`): the chronological counterpart to
  the timeline diagram — milestones and spans placed to scale on a real date
  axis, with no task table and no dependency graph. Four entry kinds
  (milestone, span, era band, project bookend), eight marker styles, four bar
  styles, open-ended spans, uncertain dates, span progress, and five design
  presets (`Modern`, `Classic`, `Minimal`, `Roadmap`, `Dark`) over the usual
  palettes and dark theme. Spans and milestone callouts share one shelf packer
  per side of the axis, so a label is never drawn on a bar; when a side runs
  out of room the least important labels are dropped rather than overprinted,
  and the marker is always kept. Wheel zoom anchored on the cursor's date, drag
  pan, double-click to refit, selection, tooltips and callbacks. New demo page
  (Info Graphics > Timeline Chart) and guide in
  `Docs/UltraCanvas/UltraCanvasTimelineChart.md`.
- New header-only **UltraCanvasTimeAxis**
  (`include/Plugins/Charts/UltraCanvasTimeAxis.h`): date<->pixel projection,
  automatic scale resolution (minutes through decades) from the current
  pixels-per-day, two-tier tick generation with Monday-based weeks and
  calendar-correct month/quarter/year stepping, and cursor-anchored zoom. Day
  serials match `GanttDate::serial`, so the axis, the Gantt chart and the
  timeline elements share one date representation.
- New **UltraCanvasTimelineDiagram** element
  (`Plugins/Diagrams/UltraCanvasTimelineDiagram`): the narrative timeline
  infographic — an ordered list of events laid out along a decorative path,
  with nine design presets (`Bar`, `Line`, `Alternating`, `Cards`, `Vertical`,
  `Serpentine`, `Hanging`, `Chevron`, `Steps`). Items carry a period caption,
  title, paragraph and icon glyph; cards, boxes and bubbles size themselves to
  their text, and the `Hanging` design wraps bubble text to the circle and
  staggers bubbles over several tiers so they never collide. Palettes
  (`CorporateBlue`, `Vibrant`, `Pastel`, `Ocean`, `Sunset`, `Forest`, `Slate`,
  `Mono`, custom), `PerItem`/`Single`/`GradientAlongPath` color modes, a dark
  theme, side policies, reversible direction, a "current position" pending
  style, an independent scale-label track, hover/selection/tooltips with
  callbacks and node/content geometry queries. `TimelinePlacement::Proportional`
  positions items by real dates (serials compatible with `GanttDate`) while
  keeping the decorative design. New demo page (Info Graphics > Timeline
  Diagram) and guide in `Docs/UltraCanvas/UltraCanvasTimelineDiagram.md`; the
  research behind splitting narrative timelines from date-accurate ones is in
  `Docs/UltraCanvas/UltraCanvasTimelineDiagramProposal.md`.
- Fixed duplicated Trim and base64 code.

#### 2026-07-30 *0.3.20*
- **UltraCanvasScatterPlotElement**: correlation / trend line display. The
  element can now fit a least-squares regression line over its data
  (`SetShowTrendLine`), styled solid, dashed or dotted with settable colour
  and width, plus an optional readout of the fitted equation and the Pearson
  correlation (`SetShowCorrelationInfo` draws `y = ax + b`, r and r² in the
  plot corner). `ComputeLinearRegression` and `GetCorrelationCoefficient`
  expose the fit programmatically. Points whose `ChartDataPoint::color` is
  set now render in that colour, so outliers or categories can be marked
  without extra elements.
- New **UltraCanvasScatterPlot3DElement**
  (`Plugins/Charts/UltraCanvasScatterPlot3D`): a software-rendered 3D scatter
  plot for (x, y, z) point clouds. Perspective camera with drag-to-orbit,
  wheel zoom and view presets; perspective axes with ticks, titles and an
  optional ground grid that re-anchor to the corner nearest the camera;
  depth cueing (perspective point sizing plus optional fade towards the
  background); per-point colours; hover tooltips with X/Y/Z. An optional 3D
  correlation line — the principal axis of the cloud, i.e. the orthogonal
  least-squares fit from the covariance matrix's dominant eigenvector — is
  depth sorted into the points so it threads through the cloud with correct
  occlusion. `GetCorrelationLine` returns the fitted centroid and direction
  in data space. The Charts > Scatter Plot demo page now shows both the 2D
  trend line and the 3D cloud side by side, and a programmer's guide lives in
  `Docs/UltraCanvas/UltraCanvasScatterPlot3D.md`.

#### 2026-07-30 *0.3.20*
- New **UltraCanvasGitGraph** element
  (`Plugins/Diagrams/UltraCanvasGitGraph`): renders a Git commit history — a
  DAG of commits decorated with branch, tag and `HEAD` refs. Two layout
  families share one data model: **Lanes**, one lane per open line of
  development with the newest commit first (the gitk / GitKraken / SourceTree
  repository view), and **Swimlane**, one band per branch either side of a
  nominated trunk (the git-flow teaching diagram). Lane assignment is a single
  active-lane sweep with two strategies — `Stable` keeps a branch on one lane
  for its whole life (straight branches), `Compact` recycles freed lanes
  (narrow graphs) — plus trunk pinning so a nominated branch always holds lane
  0. Handles merges, octopus merges, multiple roots, boundary commits whose
  parents lie outside the loaded window, and cherry-picks (dashed non-parent
  edges). Four orientations, four commit orderings (as-given, commit date,
  author date, topological), four edge routings (orthogonal with rounded
  corners, Bezier, arc, straight), ref chips with `+n` overflow, per-commit
  changed-file boxes with leader lines, callout annotations, an arrowheaded
  trunk baseline, six themes, virtualised rendering (only rows in the viewport
  are drawn), zoom/pan/selection/tooltips/keyboard navigation and SVG export.
  Ingest is programmatic, from `git log` output (`LoadFromGitLog` +
  `GitLogFormat`), or via the authoring API (`Branch`/`Commit`/`Merge`/
  `CherryPick`/`Tag`) — the element is read-only and never executes git.
  The layout core (`UltraCanvasGitGraphLayout`) is headless and covered by
  `Tests/GitGraphLayoutTest.cpp` (11 cases, including 200 randomised DAGs
  checked against the placement invariants). New demo page (Diagrams > Git
  Graph), guide in `Docs/UltraCanvas/UltraCanvasGitGraphExamples.md`, research
  write-up and roadmap in `Docs/UltraCanvas/UltraCanvasGitGraphProposal.md`.
- **UltraCanvasGitGraph**: second feature pass. **Lazy loading** —
  `IGitGraphDataSource` pages a history into the element as the viewport nears
  the end of what is loaded. **Mermaid I/O** — a headless
  `UltraCanvasGitGraphMermaid` unit imports and exports the `gitGraph` DSL
  (commit/branch/checkout/switch/merge/cherry-pick, the id/tag/type/msg/order/
  parent attributes, `%%` comments, the `%%{init: ...}%%` header and the
  LR/TB/BT direction suffixes), reporting parse errors with a line number.
  **Native `.git` reader** — new `UltraCanvasGitRepository` (core) reads refs
  (loose, `packed-refs`, annotated tags peeled), loose objects and packfiles
  including `OFS_DELTA`/`REF_DELTA` chains, inflating with the vendored miniz;
  no git executable is spawned and no dependency is added, and
  `UltraCanvasGitRepositorySource` adapts it to the lazy loader. **Filtering**
  by text, author, path, branch, date range and merge status, with edges through
  filtered-out commits re-pointed at the nearest surviving ancestor and drawn
  dashed. **Crossing reduction** — lane columns reordered by a barycentre
  heuristic that keeps the best measured permutation (about a third fewer
  crossings over 300 randomised layouts, never worse), budgeted and off by
  default. **Commit table pane** row-aligned beside the graph with configurable
  columns and a date formatter, plus a row-alignment API for pairing an external
  `UltraCanvasTableView`. **Search** over sha, subject, author and refs with
  next/previous navigation. **Minimap** with a viewport rectangle and
  click/drag navigation. New tests: `GitGraphMermaidTest`, `GitRepositoryTest`
  (runs against a real repository) and six more `GitGraphLayoutTest` cases.
- **UltraCanvasGitGraph**: third feature pass. **Time-proportional axis** —
  row position follows the commit timestamp, with a per-gap floor and ceiling so
  a burst of same-second commits stays readable and a multi-year quiet period
  costs one large gap instead of an unusable amount of empty axis; an optional
  date ruler prints one label per calendar day. **Collapsing** — a run of plain
  single-parent commits folds into one dashed pill labelled with how many
  commits it stands for, expandable by double-click; refs, merges, roots and
  cherry-picks are never folded. **Parallel rows** — commits made at the same
  moment share a row (mermaid's `parallelCommits`), but only when neither is the
  other's parent and they sit in different lanes, so no edge is ever flattened.
  **Badges** for GPG signature and build status, **stash chips**, **author
  avatars** (initials on a colour derived from the author) in the table pane,
  **label collision avoidance** so overlapping text is dropped rather than
  stacked, and **JSON export** of the laid-out geometry. Four more layout tests
  cover collapsing and parallel rows. Note: `GitGraphSignature` /
  `GitGraphBuildStatus` use `NoSignature` / `NoStatus` and `Passed` / `Failed`
  rather than `None` and `Success` / `Failure`, because X11's `X.h` defines
  `None` and `Success` as macros and the header reaches the window backend.
- **UltraCanvasGitGraph**: fourth feature pass. **Explicit lane ordering** —
  `SetLanePriority()` puts named branches in the columns you choose, applied
  after crossing reduction so it always wins over the heuristic; a pinned trunk
  keeps column 0 and unlisted branches keep their relative order. **Diff pane**
  across the bottom of the element: a header naming the commit, the files it
  touched coloured by A/M/D status, and the patch for the selected file coloured
  by unified-diff prefix, each half scrolling independently. The element never
  computes diffs — `SetFileListProvider()` and `SetDiffProvider()` supply them.
  **Drag to author** — dragging a commit onto another adds a merge on the
  target's branch, dragging into empty space branches off it; a drag that never
  moved is just a selection, and a merge duplicating an existing parent link is
  refused. Also `GetCommitScreenPosition()` for anchoring popovers to a node.
- **UltraCanvasGitRepository**: `ReadChangedFiles()` produces a commit's changed
  files by recursively diffing its tree against its first parent's (a root
  commit reports its whole tree as added), handling files replaced by
  directories and vice versa. Verified against `git show --name-status` on this
  repository, including merges. Blob-level diffing is not implemented.
- **VirtualFS / UltraCanvasFilerWidget**: fixed archives always listing as
  "(empty folder)" on Windows. `VirtualFSPath::Resolve()` prefixed a slash to
  the real-filesystem part of every absolute path, turning a drive-letter path
  like `C:/Users/…/archive.zip` into `/C:/Users/…/archive.zip` — a path no
  provider could open, so double-clicking any ZIP (or other archive) in the
  filer showed an empty view. Drive-letter paths now keep their bare `C:`
  prefix through resolution, and `Normalize()` treats them as absolute so
  `..` components can no longer escape above the drive root. The filer also
  distinguishes an unreadable archive from a genuinely empty one: when the
  provider layer cannot open the archive (missing format provider, corrupt or
  password-protected file), it reports "Cannot read archive: …" through the
  widget's error callback instead of silently rendering "(empty folder)".
  New header-only regression test `Tests/VirtualFSPathTest.cpp` covers Unix,
  relative, backslash and drive-letter archive paths, including nested
  archives.
- New **UltraCanvasCircularProgressChart** element
  (`Plugins/Charts/UltraCanvasCircularProgressChart`): the angle-encoded
  member of the circular chart family — every ring carries one independent
  value drawn as an arc whose sweep is proportional to that value within the
  ring's own range (concentric "activity rings"). Sub-styles cover the single
  thick progress ring and the progress pie (filled sector over a track disc).
  Rings take their colour from a palette or per-ring override, draw the
  remainder as an auto-tinted, explicit or hidden track, and end in round or
  butt caps. Labels: percentage/value callouts at each arc tip (optionally in
  a bubble), ring names inside the band at the arc start, or a stacked column
  of `label value` rows aligned with each ring's start point. A centre disc
  with title + subtitle, a numbered-chip or swatch legend on any side (with
  optional per-ring icons), configurable start angle and winding direction,
  hover highlighting, tooltips and click/hover callbacks complete the P1
  feature set. New demo page (Charts > Circular Progress Chart), programmer's
  guide in `Docs/UltraCanvas/UltraCanvasCircularProgressChart.md`, plus the
  family-wide guide `Docs/UltraCanvas/UltraCanvasCircularCharts.md` and the
  previously missing `Docs/UltraCanvas/UltraCanvasCircularInfoGraphic.md`.
- **UltraCanvasPieChartElement**: family-standard controls added —
  `SetStartAngle` / `SetClockwise` for orientation and winding,
  `SetCenterKPI(text, caption)` drawn in the donut hole, and
  `onSliceClick` / `onSliceHover` callbacks.

#### 2026-07-29 *0.3.19*
- New **UltraCanvasPolarChart** element
  (`Plugins/Charts/UltraCanvasPolarChart`): a general polar coordinate chart
  where every observation is an (angle, radius) pair. One element covers the
  whole family of round plots built on that system — polar scatter, line,
  spline, area, spline area and columns (the Nightingale rose / stacked polar
  column chart) — and the types can be mixed in a single chart. The angular
  axis works in numeric mode (each point carries its own angle, mapped through
  a configurable domain) or categorical mode (one evenly spaced slot per
  category, placed either on the grid spokes or between them), with settable
  zero angle, winding direction, sweep angle for fans and wedges, automatic or
  explicit tick intervals, horizontal/tangential/radial label orientation and
  an optional second ring of angular labels with its own interval and side.
  The radial axis supports linear, logarithmic and square-root (area-true)
  scales, automatic "nice" ticks or a manual range, negative minima, reversed
  direction, a donut hole and unit-suffixed labels on a selectable spoke.
  Circular or polygonal spider-web grids, minor rings, alternating ring
  shading, radial tolerance bands and angular sector bands render behind the
  data; column and area series support stacked and percent-stacked modes while
  unstacked columns group side by side inside their slot. Interaction covers
  hover highlighting, tooltips, click/hover callbacks, click-to-toggle legend
  entries in four positions and optional drag-to-rotate. New demo page
  (Charts > Polar Chart) with six examples and a live control panel, plus a
  programmer's guide in `Docs/UltraCanvas/UltraCanvasPolarChart.md`.

#### 2026-07-28 *0.3.18*
- **UltraCanvasMediaViewer**: the folder path strip now uses the same path
  mechanism as the filer. The shared builder `BuildFolderBreadcrumb()` (plus
  `ListDriveRoots()` and `FolderBreadcrumbOptions`, declared in
  `UltraCanvasBreadcrumb.h`) fills a breadcrumb with a leading **"Computer"**
  node whose dropdown lists every drive / mounted volume, the drive (or root)
  node, and one node per folder. The media viewer built its own strip before, by
  iterating the whole `std::filesystem::path`, which on Windows turned the root
  separator into a node of its own (`C:` → `\` → `Users` → …) and offered no way
  to reach another drive. The filer demo's private copy of the logic is gone —
  both now call the shared builder, so a path strip behaves identically wherever
  it appears (segment click browses the folder; the segment dropdown lists the
  sibling folders at that level).
- **UltraCanvasBreadcrumb**: fixed long paths overflowing the strip instead of
  collapsing when an interlocking item style (`Arrow` / `Parallelogram`) was
  used — as seen on the media viewer, whose last folder was clipped at the right
  edge with no `...` menu. Those styles butt their segments together and add the
  notch depth (`arrowSize`) per neighbour, but overflow handling was still
  costing a separator plus its spacing (0 for both presets), so the strip
  under-measured itself by `arrowSize × segments` and decided everything fitted.
  Measurement, collapse and slot building now share one per-neighbour cost, and
  the `...` placeholder carves the same left notch as any other segment.
- **UltraCanvasBreadcrumb**: min-content width in `Collapse` mode is now the
  collapsed floor (kept first item + `...` + the trailing items the style keeps)
  instead of the full uncollapsed path, so a parent layout can shrink the strip
  to its own width rather than being widened by a deep path. Other overflow
  modes keep every item and are unchanged.
- New **UltraCanvasSWOTDiagram** element
  (`Plugins/Diagrams/UltraCanvasSWOTDiagram`): classic four-panel SWOT
  analysis infographic rendering four text item lists (Strengths, Weaknesses,
  Opportunities, Threats) in six design presets — corner-badge panels with a
  central SWOT circle, classic 2x2 matrix (optional internal/external and
  helpful/harmful axis captions), separated header-bar cards, central letter
  diamond, stacked rows with big letter blocks, and four columns with header
  chips. Light/dark theme, per-quadrant titles/badges/accent colors,
  hover tooltips, item selection with callbacks and built-in sample data.
  New demo page (Info Graphics > SWOT Diagram) with six tabbed designs and
  runtime theme/decoration/data controls, plus a programmer's guide in
  `Docs/UltraCanvas/UltraCanvasSWOTDiagramExamples.md`. A survey of the
  SWOT presentation styles found in the wild is in
  `Docs/UltraCanvas/SWOTDiagramDesignVariants.md`.
- Fixed slow video thumbnail generation in MacOS in Album control

#### 2026-07-25 *0.3.17*
- **UltraCanvasFilerWidget**: double-click vs rename behavior corrected
  (0.3.16 had it wrong). Double-clicking an entry — name or icon — now always
  opens/activates it: folders and compressed archives are entered, files fire
  `onFileActivated` (executable start / open with the designated program).
  The inline rename is instead triggered Windows-style: a single click on the
  **name** of the entry that is already the only selected one opens the rename
  editor after a short delay (500 ms, longer than the double-click interval, so
  the first click of a double-click never starts a rename). A drag, a
  double-click, a key press, a refresh or a folder/view change cancels the
  pending rename.
- **UltraCanvasFilerWidget**: double-clicking a compressed archive now really
  opens it like a folder (it always listed as empty before). Three fixes:
  - The widget listed archive interiors with `VirtualFS_ListDirectory()`
    without ever initializing VirtualFS, so no archive provider was registered
    and every archive came back empty. `ScanFolder()` now runs
    `UltraCanvasVirtualFSBridge::Initialize()` (idempotent) before listing.
  - VirtualFS entries carry archive-internal paths (`sub/file.txt`); the
    widget stored them as the entry path, so descending into a folder inside
    an archive navigated to a nonsense path. Entry paths are now built as
    `<current folder>/<name>`, giving full virtual paths
    (`/path/archive.zip/sub`) that also work for nested archives.
  - Without the VirtualFS module in the build, activating an archive now
    fires `onFileActivated` like any other file instead of navigating into a
    permanently empty view.
- **VirtualFS (libarchive provider)**: archives that store no explicit
  directory headers (Python `zipfile`, several archivers) now show their
  subdirectories. The entry cache synthesizes a Directory entry for every
  path ancestor implied by a member (`sub/b.txt` ⇒ `sub`), so subfolders
  appear when listing the parent and can be descended into; an explicit
  directory header arriving later replaces the synthesized entry's metadata
  without duplicating it in the listing.
- New **UltraCanvasQuadrantChart** element
  (`Plugins/Charts/UltraCanvasQuadrantChart`): interactive 2x2 strategic
  matrices with presets for SWOT, BCG, Ansoff, Eisenhower, Gartner magic
  quadrant, risk and priority frameworks plus fully custom quadrant
  labels/colors/axis captions. Data points support per-point color, radius
  (BCG-style bubbles), shape (circle/square/triangle/diamond) and outline;
  hover tooltips, click (multi-)selection, double-click callbacks and
  per-quadrant statistics utilities are built in. New demo page
  (Charts > Quadrant Chart) with six tabbed examples and runtime style/data
  controls, plus a programmer's guide in
  `Docs/UltraCanvas/UltraCanvasQuadrantChartExamples.md`.

#### 2026-07-22 *0.3.16*
- **UltraCanvasFilerWidget**:
  - Default display font reduced from 13 to 12 px (Windows standard 9pt @ 96dpi).
  - The inline rename editor now uses the same font size as the on-screen name
    for the current view (base size in the row views, the small size in the
    thumbnail / treemap captions) instead of a fixed larger size.
  - Double-clicking a file's **name** now starts an inline rename; double-clicking
    its **icon** (or, in Details view, another column) still opens/activates the
    entry.
- eBook reader: the table-of-contents toolbar button now uses the
  `list-ordered` icon (drawn as a mask so it takes the button's text color)
  and highlights while the TOC pane is open (accent fill with a light icon),
  so its active state is visible. It reverts to the normal toolbar-button
  look when the pane is hidden.

#### 2026-07-22 *0.3.15*
- **UltraCanvasFilerWidget**: new **Display > Dataset** submenu with toggles for
  extra per-file facts shown under the name in the thumbnail views — Size, Edit
  date, Creation date, Attributes, Length (audio/video) and Dimensions
  (bitmaps). Each enabled field adds a caption line (Length/Dimensions only
  appear on the file kinds they apply to); tiles grow to fit and the grid stays
  aligned. Also available programmatically via `SetDatasetField()` /
  `SetDatasetFields()` with the new `FilerDatasetField` flags.

#### 2026-07-22 *0.3.14*
- **UltraCanvasFilerWidget**: picking a format from the context menu's
  "Compress" submenu now opens a modal compress dialog instead of creating the
  archive immediately. The dialog shows:
  - the archive's file-type icon on top,
  - an editable file name (with the format's extension shown as a suffix),
  - the destination folder as smaller, separate text.

  The icon can be **dragged onto any folder in the view** to retarget the
  destination path — the folder under the icon highlights while dragging, and
  dropping on it updates the "Location". Enter / the Compress button creates the
  archive; Esc / Cancel dismisses. Because the icon must be droppable onto the
  folders behind it, the dialog is an in-widget overlay rather than a separate
  top-level modal window.

#### 2026-07-22 *0.3.13*
- **UltraCanvasFilerWidget**: the right-click context menu now closes on a
  left click anywhere outside it. Previously the popup registered the whole
  Filer widget as its `popupOwner`, and the window's dismissal logic treats a
  click on the owner as "inside" the popup — so clicking in the file view left
  the menu stuck open. The context menu no longer sets an owner, so any click
  outside the menu bounds dismisses it.
- **UltraCanvasFilerWidget**: the context menu's "Compress" entry is now a
  submenu listing the available archive formats — ZIP, 7-Zip, TAR, TAR+gzip,
  TAR+bzip2, TAR+xz and TAR+Zstd. `CompressSelection(extension)` takes the
  target extension (default `zip`) which selects the format.
- **VirtualFS (libarchive provider)**: `CreateArchive` now recognises compound
  archive extensions (`.tar.gz`, `.tar.bz2`, `.tar.xz`, `.tar.zst`, `.tar.lz4`)
  when picking the format and filter. It previously inspected only the final
  token (e.g. `gz`), which selected the ZIP format and then layered a gzip
  filter on top, producing a corrupt archive for those names.
- Fix two MOBI/KF8 eBook rendering bugs (seen with the DemoApp eBook demo on
  `media/ebooks/Game-of-rat-and-dragon.mobi`):
  - **Drop caps.** Mobipocket/Project-Gutenberg books set the decorative
    first letter of a section as a floated image
    (`<div class="figleft"><img alt="P"/></div>` in front of the paragraph).
    The layout engine has no CSS `float`, so each big letter stacked as a
    centred block *above* its paragraph instead of leading it. The MOBI
    engine now folds such single-letter drop-cap figures into a large inline
    first letter at the start of the following paragraph, so "P" reads in
    front of "inlighting" as intended. Genuine illustrations (multi-character
    or empty `alt`) are left untouched as block images.
  - **Inline table of contents.** kindlegen/calibre append the book's own
    "Table of Contents" page as the last part of the file, so it appeared at
    the very end of the chapter list. It is now moved to the second page,
    right after the cover/title image, where readers expect it.
- **UltraCanvasListView now supports variable row heights.** The delegate
  hook `IItemDelegate::GetRowHeight(model, row)` — previously declared but
  never consulted — is now wired into the view when variable mode is enabled
  via `SetVariableRowHeights(true)`. Every row can report its own height; the
  view keeps a lazily-rebuilt prefix-sum of row tops so scrolling,
  hit-testing (`GetRowAtY`), `GetRowRect`, `EnsureRowVisible`, `ScrollToRow`,
  culling and Page Up/Down navigation are all height-aware. Uniform rows stay
  the default and keep their original arithmetic fast path (no table). Call
  `InvalidateRowHeights()` when a custom delegate's sizing changes without a
  model signal (e.g. an async delegate finished measuring a row).
- **UltraCanvasFilerWidget shrinks thumbnail rows to fit landscape images.**
  In the thumbnail grid views the tiles are square (the selected Small /
  Medium / Big / Maximized edge), which leaves a tall empty band above and
  below wide photos. A grid row whose images all display shorter than the
  tile edge is now shortened to the tallest image actually shown in it; a row
  that holds any full-height item (a folder, a generic-glyph file, a
  vector/portrait/square or not-yet-measured image) keeps the full edge.
  Natural image sizes come from the existing header-only probe (no decode),
  cached per file. Controlled by `SetShrinkThumbnailRows(bool)` (default on).

#### 2026-07-21 *0.3.12*
- Fix GIF export failing with `magicksave: libMagick error:
  NoEncodeDelegateForThisImageFormat 'gif'` (seen on the DemoApp bitmap
  performance-comparison page). When libvips has no native cgif `gifsave`,
  the previous fallback routed the write through ImageMagick's `magicksave`,
  but ImageMagick's GIF *coder* is itself an optional build-time delegate —
  on systems without it the save threw at run time. GIF export no longer
  depends on either cgif or ImageMagick: a bundled, dependency-free GIF89a
  encoder (`libspecific/Cairo/UltraCanvasGifEncoder.h`, median-cut
  quantisation + LZW, like the existing bundled BMP/QOI encoders) is used
  whenever native `gifsave` is unavailable. It honours the requested colour
  depth and interlacing and keeps 1-bit transparency for RGBA sources.
  Both `UCImageRaster::Save` and PixelFX `SaveGif` route through it.
 
#### 2026-07-20 *0.3.11*
- Make work VTracer/Vectorizer plugin.
- Implement RemoveFromCache() method for images used for reload
- OCR plugin now supports **all Tesseract languages**, not just the bundled
  English pack. The full upstream catalogue (~130 languages) is exposed via
  `UltraCanvasOCR::SupportedLanguages()`, and any language's `traineddata` is
  fetched on first use instead of having to be pre-bundled:
  - `EnsureLanguages({codes}, err, tier)` seeds each requested pack from a
    local copy when one exists, otherwise downloads it (via UltraNet),
    consolidates them into a single directory so Tesseract can load them
    together, and reconfigures the engine.
  - `DownloadLanguage(code, tier, err)` fetches a single pack; `OCRDataTier`
    picks the source repo (`Fast`→tessdata_fast, `Standard`→tessdata,
    `Best`→tessdata_best).
  - `InstalledLanguages()` / `IsLanguageInstalled(code)` report what is
    present locally; downloaded packs are cached once under
    `LanguageDataDir()` (per-user data dir) and discovered automatically by
    the Tesseract engine's data-path resolver.
  - On a build without network support, packs can be dropped into the
    per-user directory manually and are picked up the same way.
- The DemoApp OCR screen gains a language dropdown populated from the full
  catalogue; languages that are not installed yet are marked and downloaded
  on demand when "Run OCR" is pressed.
#### 2026-07-19 *0.3.10*
- Fix GIF export failing with `VipsOperation: class "gifsave" not found` on
  builds whose libvips lacks cgif (the MSYS2/Windows package is built with
  `-Dcgif=disabled`). `UCImageRaster::Save` and PixelFX `SaveGif` now probe
  for the native `gifsave` operation and fall back to the ImageMagick bridge
  (`magicksave` with `format=gif`), which the Windows package already ships.
- Image export errors no longer include stale libvips messages from earlier
  operations (e.g. recoverable HEIF "bad seek" noise appearing inside a GIF
  save failure): the libvips error buffer is cleared before each save.
- `UltraCanvasLabel` now renders its text vertically centered by default
  (`LabelStyle::verticalAlign` and the `SetAlignment()` vertical default
  changed from `Top` to `Middle`), so labels line up with the text of
  neighbouring buttons/checkboxes in toolbar rows. Auto-sized labels are
  unaffected (their box hugs the text); labels that need top alignment can
  request it explicitly via `SetAlignment(h, VerticalAlignment::Top)`.
  Fixes the misaligned file-dimensions info label in the DemoApp codec
  comparison benchmark toolbar.
- Filer widget: optional **"Compressed thumbnails"** mode
  (`SetCompressedThumbnails(bool)`, default off; toggle in the Filer demo).
  Finished thumbnails are held in memory QOI-compressed instead of as raw
  ARGB32 pixmaps (measured 6.4× smaller on the demo set; typically 2–4× on
  photos), with a 32 MB hot cache of decompressed tiles covering the
  visible + prefetch bands so scrolling still draws raw surfaces. The codec
  is a new Cairo-native QOI variant (`libspecific/Cairo/QoiPixmapCodec.h`,
  separate from the `qoi.cpp` file-format codec) that compresses the
  premultiplied ARGB32 buffer in place — bit-exact round trip (rendering is
  pixel-identical in both modes), ~140 µs encode / ~32 µs decode per medium
  tile, HiDPI device scale preserved. `GetThumbnailCacheStats()` reports
  stored vs. raw bytes for A/B comparison.
- Filer widget: thumbnail decoding is now **viewport-driven with a one-screen
  prefetch**. Only files whose tiles are visible — plus at most one viewport
  height (width in the horizontal List view) ahead in scroll direction — are
  ever decoded, so slow scrolling almost always lands on already-decoded
  tiles. The decode queue is rebuilt every frame in priority order (visible
  tiles first, prefetch band after), and pending decodes that scroll out of
  both bands are dropped from the queue — a fast flick past hundreds of
  photos decodes only what you stop at, and the tiles you are looking at
  never wait behind tiles you scrolled past.
- Filer widget: thumbnails are now loaded **asynchronously**. Opening a folder
  renders its content immediately (names, layout, info bar) with the generic
  category glyph in each tile; the real image thumbnails are decoded on
  background worker threads and fill in as they become ready, each batch
  posting one coalesced redraw via `PostToUIThread`. Previously the first
  frame of a folder blocked until every visible thumbnail was fully decoded
  on the UI thread, which froze the window for seconds on photo folders —
  in all views (the details/list icon column decoded images too). Decoded
  pixmaps land in the shared image/pixmap caches (so other consumers get
  cache hits), the widget's own bookkeeping is bounded by a 96 MB budget,
  decodes of the same file are serialized, and pending work is dropped on
  folder change / view change / widget destruction.

#### 2026-07-19 *0.3.9*
- Merge "JSON support in UltraCanvas API"
- Merge "JMAP support for UltraNet"

#### 2026-07-17 *0.3.8*
- Fix missing method implementation SetIconMaskColor() in the Button
- Fix crash in the PixelFX FloodFill demo

#### 2026-07-12 *0.3.7*
- Filer widget: in the thumbnail views (`ThumbnailsSmall`/`Medium`/`Big`/
  `Maximized`) images smaller than the tile are no longer upscaled to fill
  it — they are drawn at their original size, centered in the tile
  (`ImageFitMode::ScaleDown`). Larger images still scale down to fit as
  before, and the other views keep their `Contain` icon fitting.
- `UltraCanvasTabbedContainer`: the overflow dropdown is now disabled for
  vertical tab layouts (`TabPosition::Left`/`Right`). It rendered a faulty
  display there and was not usable for vertical tabs, so
  `CheckIfOverflowDropdownNeeded()` always returns `false` when tabs are
  vertical — the overflow button never displays or takes part in tab-bar
  layout. If the feature is accidentally switched on for vertical tabs
  (enabling the dropdown while vertical, or switching to a vertical position
  while the dropdown is enabled), a warning is emitted: "Overflow dropdown not
  supported for vertical tabs".
- Rating: fixed the built-in **Circle** symbol never showing its filled/selected
  state, so a circle rating appeared to ignore clicks (the "Circle and Square
  symbols" demo row). The filled portion of each symbol is painted by re-drawing
  the shape inside a `ClipRect` (clipped to the filled fraction); the circle was
  drawn with `FillCircle`, whose arc-based fill is not rendered inside an active
  clip region on some back-ends, so only the unclipped empty base showed and the
  rating looked unselectable. The circle is now drawn as a filled polygon via
  `FillLinePath`/`DrawLinePath` — the same clip-honouring primitive the Star uses
  — so all three built-in shapes (Star, Circle, Square) render their fill through
  a consistent code path. The disc is visually unchanged.
- Album demo: the video player window gained an info bar under the video
  surface showing the clip's title and its source link (clickable — opens in
  the system browser), and the Lola Lexy tile's link line now reads
  `youtube.com/LolaLexy`.
- Animated images (GIF / animated WebP) now play in the lightbox image viewer
  (`UltraCanvasImageViewer`): the zoom / pan surface steps them with the shared
  `UCImageAnimationController`, so zoom and pan apply to the running animation
  — matching `UltraCanvasImageElement` and the media viewer.
- Album demo: the seed list now leads with an animated GIF tile
  ("Charlie Chaplin run", `media/images/charlie-chaplin-run.gif`) that plays
  in the photo lightbox; removed the placeholder tiles "Brand Reel",
  "Chill Beats" and "Roadtrip".
- Fixed Album demo video-window bugs (merged as PR #102): closing the player
  window while a clip is playing (title-bar close button included) now stops
  playback — the demo viewer hooks `onWindowClosed` to stop the player and
  release its retained window reference, so the decode pipeline no longer
  keeps playing audio after the window is gone. Replaying a finished clip
  shows video again: `UltraCanvasVideoPlayer::Play()` rewinds to 0 after
  end-of-stream (an EOS-parked pipeline produces no data), and
  `UltraCanvasVideoPlayerElement` no longer stops its frame timer on EOS
  (Play re-arms it), so frame uploads resume instead of leaving a frozen
  surface with audio only.

#### 2026-07-11 *0.3.6*
- Hover video preview for the album widget: resting the cursor on a Video tile
  plays a short muted inline preview of the clip in place of its static poster
  frame (opt-in via `AlbumConfig::videoHoverPreview`, with configurable dwell
  delay, duration, loop, start offset, mute and preview fps). The engine behind
  it is the new reusable `UltraCanvasVideoHoverPreview`
  (`include/UltraCanvasVideoHoverPreview.h`): dwell-delayed muted playback,
  frames delivered as a ready-to-draw `UCPixmap`, self-limiting duration, one
  decode session at a time, and a silent fallback to the static thumbnail with
  the null video backend. The demo's Album page enables it on its video tiles.
  See the "Hover video preview" section in
  `Docs/UltraCanvas/UltraCanvasAlbumExamples.md`.
- Added "jump to last window": the application now keeps a most-recently-used
  window focus history and `JumpToLastWindow()` raises + focuses the window
  used before the current one, restoring keyboard focus to the input field
  that was active there; repeated triggers toggle between the two most recent
  windows. Bindable to a keyboard shortcut and/or a mouse button via
  `SetJumpToLastWindowKey()` / `SetJumpToLastWindowMouseButton()` (disabled by
  default; the demo binds F6 and the mouse Back button). The Linux and Windows
  back-ends now translate the mouse side/thumb buttons (X11 buttons 8/9,
  Windows XBUTTON1/2) as `UCMouseButton::Back`/`::Forward`. Click-to-focus now
  sends proper `WindowBlur`/`WindowFocus` events to the windows involved
  (previously the raw MouseDown event was re-dispatched to both). See
  `Docs/UltraCanvas/UltraCanvasJumpToLastWindow.md`.

#### 2026-07-11 *0.3.5*
- `UltraCanvasListView`: new cell-level callbacks `onCellClicked` and
  `onCellHovered` (row, column, cell-local position) plus the `GetColumnAt()`
  hit-test helper, so delegates can implement per-cell interactive regions
  (links, buttons) in multi-column views. Hover leaves are reported as
  (-1, -1) and the view now also resets its hover state on `MouseLeave`.
- Demo: the Dependencies & Third-Party page is now interactive. Library names
  render as links — hovering underlines them, shows a hand cursor and a
  tooltip with the website / source repository / license; clicking opens a
  popup with "Website" and "Source code" entries (or opens the site directly
  for OS frameworks without a public repository). Each library additionally
  carries its license tag after the name — e.g. (MIT), (LGPL 2.1) — which is
  its own link to the license description page on spdx.org.
  `Docs/Dependencies.md` gained the matching License column and the
  previously missing OCR / Vectorizer / LaTeX plugin libraries.
- Scrollbar: a custom handle image (`thumbImagePath` /
  `thumbImagePathHorizontal`) is no longer stretched to the thumb rectangle.
  The handle is now always scaled preserving its aspect ratio and centered in
  the thumb, so the grip keeps its shape regardless of thumb length. The
  `thumbImageFit` style field was removed accordingly.

#### 2026-07-10 *0.3.4*
- Demo: the "Networking (UltraNet)" page moved from Tools into the
  "ULTRA OS modules ▸ Ultra Net" tree entry and is now presented like the
  FileLoader module page — Overview / Details / Examples tabs, with the live
  remote-resource loader on the Examples tab.
- UltraNet: fixed https:// requests failing with "Problem with the SSL CA
  cert (path? access rights?)". libcurl bakes the CA bundle path of the
  build machine into the library; when that path does not exist on the
  machine the app actually runs on, every TLS request failed. When no
  `UltraNetConfig::caBundlePath` is set, UltraNet now discovers the system
  trust anchors at runtime — `CURL_CA_BUNDLE` / `SSL_CERT_FILE` environment
  overrides first, then the well-known distro bundle locations
  (Debian/Ubuntu, Fedora/RHEL, openSUSE, Alpine/BSD) and the hashed
  certificate directory — and passes them to libcurl.
- Rating: fixed half-step (0.5) values never displaying. `CreateHalfRating`
  applied the initial value before enabling half steps, so it was snapped to
  a whole number (e.g. 3.5 became 4) and the half-filled symbol never
  appeared. Half steps are now enabled before the value is set, so ratings
  like 3.5 render as three full symbols plus a left-half-filled one.
- Implemented GIF (and animated WebP) animation support. Animated images now
  play in `UltraCanvasImageElement` (auto-play on load, with
  Play/Pause/Stop/SetAnimationEnabled control) and in the media viewer, where
  zoom/pan/rotate/mirror apply live to the running animation and colour
  adjustments freeze it on the current frame. Frames are decoded once through
  libvips' multi-page loader into a shared, cached `UCImageAnimation`
  (per-frame delays, loop count honoured, near-zero delays shown at 100ms);
  playback is stepped by the new reusable `UCImageAnimationController` on the
  same main-thread app timer the video player element uses for its frame
  ticks. Multi-page stills (TIFF/PDF) keep displaying as static images, and
  the info popup shows the frame count for animated files. See
  `Docs/UltraCanvas/UltraCanvasAnimatedImages.md`.
- Fixed Tesseract OCR plugin

#### 2026-07-08 *0.3.3*
- ODT reader: real-world letter documents now render their letterhead
  sections. `draw:text-box` frames (sender/contact blocks) are parsed into
  regular blocks instead of being dropped; master-page headers and footers
  from `styles.xml` (bank details, register lines, region-left/center/right
  columns) are emitted before/after the body separated by a rule;
  page-anchored `draw:frame`s directly in the text flow (e.g. signature
  images) are handled; picture hrefs with a `./` prefix load correctly and
  external (linked) pictures are skipped; hidden sections
  (`text:display="none"`) and hidden text no longer leak into the output;
  text boxes anchored inside table cells flatten into line-broken cell text;
  named/automatic list styles in `styles.xml` are now honored.
- Demo: the ODT Documents page now presents the loaded document as a full
  DIN A4 page (794 x 1123 px at 96 DPI) — a white page centered on a neutral
  desk background with letter-like margins; the demo display area scrolls
  to reach the rest of the page.
- Implemented clipboard handling fort TextInput controls
- Merged "UltraCanvas arrow-key value selector"
- Merged "UC eBook renderer issues"
- Merged "UltraCanvas demo treeview fixes"
- Merged "Docusaurus integration for UltraWeb"
- Merged "UltraCanvas ODT rendering gaps"

#### 2026-07-06 *0.3.2*
- Demo: the LaTeX Documents page now typesets every document **live** from its
  `.tex` source through the on-demand UltraCanvas LaTeX engine instead of
  showing a pre-rendered screenshot. Added a set of math-mode example
  documents (`media/LaTex/math-*.tex`) that render live, and removed the
  TikZ / pgfplots and document-mode (`tabular`, `figure`) examples — which the
  math engine cannot typeset — along with their reference images. A reference
  image fallback remains in the demo for any unsupported `.tex` dropped into
  the folder later.

#### 2026-07-05 *0.3.1*
- Merged "ODT/DOCX support for file elements"
- Merged "UltraCanvas text document support"
- Merged "UltraCanvas eBook file support validation"

#### 2026-07-04 *0.3.0*
- Implemented UltraNet networking module — full v1.0 master-registry surface
  plus v1.1 extensions (`UltraNet/UltraNetCore.h`, `UltraNetHttp.h`,
  `UltraNetUrl.h`, `UltraNetWebSocket.h`, `UltraNetFtp.h`, `UltraNetSocket.h`,
  `UltraNetTls.h`, `UltraNetDns.h`, `UltraNetCookies.h`, `UltraNetPlugins.h`,
  `UltraNetSse.h`)
- HTTP / HTTPS sync + async via libcurl multi-handle worker thread; HTTP/3
  (QUIC) via nghttp3 when libcurl was built with it
- WebSocket client (libcurl native `curl_ws_*`; runtime capability check
  with a clear error when libcurl was built without `--enable-websockets`)
- FTP / FTPS / SFTP download / upload / list / delete / rename / mkdir /
  rmdir; rich listings via MLSD (with a UNIX `ls -l` fallback)
- Raw TCP / UDP sockets (POSIX sockets / Winsock)
- TLS wrap on raw TCP — OS-native backends: OpenSSL (Linux),
  Schannel (Windows, `SCHANNEL_CRED`), SecureTransport (macOS) — no extra
  TLS deps on Windows / macOS
- DNS: A / AAAA / PTR via getaddrinfo; MX / TXT / SRV / NS / CNAME / SOA via
  libresolv (Linux/macOS) and dnsapi (Windows); optional async c-ares
  backend (`ARES_OPT_EVENT_THREAD`) covering the full record set
- Sessions with `CURLSH`-backed cookie + connection-pool sharing
- Plugin system with `IUltraNetPlugin` + seven specialised category
  interfaces (mail, messaging, remote-access, directory, streaming,
  file-share, RPC); v2 host-vtable DSO contract (`UltraNet_PluginInit`) with
  v1 (`UltraNet_PluginRegister`) fallback; dynamic DSO loading via
  dlopen / LoadLibrary
- **All 17 spec plug-ins ship** in `Plugins/UltraNet/`:
    - Mail: SMTP · IMAP · POP3
    - Messaging: MQTT · AMQP
    - Remote access: SSH · Telnet
    - Directory: LDAP
    - Streaming: RTSP · RTMP · RTP (in-tree RFC 3550 receiver) · SIP
      (in-tree RFC 3261 UDP)
    - IoT: CoAP · SNMP
    - Discovery: mDNS (Avahi / Bonjour / DnsQuery_W)
    - Web modern: gRPC · WebDAV
- SSE / chunked HTTP streaming (`UltraNet_SseStream` + parser) — for
  token-by-token LLM responses
- Per-request progress callbacks alongside the global transfer-callbacks bag
- Streamed HTTP upload from disk (constant memory)
- `UltraCanvasApplicationBase::PostToUIThread(std::function<void()>)` for
  marshaling network completions back to the UI thread from background
  worker threads
- `UltraCanvasFileLoader::LoadFile(pathOrUrl)` now dispatches `http://` /
  `https://` URLs to UltraNet automatically
- Networking demo screen in the demo app (Tools → Networking) — loads a
  remote image via `UltraCanvasFileLoader::LoadFile(url)`
- UltraNet test suite (`Tests/UltraNet/`, 102 tests, in-tree framework, CI
  wired via `ULTRACANVAS_BUILD_NET_TESTS=ON`)

#### 2026-06-26 *0.2.32*
- Implemented HiDPI and scaling for all platforms
- Merge "LaTeX document renderer for UltraCanvas"
- Merge "OCR and vectorize solution for UltraCanvas"
- Merge "UltraCanvas heatmap demo presets"
- Merge "Media viewer widget for UltraCanvas"
- Merge "UltraCanvas Gauges demo fixes"

#### 2026-06-26 *0.2.31*
- Merge "UltraCanvas popup changelog link"
- Merge "UltraCanvas Gauge demo layout"
- Merge "UltraCanvas Slider demo layout"

#### 2026-06-26 *0.2.30*
- Demo: moved the **Waveform Chart** example out of *Audio Elements* and into the *Charts* category, where it belongs alongside the other data visualizations (a waveform is an amplitude-over-time plot, not a structural diagram).
- Waveform: added a **Display range** control to the waveform demo and a backing `SetVisibleWindowSeconds()` API on `UltraCanvasWaveformElement`. The view can now show the whole track ("All audio") or a trailing window that scrolls with the playhead — "Last 10 seconds" or "Last 60 seconds". Rendering, click-to-seek and the playhead all map to the visible window.
- Merge "UltraCanvas Album demo optimizations"
- Merge "UltraCanvas Gauge layout overlap"

#### 2026-06-24 *0.2.29*
- Merge "Waveform chart UltraCanvas integration"
- Merge "UltraCanvas Heatmap demo optimization"
- Merge "UltraCanvas treeview auto-scroll"
- Merge "Module infos MD diagram/image viewer"
- Changelog link on startup info page
- Merge "UltraCanvas demo Modules info pages"

#### 2026-06-24 *0.2.28*
- Fixed crash in the UltraCanvasDependenciesExamples demo screen
- In the UltraCanvasListView change model pointer to shared_ptr instead of raw pointer, prevent possible crashes

#### 2026-06-23 *0.2.27*
- Fixes Video Player issues (freeze video/audio mainly in Linux)
- Fix Video Player layout issues, wrong aligned text, use icons instead of manual drawing

#### 2026-06-23 *0.2.26*
- Docs: audited every implemented demo element for a wired-up Programmer's Guide / example doc and filled all the gaps. Added five new guides — `UltraCanvasScrollbarExamples.md`, `UltraCanvasSlideshowExamples.md`, `UltraCanvasQRCodeExamples.md`, `UltraCanvasSpreadsheetExamples.md` and `UltraCanvasXARExamples.md` — each documenting the real public API (no invented symbols) with runnable examples drawn from the matching demo source.
- Demo: wired the **C++ source** and **documentation** header icons for the elements that were missing them — Scrollbars, Spreadsheet, Slideshow, QR code and XAR now point at their example `.cpp` and new `.md`; Video and Audio now point at their existing `UltraCanvasVideoExamples.cpp`/`UltraCanvasVideo.md` and `UltraCanvasAudioExamples.cpp`/`UltraCanvasAudio.md`; and Album now links its existing `UltraCanvasAlbumExamples.md`.

#### 2026-06-23 *0.2.25*
- Gauges: added a Programmer's Guide (`Docs/UltraCanvas/UltraCanvasGaugeExamples.md`) covering the full mode-driven API — all 17 `GaugeMode`s, the round-gauge (CircularRing) style system, decorations (ranges/thresholds/external pointers/sub-dial), live clock & stopwatch controls, and a runnable code example per gauge family.
- Demo: the Gauges demo page now shows the **C++ source** and **documentation** header icons (wired its `demoSource`/`demoDoc` to `UltraCanvasGaugeExamples.cpp` and the new guide), matching every other element, plus the four tab variants (Round Gauges, Progress & Linear, Specialized, Analog) in the tree.

- Fix Windows video playback: the player loaded a file and played audio but showed no picture, and the play/pause/stop transport misbehaved (multiple clicks to stop, no resume after pause). Media Foundation's sample-grabber video sink rejected the session's rate control (`MF_E_UNSUPPORTED_RATE`) and never delivered a single frame, which also corrupted the session and scrambled the transport state. `MFDecodeSession` now decodes video through an `IMFSourceReader` (RGB32, advanced video processing) paced to the audio clock, with an audio-only Media Session providing sound + the master clock (video-only files fall back to a wall clock). Also force opaque alpha on MF RGB32 frames — the unused "X" byte was 0, which rendered fully transparent in the premultiplied Cairo `ARGB32` pixmap. Linux/GStreamer playback is unchanged.
- Video: added a cross-platform thumbnail / poster-frame API (`UltraCanvasVideoThumbnail.h`). `CaptureVideoThumbnail()` returns a single decoded frame, `CaptureVideoThumbnailPixmap()` a ready-to-draw `UCPixmap`, and `SaveVideoThumbnail()` writes a file (encoder chosen from the extension: `.qoi` → QOI, otherwise PNG — both need no libvips), e.g. for `UltraCanvasAlbum` `thumbnailPath`. Each takes a `VideoThumbnailRequest` (target time — or an automatic position — plus optional aspect-preserving `maxWidth`/`maxHeight`).
- Video: new opt-in backend capability `IVideoBackend::GrabThumbnail()`. The GStreamer (Linux) backend implements it as a throwaway `uridecodebin` pipeline that prerolls to PAUSED, seeks accurately and pulls one preroll sample (no audio, no full playback). Backends without it (null / Media Foundation / AVFoundation) use a generic decode-session fallback, so thumbnails work wherever decoding does.

#### 2026-06-23 *0.2.24*
- Merge "Color picker widget for UltraCanvas" and fix layout errors.
- Fix toolbar button width (make it auto)
- Fixed incremental search in TextArea (stop advance on each typed matched character)
- Refactor Audio element. Use composite widget instead manual draw. Use SVG icons for play/pause/etc.. buttons
  
#### 2026-06-21 *0.2.23*
- `UltraCanvasGLSurface` now resizes its render target / framebuffer to follow the element's actual bounds on every render, however the bounds were changed. Previously the framebuffer size (`surfaceWidth_`/`surfaceHeight_`) was only updated from the `SetBounds` override, so a layout-driven resize — flex/grid stretch, a parent resize, `SetElementSize`, a window resize — left the GL content stuck at its old size (it wrote `finalBounds` without routing through `SetBounds`). `Render()` now syncs the framebuffer size from `GetLocalBounds()` and forces a content re-render that pass, so GL surfaces resize correctly under any layout path (this is what made the Shaders-tab "maximize" need an explicit `SetBounds`; flexible/maximized GL surfaces now grow on their own).

#### 2026-06-21 *0.2.22*
- Fix the "maximize canvas" control in the OpenGL 3D Showcase "Shaders" tab. Three issues: (1) the toggle button was pinned to the tab content-area's right edge instead of the canvas's top-right corner, so once maximized it sat far from the (un-grown) canvas; (2) it showed a "⛶" glyph the demo font lacks (rendered as "…"); (3) clicking it did not enlarge the canvas. The canvas was being resized via `SetElementSize`, which only sets a CSS dimension — but the tab content is absolutely positioned (no layout pass re-applies it) and, crucially, `UltraCanvasGLSurface` only grows its GL framebuffer from its own `SetBounds` override. The maximize now resizes the surface with `SetBounds` (so the framebuffer actually grows to fill the tab), pins the button to the surface's current top-right corner in both states, and renders the new `media/icons/maximise.svg` icon (drawn as a white mask on the dark translucent button) instead of the missing glyph.

#### 2026-06-21 *0.2.21*
- Fix the per-effect parameter-slider captions ("Brightness", "Splat radius (px)", etc.) being partially obscured in the OpenGL 3D Showcase "Shaders" tab control panel. The sliders use an always-on `Number` value display, which `UltraCanvasSlider` draws just above the track — and since the slider pins its track to the bottom `handleSize` (16px) of its bounds, a short (22px) slider drew the value text ~10px above its own top edge, on top of the caption placed above it. The four per-effect slider groups (Ball Surface, Pulse, Fragments, Circles) now use 36px-tall sliders (handle + a value-text row + margin) and re-spaced rows, so each value number sits inside its slider below the caption with no overlap.

#### 2026-06-21 *0.2.20*
- Curated the OpenGL 3D Showcase "Shaders" tab effect list. Removed four effects together with their GLSL/source, formula headers, per-effect uniforms, state fields and parameter-slider groups: "Warp Starfield", "Rössler Attractor", "Mandala (12-wave)" and "ULTRA OS Logo". Reordered the remaining list so the three Twigl one-liners "Horizon", "Protostar2" and "Plasma Orb" appear first. The info text and `Docs/UltraCanvas/UltraCanvasGLSurfaceExamples.md` were updated to match; effects with live sliders are now Ball Surface, Pulse, Fragments and Circles.

#### 2026-06-21 *0.2.19*
- Fix the OpenGL 3D Showcase demo showing horizontal and vertical scrollbars inside every tab even when the window had room. The tabbed container was `980×690` with a 34px tab bar, so each tab's content area was only `980×656` — smaller than the tab contents, whose control panel reaches x≈986 and whose Shaders source viewer reaches y≈680. Since each tab's root is resized to the content-area bounds (`autoShowScrollbars` defaults on), the small overflow forced scrollbars. The showcase container (`1024×800`) and tabbed container (`1004×726`, content area `1004×692`) were enlarged so the content area clears every tab's children with margin; the three tab roots were updated to match. No scrollbars now appear in any tab.

#### 2026-06-20 *0.2.18*
- Breadcrumb: added a `Parallelogram` item style (`BreadcrumbItemStyle::Parallelogram` + `BreadcrumbStyle::Parallelogram()` preset) — interlocking slanted/skewed segments (outer edges of the first/last segment stay vertical) sharing the `arrowSize` skew depth with the Arrow style.
- Breadcrumb: added an optional **level indicator** — a leading numbered badge per item. `BreadcrumbStyle::showLevelIndicator` enables it; `levelIndicatorBackground` selects the badge background (`Round`, `Rectangle`, or `NoBackground`); `levelIndicatorBorder` outlines it; plus `levelIndicatorSize`/`levelIndicatorColor`/`levelIndicatorTextColor`/`levelIndicatorBorderColor`/`levelIndicatorBorderWidth`. The new `BreadcrumbStyle::Steps()` preset combines Arrow segments with round numbered badges (dark "wizard step" strip). Works with any item style.
- Demo: added "15. Numbered steps", "16. Parallelogram", and "17. Level indicators" (round / rectangle / none / bordered) rows to the breadcrumb demo page.

#### 2026-06-20 *0.2.17*
- Breadcrumb: added an `Arrow` item style (`BreadcrumbItemStyle::Arrow` + `BreadcrumbStyle::Arrow()` preset) — interlocking right-pointing arrow/chevron "steps". Each segment grows a pointed tip past its right edge that nests into the next segment's matching left notch (the first segment is flat-left, the last one's tip trails off), with the current step highlighted. New `BreadcrumbStyle::arrowSize` controls the tip/notch depth. Added a "14. Arrow steps" row to the breadcrumb demo page.

#### 2026-06-20 *0.2.16*
- Fix the Breadcrumb demo: item text was not vertically centered within the strip/pills. The element hand-rolled its vertical centering as `centerY - (int)textHeight / 2`, truncating the half-height to an integer (drifting the glyphs ~1px off the center line shared by the separators and icons) and, more importantly, never routing through the text layout's automatic centering — so it never compensated for the font's top line-leading. On fonts that split external leading above the baseline (e.g. Segoe UI on Windows) this pushed the visible text several pixels low. Breadcrumb item/overflow text now centers via the text layout's `VerticalAlignment::Middle` over the slot height (full sub-pixel precision), and `UCTextLayout::GetLayoutVerticalOffset` re-enables the top-leading compensation for Middle alignment, computed in Pango units to keep the sub-pixel offset (a previous int-pixel version had regressed Segoe UI 12pt). The compensation is a no-op where `baseline == ascent` (DejaVu/FreeSans on Linux), so other platforms are unaffected.

#### 2026-06-19 *0.2.15*
- Fixes Gstreamer build in Linux
- Fixes Gstreamer (video player) and native dialogs crash in Linux
- Merged "Add Radar Chart element"
- Fix Radar Chart rendering and animation
- Show ULTRA OS overview page when "ULTRA OS modules" tree node is selected
- Merge "Add rounded corner label examples and fix resource paths"
- Merge "Add digital clock, segmented ring, centre content, and faded colours to gauges"

#### 2026-06-19 *0.2.15*
- Added intro description for different Modules

#### 2026-06-19 *0.2.14*
- Merge "PDF Text demo page layout" fix
- Merge heatmap implementation
- Merge "Shader graphics for demo" (more shader examples)
- Merge "UltraCanvas module demo content"

#### 2026-06-18 *0.2.13*
- Fix the Gauge element's linear/progress "rounded bar" rendering at low values. `RenderLinearBar` drew the value fill with a corner radius of half the bar's thickness regardless of the fill length. `FillRoundedRectangle` does not clamp the radius, so once the fill became shorter than the bar's thickness (roughly under 10% on a wide bar) the four corner arcs overlapped and collapsed the fill into a perfect circle. The radius is now clamped to half of the fill's smaller dimension, so short fills render as a proper pill that stays inside the track.

#### 2026-06-17 *0.2.12*
- Fix two bugs in the OpenGL "Zarch" 3D demo:
- The application could not be closed while an animated (Continuous) GL surface was on screen, and the debug log kept spinning. `UltraCanvasGLSurface::Render` re-posted a full-window `Redraw` event every frame; it now invalidates only the surface's own region and stops re-arming once the window is closing/closed/hidden, so sibling widgets no longer repaint at the animation frame rate and the event loop can shut down.
- The release build crashed when opening the Zarch tab (before any 3D was drawn) while the debug build did not. The terrain/tree hash (`Hash2`) multiplied signed `int`s past `INT_MAX`, which is undefined behaviour that an optimised (`-O3`) build is free to miscompile; it now uses well-defined unsigned arithmetic. The Models/Shaders tabs do not use this hash, which is why only Zarch was affected.
- Merge "Groupbox border alignment bug" fix
- Merge "DatePicker demo layout bug" fix
- Merge "Spreadsheet save button with format options" fix
- Merge "Texter unsaved tab indicator bug" fix
- Merge "UltraCanvas audio layout improvements"

#### 2026-06-17 *0.2.11*
- Spreadsheet demo: added a "Save…" button to the toolbar that offers every format the engine can write (OpenDocument `.ods`, `.csv`, `.tsv`). Choosing a CSV/TSV name opens a new "Text Export" options dialog (character set, field separator, text delimiter, quoting policy, line ending, optional BOM) with a live text preview; `.ods` saves directly.
- Spreadsheet engine: added `SaveCSVWithOptions`/`ExportCSVToString` plus a `CSVExportOptions` struct and a `CSVEncodeFromUtf8` charset encoder (UTF-8/UTF-16/Latin-1/Windows-1252, optional BOM). Fixed `.tsv` saving to actually use a tab separator.
- Merge "PDF support", implemented PDF demo (viewer in the demo app)
- Merge "Datepicker multi-month display"
- Merge "UltraCanvas album widget improvements"

#### 2026-06-15 *0.2.10*
- Fix the spreadsheet component, editing, keys navigation, cell size changing, scrolling, loading files

#### 2026-06-14 *0.2.9*
- OpenGL surface support enabled on Windows: implemented the WGL context manager (hidden helper window + legacy-context bootstrap to load `wglCreateContextAttribsARB`, then a requested core/compatibility context). Modern GL entry points are resolved via GLEW (`mingw-w64-x86_64-glew`), since `opengl32.dll` only exports OpenGL 1.1. `ULTRACANVAS_ENABLE_GL` now defaults ON for Windows when GLEW is found.
- OpenGL surface support enabled on macOS: completed the CGL context manager (honors the requested GL version/profile and color/depth/stencil config, real extension querying via `glGetStringi`) and let `ULTRACANVAS_ENABLE_GL` default ON for macOS as well as Linux.
- Merged with the "UltraCanvas date picker widget" code
- Merged with the "UltraCanvas Album widget" code
- Merged with the "UltraCanvas Demo Slideshow layout" code
- Merged with the "UltraCanvas QR code demo page" code

#### 2026-06-12 *0.2.8*
- Slideshow demo: reworked the options panel into a labelled-row grid (label column on the left, wrapping option buttons on the right) grouped Controls / Indicator / Indicator edge / Fade style / Panel layout / Image / Letterbox fill. Each group now behaves like a radio with the active choice highlighted, the panel-layout split/overlay/off positions are grouped under sub-labels, and crop focus dims unless the Cover fit is selected
- Slideshow demo: retitled the page to "UltraCanvas Slideshow widget" and removed brand-specific references throughout the slideshow widget and its demo
- Slideshow demo: framed the live widget with a captioned "Slideshow widget" box and added a "Slideshow widget options" heading above the controls, so it's clear which part is the actual widget and which are programmer-facing options
- Slideshow: the widget now takes keyboard focus on click and supports manual navigation with the arrow keys — Left/Down go to the previous slide, Right/Up to the next (numpad arrows too)

#### 2026-06-09 *0.2.7*
- Merged and fixed the "# Spreadsheet support for UltraCanvas"

#### 2026-06-09 *0.2.6*
- Merged and fixed the "OpenGL 3D showcase demo"

#### 2026-06-09 *0.2.5*
- Slideshow: added comprehensive info-panel layouts via `SlideshowInfoLayout` — split on any of the four sides (`SplitLeft/Right/Top/Bottom`), edge overlays on the image (`OverlayLeft/Right/Top/Bottom`), corner overlays (`OverlayTopLeft/TopRight/BottomLeft/BottomRight`), `OverlayCenter`, `OverlayFull`, and `Hidden`
- Slideshow: indicators can now hug any edge via `SlideshowIndicatorEdge` (Top/Bottom rows, Left/Right stacked columns)
- Slideshow: added `SlideVertical` and `ZoomFade` transition styles
- Slideshow: configurable image fitting for mismatched images — `imageFit` (Cover auto-crop, Contain, Fill, ScaleDown, NoScale), an `imageFocus` focal point that picks which part survives a crop, and `gapFill` for letterboxed images (background color, dedicated letterbox color, or a zoomed image backdrop)
- Slideshow demo: added pickers for info-panel layout, indicator edge, image fit, crop focus and letterbox fill, plus the new transitions

#### 2026-06-07 *0.2.4*
- Fix QRCode examples page (fix wrong character and change the default QR Code generation)

#### 2026-06-07 *0.2.3*
- QR code decoder fixed (installed missing lib and configured github build)
- Merged branch "UltraCanvas QR demo field visibility bug"
- Merged branch "Barcode widget for UltraCanvas" and fix barcode widget and its demo

#### 2026-06-04 *0.2.2*
- Added Gauges, Compositor diagram, QR code
- Fixed Tabbed container and inner tabs layout
- Implemented SortChildNodes method and autoSortChildren property in the TreeView
- Fix label layout resize bug
- Make Arc diagram and Architectural Adjacency Diagram use Grid layout instead of fixed elements positions

#### 2026-06-03 *0.2.1*
- Major update. Implemented CSS Flex/Grid/Absolute layout support.
 
#### 2026-05-20 *0.1.39*
- Show cursor and allow selection in the TextArea in read-only mode
- Autodetext syntax highlighting rules by filename with auto fallbask to extension
- Fix possible bug in menu and tooltips rendering (wrong color)

#### 2026-05-19 *0.1.38*
- Implemented the SplitPane element
- Fix bug when scrollbar of outer container overlap with inner container's elements or scrollbar (mouse events incorrectly goes to inner container instead of outer container's scrollbar)
- Implemented the Barcode widget with 1D symbology encoders: Code 39 / 39 Extended / 93 / 128 (A/B/C/auto), GS1-128, EAN-13/8, UPC-A/E, ISBN-13, ITF, ITF-14 (with bearer bars), Standard 2 of 5, Codabar, MSI Plessey (4 check-digit modes), Pharmacode. From-scratch C++20 encoders, no external dependencies; live editor + gallery in Tools → Bar code

#### 2026-05-18 *0.1.37*
- Arc diagram improvement
- Change layout of "Bitmap elements" screen in the Demo app
- Use TextArea instead of Markdown element in the "Text Document/Markdown" screen in the Demo app
- Replace DejaVu default embedded font by Ubuntu font

#### 2026-05-17 *0.1.36*
- Change the Breadcrumbs element demo
- Make more Docs for different controls 
- Added Slideshow example in Widgets/Slideshow section of Demo app
- Fix for Pie Chart labels

#### 2026-05-15 *0.1.35*
- Remove JXL from Image Performance test as JXL format does not supported by currently used libvips
- Fix buttons size to fit text
- Fix bug when element is deleted but capturing the mouse or focused then app may crash
- Implemented Breadcrumbs element demo

#### 2026-05-15 *0.1.34*
- Use TextArea to show Markdown info in the Modules section
- Add more modules description to Modules section
- Implement Breadcrumb demo
- 
#### 2026-05-14 *0.1.33*
- Implemented new Image performance demo
- Fix problem with AltGr+key in Windows
- Fix ordered list content offset in MD-mode in TextArea
- Implemented Pie Chart element
- Implemented Adjacency Diagram element
- Implemented Arc Diagram element
- Implemented Breadcrumb element

#### 2026-05-12 *0.1.32*
- Implemented UltraCanvasFileLoader
- Implemented OS Recent files support (add opened files to OS Recent files list)
- Fix wrong calculation of mouse coordinates and bounds in the some diagrams 

#### 2026-05-10 *0.1.31*
- Fixed font rendering, now Windows and Linux will rendered using same included DejaVue fonts

#### 2026-05-09 *0.1.30*
- Implemented more different diagrams
- Implemented JitterChart
- Attempt to fix menu crash on MacOS

#### 2026-05-06 *0.1.29*
- Refactor Checkbox code, split to Checkbox/Redio/Switch
- Implemented more visual styles for Switch
- Attempt to fix crash on MacOS

#### 2026-05-06 *0.1.28*
- Implement support tooltips for menu itmes
- Implement maxWidth option for menu and ellipsize mode for menu items

#### 2026-05-04 *0.1.27*
- Attempt to implement MacOS HiDPI support

#### 2026-04-30 *0.1.26*
- Fix cursor position in Markdown mode in TextArea

#### 2026-04-28 *0.1.25*
- Major rework in the UltraCanvas rendering, implemented optimized rendering.
  Now popups/tooltips rendered in own surfaces (does not need to repaint main content after show/hide)
  Implemented partial rendering using dirty rectangles (for any content) 

#### 2026-04-23 *0.1.24*
- Fix tooltips rendering

#### 2026-04-23 *0.1.23*
- Some color fixes for Dark mode

#### 2026-04-20 *0.1.21*
- Refactored elements rendering/events coordinate system to use element-based coordinates where 0,0 is
  top-left element's corner instead of container-based where 0,0 was container top-left corner.
  Set clipping to element's bounds and save/restore state in container's rendering loop instead rely on element Render().
  Don't render invisible elements (hidden by scroll position)

#### 2026-04-20 *0.1.20*
- Shard very long lines to speed up TextArea on big files.
  Lines longer than 4000 codepoints are split at a break char
  (space/tab/punct) or force-split at 12000 during SetText.
  Known problems: 
    if line has no break chars (very rare case) it will splitted at 12000 char boundary,
    attempt to glue that lines (backspace or delete) will cause reshard (resplit) again
    and it will splitted at same boundary, visually it will looks like delete/backspace did not work 
- Show current line marker (red box) for cursor position
- Calculate and show real logical line numbers for split lines. 

#### 2026-04-17 *0.1.19*
- UpdateGeometry for visible childs in container only

#### 2026-04-16 *0.1.18*
- Revert back to the Sans font for Windows insetad of detecting default font. It detected the "Segoe UI" and this shit font can't be vertically centerted without special patches especially for that font, it always shifted down a little (even in browsers)
- Fixed bug with high CPU usage and slow cursor movement on the big files with very long lines.

#### 2026-04-16 *0.1.17*
- Add on-the-fly submenu regeneration to UltraCanvasMenu
- Fix Windows high CPU usage when app is idle.
- Fix main-row digit/punctuation key mappings on Linux and macOS
- Fix vertical text position issues in Windows (was shifted down a little)

#### 2026-04-16 *0.1.16*
- Full rework of display and rendering the text. Use Pango layout to format text. Allow to use variable height lines

#### 2026-04-06 *0.1.15*
- Add platform-native system font detection, replace hardcoded "Sans" defaults

