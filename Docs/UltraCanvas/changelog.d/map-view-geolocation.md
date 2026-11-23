- **UltraCanvasMapView: a map in any application.** A map of the world
  from OpenStreetMap's tiles (or another slippy-map server) as an element -
  dragged to move, the wheel to zoom, a click to put the pin - and as a
  map drawn into any rectangle (`DrawMap`, for a card in a list's row) or
  into pixels (`RenderMapToRgba`). The tiles are fetched in the background,
  shared by every map of the program, within OpenStreetMap's usage policy.
  `Docs/UltraCanvas/UltraCanvasMapView.md`.
- **UltraCanvasGeolocation: where this device is.** One position from the
  system's location service, with how far off it may be: GeoClue on Linux,
  Location Services on macOS (the app's Info.plist says why it asks),
  Windows' location service where the build has its WinRT headers.
  `Docs/UltraCanvas/UltraCanvasGeolocation.md`.
