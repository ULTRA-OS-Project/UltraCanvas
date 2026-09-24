- **A drive's entries could be dragged out of the window and copied to the
  system clipboard, and neither gave the receiver anything it could open.**
  A path on a drive is `ultracloud://<account>/<path>`: it names a file on a
  server, not a file on this computer. `UpdateItemDrag` handed those straight
  to `StartNativeDragOfPaths` when the pointer left the window, and
  `EntriesToClipboard` mirrored them to the system clipboard as a
  `text/uri-list`, so another application would accept the drop or the paste
  and then fail on a path nothing there can resolve.
  - **The native drag is refused for them.** The gesture is not lost: it
    carries on as the widget's own in-window drag, which is where it can
    actually do something - dropped on a local folder it downloads.
  - **A copy puts the entry NAMES on the system clipboard as text**, rather
    than paths or nothing at all. The clipboard still has to be *taken* - a
    paste reads the system clipboard before the internal one, so leaving the
    previous copy's file list in place would paste those files instead of
    these - and text takes it while giving another application something
    usable. The paths stay on the widget's internal clipboard, which is
    shared between panes, so copy in a drive pane and paste in a local one
    works.
- **Ctrl+V now does on the keyboard what a drop already did with the mouse.**
  `Paste` began with `RefuseWriteHere`, so pasting files INTO a drive was
  refused outright although dropping the same files on it uploaded them, and
  pasting a drive's entries into a local folder handed `std::filesystem` a
  path no disk has and did nothing at all. Into a drive is now an upload
  (through `remoteUpload`), out of one is a download (through
  `remoteDownload`), and a clipboard holding both kinds is split with each
  half taking its own route. Only the two genuinely unsupported cases still
  refuse: a paste from one place on a drive to another (no provider has a
  server-side copy) and pasting raw clipboard data - an image, text - as a new
  file on a drive.
- **Cut and Duplicate are greyed out on a drive** instead of being offered and
  then refused. A cut is a move, and moving a file off a drive is a download
  followed by a destructive delete with nothing to undo it if the first half
  only partly arrived; a duplicate is a server-side copy no provider offers.
  A cut that ghosts the entries and then cannot complete is worse than one
  that never starts.
- `RefuseWriteHere`'s message said a drive could be browsed and not changed.
  That stopped being true when uploads landed: it now names what a drive *can*
  do - files copied to and from it, renamed, deleted, folders created - so the
  refusal points somewhere instead of just closing the door.
