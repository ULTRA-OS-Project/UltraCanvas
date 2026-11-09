- **A remote drive that can no longer sign in can be deleted or mended.**
  Its row under *Remote Drives* kept *Delete* greyed out, like every root of
  the tree, and nothing offered to change its settings, so a drive whose
  password was changed on the server stayed in the tree for good, failing.
  - *Delete* on a drive's row now deletes the drive from UltraFiler, after a
    question that says the files on the server are not touched: its account
    and saved password go (`UltraFilerRemoteDrives::RemoveDrive`), and so do
    its cached listings and the requests still waiting for it - the question
    counts the uploads and downloads that are dropped. A display on the drive
    moves to Home.
  - *Configure drive* beside it opens the add-drive form filled in with the
    drive's settings (UltraCloud's new `ShowEditAccountDialog`): the kind of
    server fixed, the protocol, server, port, user, key file and folder as
    stored, and the password field empty, which keeps the saved password
    unless a new one is typed. Saving signs in first and stores only what the
    server accepts; the drive keeps its id, so open folders and pins on it
    still work, and is read again with the new settings. Its sign-ins are in
    the connection log as *Configure drive*.
- **Refresh on every folder.** A drive's folders are shown from a cache, so
  a file uploaded or deleted from another device did not show, even after
  leaving the folder and coming back. *Refresh* is now in the folder tree's
  context menu for every folder and drive row, and in the folder display's
  context menu for the folder shown or the folder right-clicked: the folder
  is read again with its tree rows and every display showing it, and on a
  drive the server is asked again, ahead of the queue, while the files it
  showed stay on screen (`RefreshFolderEverywhere`,
  `UltraFilerRemoteDrives::RefreshFolder`). The toolbar's *Refresh* does the
  same on a drive instead of blanking the folder until the answer is in.
- **A file deleted on a drive says so in a dialog.** Selecting such a file -
  a picture on Google Drive deleted from another device, say - only put
  *No preview of ...* on the status line. A preview fetch that fails now
  carries what kind of failure it was (`RemoteFailure`): a file that is not
  there opens *File not found* and its folder is read again, so the file
  leaves the display; a refused sign-in or another refusal opens *Cannot open
  the file*, pointing to the connection log. A network that does not answer
  or a service that asks to wait stays a note on the status line, and the
  dialog comes once per selection, not at every update of the pane.
