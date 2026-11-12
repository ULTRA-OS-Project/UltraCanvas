- **Every drawing the framework writes was wrong on a localised desktop, and a
  DXF or EPS one could not be opened at all.** The same defect as the model
  writers above, in the formats where a misplaced comma is not a rounded
  number but a different drawing - or a count the reader cannot resolve:
  - **PDF** wrote `/MediaBox [0 0 420,5 297,25]`, a five-element array where
    a reader counts four, and the same comma in every `cm`, `re` and `l`
    operator of the content stream.
  - **DXF** wrote `$EXTMIN` as `0,` and every coordinate after it the same
    way. No CAD program opens that drawing.
  - **EPS** wrote `%%HiResBoundingBox: 0 0 420,5 297,25` and `1,5
    setlinewidth`; PostScript counts an operator's operands, so neither is a
    number out by a factor of ten - it is a syntax error.
  - **ASCII STL**, which is the one text form a person chooses deliberately
    (a slicer that wants a readable file), wrote `1,5e+00` - and its reader
    could not read even a correct file, because `>>` takes the '.' for a
    digit-group separator in such a locale and stops. A .stl saved as text and
    loaded again came back empty.
  - **DWG** reads by converting to DXF text in memory, and wrote `1,5` into
    that text, so a .dwg lost its geometry on the way in; the DXF reader's own
    `atof` and the EPS reader's `strtod` were the other half of the same trip.
  - PDF's object numbers, byte offsets and stream lengths were streamed
    unimbued as well, so a drawing with more than a thousand objects also
    carried `/Length 1.234` and a grouped xref count.
  - **The SVG reader lost a dash pattern, a polygon's points and a symbol's
    viewBox** the same way: three of its `istringstream` number readers were
    never imbued, while two beside them were. `SVGLocaleTest` could not see it
    because it set only the C locale, which leaves a stream reading '.'
    however it is built; it now sets both.
  - `Tests/VectorLocaleTest.cpp` writes one drawing with all ten vector
    writers under "C" and under a comma-decimal locale, compares the bytes,
    and - for the formats that read back - reads each file under both locales
    and compares the drawing. PDF, EPS, DXF and the SVG reader failed it the
    first time it ran; EMF, WMF, CDR and XAR passed. `ModelLocaleDecimalTest`
    gained the ASCII STL cases, both halves.
- **One guard for the numbers a text format writes, instead of five copies.**
  `ScopedPrecision` (`UltraCanvasTextUtils.h`) now pins both halves of a
  stream's number shape - the decimal point and the digit count - and the OBJ,
  X3D, COLLADA and STEP writers use it in place of a private copy each; three
  of those copies had had the locale half wrong or missing at some point, and
  one had it right in a scope too small to cover the file. `DigitsFor`
  (`UltraCanvasModelConverter.h`) is where `NumericPrecision` becomes a digit
  count, so the writers stop spelling 17, 9 and 6 out themselves, and
  `FormatFixedClassic` is the fixed-point formatter the three drawing writers
  had each written with `snprintf`. Byte-for-byte identical output: every
  writer's file is unchanged under "C", and 400,000 random values round-trip
  through the shared formatter exactly as the three `snprintf` copies did.
- `scripts/check_locale_numbers.py` recognises those guards, so a writer that
  holds one is not reported for the stream it guards - and its own comment now
  says what it cannot see (the locale of a stream a writer is handed, which is
  what the two locale tests are for). The baseline is down to 12 keys, 49
  sites; the whole `Plugins/Vector` tree is off it.
