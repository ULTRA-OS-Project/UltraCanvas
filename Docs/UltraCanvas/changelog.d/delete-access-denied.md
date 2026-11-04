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
