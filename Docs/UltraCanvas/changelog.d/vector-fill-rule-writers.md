- **Every vector writer keeps a shape's fill rule.** XAR, CorelDRAW, DXF,
  DWG, EMF, WMF and ODG wrote every shape the same way whatever its
  `VectorStyle::ShapeFillRule`, so one of the two rules always came out
  wrong: EMF and WMF set WINDING for the whole file, and XAR, CorelDRAW,
  DXF and ODG fill even-odd. Drawn after a round trip, a non-zero ring
  wound one way lost its middle in CDR and ODG, and an even-odd one filled
  solid in EMF, WMF, DXF and XAR.
  - EMF and WMF set `SETPOLYFILLMODE` per shape (ALTERNATE, WINDING), and
    their readers read it, starting in ALTERNATE as GDI does (they ignored
    it).
  - XAR, CDR, DXF (and so DWG) and ODG fill only even-odd. A non-zero shape
    is written as the subpaths that bound its fill - new
    `NonZeroAsEvenOdd` (`Plugins/Vector/UltraCanvasVectorFillRule.h`) keeps
    a subpath when its two sides differ under non-zero, which filled
    even-odd covers exactly the same while subpaths do not cross; crossing
    ones are reported. An outlined shape that loses subpaths is written as
    its fill and its full outline. EMF and WMF write non-zero shapes that
    way too, beside WINDING, because LibreOffice ignores WINDING.
  - The XAR reader carries Xara's winding rule into the model (even-odd by
    default) - Midget.xar's letters, wound like their counters, read with
    their counters open - and the DXF reader reads a hatch's odd parity as
    even-odd (style 2, "ignore islands", stays non-zero).
  - New `VectorFillRuleTest`: three rings through EMF, WMF, DXF, XAR, CDR,
    ODG and SVG, written, read back and drawn (6 checks fail on the old
    writers and readers), plus the subpath helper; the EMF, WMF and ODG
    output was also rendered in LibreOffice 24.2, rings as drawn.
  - `UltraCanvasXARExamples.md` listed the winding enum as `NonZero = 0`,
    against the code and its own text; corrected.
