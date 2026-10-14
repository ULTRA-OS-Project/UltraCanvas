- **Clipboard history rows: a Save as action, and the icons left of the
  text.** `ClipboardRowAction::SaveAs` is painted between Edit and Delete when
  `ClipboardRowStyle::saveAction` is set (UltraClipboard sets it; the
  desktop's quick panel keeps its three), with its tooltip *"Save as
  (Ctrl+S)"* and a new `media/icons/clipboard/save.svg`.
  `ClipboardRowStyle::actionsLeftOfText` (`SetActionsLeftOfText` on a
  delegate in use) puts the actions between the thumbnail and the text
  instead of at the row's right edge, and keeps their place while a compact
  row hides them so the text does not move. `ClipboardHistoryPolicy::iconSide`
  (`ClipboardIconSide::Right` / `Left`) keeps that choice with the history's
  settings, shared by every process that opens it: UltraClipboard's rows and
  UltraDesktop's quick panel both follow it.
