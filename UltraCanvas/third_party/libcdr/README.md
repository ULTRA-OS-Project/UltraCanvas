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

## Offering the patches upstream

`upstream/` holds both fixes as `git format-patch` files against the base
commit, one per fix. They contain the same code as `ultracanvas.patch`, but
with upstream-style commit messages and without the `UltraCanvas:` markers:

- `0001-read-the-alpha-mask-stored-after-a-transparent-bitma.patch`: bitmap
  transparency.
- `0002-draw-the-contents-of-PowerClip-frames.patch`: PowerClip. It builds
  on 0001.

Both apply with `git am` on commit `4401de4`, and each step compiles on its
own. They are authored as "Claude"; the person submitting takes authorship.

libcdr takes changes through LibreOffice's Gerrit, not GitHub pull requests.
To submit:

1. Set up Gerrit for `libcdr`
   (https://wiki.documentfoundation.org/Development/gerrit/setup) and send the
   license statement to the LibreOffice developer list, once.
2. Run `git clone https://git.libreoffice.org/libcdr`, then `cd libcdr`.
3. Install the Change-Id hook from the Gerrit setup page.
4. Run `git am /path/to/upstream/*.patch`. Then run
   `git rebase -x "git commit --amend --no-edit --reset-author" origin/master`
   to take authorship and add the Change-Ids.
5. Run `git push origin HEAD:refs/for/master`.

Attach `media/vector/CDR/detailed.cdr` to the review if its licence allows,
or describe the case: CorelDRAW's preview shows four business cards, but
libcdr's SVG output is an empty white sheet.

When upstream merges the patches, move the base commit forward and drop
`ultracanvas.patch` and the `UltraCanvas:` markers.
