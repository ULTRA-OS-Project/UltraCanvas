- **`UltraCanvasVideoTimeline` 1.3.0: `SetDropMarker`.** An application that
  carries a drop of its own onto a lane (UltraVideo drags a title style from
  its bin onto the title lane) sets the insertion marker and readout the
  element shows for files it is handed, at a time on a track, snapped as a
  file drop is, with its own label in the readout; the call returns the
  time the drop would land at. `ClearDropMarker` and `HasDropMarker` go
  with it. The element's own drop path for files is unchanged.
