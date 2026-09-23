- **A gauge's `LinearBar` can say "busy, total unknown".** `SetIndeterminate`
  drops the value entirely and slides a block along the track - a download
  whose server sent no length, a queue still being counted - where before the
  only honest option was to leave the bar at zero, which reads as progress
  that is stuck, or to hide it and say nothing. It animates on a timer the
  gauge owns, started and stopped with the flag and torn down with the element:
  a caller reporting bytes has nothing to report while the total is unknown, so
  a bar driven by those reports would freeze whenever a chunk was in flight.
  LinearBar only; other modes ignore it.
- **A gauge's `LinearBar` fits the box it is given.** It is the framework's
  progress bar - "Horizontal or vertical bar (e.g. download progress)" - but it
  was sized only as a dashboard gauge: a caption over a 28 px bar with the
  value spelled out underneath, which needs some 114 px of height before any of
  it fits. In anything shorter it laid out for the height it wanted rather than
  the height it was given and drew its bar and its value outside the element,
  which is what kept it out of the one place a progress bar is most wanted - a
  status line, a list row, a panel footer, all of them twenty-odd pixels tall.
  Below the height its caption and value line need it now drops both, drops its
  side padding, and is simply the bar across the whole element. A gauge with the
  room to be a dashboard gauge is unchanged, pixel for pixel.
- **`UltraCanvasGaugeDiagramElement` is in the element catalogue.** It was not,
  so `Docs/UltraCanvas/UltraCanvasUIElements.md` - the file every assistant and
  contributor is told to consult before building UI - offered a progress
  *dialog* and nothing else, and the gauge was findable only by already knowing
  its name. That is exactly how a second progress bar gets written.
- **An FTP transfer reports its bytes.** `UltraNet_FtpUpload` and
  `UltraNet_FtpDownload` set up libcurl without a progress callback, so a file
  moving to or from a server was silent from first byte to last and nothing
  above them could draw a progress bar however much it wanted to. Both install
  one now, feeding the module's existing global transfer callbacks
  (`UltraNet_SetTransferCallbacks`) - the same bag every HTTP request already
  reports through, so a caller sets it once and hears about every transfer
  whatever the protocol. Listings and the one-shot verbs are left alone: they
  move too little for anyone to watch.
