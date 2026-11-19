- **A new progress window for copying, moving, deleting, packing and
  unpacking.** `UltraCanvasProgressDialog` keeps its ring - the percentage
  alone in its centre - and adds beside it the operation as a headline, the
  route as two chips ("ULTRA OS → Projekte") and the file being handled; under
  them a strip of speed, time left, items and size; and Pause / Resume next to
  Cancel (`ProgressDialogOptions`, `SetFigures`). The texts are plain labels
  refreshed at most four times a second, and nothing is counted for the
  window: the file display hands in what its workers count anyway (one atomic
  add per file) and totals only where it counts them before starting. Copying
  100,000 small files, the window's thread used 1.5 - 1.9 % of one core,
  against 1.1 - 1.7 % before. The window's titles are translated ("Copying"
  was English in every language).
