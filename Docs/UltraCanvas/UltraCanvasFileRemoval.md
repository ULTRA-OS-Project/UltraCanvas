# UltraCanvasFileRemoval

<!-- doc-check: std::string path; -->

**Deleting for good, and saying why it failed** in terms of what the user can
do about it: close a program, ask an administrator, or nothing.

```cpp
#include "UltraCanvasFileRemoval.h"
#include "UltraCanvasPathUtf8.h"

std::error_code ec;
FileRemovalProblem problem = FileRemovalProblem::NoProblem;
std::filesystem::path stoppedAt;
if (!RemoveFileSystemTree(PathFromUtf8(path), ec, &problem, &stoppedAt)) {
    switch (problem) {
        case FileRemovalProblem::InUse:        /* a program holds it: close it, try again */ break;
        case FileRemovalProblem::NotPermitted: /* an administrator may succeed */ break;
        default:                               /* ec.message() says what */ break;
    }
}
```

`RemoveFileSystemTree` is `std::filesystem::remove_all` with a reason. It works
post-order, removes a link as the link and never follows it, and names in
`stoppedAt` the entry that refused. Inside a folder that is rarely the folder
itself.

## Why "access denied" is not enough

On Windows, `std::filesystem::remove` answers *Access is denied* for four
different things, and only one of them is about permission:

| What it really is | Example | What this module does |
|---|---|---|
| The **read-only attribute**, on a file *or a folder* | A folder copied off a CD or a camera card; a folder with a `desktop.ini` | Lifts it, as Explorer does, and puts it back if the delete still fails. FAT and exFAT refuse even to open such an entry for deleting. |
| **Delete pending**: the entry was deleted, but a program still holds it open | A virus scanner or the search indexer reading the file at that moment | Counts it as removed. Windows lists it until the holder lets go, and refuses every look at it meanwhile. Its folder is waited for, about two seconds at most. |
| A file **mapped into a process** | A running program's `.exe` or DLL; a picture libvips memory-mapped when it was decoded | `FileRemovalProblem::InUse`. Nobody can delete it while the mapping lasts, not even an administrator. |
| The **access list** says no | A file of another user, or of the system | `FileRemovalProblem::NotPermitted`: the one case a retry as administrator ([UltraCanvasElevatedFileOperations](UltraCanvasElevatedFileOperations.md)) can help. |

The second column of the table is how they are told apart. When opening the
entry for deletion fails, the NTSTATUS behind the Win32 error says whether it
was delete pending (`STATUS_DELETE_PENDING`). If the read-only bit is not set
either, what remains is the access list. When the open *succeeds* and the
delete is still refused (`STATUS_CANNOT_DELETE`), the system has already
granted the delete access. A refusal at that point is never one an
administrator can get past.

This is what used to go wrong in UltraFiler. A folder on an external drive
held pictures the Filer had drawn thumbnails of, and libvips kept them
memory-mapped. Deleting it answered *Access is denied*, the Filer offered
*Delete as administrator*, and the elevated helper got the same answer. Its
summary read *could not be deleted even with administrator permission*,
while Explorer deleted the folder without asking. The
[Filer](UltraCanvasFilerWidget.md#delete-problems-locked--failing-entries) now
releases what its image loader holds (`UCImage::ReleaseCachedFiles`) and tries
again when a removal comes back `InUse`. It offers the administrator only for
`NotPermitted`.

## API

| Function | Purpose |
|---|---|
| `RemoveFileSystemEntry(path, ec, &problem)` | One file, one link or junction (never its target), or one **empty** folder. True when it is gone: removed now, not there to begin with, or delete pending. |
| `RemoveFileSystemTree(path, ec, &problem, &stoppedAt)` | `path` and everything under it, every entry through `RemoveFileSystemEntry`. |
| `FileSystemEntryIsDeletePending(path)` | Windows: deleted already, listed only while a program holds it. Always false elsewhere. |
| `ClassifyFileRemovalError(ec)` | The `FileRemovalProblem` for an error from another `std::filesystem` call, such as listing a folder on the way to removing it. |

`FileRemovalProblem`:

| Value | Meaning |
|---|---|
| `NoProblem` | Removed, already gone, or on its way out. |
| `InUse` | A program holds it: open without delete sharing, mapped, or running. Closing that program helps; administrator rights do not. |
| `NotPermitted` | This user may not delete it; an administrator may. |
| `NotEmpty` | A folder that still has entries in it (`RemoveFileSystemEntry` removes empty ones only). |
| `Other` | Anything else; `ec` says what. |

## The delete itself

On Windows 10 1809 and later, on NTFS, an entry is marked deleted with
`FILE_DISPOSITION_INFO_EX` using POSIX semantics and *ignore read-only*. Its
name leaves the folder at once, even while another program still holds the
file with delete sharing, so the folder can be removed straight after. That
is what `DeleteFileW` itself does on current Windows. FAT, exFAT, network
shares and older Windows get the classic `FILE_DISPOSITION_INFO`, after the
read-only attribute is lifted.

A folder that answers *not empty* is checked before anything is reported. If
every entry left in it is delete pending, it is tried again a few times,
about two seconds in all. A holder that keeps an entry longer than that makes
the folder `InUse`.

## Backends

| Platform | Backend |
|---|---|
| Windows | `OS/MSWindows/UltraCanvasWindowsFileRemoval.cpp`: `CreateFileW` with `DELETE`, `SetFileInformationByHandle`, `RtlGetLastNtStatus` from `ntdll` for the status behind *access denied*. |
| Linux, macOS, BSD, WebAssembly | `std::filesystem::remove` in `core/UltraCanvasFileRemoval.cpp`, with the errno classified: `EACCES` / `EPERM` / `EROFS` are `NotPermitted`, `EBUSY` / `ETXTBSY` are `InUse`. Unlinking an open file is allowed there, and a deleted entry is never listed, so the Windows cases do not arise. |

`Tests/FileRemovalTest.cpp` exercises it everywhere. On Windows it creates
each case for real: read-only folders, a delete-pending file whose holder lets
go during the wait and one whose holder does not, a memory-mapped file, and a
file open without delete sharing. CI runs it on the Windows runners, in a
folder with a Thai name.
