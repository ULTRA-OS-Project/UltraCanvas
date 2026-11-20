- **Run in background.** The progress window of a copy, move, delete, pack or
  unpack in `UltraCanvasFilerWidget` offers "Run in background" when the host
  sets `onBackgroundProgress`: the window closes, the work goes on, and the
  host hears its progress on every tick (`FilerBackgroundProgress`) until it
  ends; `BringOperationToFront()` opens the window again.
  `UltraCanvasProgressDialog` has the button for any caller
  (`ProgressDialogOptions::onBackground`, `IsInBackground()`) and opens on
  paused work with Resume (`startPaused`). A LinearBar gauge's track takes
  `SetTrackColor` (it was a fixed light grey, glaring on a dark bar).
