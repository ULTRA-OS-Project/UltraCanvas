- **Delete asks "Move to the Trash" or "Delete permanently".** Every delete
  in the Filer widget used to be permanent - there was no trash at all, and
  Shift+Del did exactly what Del did. The confirmation now carries the choice
  as two radio buttons, and the line under the question follows it ("It can
  be restored from the Trash." / "This cannot be undone."). Del opens it on
  the trash, Shift+Del on the permanent delete, as in Explorer; the context
  menu gains **Delete Permanently** (Shift+Del) beside **Delete** (Del).
  - New `UltraCanvasTrash.h`: `MoveToTrash`, `TrashAvailable`,
    `TrashDisplayName`. Windows recycles through `SHFileOperationW` with
    `FOF_ALLOWUNDO`, and `FOF_WANTNUKEWARNING` makes the shell ask before an
    item the Recycle Bin cannot hold is destroyed. macOS uses
    `NSFileManager trashItemAtURL`, so Finder's Put Back works. Linux and the
    BSDs follow the freedesktop.org Trash specification 1.0: the home trash
    for files on the home drive, the drive's own `.Trash/$uid` or
    `.Trash-$uid` for a USB stick or second partition (never a copy across
    drives), names claimed with `O_EXCL` on the `.trashinfo`, and one
    `rename` per item. The trash itself, anything in it and a folder holding
    it are refused. Android and WebAssembly have none (`TrashAvailable()`
    false).
  - `UltraCanvasFilerWidget`: `FilerDeleteMode { MoveToTrash, Permanently }`;
    `DeleteSelection(preferred)`, `DeleteEntries(victims, preferred)`,
    `DeletePaths(paths, onDone, mode)` (default still Permanently: its caller
    confirmed), new `ConfirmDeletePaths` (the dialog for paths the display is
    not showing) and `CanMoveToTrash`. A trash move is one step per entry and
    asks nothing about write-protected entries; an entry the trash refuses
    stops at a "Cannot Move to the Trash" problem dialog and is never
    deleted for good instead. Where the trash cannot take the entries
    (inside an archive, on a remote drive, no trash on the platform) the
    trash option is greyed out and the dialog says why.
  - The confirmation's folder preview cut names at 11 bytes, which split a
    Thai, Cyrillic or CJK character; it now cuts at 12 characters, and the
    names in the dialog are shown decoded when they are not UTF-8.
  - New test: `TrashTest` (the freedesktop backend against a private
    `XDG_DATA_HOME`: files, folders, links, UTF-8 names, name collisions,
    refusals, and a second drive's `.Trash-$uid` when `/dev/shm` is one).
