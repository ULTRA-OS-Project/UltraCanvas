- **The localisation editor has a Window arrangement.**
  `UltraCanvasLocalisationEditor` takes a `LocalisationEditorLayout` when it
  is made: `Compact` (the default, as before) for a page or a dialog, and
  `Window` for a window of its own - the choices on two rows across the
  width, the grid beside a 380 px panel that shows the selected row whole
  (its key, its English, the comparison, what is shown and its status,
  every set's text, wrapped rather than cut to a column) with the edit at
  its foot (*Apply*, *Clear override*), the status line and the actions at
  the window's foot. The Draft button shows there only when a drafter is
  set. The parts are made once (`MakeParts`) and each arrangement places
  them. UOS-Localisation, the new translation editor program, uses it.
