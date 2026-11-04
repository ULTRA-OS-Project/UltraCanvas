- **A delete Windows answers "Access is denied" is no longer taken for a
  permission problem by default.** On Windows that one error stands for four
  refusals, and the Filer's delete treated every one of them as the
  access-list case, offering "Delete as administrator". The elevated helper
  then got the same refusal and reported "could not be deleted even with
  administrator permission", while Explorer deleted the folder without
  asking. The new `UltraCanvasFileRemoval`
  (`RemoveFileSystemEntry`, `RemoveFileSystemTree`,
  `FileSystemEntryIsDeletePending`, `FileRemovalProblem`) reads the NTSTATUS
  behind the error and the point at which the system refused, and tells the
  four apart:
  - a read-only **folder** is lifted like a read-only file. Folders copied off
    a CD or a camera card, or carrying a `desktop.ini`, have the read-only bit,
    and FAT and exFAT drives refuse such a folder for deletion. The bit is put
    back if the delete still fails;
  - an entry already deleted but still held open by another program (a
    scanner or the indexer; Windows calls it "delete pending") counts as
    removed. The folder it empties is waited for, about two seconds at most;
  - a file mapped into a process is `InUse`. This covers a running program
    and a picture libvips memory-mapped when the Filer drew its thumbnail or
    folder preview. The Filer releases what its image loader still holds
    (`UCImage::ReleaseCachedFiles`, new) and tries once more. Whatever is
    still held gets the "in use" dialog, not the administrator one;
  - only a refusal of the access list is `NotPermitted`, the one case
    "Delete as administrator" is offered for.
  Where the system has it (Windows 10 1809+ on NTFS), a delete uses POSIX
  semantics, so a held file's name goes at once and its folder can follow.
  The elevated helper removes through the same code. A failure it reports
  names the file inside that refused, and says so when a program holds it.
  `Tests/FileRemovalTest.cpp` builds each of these cases on the Windows CI
  runner. `Tests/ElevatedFileOperationsTest.cpp` was built nowhere; it is now
  registered and runs there too.
- **A folder being deleted, moved or renamed is no longer held open by
  another file display's folder watch.** On Windows a folder watch keeps its
  folder open. When the second pane of UltraFiler showed a subfolder of the
  folder being deleted, that folder stayed listed as "delete pending" on a FAT
  or exFAT drive. On any drive, a folder above the watched one could not be
  moved, not even to the Recycle Bin. The new `UltraCanvasFolderWatchHold`
  keeps every watch in the process off the given folders while it lives.
  Watches already there let go: each reports itself failed, so its owner
  polls instead and notices the folder is gone. New ones are refused until
  the hold ends. The Filer's delete (permanent or to the trash), move and
  folder rename take one for as long as they run. `UltraCanvasFolderWatcher`
  is now safe to release from another thread.
- **A read-only folder is deleted without asking on Windows.** The attribute
  protects nothing on a Windows folder. Windows sets it itself on a folder
  with a `desktop.ini` and on folders copied off a CD or a camera card, so
  Explorer deletes such a folder without a word. The Filer no longer asks
  "write-protected?" for one there; it lifts the attribute as it deletes. A
  read-only file is still asked about, and so is a folder without write
  permission on Linux and macOS, where nothing in it can be deleted.
