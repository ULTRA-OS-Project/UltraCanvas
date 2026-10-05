- **`UltraCanvasDialogManager::CloseAllDialogs()` closes every dialog.** It
  walked the list of open dialogs while each close erased that dialog from the
  same list, so every second dialog stayed open, and the loop went on to read
  the vacated slots past the list's end. The `clear()` after it then
  unregistered a dialog that a result callback had opened meanwhile, leaving it
  on screen where no later call could reach it. It now closes from a copy and
  unregisters only the dialogs it closed. Apps call it to lock or reset
  (UltraPassword's `Lock()`), so far never with more than one dialog open. New
  `DialogCloseAllTest` (Xvfb) closes one and three dialogs and one whose
  callback opens another; three of its checks fail without the fix.
