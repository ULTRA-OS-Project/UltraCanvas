- **The registry findings list keeps its place.** Cleaning, restoring or
  rescanning lists the findings again, and the view fell back to the first row
  each time - on a list of hundreds, the entry just acted on was gone from the
  screen. The list now stays where it was (the framework's
  `TreeRebuildScope`).
