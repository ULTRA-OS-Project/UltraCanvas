- **A file on a drive with no preview opens in its program.** Double-clicking
  a document, a spreadsheet, an archive or anything else on a drive that the
  preview pane does not show did nothing: the system's opener was handed the
  drive's path and found no file. It is now fetched like any download, with
  its progress on the status line, into UltraCanvas's cache folder
  (`remote-opened`, one folder per file and version) and opened with the
  program this computer opens its type with (`OpenRemoteFile`). The status
  line says changes to the copy are not saved to the drive. Opened again
  while the file on the server is unchanged, the same copy opens without a
  request; a fetch that did not finish is never opened. A program, a script,
  an installer or a launcher is not started from a drive - the dialog says to
  copy it to this computer first - since on Windows the system's opener runs
  such a file. A fetch that fails says so in a dialog, a file no longer there
  as *File not found* with its folder read again.
- **A drive's folder fetched a while ago is asked for again when opened.**
  Within a session a listing was served from the cache for good, so leaving
  a folder and coming back never showed a file another device had added or
  deleted. A listing older than two minutes (`kRemoteListingFreshFor`) is now
  shown as it was and fetched again behind it when its folder is listed, as
  a listing kept from the last run is; within the two minutes, and for a
  subfolder fetched ahead, it costs no request.
- **Configure drive saves while the server is out of reach.** Save always
  signed in first, so a drive whose server could not be reached could not
  even be renamed. Once that sign-in fails, *Save without signing in* appears
  beside Cancel and stores the settings as they are, to be tried the next
  time the drive is opened.
