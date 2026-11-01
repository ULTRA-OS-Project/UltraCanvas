- **`UltraCanvasVideoTimeline` 1.4.0: a close-gap button in every gap of a
  free track.** Each empty stretch before a clip on a non-magnetic lane
  shows a `|<-` button, centred in the part of the gap on screen; a click
  reports a `Move` of the clip after it to the end of the clip before (or
  to 0), `Shift`+click a rippled `Move` of every clip after the gap.
  `SetShowGapButtons(false)` turns them off; a locked lane has none. The
  model gains `TimelineGap`, `TimelineGaps` and `TimelineCloseGap`.
