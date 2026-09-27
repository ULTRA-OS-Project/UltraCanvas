- **`UltraCanvasMediaViewer::ClassifyFile` is public.** It answers which view
  a path opens in (`MediaKind::Image`, `Vector`, `Model`, `Video`, ...) from
  the name alone. A host needs this to decide whether a file is worth fetching
  before it can be shown: UltraFiler uses it to preview pictures, vector
  drawings and 3D models from FTP and cloud drives, and not videos or
  documents. An unknown extension still answers `Image`, so check
  `IsSupportedMedia` first.
