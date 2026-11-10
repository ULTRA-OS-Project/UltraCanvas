- **`FolderBreadcrumbOptions::driveLabel`: a folder strip can name its drives.**
  `BuildFolderBreadcrumb` called a drive by its letter ("D:") or root path,
  on its own node and in the "Computer" node's list. A host that has read
  the volumes' names (`ReadVolumeName`, off the UI thread) now passes a
  callback that answers with "My Passport Ultra (D:)"; unset, or an empty
  answer, keeps the letter (`UltraCanvasBreadcrumb.h` 1.8.0).
