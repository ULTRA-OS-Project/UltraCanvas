- **Deleting a folder on an external drive no longer ends in "could not be
  deleted even with administrator permission".** Windows answers "Access is
  denied" for several things that are not about permission. Read-only
  folders inside the one being deleted are one of them. So is a picture
  UltraFiler itself still held from drawing its thumbnail. UltraFiler now
  handles each of these as Explorer does, and asks for administrator rights
  only when the access list says no. The framework change is
  `UltraCanvasFileRemoval`; see the framework changelog.
- **Deleting, moving or renaming a folder while the other pane shows a
  subfolder of it works as it does with one pane.** The other pane's folder
  watch no longer holds the subfolder open while the operation runs.
- **A read-only folder is deleted without the "write-protected?" question on
  Windows**, as Explorer deletes it.
- **A pane whose folder is deleted - from the other pane or by another
  program - moves up to the nearest folder that is still there**, instead of
  showing an empty folder that no longer exists. A USB stick pulled out is
  still waited for, so plugging it back in shows its folder again.
