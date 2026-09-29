- **Programs inside archives can be run.** An entry inside an archive has a
  virtual path, which no system can execute, so `UltraCanvasFilerWidget`
  ignored a double-click on a program in a zip. `ExtractAndRunEntry` now
  unpacks the archive holding it — the whole archive, so the program finds
  its DLLs and data beside it — into a run folder, starts the program there
  and deletes the folder once the program and everything it started have
  ended. Double-click, Enter and `OpenEntryWithOS` do it for such an entry,
  and the context menu offers it as *Extract and Run*.
  - New `UltraCanvasArchiveRun.h`: `IsRunnableArchiveEntry` (Windows by
    extension — `.exe`, `.com`, `.bat`, `.cmd`, `.msi`; POSIX by the execute
    bit the archive recorded, or `.AppImage`), `LaunchWatchedProgram` (a
    launch whose end can be waited for: a job object on Windows, so an
    installer's second stage counts; a process group of its own on POSIX,
    with a failed `exec` reported instead of lost), `CopyDownloadMarking`
    (Windows: the archive's `Zone.Identifier` goes onto the unpacked files,
    so SmartScreen still checks them) and the run folders — each with a
    marker naming the processes using it, removed only when nothing holds a
    file in it, and swept up by `SweepArchiveRunFolders` after an
    application that closed while its program still ran.
  - `FilerEntry::archiveExecutable` carries the execute bit an archive
    recorded; `chooseArchiveRunRoot` lets the host pick where run folders go.
  - `Tests/ArchiveRunTest.cpp` covers the rule, the run folders and the
    watched launch, including a program that exits while its child runs on.
