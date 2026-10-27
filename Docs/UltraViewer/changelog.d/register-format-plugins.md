- **A CorelDRAW, Xara, DXF or DWG file shows the drawing.** UltraViewer
  registered none of the framework's format plugins, so a CDR showed only the
  small preview bitmap stored inside it, a DXF or DWG nothing, and a 3D model
  opened only as STL. It now links `UltraCanvasAllFormats`, as UltraFiler
  does, and every format plugin the build produced is registered before
  `main()`: drawings open in the vector view, sharp at any zoom, and the 3D
  formats of the Models plugin open in the 3D view.
