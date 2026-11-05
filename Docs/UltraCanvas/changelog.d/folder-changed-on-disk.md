- **`UltraCanvasFilerWidget::onFolderChangedOnDisk(folderPath)`: what the
  folder watch saw change.** The widget told its host about its own
  operations (`onFolderModified`) and about every rescan
  (`onFolderRefreshed`). A host's folder tree therefore never heard of a
  change it did not make. That covers a subfolder another program deleted.
  It also covers one this widget deleted that Windows still listed as
  "delete pending" when the delete reported, because a program held a
  locked file in it. The new callback fires after every re-listing the
  folder watch asked for, with the folder that changed. When that folder
  itself is gone, the display has already moved up and the argument is the
  folder that went.
