# libcdr (vendored, patched)

CorelDRAW (`.cdr`, `.cmx`) parser from The Document Liberation Project — the
same engine LibreOffice uses — carried here so UltraCanvas can fix what it
gets wrong instead of working around it.

- **Upstream:** https://git.libreoffice.org/libcdr (GitHub mirror
  `LibreOffice/libcdr`)
- **Base:** commit `4401de4df11e68ba84dd0e594dcfa6daf7ef9ffa` (2026-09-25),
  newer than the last release (0.1.7)
- **License:** MPL 2.0 (`COPYING.MPL`); contributors in `AUTHORS`. The
  modified files stay available in source form right here, which is what the
  MPL asks of a modified copy.
- **Layout:** `inc/libcdr/` = upstream `inc/libcdr/`, `src/` = upstream
  `src/lib/`. The autotools files and the command-line converters are not
  carried.
- **Built by:** `UltraCanvas/Plugins/Vector/CDR/CMakeLists.txt` as the static
  library `ultracanvas_libcdr`, when librevenge, lcms2, ICU, zlib and the
  Boost headers are present; otherwise the CDR plugin falls back to the
  system libcdr.

## UltraCanvas patches

The full diff against the base commit is `ultracanvas.patch`; every changed
place is marked `UltraCanvas:` in the source. Both fixes were found on
`media/vector/CDR/detailed.cdr`, where CorelDRAW's own preview shows four
business cards and libcdr (and LibreOffice) showed an empty white sheet.

1. **Bitmap transparency** (`CDRParser::readBmp`,
   `CDRStylesCollector::collectBmpAlpha`, `CDRContentCollector::_bitmapMimeType`).
   CorelDRAW stores a transparent bitmap as its colour image followed by an
   8-bit alpha mask — an image record of its own with colour model 99, rows
   bottom-up like the colour data. libcdr read the colour image only, so
   drop shadows became solid black boxes and cut-out overlays opaque sheets
   covering everything under them. The mask is now read and the bitmap
   re-encoded as RGBA PNG (zlib, already a dependency); images with an
   opaque mask stay BMP. The output's MIME type follows the bytes.
2. **PowerClip** (`CDRParser::readLoda` argument `0x1f45`,
   `CDRContentCollector::collectPowerClip` / `_addPowerClipContent`,
   `CDRParserState::m_vectBoxes`). An object with this argument is a
   PowerClip frame; the argument's first word names the `vect` holding the
   artwork clipped into it. libcdr parsed that vect but never drew it, so
   all PowerClipped artwork went missing (the sample's leaves, waves and
   the shield's gloss). The frame is now followed by an image of MIME type
   `image/svg+xml` over the contents' box: a small SVG whose `clipPath` is
   the frame's outline, around the vect's own SVG — clipped exactly as
   CorelDRAW draws it, for any SVG consumer.

Both are candidates to offer upstream.
