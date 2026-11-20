- **CI draws the vector writers' output in LibreOffice.** The writers were
  tested only by reading their files back with our own readers, which can
  agree with them and still both be wrong. Before the fill-rule fix every
  EMF, WMF, CDR and ODG round trip passed, while LibreOffice drew the rings
  wrong.
  - New workflow `Vector output in LibreOffice`
    (`.github/workflows/vector-libreoffice.yml`), on changes to the Vector
    plugin or the vector model. New `VectorExportFixtures` writes fixture
    drawings through every writer with a manifest of probe points. New
    `scripts/check_libreoffice_render.py` has LibreOffice convert each file
    to PNG, finds the drawing on the page and checks each probe.
  - EMF, WMF, SVG, ODG, PDF, CDR and DXF are checked (22 files). Against
    the writers from before the fill-rule fix, 8 of them fail: CDR, EMF,
    WMF, the three ODG variants and both SVG variants.
  - It found four files LibreOffice's own import draws wrong, now listed as
    expected failures that must keep failing. Its PDF import fills every
    shape even-odd (poppler draws our file right), and its DXF import drops
    a hatch with curved boundary edges.
