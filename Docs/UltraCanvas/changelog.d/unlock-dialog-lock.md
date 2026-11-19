- **The Unlock dialog shows a lock.** `InputDialogConfig::iconAboveInput` (and
  `iconAboveInputSize`) puts a picture centred above an input dialog's field,
  drawn in the dialog's text colour so it follows a light or dark theme, and
  `UltraCanvasDialogManager::ShowInputDialog(config, onResult, parent)` shows a
  dialog from a whole config. UltraCanvasMediaViewer's Unlock dialog (what
  UltraViewer and UltraFiler's preview ask with) and
  `UltraCanvasFileLoader::RequestDocumentPassword`'s prompt show the lock of
  the Unlock button above the password field. `InputDialogPromptTest` checks
  its place, size and ink, and that a missing picture leaves no gap.
