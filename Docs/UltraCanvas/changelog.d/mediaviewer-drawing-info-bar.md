- **UltraCanvasMediaViewer 1.15.1: a drawing on screen is no longer reported as "No media".**
  The info bar and the Details panel had a branch for every view but the
  drawing one, so a file shown by the vector view (DXF, DWG, EMF, ...) or by a
  plugin's own element (CorelDRAW through libcdr) fell through to the picture
  branch, found no bitmap and said *No media* under a drawing that was plainly
  there. The info bar now reads `name · DRAWING (CDR) · size · n / m`, adding
  the document size and zoom in the vector view (and following its zoom); the
  Details panel calls it a *Vector drawing*. A drawing with no reader and no
  stored preview keeps saying so instead of having it overwritten by
  *No media*.
