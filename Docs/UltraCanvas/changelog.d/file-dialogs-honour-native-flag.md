- **`UltraCanvasFileLoader`'s file dialogs honour the native-dialogs
  setting.** `OpenFileDialog`, `OpenMultipleFilesDialog`, `SaveFileDialog`
  and `SelectFolderDialog` always opened the platform's picker, whatever
  `UltraCanvasDialogManager::SetUseNativeDialogs` said, so an app that turned
  native dialogs off for one look throughout still got the platform's file
  picker among its own dialogs (UltraCleaner had to build the framework's
  file dialog by hand to get around it). With the setting off they now open
  `UltraCanvasFileDialog` in the matching mode, with the caller's title,
  start directory, default name, filters and hidden-files choice carried
  across, and the callback runs when that dialog closes rather than before
  the call returns. Apps that set the flag to true are unaffected; an app
  that never set it (the default is off) now gets the framework's file
  browser, and one `SetUseNativeDialogs(true)` at start-up restores the
  platform's.
- **`UltraCanvasDialogManager::CreateFileDialog` is public**, beside the
  other factories, so a caller can build the file browser directly instead
  of constructing `UltraCanvasFileDialog` itself.
