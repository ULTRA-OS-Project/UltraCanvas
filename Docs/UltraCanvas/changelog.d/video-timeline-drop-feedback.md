- **`UltraCanvasVideoTimeline` 1.3.0: `SetDropFeedback`.** An application that
  carries a drop of its own onto the timeline (UltraVideo drags an effect
  preset onto a clip and a title style onto the title lane) tells the
  element in one call per pointer move what it would do: the clip to ring
  (as `SetDropTargetClip` rings it) and the lane and time of the insertion
  marker, with its own label in the readout, the time snapped as a file
  drop is and returned. `ClearDropFeedback` takes both away, and so does a
  `DragLeave` event now, so a carried drag ends the way a system drag does.
