- **The path bar names a drive by its volume, and a setting says whether.**
  The path bar above the file display called a drive "D:" while the folder
  tree and the Computer page call it "My Passport Ultra (D:)". It now shows
  the same name, on the drive's own node and in the list of drives under its
  Computer entry, also over each pane of the split view. Settings > Display
  > Path bar turns it off, for a shorter strip with the letter alone (saved
  as `display.pathbar.volume.names`, on by default). The names are the ones
  the tree has already read, so the strip never waits on a disk; a drive
  whose volume has no name, and a network drive, keep the letter.
