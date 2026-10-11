- **`UltraCanvasVideoTimeline`: a media timeline editor element.** Clips on
  tracks under a time ruler, with a playhead, trim handles, reordering on
  the magnetic main track and free placement elsewhere, splitting (blade
  tool or `S`), transitions as badges on the cuts with a draggable length,
  fade knobs on audio clips (drag the top of a ramp; reported as a
  `FadeSet`), markers, beat marks, a work-area range, snapping, a rubber-band
  selection, file drop with an insertion marker, and a live timecode
  readout on every drag. It edits a module-free model
  (`UltraCanvasVideoTimelineModel.h`: `TimelineModel`, `TimelineTrack`,
  `TimelineClip`, `TimelineTransition`, `TimelineMarker`) and *reports*
  every edit through `onChangeRequested` with the clips before and after,
  for the application to validate against its engine, apply and feed back
  with `SetModel`; it never applies an edit itself. Thumbnails and waveforms
  come from providers the app installs and are drawn when they arrive. The
  pure operations - derived starts with transition overlaps, split, trim
  with source and neighbour limits, move, delete with ripple, duplicate,
  the ruler's tick steps, `HH:MM:SS:FF` timecode, snapping - are free
  functions tested without a display (`VideoTimelineModelTest`). Light and
  dark palettes from the UltraVideo design. First phase of
  `Docs/Research/UltraVideoDesignProposal.md`; the DemoApp shows it under
  *Media*.
