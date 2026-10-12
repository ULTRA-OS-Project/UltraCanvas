- **Rendering: SetAlpha fades fills, strokes and text, not only images.** The
  Cairo context applied the global alpha to pictures alone: a fill, a stroke
  or a line of text set the source back to its colour at the colour's own
  alpha, so whatever a caller dimmed with `SetAlpha` came out as a faded
  picture beside full-strength shapes and text. A colour's alpha is now
  scaled by the global alpha, and a gradient (or any pattern) is painted
  through a group laid down at it. What that puts right: the file display's
  dimmed entries (a file the type filter does not ask for) grey out their
  name and columns as well as the icon, as their comment always said;
  slideshow transitions fade the letterbox and the caption; a locked or
  hidden video timeline track, a dragged Kanban card and the album's drag
  marker are translucent; a connection's opacity and a vector drawing's
  group, layer and object opacity apply to its shapes and text. The
  CorelDRAW painter folded an object's opacity into its fill and stroke
  colours and also set it with `SetAlpha`; the colours now carry only their
  fill / stroke opacity, so an object is not faded twice. `RenderAlphaTest`
  reads the pixels of a fill, a stroke, a gradient and text under
  `SetAlpha`, and the render suites (vector, XAR, CDR, EPS, SVG, models,
  Filer, image, screenshots) pass as before.
- **Image export dialog: the save settings, the file's format, the colour
  depth.** `UltraCanvasImageExportDialog` opens every format on the user's
  save settings for it (`UltraCanvasFileLoader::GetImageExportOptions`, the
  values the Save dialog's Settings button edits) and its Save stores what it
  was left on back as that format's settings
  (`UltraCanvasFileLoader::SetImageExportOptions`, new: the image writer's
  options turned back into a format's settings; the size is not stored).
  `SetFileName` picks the format the name's extension names - the dialog
  stayed on PNG whatever it was given, so UltraPaint's Export wrote
  "photo.jpg" as a PNG. `SetOptions` now moves the controls (they showed
  their construction defaults whatever the options said), the colour depth
  list is applied (choosing a depth changed nothing), JPEG XL's quality
  slider is applied, and `SetTargetSize` with a size in another shape than
  the picture's turns Keep proportions off, so the size is met exactly.
- **FileLoader docs compile, and CI keeps them so.** The eight examples of
  `Docs/Modules/FileLoader/README.md` that did not compile (`{ ... }`
  placeholders, two plugin registrations without their headers, an undefined
  plugin class, helpers the reader writes) are fixed, and
  `scripts/check_doc_examples.py --all` - CI's doc check - now covers the
  module docs listed in `CHECKED_MODULE_DOCS`, FileLoader's first. A module
  joins once its examples compile; its findings are fixed, never baselined.
