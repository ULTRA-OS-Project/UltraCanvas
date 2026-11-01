- **VideoFX 0.17.0: a recorder for a live source.** `VideoFXFrameRecorder`
  takes frames one by one with the moment each was taken - RGBA or BGRA, any
  stride - and encodes them into the file as they come, so the file grows
  while the recording runs: a frame lands on the nearest tick of the file's
  rate, a duplicate is dropped, a gap shows the last frame longer, and the
  recording keeps real time however many frames the source managed. The
  export's codec, quality and hardware rules apply; the preset is
  `veryfast` with `zerolatency`, and an MP4 is fragmented so a recording cut
  short still plays. `VideoFXRecorderSettings`, `VideoFXPixelLayout`; the
  stub answers NotAvailable. UltraSnip records the screen with it
  (`VideoFXTest`: red then blue frames read back at their times, a BGRA
  frame read in its own order, 321 x 241 recorded as 320 x 240).
