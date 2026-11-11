- **3D files written on a localised desktop were unreadable, and COLLADA read
  back short.** A C++ stream renders its numbers through the *C++ global*
  locale — a separate switch from the C one, and the one a host application
  sets when it honours the desktop's language (`std::locale::global`) — so
  every model writer that streams its numbers put that locale into the file.
  STEP wrote `COLOUR_RGB('Steel',0,720000028610229.,0,…)`, where the comma is
  Part 21's parameter separator: a three-parameter entity became six and no
  CAD system would open the file at all. ASCII PLY wrote `element vertex
  12.227` for a mesh of 12227 vertices — digit grouping is the same locale's
  business, so a header with no floating-point number in it came out broken
  too. COLLADA wrote `count="33.639"` and `-0,012582`. OBJ's face lines, which
  are written between its guarded coordinate blocks rather than inside one,
  went out as `f 1.000/1.000/1.000`.
  - **COLLADA's reader lost geometry for the mirror-image reason:** `>>` takes
    the '.' for a digit-group separator in such a locale, so a `<float_array>`
    stopped extracting at its first coordinate and a mesh came back short or
    empty rather than slightly wrong — and `<p>` index lists re-read a written
    `1.000` as 1.
  - Every one of those sites now pins its stream to the classic locale.
    `ClassicNumbers` (`UltraCanvasTextUtils.h`) is the new guard for the
    common case: it imbues a stream for as long as the writer is writing and
    gives the caller's own locale back afterwards, because an application that
    hands a writer its `std::ostream` has not asked for that stream to be
    changed.
  - **`ModelLocaleDecimalTest` grew the half that was missing.** It compared
    the readers' meshes between the two locales but nothing compared what the
    writers produced, and it set only the C locale — which leaves every stream
    writing '.' however the writer is built, so the whole class was invisible
    to it. It now sets both locales and compares each text writer's bytes
    between "C" and a comma-decimal locale: OBJ, PLY, X3D, COLLADA and STEP.
    Four of the five failed the first time it ran.
  - The STEP, FBX, PLY and DirectX `.x` readers were already dot-decimal from
    0.9.43; what remained for those four formats was all on the writing side,
    and FBX and `.x` have no writer at all.
