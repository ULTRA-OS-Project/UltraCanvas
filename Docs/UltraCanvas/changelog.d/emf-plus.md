- **EMF+ is read: GDI+ drawings keep their transparency.** The EMF reader
  read only the GDI records, so a GDI+ metafile drew from the copy a dual
  file carries for older players: opaque, and sometimes offset from where
  GDI+ put the shape (the sample files' circles came out solid and 14 px
  up and left).
  - New `EmfPlusPlayer` (`Plugins/Vector/UltraCanvasEMFPlus.h`) plays the
    EMF+ records. It covers solid, hatch and linear-gradient brushes with
    their alpha, pens, path objects, polygons, rectangles, ellipses, pies,
    arcs, cardinal curves, Beziers and `DrawString`, through the world,
    page and container transforms.
  - In a dual file, a stretch of EMF+ drawing that was played skips its
    GDI copy. A stretch with a record the reader cannot draw (an image, a
    region fill, a driver string, a texture or path-gradient brush) is
    drawn from that copy instead and reported. GDI drawing after an EMF+
    `GetDC` is drawn.
  - A WMF that carries its EMF in `META_ESCAPE_ENHANCED_METAFILE` records,
    as GDI+ writes one, is read from that EMF, as LibreOffice reads it.
  - Checked against LibreOffice 24.2 on three GDI+ EMF files and two WMF
    files: same positions, same translucency.
  - New `EMFPlusTest` builds a dual EMF record by record: alpha kept, the
    GDI copy not drawn twice, the fallback, an EMF+-only file and the WMF
    wrapper (7 checks fail on the old reader).
