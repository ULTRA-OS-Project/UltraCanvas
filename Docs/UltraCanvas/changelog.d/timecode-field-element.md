- **`UltraCanvasTimecodeField`: a timecode entry and readout.** `HH:MM:SS:FF`
  at a frame rate, as a count of frames. A click selects one of the four
  segments, `↑`/`↓` and the wheel step it, `Tab` and the arrow keys move
  between them; typed digits fill the timecode from the right with the
  untyped leading zeros dimmed, a leading `+` or `-` enters an offset in
  frames, `Enter` commits, `Esc` restores the last valid value, and an entry
  that is not a timecode (seconds of 75, a frame past the rate, past the
  maximum) shows a danger border until it is corrected. `Ctrl+C` copies the
  timecode, `Ctrl+V` takes one. `SetReadout(true)` is the large read-only
  variant a transport bar shows. `onCommit` reports the frames. Light and
  dark palettes. The frame arithmetic, `TimelineFramesToTimecode` and
  `TimelineTimecodeToFrames` (full timecodes, fewer groups counted from the
  right, dots or colons, a sign), lives in `UltraCanvasVideoTimelineModel.h`
  and is tested without a display. Part of the UltraVideo proposal
  (`Docs/Research/UltraVideoDesignProposal.md` §7.6); the DemoApp's *Video
  Timeline* page drives its playhead with one.
