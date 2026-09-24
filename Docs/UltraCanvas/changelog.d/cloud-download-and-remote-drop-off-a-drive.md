- **A file could be put onto a drive but never taken off one.** `CloudService`
  had `Upload` and no `Download`, although every provider - FTP, WebDAV,
  Nextcloud, Dropbox, OneDrive, Google Drive - has implemented `Download`
  since the module was written. The facade simply never exposed it, so the
  only way bytes moved was outwards, and the FTP transfer progress added in
  the last release reported a direction nothing could ask for. `Download` now
  sits beside `Upload`: it takes the full local path to write, because the
  caller is the one who knows what the file should be called and a provider
  inventing the name could not see a collision it was about to cause, and it
  checks that the destination folder exists first - every provider but FTP
  writes with an `ofstream`, which fails with nothing more useful than
  "cannot write" when the directory is missing.
- **Dragging a file off a drive onto a local folder now copies it down.**
  `UltraCanvasFilerWidget` gained `remoteDownload`, the exact mirror of
  `remoteUpload`: the host is handed the remote paths and the local folder,
  queues the transfers and refreshes when the server has answered. Before
  this, the widget passed those entries to the local paste machinery, which
  handed `std::filesystem` an `ultracloud://` path no disk has - so the drag
  that most obviously means "copy this off the server" did nothing at all.
  A drop that carries entries from a drive *and* files from this disk at once
  - a selection dragged out of a drive pane and one out of a local pane - is
  split, and each half done its own way.
- **`UltraCanvasFilerWidget::UniquePathIn` is public.** It answers what a
  "Keep both" paste would call a file in a given folder ("name (2)", with the
  extension kept on the end). A host that writes into a folder without going
  through the widget - saving a file fetched off a drive - needs the same
  answer, and a second implementation of it would be a second set of rules
  about what "(2)" means.
