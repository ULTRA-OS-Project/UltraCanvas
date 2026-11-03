- **DemoApp, Matrix Diagram: Save JSON... goes through `UltraCanvasFileLoader`.**
  It wrote the file with `JSON::SerializeToFile`, past the loader that every
  user file goes through (CLAUDE.md). It now writes `ExportMatrixJson` with
  `UltraCanvasFileLoader::SaveFile`, so a failed write leaves any file already
  at the path intact, and the read-out names the reason a save failed.
