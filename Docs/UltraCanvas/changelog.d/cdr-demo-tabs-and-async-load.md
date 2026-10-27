- **The demo's CorelDRAW page opens at once and shows one drawing at a
  time.** Opening it read all three sample drawings on the UI thread first -
  over two seconds of a frozen window in a debug build, nearly all of it
  `detailed.cdr`. The page now has a tab per file on the left; a drawing is
  read the first time its tab is shown, on a worker thread, behind a turning
  busy indicator, and the status line says how long it took.
- **UltraCanvasCDRElement: `LoadFromFileAsync` / `IsLoading`** (CDR plugin
  1.3.0). The file is parsed on a worker thread and the document handed to
  the element on the UI thread; only the latest of several loads is shown,
  and an element destroyed mid-parse waits for its worker. The element also
  draws in its own coordinates now: it clipped the drawing with its
  parent-relative content rect, which cut a strip the size of its offset off
  any drawing not placed at its parent's top-left corner.
- **libcdr: bitmap colours are converted once per colour, not once per
  pixel** (third patch in `UltraCanvas/third_party/libcdr`, also offered
  upstream as `upstream/0003-…`). Each pixel of an embedded bitmap went
  through its own lcms transform call - 70% of the time `detailed.cdr` took
  to read. The parse now takes 0.7 s instead of 2.3 s (debug build), with
  byte-identical output.
- **A CorelDRAW drawing's hairlines are drawn dark again.** Its thinnest
  outlines (a quarter of a point) came out a fifth of a pixel wide at a page
  fitted into a window, and antialiasing faded them to a pale grey: the
  dubai-atlantis and door-panel samples were barely visible. An outline is
  now at least one device pixel wide, as CorelDRAW draws it; the SVG export
  is unchanged.
