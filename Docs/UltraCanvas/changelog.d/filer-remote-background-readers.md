- **The folder display no longer hands files on a remote drive to its
  background readers.** A remote listing (an FTP or cloud drive in UltraFiler)
  went through the same readers as a local folder, so every `.mp4` on the drive
  was given to the video thumbnailer as `ultracloud://...`, a path no decoder
  can open; a grab that does not fail at once waits out its 8-second timeout.
  Closing a tab destroys its `UltraCanvasFilerWidget` on the UI thread, which
  joins those workers, so a reader busy with such a path held the window - a
  suspect in UltraFiler stalling when one of two tabs on an FTP folder is
  closed (each display retries every file). Thumbnails, text and folder
  previews, media probes, folder statistics and folder prefetch now skip a path
  the `isRemotePath` hook claims (`IsRemoteEntryPath`); a local thumbnail the
  host supplies is still shown.
