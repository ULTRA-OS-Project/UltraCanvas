- **A remote drive's path bar shows the drive, not a broken local path.** The
  path bar read an FTP / SFTP / cloud drive's path as a folder on this
  computer: on Windows it showed *Computer > C: > Users > <name> >
  ultracloud:*, and the *ultracloud:* node opened a local folder that does
  not exist - an empty display, with that path in the title and the tab. A
  drive's path bar is now *Computer > FTP: <drive> > <folder> > ...*, each
  node opening that folder on the drive; the split view's pane headers
  follow it.
- **A tab on a drive's top folder is titled with the drive's name** instead of
  its raw `ultracloud://<account>/` address; a tab deeper in the drive keeps
  the folder's name.
